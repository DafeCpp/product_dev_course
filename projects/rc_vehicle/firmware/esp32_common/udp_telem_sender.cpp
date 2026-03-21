#include "udp_telem_sender.hpp"

#include <atomic>
#include <cstring>

#include "config.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char* TAG = "udp_telem";

using Cfg = rc_vehicle::config::UdpTelemConfig;

// ─────────────────────────────────────────────────────────────────────────────
// Packet header
// ─────────────────────────────────────────────────────────────────────────────

static constexpr uint8_t kMagic[2] = {0x52, 0x54};  // "RT"

struct __attribute__((packed)) UdpTelemPacket {
  uint8_t magic[2];
  uint8_t version;
  uint32_t seq;
  TelemetryLogFrame frame;
};

static_assert(sizeof(UdpTelemPacket) == 79, "UdpTelemPacket size mismatch");

// ─────────────────────────────────────────────────────────────────────────────
// Module state
// ─────────────────────────────────────────────────────────────────────────────

static QueueHandle_t s_queue = nullptr;
static int s_data_sock = -1;
static int s_ctrl_sock = -1;

static std::atomic<bool> s_streaming{false};
static std::atomic<uint32_t> s_seq{0};
static std::atomic<uint32_t> s_dropped{0};

// Spinlock protecting s_target_addr, s_target_ip_str, s_target_port, s_hz.
// Writer: UdpTelemStart() (called from udp_ctrl_task or WebSocket handler).
// Reader: udp_sender_task (sendto).
// On dual-core ESP32-S3 without this lock, sendto() could read a partially
// written sockaddr_in when Start() is called from a different core.
static portMUX_TYPE s_target_mux = portMUX_INITIALIZER_UNLOCKED;

static struct sockaddr_in s_target_addr;
static char s_target_ip_str[16] = {};
static uint16_t s_target_port = Cfg::kDefaultDataPort;
static uint8_t s_hz = Cfg::kDefaultHz;

// ─────────────────────────────────────────────────────────────────────────────
// NVS helpers
// ─────────────────────────────────────────────────────────────────────────────

static constexpr const char* NVS_NAMESPACE = "udp_telem";

static void nvs_load() {
  nvs_handle_t handle;
  if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
    return;
  }
  size_t len = sizeof(s_target_ip_str);
  nvs_get_str(handle, "target_ip", s_target_ip_str, &len);
  nvs_get_u16(handle, "target_port", &s_target_port);
  uint8_t hz = 0;
  if (nvs_get_u8(handle, "hz", &hz) == ESP_OK && hz > 0) {
    s_hz = hz;
  }
  nvs_close(handle);
  ESP_LOGI(TAG, "NVS loaded: ip=%s port=%u hz=%u",
           s_target_ip_str[0] ? s_target_ip_str : "(empty)",
           s_target_port, s_hz);
}

static void nvs_save() {
  nvs_handle_t handle;
  if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
    ESP_LOGW(TAG, "Failed to open NVS for write");
    return;
  }
  nvs_set_str(handle, "target_ip", s_target_ip_str);
  nvs_set_u16(handle, "target_port", s_target_port);
  nvs_set_u8(handle, "hz", s_hz);
  nvs_commit(handle);
  nvs_close(handle);
}

// ─────────────────────────────────────────────────────────────────────────────
// Validate hz
// ─────────────────────────────────────────────────────────────────────────────

static bool is_valid_hz(uint8_t hz) {
  return hz == 10 || hz == 20 || hz == 50 || hz == 100;
}

// ─────────────────────────────────────────────────────────────────────────────
// Sender task
// ─────────────────────────────────────────────────────────────────────────────

