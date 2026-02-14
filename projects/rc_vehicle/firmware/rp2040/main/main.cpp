#include <cstdarg>
#include <cstdio>

#include "config.hpp"
#include "failsafe.hpp"
#include "hardware/gpio.h"
#include "imu.hpp"
#include "pico/stdlib.h"
#include "protocol.hpp"
#include "pwm_control.hpp"
#include "rc_input.hpp"
#include "slew_rate.hpp"
#include "uart_bridge.hpp"

static const char *TAG = "main";
static bool s_uart_ready = false;

/**
 * printf + отправка по UART на ESP32 (если мост инициализирован).
 * Формат вызова как у printf.
 */
static void log_remote(const char *fmt, ...) {
  char buf[201];
  va_list args;
  va_start(args, fmt);
  int n = vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  if (n <= 0) return;
  size_t len =
      static_cast<size_t>(n) < sizeof(buf) - 1 ? static_cast<size_t>(n) : sizeof(buf) - 1;
  // Локальный USB Serial
  printf("%.*s", static_cast<int>(len), buf);
  // Удалённо на ESP32
  if (s_uart_ready) {
    UartBridgeSendLog(buf, len);
  }
}

// commanded_* — что хотим (RC/Wi‑Fi), applied_* — что реально подаём на PWM
// (slew-rate)
static float commanded_throttle = 0.0f;
static float commanded_steering = 0.0f;
static float applied_throttle = 0.0f;
static float applied_steering = 0.0f;
static bool rc_active = false;
static bool wifi_active = false;

