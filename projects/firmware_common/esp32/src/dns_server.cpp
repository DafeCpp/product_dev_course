#include <string.h>

#include <firmware_common/esp32/dns_server.hpp>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/err.h"
#include "lwip/sockets.h"

namespace firmware_common::esp32 {

static const char* TAG = "dns_server";

#define DNS_PORT 53
#define DNS_MAX_LEN 256
static constexpr uint32_t DNS_TASK_STACK = 6144;
// DnsServerStop() ждёт фактического выхода задачи не дольше этого времени —
// recvfrom() разматывается почти сразу после close(), это просто защита от
// зависания, а не расчётный таймаут.
static constexpr uint32_t kDnsStopWaitTimeoutMs = 1000;

static TaskHandle_t s_dns_task_handle = nullptr;
// Сокет задачи; DnsServerStop() закрывает его извне, чтобы прервать блокирующий
// recvfrom() и корректно завершить задачу.
static int s_dns_sock = -1;
// Защищает публикацию s_dns_sock задачей и её чтение/очистку в
// DnsServerStop() — без неё возможна гонка в окне между xTaskCreate() (уже
// проставил s_dns_task_handle) и bind() внутри задачи (s_dns_sock ещё -1):
// Stop() решит, что закрывать нечего, и вернёт ESP_OK, а задача продолжит
// слушать порт 53.
static portMUX_TYPE s_dns_mux = portMUX_INITIALIZER_UNLOCKED;
// Stop() пришёл до того, как задача успела опубликовать сокет — задача
// должна закрыть его и выйти сразу после bind(), не начиная обслуживать
// запросы. Сбрасывается в начале DnsServerStart(), чтобы стук от прошлого
// цикла Stop()/Start() не убил следующую легитимную задачу.
static bool s_stop_requested = false;
// Отдаётся задачей непосредственно перед vTaskDelete() на любом пути выхода;
// DnsServerStop() ждёт его, чтобы гарантировать: к моменту возврата
// s_dns_task_handle уже nullptr, и следующий DnsServerStart() создаст новую
// задачу, а не решит, что сервер "уже запущен".
static SemaphoreHandle_t s_dns_stopped_sem = nullptr;

// Минимальный DNS response: заголовок + вопрос (echo) + ответ A record
static void build_dns_response(const uint8_t* query, size_t query_len,
                               uint32_t answer_ip, uint8_t* out,
                               size_t* out_len) {
  if (query_len < 12 || *out_len < query_len + 16) {
    *out_len = 0;
    return;
  }

  memcpy(out, query, query_len);

  // Заголовок: QR=1 (response), AA=1 (authoritative), RCODE=0
  out[2] = 0x81;  // QR=1, Opcode=0, AA=0, TC=0, RD=1
  out[3] = 0x80;  // RA=1, Z=0, RCODE=0
  out[6] = 0;     // ANCOUNT high
  out[7] = 1;     // ANCOUNT low = 1 answer

  // После вопроса добавляем A record
  size_t off = query_len;
  out[off++] = 0xC0;  // Pointer to name at offset 12
  out[off++] = 0x0C;
  out[off++] = 0;  // TYPE A
  out[off++] = 1;
  out[off++] = 0;  // CLASS IN
  out[off++] = 1;
  out[off++] = 0;  // TTL
  out[off++] = 0;
  out[off++] = 0;
  out[off++] = 60;  // 60 seconds
  out[off++] = 0;   // RDLENGTH
  out[off++] = 4;
  // A record: answer_ip is already in network byte order — copy bytes directly
  memcpy(out + off, &answer_ip, 4);
  off += 4;

  *out_len = off;
}

// Общий хвост для всех путей выхода задачи: сокет к этому моменту либо уже
// закрыт вызывающим (DnsServerStop() забрал его через s_dns_sock), либо
// должен быть закрыт самой задачей — это решает вызывающая сторона.
static void FinishTask() {
  s_dns_task_handle = nullptr;
  if (s_dns_stopped_sem) {
    xSemaphoreGive(s_dns_stopped_sem);
  }
  vTaskDelete(NULL);
}

static void dns_server_task(void* arg) {
  const uint32_t ap_ip = *(uint32_t*)arg;
  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0) {
    ESP_LOGE(TAG, "Failed to create socket: %d", errno);
    FinishTask();
    return;
  }

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = ap_ip;  // lwIP ip4_addr_t уже в network byte order
  addr.sin_port = htons(DNS_PORT);

