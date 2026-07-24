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

static portMUX_TYPE s_dns_mux = portMUX_INITIALIZER_UNLOCKED;
// Единственный источник истины "жива ли сейчас задача" — читают и пишут
// ТОЛЬКО под s_dns_mux, и Start(), и Stop(). До этого роль такого флага
// играл TaskHandle_t (non-null = "занято"), а завершение сигнализировалось
// отдельным семафором — независимость этих двух шагов друг от друга
// порождала гонки в обе стороны (см. историю фиксов ниже) вне зависимости
// от того, в каком порядке их выполнял FinishTask(). Здесь состояние одно,
// поэтому такого зазора в принципе нет.
static bool s_dns_task_running = false;
// Инкрементируется в DnsServerStart() при каждом реальном создании новой
// задачи. Плоского s_dns_task_running недостаточно: если задача A уже
// обнулила флаг, но DnsServerStop(), ожидающий именно её, ещё не успел это
// заметить, а между этими двумя моментами DnsServerStart() создал задачу B
// (флаг снова true) — ожидание должно закончиться сразу (A завершилась),
// а не продолжаться, спутав B с A. DnsServerStop() запоминает поколение
// задачи на момент вызова и завершает ожидание, если оно изменилось, даже
// при s_dns_task_running == true.
static uint32_t s_dns_generation = 0;
// Гонка bind() (в задаче) vs DnsServerStop() — чистая логика вынесена в
// DnsServerRaceState (firmware_common/dns_server_race_state.hpp) и покрыта
// host-тестами в projects/firmware_common/tests; здесь она только
// оборачивается в критическую секцию.
static DnsServerRaceState s_race_state;
// Будильник для DnsServerStop(), не источник истины: после каждого
// пробуждения (в том числе спонтанного — см. FinishTask()) Stop()
// перечитывает s_dns_task_running под локом и либо возвращается, либо ждёт
// снова. Поэтому «протухший» give() от чужого/прошлого завершения не может
// обмануть Stop() — он просто вызовет один лишний холостой цикл ожидания.
static SemaphoreHandle_t s_dns_stopped_sem = nullptr;

// Общий хвост для всех путей выхода задачи: сокет к этому моменту либо уже
// закрыт вызывающим (DnsServerStop() забрал его через RequestStop()), либо
// должен быть закрыт самой задачей — это решает вызывающая сторона.
static void FinishTask() {
  portENTER_CRITICAL(&s_dns_mux);
  s_dns_task_running = false;
  portEXIT_CRITICAL(&s_dns_mux);
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
  if (s_dns_stopped_sem == nullptr) {
    s_dns_stopped_sem = xSemaphoreCreateBinary();
    if (s_dns_stopped_sem == nullptr) {
      // Без семафора DnsServerStop() ниже вызвал бы xSemaphoreTake(NULL, ...)
      // — падение, а не чистая ошибка. Не публикуем задачу как запущенную.
      ESP_LOGE(TAG, "Failed to allocate DNS stop semaphore");
      return ESP_ERR_NO_MEM;
    }
  }

  // Проверка "уже запущено" и установка s_dns_task_running — одна
  // критическая секция, иначе два конкурентных DnsServerStart() могли бы
  // оба увидеть "свободно" и создать по задаче каждый.
  bool already_running = false;
  portENTER_CRITICAL(&s_dns_mux);
  already_running = s_dns_task_running;
  if (!already_running) {
    s_dns_task_running = true;
    ++s_dns_generation;
    // Стук от гонки прошлого цикла Stop()/Start() (см. dns_server_task) не
    // должен убить только что стартующую задачу. Только для НОВОЙ задачи —
    // если already_running уже true, это стёрло бы опубликованный сокет
    // живой задачи, и следующий DnsServerStop() не смог бы её остановить.
    s_race_state.ResetForNewTask();
  }
  portEXIT_CRITICAL(&s_dns_mux);

  if (already_running) {
    ESP_LOGW(TAG, "DNS server is already running");
    return ESP_OK;
  }

  static uint32_t s_ap_ip;  // Task использует после возврата
  s_ap_ip = ap_ip;

  BaseType_t ret = xTaskCreate(dns_server_task, "dns_srv", DNS_TASK_STACK,
                               &s_ap_ip, 5, nullptr);
  if (ret != pdPASS) {
    portENTER_CRITICAL(&s_dns_mux);
    s_dns_task_running = false;
    portEXIT_CRITICAL(&s_dns_mux);
    return ESP_FAIL;
  }
  return ESP_OK;
}

esp_err_t DnsServerStop(void) {
  bool running = false;
  int sock_to_close = -1;
  uint32_t target_generation = 0;

  portENTER_CRITICAL(&s_dns_mux);
  running = s_dns_task_running;
  if (running) {
    target_generation = s_dns_generation;
    s_race_state.RequestStop(&sock_to_close);
  }
  portEXIT_CRITICAL(&s_dns_mux);

  if (!running) {
    return ESP_OK;  // уже остановлен
  }

  if (sock_to_close >= 0) {
    shutdown(sock_to_close, SHUT_RDWR);
    close(sock_to_close);
  }
  // Иначе задача создана, но ещё не дошла до bind() и не опубликовала
  // сокет — она закроется самостоятельно сразу после (см. dns_server_task).

  // Дожидаемся фактического обнуления s_dns_task_running у ИМЕННО этой
  // задачи (по поколению): плоского флага недостаточно — если задача A уже
  // обнулила его, а DnsServerStart() успел создать задачу B (флаг снова
  // true) прежде, чем этот цикл заметил обнуление, флаг сам по себе не
  // отличит "A жива" от "B стартовала". После каждого пробуждения
  // перечитываем оба под локом, а не доверяем самому факту пробуждения (см.
  // комментарий у s_dns_stopped_sem).
  TickType_t deadline =
      xTaskGetTickCount() + pdMS_TO_TICKS(kDnsStopWaitTimeoutMs);
  for (;;) {
    portENTER_CRITICAL(&s_dns_mux);
    bool still_running = s_dns_task_running;
    uint32_t current_generation = s_dns_generation;
    portEXIT_CRITICAL(&s_dns_mux);
    if (!still_running || current_generation != target_generation) {
      // Целевая задача либо явно завершилась, либо (поколение сменилось)
      // успела смениться другой — в обоих случаях она уже не жива.
      return ESP_OK;
    }

    TickType_t now = xTaskGetTickCount();
    if (now >= deadline) {
      ESP_LOGW(TAG, "Timed out waiting for DNS task to exit");
      return ESP_ERR_TIMEOUT;
    }
    xSemaphoreTake(s_dns_stopped_sem, deadline - now);
  }
}

}  // namespace firmware_common::esp32