int main() {
  // Инициализация stdio для отладки (USB Serial)
  stdio_init_all();

  printf("[%s] RC Vehicle RP2040 firmware starting...\n", TAG);

  // Инициализация UART моста ПЕРВЫМ — чтобы дальнейшие логи шли на ESP32
  printf("[%s] Initializing UART bridge...\n", TAG);
  if (UartBridgeInit() != 0) {
    printf("[%s] ERROR: Failed to initialize UART bridge\n", TAG);
    return -1;
  }
  s_uart_ready = true;
  log_remote("[%s] UART bridge OK, remote logging enabled\n", TAG);

  // Инициализация PWM
  log_remote("[%s] Initializing PWM...\n", TAG);
  if (PwmControlInit() != 0) {
    log_remote("[%s] ERROR: Failed to initialize PWM\n", TAG);
    return -1;
  }

  // Инициализация RC-in
  log_remote("[%s] Initializing RC input...\n", TAG);
  if (RcInputInit() != 0) {
    log_remote("[%s] ERROR: Failed to initialize RC input\n", TAG);
    return -1;
  }

  // Инициализация IMU
  // MPU-6050/6500 требует ~100 мс после подачи питания (даташит: start-up time).
  sleep_ms(100);
  log_remote("[%s] Initializing IMU...\n", TAG);
  if (ImuInit() != 0) {
    int who = ImuGetLastWhoAmI();
    log_remote(
        "[%s] WARNING: Failed to initialize IMU (continuing without IMU)\n",
        TAG);
    if (who >= 0) {
      log_remote(
          "[%s] IMU WHO_AM_I=0x%02X (expected 0x68 MPU-6050 or 0x70 "
          "MPU-6500)\n",
          TAG, who);
    } else {
      log_remote(
          "[%s] IMU SPI read failed — check wiring: CS=GPIO8, SCK=6, MOSI=7, "
          "MISO=4, 3V3/GND\n",
          TAG);
    }
  } else {
    log_remote("[%s] IMU initialized OK (WHO_AM_I=0x%02X)\n", TAG,
               ImuGetLastWhoAmI());
  }

  // Инициализация failsafe
  log_remote("[%s] Initializing failsafe...\n", TAG);
  FailsafeInit();

  // Встроенный светодиод — индикатор работы
  gpio_init(PICO_DEFAULT_LED_PIN);
  gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);

  log_remote("[%s] All systems initialized. Starting main loop.\n", TAG);

  // Таймеры для периодических задач
  uint32_t last_pwm_update = time_us_32() / 1000;
  uint32_t last_rc_poll = time_us_32() / 1000;
  uint32_t last_imu_read = time_us_32() / 1000;
  uint32_t last_telem_send = time_us_32() / 1000;
  uint32_t last_failsafe_update = time_us_32() / 1000;
  uint32_t last_wifi_cmd_ms = 0;

  // Данные IMU
  ImuData imu_data = {0};
  TelemetryData telem_data = {0};
  uint16_t telem_seq = 0;

  // Инициализация счётчика последовательности для телеметрии
  telem_data.seq = 0;

  // Основной цикл
  while (true) {
    uint32_t now = time_us_32() / 1000;  // в миллисекундах

    // Обновление PWM (50 Hz)
    if (now - last_pwm_update >= PWM_UPDATE_INTERVAL_MS) {
      uint32_t dt_ms = now - last_pwm_update;
      last_pwm_update = now;

      // Slew-rate limiting: applied_* тянется к commanded_*
      applied_throttle = ApplySlewRate(commanded_throttle, applied_throttle,
                                       SLEW_RATE_THROTTLE_MAX_PER_SEC, dt_ms);
      applied_steering = ApplySlewRate(commanded_steering, applied_steering,
                                       SLEW_RATE_STEERING_MAX_PER_SEC, dt_ms);

      PwmControlSetThrottle(applied_throttle);
      PwmControlSetSteering(applied_steering);
    }

    // Опрос RC-in (50 Hz)
    if (now - last_rc_poll >= RC_IN_POLL_INTERVAL_MS) {
      last_rc_poll = now;

      auto rc_throttle = RcInputReadThrottle();
      auto rc_steering = RcInputReadSteering();

      rc_active = rc_throttle.has_value() && rc_steering.has_value();

      // RC имеет приоритет над Wi-Fi
      if (rc_active) {
        commanded_throttle = *rc_throttle;
        commanded_steering = *rc_steering;
      }
    }

    // Ответ на PING от ESP32 (проверка связи); светодиод переключается при
    // каждом PING.  При первом PING шлём сводку инициализации (стартовые логи
    // теряются, т.к. ESP32 ещё не готов принимать).
    while (UartBridgeReceivePing()) {
      UartBridgeSendPong();
      gpio_put(PICO_DEFAULT_LED_PIN, !gpio_get(PICO_DEFAULT_LED_PIN));

      static bool first_ping = true;
      if (first_ping) {
        first_ping = false;
        int who = ImuGetLastWhoAmI();
        if (who == 0x68 || who == 0x70) {
          log_remote("[%s] INIT OK: IMU WHO_AM_I=0x%02X\n", TAG, who);
        } else if (who >= 0) {
          log_remote(
              "[%s] INIT FAIL: IMU WHO_AM_I=0x%02X (expected 0x68 or 0x70)\n",
              TAG, who);
        } else {
          log_remote(
              "[%s] INIT FAIL: IMU SPI no response — check wiring: "
              "CS=GPIO8, SCK=6, MOSI=7, MISO=4, 3V3/GND\n",
              TAG);
        }
      }
    }

    // Чтение команд от ESP32 (Wi-Fi)
    if (auto cmd = UartBridgeReceiveCommand()) {
      // Wi-Fi команды принимаются только если RC не активен
      if (!rc_active) {
        commanded_throttle = cmd->throttle;
        commanded_steering = cmd->steering;
        last_wifi_cmd_ms = now;
      }
    }
    // Wi‑Fi активен, если команда приходила недавно и RC не активен
    wifi_active =
        (!rc_active) && ((now - last_wifi_cmd_ms) < WIFI_CMD_TIMEOUT_MS);

    // Чтение IMU (50 Hz)
    if (now - last_imu_read >= IMU_READ_INTERVAL_MS) {
      last_imu_read = now;
      int imu_rc = ImuRead(imu_data);

      // Периодический лог IMU (каждые 2 с) для отладки
      static uint32_t last_imu_dbg = 0;
      if (now - last_imu_dbg >= 2000) {
        last_imu_dbg = now;
        log_remote("[imu] rc=%d ax=%.3f ay=%.3f az=%.3f gx=%.1f gy=%.1f gz=%.1f\n",
                   imu_rc, imu_data.ax, imu_data.ay, imu_data.az,
                   imu_data.gx, imu_data.gy, imu_data.gz);
      }
    }

    // Обновление failsafe
    if (now - last_failsafe_update >= 10) {  // Каждые 10 мс
      last_failsafe_update = now;
      bool failsafe = FailsafeUpdate(rc_active, wifi_active);

      if (failsafe) {
        // Failsafe активен: нейтраль
        commanded_throttle = 0.0f;
        commanded_steering = 0.0f;
        applied_throttle = 0.0f;
        applied_steering = 0.0f;
        PwmControlSetNeutral();
      }
    }

    // Отправка телеметрии (20 Hz)
    if (now - last_telem_send >= TELEM_SEND_INTERVAL_MS) {
      last_telem_send = now;

      // Формирование данных телеметрии
      telem_data.seq = telem_seq++;
      telem_data.status = 0;
      if (rc_active) telem_data.status |= 0x01;    // bit0: rc_ok
      if (wifi_active) telem_data.status |= 0x02;  // bit1: wifi_ok
      if (FailsafeIsActive())
        telem_data.status |= 0x04;  // bit2: failsafe_active

      // Конвертация IMU данных
      ImuConvertToTelem(imu_data, telem_data.ax, telem_data.ay, telem_data.az,
                        telem_data.gx, telem_data.gy, telem_data.gz);

      // Отправка телеметрии
      UartBridgeSendTelem(telem_data);
    }

    // Небольшая задержка для снижения нагрузки на CPU
    sleep_us(1000);  // 1 мс
  }

  return 0;
}