  if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
    ESP_LOGE(TAG, "Failed to bind DNS port %d: %d", DNS_PORT, errno);
    close(sock);
    FinishTask();
    return;
  }

  bool stop_requested = false;
  portENTER_CRITICAL(&s_dns_mux);
  if (s_stop_requested) {
    s_stop_requested = false;
    stop_requested = true;
  } else {
    s_dns_sock = sock;
  }
  portEXIT_CRITICAL(&s_dns_mux);

  if (stop_requested) {
    // DnsServerStop() вызвали до этой точки — публиковать сокет уже поздно,
    // закрываем его сами (Stop() его не видел и не закрывал) и завершаемся,
    // ничего не слушая.
    ESP_LOGI(TAG, "DNS server stop requested during startup, exiting");
    close(sock);
    FinishTask();
    return;
  }

  ESP_LOGI(TAG, "DNS server listening on %d.%d.%d.%d:%d",
           (int)((ap_ip >> 0) & 0xFF), (int)((ap_ip >> 8) & 0xFF),
           (int)((ap_ip >> 16) & 0xFF), (int)((ap_ip >> 24) & 0xFF), DNS_PORT);

  uint8_t buf[DNS_MAX_LEN];
  struct sockaddr_in from;
  socklen_t from_len = sizeof(from);

  while (1) {
    from_len = sizeof(from);
    int n =
        recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr*)&from, &from_len);
    if (n <= 0) {
      if (s_dns_sock < 0) {
        // DnsServerStop() закрыл сокет намеренно — завершаем задачу.
        break;
      }
      continue;
    }

    size_t resp_len = sizeof(buf);
    build_dns_response(buf, (size_t)n, ap_ip, buf, &resp_len);
    if (resp_len > 0) {
      sendto(sock, buf, resp_len, 0, (struct sockaddr*)&from, from_len);
    }
  }

  // Сокет здесь уже закрыт DnsServerStop() (это тот же fd, что и sock) —
  // повторный close() был бы закрытием чужого, переиспользованного fd.
  ESP_LOGI(TAG, "DNS server stopped");
  FinishTask();
}

esp_err_t DnsServerStart(uint32_t ap_ip) {
  if (s_dns_task_handle != nullptr) {
    ESP_LOGW(TAG, "DNS server is already running");
    return ESP_OK;
  }

  if (s_dns_stopped_sem == nullptr) {
    s_dns_stopped_sem = xSemaphoreCreateBinary();
  } else {
    // Предыдущая задача могла отдать семафор без парного Take() в Stop():
    // сама завершилась независимо от Stop() (сбой socket()/bind()) или Stop()
    // не дождался её и вышел по таймауту. В обоих случаях семафор остаётся
    // "подписанным" чужим give(), и следующий Stop() возьмёт этот токен
    // мгновенно, решив, что новая задача уже остановилась, не дождавшись её
    // на самом деле. Осушаем перед стартом.
    xSemaphoreTake(s_dns_stopped_sem, 0);
  }
  // Стук от гонки прошлого цикла Stop()/Start() (см. dns_server_task) не
  // должен убить только что стартующую задачу.
  portENTER_CRITICAL(&s_dns_mux);
  s_stop_requested = false;
  portEXIT_CRITICAL(&s_dns_mux);

  static uint32_t s_ap_ip;  // Task использует после возврата
  s_ap_ip = ap_ip;

  BaseType_t ret = xTaskCreate(dns_server_task, "dns_srv", DNS_TASK_STACK,
                               &s_ap_ip, 5, &s_dns_task_handle);
  if (ret != pdPASS) {
    return ESP_FAIL;
  }
  return ESP_OK;
}

esp_err_t DnsServerStop(void) {
  if (s_dns_task_handle == nullptr) {
    return ESP_OK;  // уже остановлен
  }

  int sock_to_close = -1;
  portENTER_CRITICAL(&s_dns_mux);
  if (s_dns_sock >= 0) {
    sock_to_close = s_dns_sock;
    s_dns_sock = -1;  // сигнал задаче: сокет закрывается намеренно
  } else {
    // Задача создана (xTaskCreate уже отработал), но ещё не дошла до bind() и
    // не опубликовала сокет — просим её закрыться самостоятельно сразу после.
    s_stop_requested = true;
  }
  portEXIT_CRITICAL(&s_dns_mux);

  if (sock_to_close >= 0) {
    shutdown(sock_to_close, SHUT_RDWR);
    close(sock_to_close);
  }

  // Дожидаемся фактического завершения задачи: recvfrom() разматывается не
  // мгновенно, а следующий DnsServerStart() (например, при повторном
  // включении радио) должен увидеть s_dns_task_handle == nullptr, иначе он
  // молча ничего не создаст, приняв старую (уже умирающую) задачу за живую.
  if (s_dns_stopped_sem) {
    if (xSemaphoreTake(s_dns_stopped_sem,
                       pdMS_TO_TICKS(kDnsStopWaitTimeoutMs)) != pdTRUE) {
      ESP_LOGW(TAG, "Timed out waiting for DNS task to exit");
      return ESP_ERR_TIMEOUT;
    }
  }
  return ESP_OK;
}

}  // namespace firmware_common::esp32
