#include <stdio.h>

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

  // Инициализация PWM
  printf("[%s] Initializing PWM...\n", TAG);
  if (PwmControlInit() != 0) {
    printf("[%s] ERROR: Failed to initialize PWM\n", TAG);
    return -1;
  }

  // Инициализация RC-in
  printf("[%s] Initializing RC input...\n", TAG);
  if (RcInputInit() != 0) {
    printf("[%s] ERROR: Failed to initialize RC input\n", TAG);
    return -1;
  }

  // Инициализация IMU
  printf("[%s] Initializing IMU...\n", TAG);
  if (ImuInit() != 0) {
    int who = ImuGetLastWhoAmI();
    printf("[%s] WARNING: Failed to initialize IMU (continuing without IMU)\n",
           TAG);
    if (who >= 0) {
      printf(
          "[%s] IMU WHO_AM_I=0x%02X (expected 0x68 MPU-6050 or 0x70 "
          "MPU-6500)\n",
          TAG, who);
    } else {
      printf(
          "[%s] IMU SPI read failed — check wiring: CS=GPIO8, SCK=6, MOSI=7, "
          "MISO=9, 3V3/GND\n",
          TAG);
    }
  }

  // Инициализация UART моста
  printf("[%s] Initializing UART bridge...\n", TAG);
  if (UartBridgeInit() != 0) {
    printf("[%s] ERROR: Failed to initialize UART bridge\n", TAG);
    return -1;
  }

  // Инициализация failsafe
  printf("[%s] Initializing failsafe...\n", TAG);
  FailsafeInit();

  // Встроенный светодиод — индикатор работы
  gpio_init(PICO_DEFAULT_LED_PIN);
  gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);

  printf("[%s] All systems initialized. Starting main loop.\n", TAG);

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
    // каждом PING
    while (UartBridgeReceivePing()) {
      UartBridgeSendPong();
      gpio_put(PICO_DEFAULT_LED_PIN, !gpio_get(PICO_DEFAULT_LED_PIN));
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
      ImuRead(imu_data);
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