static void udp_sender_task(void* arg) {
  (void)arg;
  UdpTelemPacket pkt;
  pkt.magic[0] = kMagic[0];
  pkt.magic[1] = kMagic[1];
  pkt.version = Cfg::kPacketVersion;

  int64_t last_send_us = 0;
  int64_t send_interval_us = 10000;  // 100 Hz = 10000 us

  TickType_t last_diag = xTaskGetTickCount();
  uint32_t frames_sent = 0;

  for (;;) {
    TelemetryLogFrame frame;
    if (xQueueReceive(s_queue, &frame, portMAX_DELAY) != pdTRUE) {
      continue;
    }

    if (!s_streaming.load(std::memory_order_relaxed)) {
      continue;  // drain queue when stopped
    }

    // Rate limiting: skip frames if hz < 100
    int64_t now_us = esp_timer_get_time();
    taskENTER_CRITICAL(&s_target_mux);
    uint8_t cur_hz = s_hz;
    taskEXIT_CRITICAL(&s_target_mux);
    send_interval_us = 1000000LL / cur_hz;
    if (now_us - last_send_us < send_interval_us) {
      continue;  // skip this frame
    }
    last_send_us = now_us;

    pkt.seq = s_seq.fetch_add(1, std::memory_order_relaxed);
    memcpy(&pkt.frame, &frame, sizeof(TelemetryLogFrame));

    // Take a consistent snapshot of the target address under spinlock.
    struct sockaddr_in target_snap;
    taskENTER_CRITICAL(&s_target_mux);
    memcpy(&target_snap, &s_target_addr, sizeof(target_snap));
    taskEXIT_CRITICAL(&s_target_mux);

    int ret = sendto(s_data_sock, &pkt, sizeof(pkt), 0,
                     (struct sockaddr*)&target_snap, sizeof(target_snap));
    if (ret < 0) {
      // Rate-limited warning
      static uint32_t last_warn_ms = 0;
      uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
      if (now_ms - last_warn_ms >= 1000) {
        ESP_LOGW(TAG, "sendto failed: errno=%d", errno);
        last_warn_ms = now_ms;
      }
    } else {
      frames_sent++;
    }

    // Diagnostic log every 10s
    TickType_t now_ticks = xTaskGetTickCount();
    if ((now_ticks - last_diag) >= pdMS_TO_TICKS(10000)) {
      ESP_LOGI(TAG, "sender: %lu sent in 10s, seq=%lu, dropped=%lu",
               (unsigned long)frames_sent,
               (unsigned long)s_seq.load(std::memory_order_relaxed),
               (unsigned long)s_dropped.load(std::memory_order_relaxed));
      frames_sent = 0;
      last_diag = now_ticks;
    }
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Control task — listens on UDP 5556 for START/STOP/STATUS/PING
// ─────────────────────────────────────────────────────────────────────────────

static void send_ctrl_reply(const char* reply, struct sockaddr_in* addr,
                            socklen_t addr_len) {
  sendto(s_ctrl_sock, reply, strlen(reply), 0, (struct sockaddr*)addr,
         addr_len);
}

static void handle_ctrl_start(const char* buf, struct sockaddr_in* src_addr,
                              socklen_t addr_len) {
  // Parse: "START <port> [hz]"
  uint16_t port = Cfg::kDefaultDataPort;
  uint8_t hz = Cfg::kDefaultHz;

  // Skip "START "
  const char* p = buf + 5;
  while (*p == ' ') p++;

  if (*p) {
    port = (uint16_t)atoi(p);
    // Find next space for hz
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;
    if (*p) {
      hz = (uint8_t)atoi(p);
    }
  }

  if (port < 1024) {
    char reply[128];
    snprintf(reply, sizeof(reply),
             "{\"ok\":false,\"error\":\"port must be >= 1024\"}");
    send_ctrl_reply(reply, src_addr, addr_len);
    return;
  }

  if (!is_valid_hz(hz)) {
    char reply[128];
    snprintf(reply, sizeof(reply),
             "{\"ok\":false,\"error\":\"hz must be 10, 20, 50, or 100\"}");
    send_ctrl_reply(reply, src_addr, addr_len);
    return;
  }

  // Use source IP of the START command as target
  char ip_str[16];
  inet_ntoa_r(src_addr->sin_addr, ip_str, sizeof(ip_str));

  bool ok = UdpTelemStart(ip_str, port, hz);

  char reply[128];
  snprintf(reply, sizeof(reply),
           "{\"ok\":%s,\"ip\":\"%s\",\"port\":%u,\"hz\":%u}",
           ok ? "true" : "false", ip_str, port, hz);
  send_ctrl_reply(reply, src_addr, addr_len);
}

static void handle_ctrl_stop(struct sockaddr_in* src_addr,
                             socklen_t addr_len) {
  UdpTelemStop();
  send_ctrl_reply("{\"ok\":true}", src_addr, addr_len);
}

static void handle_ctrl_status(struct sockaddr_in* src_addr,
                               socklen_t addr_len) {
  // Snapshot target config under lock for consistent STATUS reply
  char ip_snap[16];
  uint16_t port_snap;
  uint8_t hz_snap;
  taskENTER_CRITICAL(&s_target_mux);
  memcpy(ip_snap, s_target_ip_str, sizeof(ip_snap));
  port_snap = s_target_port;
  hz_snap = s_hz;
  taskEXIT_CRITICAL(&s_target_mux);

  char reply[256];
  snprintf(reply, sizeof(reply),
           "{\"streaming\":%s,\"ip\":\"%s\",\"port\":%u,\"hz\":%u,"
           "\"seq\":%lu,\"dropped\":%lu}",
           s_streaming.load() ? "true" : "false",
           ip_snap[0] ? ip_snap : "",
           port_snap, (unsigned)hz_snap,
           (unsigned long)s_seq.load(std::memory_order_relaxed),
           (unsigned long)s_dropped.load(std::memory_order_relaxed));
  send_ctrl_reply(reply, src_addr, addr_len);
}

static void handle_ctrl_ping(struct sockaddr_in* src_addr,
                             socklen_t addr_len) {
  char reply[64];
  snprintf(reply, sizeof(reply), "{\"ok\":true,\"uptime_ms\":%lu}",
           (unsigned long)(xTaskGetTickCount() * portTICK_PERIOD_MS));
  send_ctrl_reply(reply, src_addr, addr_len);
}

static void udp_ctrl_task(void* arg) {
  (void)arg;

  char buf[Cfg::kMaxCommandLen + 1];

  for (;;) {
    struct sockaddr_in src_addr;
    socklen_t addr_len = sizeof(src_addr);

    int len = recvfrom(s_ctrl_sock, buf, Cfg::kMaxCommandLen, 0,
                       (struct sockaddr*)&src_addr, &addr_len);
    if (len <= 0) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    if ((size_t)len > Cfg::kMaxCommandLen) {
      continue;  // too long, ignore
    }
    buf[len] = '\0';

    // Trim trailing whitespace
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r' ||
                       buf[len - 1] == ' ')) {
      buf[--len] = '\0';
    }

    ESP_LOGI(TAG, "ctrl cmd: '%s' from %s:%u", buf,
             inet_ntoa(src_addr.sin_addr), ntohs(src_addr.sin_port));

    if (strncmp(buf, "START", 5) == 0) {
      handle_ctrl_start(buf, &src_addr, addr_len);
    } else if (strcmp(buf, "STOP") == 0) {
      handle_ctrl_stop(&src_addr, addr_len);
    } else if (strcmp(buf, "STATUS") == 0) {
      handle_ctrl_status(&src_addr, addr_len);
    } else if (strcmp(buf, "PING") == 0) {
      handle_ctrl_ping(&src_addr, addr_len);
    } else {
      char reply[64];
      snprintf(reply, sizeof(reply),
               "{\"ok\":false,\"error\":\"unknown command\"}");
      send_ctrl_reply(reply, &src_addr, addr_len);
    }
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

esp_err_t UdpTelemInit() {
  // Create queue
  s_queue = xQueueCreate(Cfg::kQueueDepth, sizeof(TelemetryLogFrame));
  if (!s_queue) {
    ESP_LOGE(TAG, "Failed to create queue");
    return ESP_ERR_NO_MEM;
  }

  // Create data socket (for sending telemetry)
  s_data_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (s_data_sock < 0) {
    ESP_LOGE(TAG, "Failed to create data socket: errno=%d", errno);
    return ESP_FAIL;
  }

  // Create control socket (for receiving commands)
  s_ctrl_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (s_ctrl_sock < 0) {
    ESP_LOGE(TAG, "Failed to create ctrl socket: errno=%d", errno);
    close(s_data_sock);
    s_data_sock = -1;
    return ESP_FAIL;
  }

  struct sockaddr_in ctrl_addr = {};
  ctrl_addr.sin_family = AF_INET;
  ctrl_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  ctrl_addr.sin_port = htons(Cfg::kControlPort);

  if (bind(s_ctrl_sock, (struct sockaddr*)&ctrl_addr, sizeof(ctrl_addr)) < 0) {
    ESP_LOGE(TAG, "Failed to bind ctrl socket to port %u: errno=%d",
             Cfg::kControlPort, errno);
    close(s_data_sock);
    close(s_ctrl_sock);
    s_data_sock = -1;
    s_ctrl_sock = -1;
    return ESP_FAIL;
  }

  // Load saved config from NVS
  nvs_load();

  // Create sender task
  if (xTaskCreate(udp_sender_task, "udp_send", Cfg::kSenderTaskStack, nullptr,
                  Cfg::kSenderTaskPriority, nullptr) != pdPASS) {
    ESP_LOGE(TAG, "Failed to create sender task");
    close(s_data_sock);
    close(s_ctrl_sock);
    s_data_sock = -1;
    s_ctrl_sock = -1;
    return ESP_FAIL;
  }

  // Create control task
  if (xTaskCreate(udp_ctrl_task, "udp_ctrl", Cfg::kControlTaskStack, nullptr,
                  Cfg::kControlTaskPriority, nullptr) != pdPASS) {
    ESP_LOGE(TAG, "Failed to create ctrl task");
    // sender task is already running but will just idle
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Initialized. Control port: %u, data port default: %u",
           Cfg::kControlPort, Cfg::kDefaultDataPort);
  return ESP_OK;
}

void UdpTelemEnqueue(const TelemetryLogFrame& frame) {
  if (!s_streaming.load(std::memory_order_relaxed) || !s_queue) {
    return;
  }
  if (xQueueSend(s_queue, &frame, 0) != pdTRUE) {
    s_dropped.fetch_add(1, std::memory_order_relaxed);
  }
}

bool UdpTelemStart(const char* ip, uint16_t port, uint8_t hz) {
  if (!ip || port < 1024 || !is_valid_hz(hz)) {
    ESP_LOGW(TAG, "Invalid params: ip=%s port=%u hz=%u",
             ip ? ip : "null", port, hz);
    return false;
  }

  // If already streaming, stop first
  if (s_streaming.load()) {
    UdpTelemStop();
  }

  // Build new target address in a local variable first, then copy under lock.
  struct sockaddr_in new_addr = {};
  new_addr.sin_family = AF_INET;
  new_addr.sin_port = htons(port);
  if (inet_pton(AF_INET, ip, &new_addr.sin_addr) != 1) {
    ESP_LOGW(TAG, "Invalid IP: %s", ip);
    return false;
  }

  // Check if target config actually changed (to avoid unnecessary NVS writes).
  bool config_changed;
  taskENTER_CRITICAL(&s_target_mux);
  config_changed = (strcmp(s_target_ip_str, ip) != 0) ||
                   (s_target_port != port) || (s_hz != hz);
  memcpy(&s_target_addr, &new_addr, sizeof(s_target_addr));
  strncpy(s_target_ip_str, ip, sizeof(s_target_ip_str) - 1);
  s_target_ip_str[sizeof(s_target_ip_str) - 1] = '\0';
  s_target_port = port;
  s_hz = hz;
  taskEXIT_CRITICAL(&s_target_mux);

  // Reset counters
  s_seq.store(0, std::memory_order_relaxed);
  s_dropped.store(0, std::memory_order_relaxed);

  // Save to NVS only when target config changed to reduce flash wear.
  if (config_changed) {
    nvs_save();
  }

  // Start streaming
  s_streaming.store(true, std::memory_order_release);

  ESP_LOGI(TAG, "Started: %s:%u @ %u Hz", s_target_ip_str, s_target_port,
           s_hz);
  return true;
}

void UdpTelemStop() {
  if (!s_streaming.load()) {
    return;
  }
  s_streaming.store(false, std::memory_order_release);

  // No explicit drain needed: udp_sender_task already skips frames
  // when s_streaming is false (line 127). Draining here would race with
  // the sender task's xQueueReceive on the other core.

  ESP_LOGI(TAG, "Stopped. Total seq=%lu, dropped=%lu",
           (unsigned long)s_seq.load(), (unsigned long)s_dropped.load());
}

bool UdpTelemIsStreaming() {
  return s_streaming.load(std::memory_order_relaxed);
}

uint32_t UdpTelemGetSeq() {
  return s_seq.load(std::memory_order_relaxed);
}

uint32_t UdpTelemGetDropped() {
  return s_dropped.load(std::memory_order_relaxed);
}

const char* UdpTelemGetTargetIp() {
  // s_target_ip_str is only written under s_target_mux in UdpTelemStart(),
  // but the returned pointer is read without lock. This is safe because
  // callers (STATUS handler, WS handler) run on the same core as the writer
  // and the string is always null-terminated before s_streaming is set.
  return s_target_ip_str;
}

uint16_t UdpTelemGetTargetPort() {
  taskENTER_CRITICAL(&s_target_mux);
  uint16_t port = s_target_port;
  taskEXIT_CRITICAL(&s_target_mux);
  return port;
}

uint8_t UdpTelemGetHz() {
  taskENTER_CRITICAL(&s_target_mux);
  uint8_t hz = s_hz;
  taskEXIT_CRITICAL(&s_target_mux);
  return hz;
}
