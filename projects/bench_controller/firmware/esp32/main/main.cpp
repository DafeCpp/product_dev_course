// bench_controller ESP32-S3 — измерительная сессия спайка LOS-76.
//
// Полный CANopen-контур 500 Гц на одной плате через TWAI в loopback
// (без трансивера): мастер (CANopenNode) на ядре 1, эмулятор узла
// клапана на ядре 0, общая «виртуальная шина». Через 60 с печатает
// джиттер тика, свежесть feedback и статистику шины — числа для отчёта
// spike-mcu-control-loop.md.
//
// Конфигурация контура идентична sim_host (--fake-plant / --socketcan),
// чтобы host-SIL, vcan-риг и железо сравнивались напрямую.

#include <cstdio>

#include "bench_control_loop.hpp"
#include "bench_platform_esp32.hpp"
#include "canopen_valve_channel.hpp"
#include "co_master.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "supervisory_link.hpp"
#include "twai_can_bus.hpp"
#include "valve_emulator.hpp"

namespace {
constexpr const char* kTag = "bench";

// GPIO для TWAI в loopback: реально линия никуда не идёт (контроллер
// слышит себя), но пины должны быть валидны.
constexpr int kGpioTx = 4;
constexpr int kGpioRx = 5;
constexpr uint32_t kBitrate = 1'000'000;  // 1 Мбит/с
constexpr uint32_t kMeasureSeconds = 60;

// Живут всё время работы — контур крутится в своей задаче.
bench::BenchPlatformEsp32 g_platform;
bench::TwaiCanBus g_bus;
bench::CoMaster g_master;
bench::CanopenValveChannel g_channel;
bench::StubSupervisoryLink g_link;
bench::ValveEmulator* g_emulator = nullptr;
bench::BenchControlLoop* g_loop = nullptr;
QueueHandle_t g_tap = nullptr;

bench::BenchControlLoop::Config MakeConfig() {
  bench::BenchControlLoop::Config cfg;
  cfg.controller.force_gains = {.kp = 4e-5f,
                                .ki = 2e-3f,
                                .kd = 0.0f,
                                .max_integral = 400.0f,
                                .max_output = 1.0f};
  cfg.controller.disp_gains = {.kp = 0.3f,
                               .ki = 4.0f,
                               .kd = 0.0f,
                               .max_integral = 0.2f,
                               .max_output = 1.0f};
  cfg.controller.force_ff = 1.0f / (5000.0f * 400.0f);
  cfg.controller.disp_ff = 1.0f / 400.0f;
  cfg.controller.ff_lead_tau_s = 0.008f;
  cfg.controller.output_slew_per_s = 400.0f;
  cfg.controller.capture_ramp_s = 0.3f;
  cfg.watchdog = {.grace_ms = 200, .ramp_ms = 1000};
  return cfg;
}

}  // namespace

extern "C" void app_main(void) {
  ESP_LOGI(kTag, "bench_controller ESP32-S3 spike (LOS-76)");

  if (!g_bus.Init(kGpioTx, kGpioRx, kBitrate)) {
    ESP_LOGE(kTag, "TWAI init failed");
    return;
  }

  // tap-очередь: копия каждого RX-кадра для эмулятора клапана
  g_tap = xQueueCreate(64, sizeof(bench::TwaiRxItem));
  g_bus.SetTap(g_tap);

  // Эмулятор клапана — ядро 0 (setpoint→модель→feedback)
  static bench::ValveEmulator emulator(g_bus, g_tap, /*sync_mode=*/false);
  g_emulator = &emulator;
  xTaskCreatePinnedToCore(&bench::ValveEmulator::TaskEntry, "valve_emu", 4096,
                          g_emulator, 5, nullptr, 0);
  vTaskDelay(pdMS_TO_TICKS(50));  // дать эмулятору стартовать (bootup HB)

  // CANopen-мастер. Attach — ДО Init (OD-extension фиксируется в InitPDO)
  g_channel.Attach(g_master);
  if (!g_master.Init(g_bus, {.node_id = 0x01, .valve_node_id = 0x20})) {
    ESP_LOGE(kTag, "CANopen master init failed");
    return;
  }

  static bench::BenchControlLoop loop(MakeConfig(), g_platform, g_channel,
                                      g_link);
  g_loop = &loop;
  static const bench::SineProgram::Segment kProgram[] = {
      {.mean = 20'000.0f,
       .amplitude = 10'000.0f,
       .freq_hz = 10.0f,
       .cycles = 1'000'000}};
  g_loop->SetProgram(kProgram);

  if (!g_loop->Init()) {
    ESP_LOGE(kTag, "control task start failed");
    return;
  }

  ESP_LOGI(kTag, "measuring %u s ...", kMeasureSeconds);
  const uint32_t fb_before = g_channel.FeedbackCount();
  const uint64_t t0 = static_cast<uint64_t>(esp_timer_get_time());
  vTaskDelay(pdMS_TO_TICKS(kMeasureSeconds * 1000));
  const uint64_t t1 = static_cast<uint64_t>(esp_timer_get_time());

  // Остановить тики контура ДО чтения статистики: TickStats и
  // feedback-счётчик мутируются задачей на ядре 1, а читаем мы с ядра
  // 0 — без остановки снимок был бы неатомарным (в т.ч. 64-битные
  // поля TickStats). Ждём подтверждения, что TickOnce больше не идёт.
  g_loop->RequestStop();
  for (int i = 0; i < 100 && !g_loop->Stopped(); ++i) {
    vTaskDelay(pdMS_TO_TICKS(2));
  }
  const uint32_t fb_after = g_channel.FeedbackCount();

  const bench::TickStats::Report r = g_loop->Stats().MakeReport();
  const uint32_t elapsed_ms = static_cast<uint32_t>((t1 - t0) / 1000);
  const uint32_t fb_count = fb_after - fb_before;

  std::printf("\n===== BENCH ESP32-S3 RESULTS (LOS-76) =====\n");
  std::printf("elapsed_ms=%u\n", elapsed_ms);
  std::printf("tick_period_us: min=%u avg=%u p99=%u max=%u n=%u\n", r.min_us,
              r.avg_us, r.p99_us, r.max_us, r.count);
  std::printf("feedback_frames=%u (~%.1f Hz)\n", fb_count,
              1000.0 * fb_count / elapsed_ms);
  std::printf("valve_operational=%d\n", g_channel.IsOperational() ? 1 : 0);
  std::printf("===========================================\n");
}
