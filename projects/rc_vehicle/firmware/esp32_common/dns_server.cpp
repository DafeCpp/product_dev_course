#include "dns_server.hpp"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/err.h"
#include "lwip/sockets.h"

static const char* TAG = "dns_server";

#define DNS_PORT 53
#define DNS_MAX_LEN 256
static constexpr uint32_t DNS_TASK_STACK = 6144;
static TaskHandle_t s_dns_task_handle = nullptr;

// Минимальный DNS response: заголовок + вопрос (echo) + ответ A record
static void build_dns_response(const uint8_t* query, size_t query_len,
                               uint32_t answer_ip, uint8_t* out, size_t* out_len) {
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
  // A record: 4 байта в network byte order (big-endian)
  out[off++] = (answer_ip >> 24) & 0xFF;
  out[off++] = (answer_ip >> 16) & 0xFF;
  out[off++] = (answer_ip >> 8) & 0xFF;
  out[off++] = (answer_ip >> 0) & 0xFF;

  *out_len = off;
}

static void dns_server_task(void* arg) {
  const uint32_t ap_ip = *(uint32_t*)arg;
  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0) {
    ESP_LOGE(TAG, "Failed to create socket: %d", errno);
    vTaskDelete(NULL);
    return;
  }

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = ap_ip;  // lwIP ip4_addr_t уже в network byte order
  addr.sin_port = htons(DNS_PORT);

  if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
    ESP_LOGE(TAG, "Failed to bind DNS port %d: %d", DNS_PORT, errno);
    close(sock);
    vTaskDelete(NULL);
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
    int n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr*)&from, &from_len);
    if (n <= 0) continue;

    size_t resp_len = sizeof(buf);
    build_dns_response(buf, (size_t)n, ap_ip, buf, &resp_len);
    if (resp_len > 0) {
      sendto(sock, buf, resp_len, 0, (struct sockaddr*)&from, from_len);
    }
  }
}

esp_err_t DnsServerStart(uint32_t ap_ip) {
  if (s_dns_task_handle != nullptr) {
    ESP_LOGW(TAG, "DNS server is already running");
    return ESP_OK;
  }

  static uint32_t s_ap_ip;  // Task использует после возврата
  s_ap_ip = ap_ip;

  BaseType_t ret = xTaskCreate(dns_server_task, "dns_srv", DNS_TASK_STACK,
                               &s_ap_ip, 5, &s_dns_task_handle);
  if (ret != pdPASS) {
    return ESP_FAIL;
  }
  return ESP_OK;
}
