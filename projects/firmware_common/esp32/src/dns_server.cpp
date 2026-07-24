#include <firmware_common/dns_response_builder.hpp>
#include <firmware_common/dns_server_race_state.hpp>
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
// Гонка bind() (в задаче) vs DnsServerStop() — чистая логика вынесена в
// DnsServerRaceState (firmware_common/dns_server_race_state.hpp) и покрыта
// host-тестами в projects/firmware_common/tests; здесь она только
// оборачивается в критическую секцию.
static DnsServerRaceState s_race_state;
static portMUX_TYPE s_dns_mux = portMUX_INITIALIZER_UNLOCKED;
// Отдаётся задачей непосредственно перед vTaskDelete() на любом пути выхода;
// DnsServerStop() ждёт его, чтобы гарантировать: к моменту возврата
// s_dns_task_handle уже nullptr, и следующий DnsServerStart() создаст новую
// задачу, а не решит, что сервер "уже запущен".
static SemaphoreHandle_t s_dns_stopped_sem = nullptr;

// Общий хвост для всех путей выхода задачи: сокет к этому моменту либо уже
// закрыт вызывающим (DnsServerStop() забрал его через RequestStop()), либо
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

  portENTER_CRITICAL(&s_dns_mux);
  DnsServerRaceState::BindOutcome outcome = s_race_state.OnBindSucceeded(sock);
  portEXIT_CRITICAL(&s_dns_mux);

  if (outcome == DnsServerRaceState::BindOutcome::kStopRequested) {
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
      // IsSocketPublished() читаем под той же критической секцией, что и
      // запись в RequestStop() (контракт DnsServerRaceState) — без неё на
      // dual-core ESP32 запись из DnsServerStop() может быть не видна этому
      // ядру, и цикл будет впустую крутить recvfrom() на закрытом сокете.
      portENTER_CRITICAL(&s_dns_mux);
      bool socket_published = s_race_state.IsSocketPublished();
      portEXIT_CRITICAL(&s_dns_mux);
      if (!socket_published) {
        // DnsServerStop() закрыл сокет намеренно — завершаем задачу.
        break;
      }
      continue;
    }

    size_t resp_len = sizeof(buf);
    BuildDnsResponse(buf, (size_t)n, ap_ip, buf, &resp_len);
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
  // Стук от гонки прошлого цикла Stop()/Start() не должен убить только что
  // стартующую задачу.
  portENTER_CRITICAL(&s_dns_mux);
  s_race_state.ResetForNewTask();
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
  DnsServerRaceState::StopOutcome outcome =
      s_race_state.RequestStop(&sock_to_close);
  portEXIT_CRITICAL(&s_dns_mux);

  if (outcome == DnsServerRaceState::StopOutcome::kCloseSocket) {
    shutdown(sock_to_close, SHUT_RDWR);
    close(sock_to_close);
  }
  // Иначе (kMarkPending): задача создана, но ещё не дошла до bind() и не
  // опубликовала сокет — она закроется самостоятельно сразу после (см.
  // dns_server_task).

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
