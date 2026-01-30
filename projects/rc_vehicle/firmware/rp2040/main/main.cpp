#include <stdio.h>

#include "config.hpp"
#include "failsafe.hpp"
#include "imu.hpp"
#include "pico/stdlib.h"
#include "protocol.hpp"
#include "pwm_control.hpp"
#include "rc_input.hpp"
#include "slew_rate.hpp"
#include "uart_bridge.hpp"

static const char *TAG = "main";

static float current_throttle = 0.0f;
static float current_steering = 0.0f;
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
    printf("[%s] WARNING: Failed to initialize IMU (continuing without IMU)\n",
           TAG);
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

  printf("[%s] All systems initialized. Starting main loop.\n", TAG);

  // Таймеры для периодических задач
  uint32_t last_pwm_update = time_us_32() / 1000;
  uint32_t last_rc_poll = time_us_32() / 1000;
  uint32_t last_imu_read = time_us_32() / 1000;
  uint32_t last_telem_send = time_us_32() / 1000;
  uint32_t last_failsafe_update = time_us_32() / 1000;

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

      // Применение slew-rate limiting (опционально)
      float target_throttle = current_throttle;
      float target_steering = current_steering;

      target_throttle = ApplySlewRate(
          target_throttle, current_throttle,
          SLEW_RATE_THROTTLE_MAX_PER_SEC, dt_ms);
      target_steering = ApplySlewRate(
          target_steering, current_steering,
          SLEW_RATE_STEERING_MAX_PER_SEC, dt_ms);

      current_throttle = target_throttle;
      current_steering = target_steering;

      PwmControlSetThrottle(current_throttle);
      PwmControlSetSteering(current_steering);
    }

    // Опрос RC-in (50 Hz)
    if (now - last_rc_poll >= RC_IN_POLL_INTERVAL_MS) {
      last_rc_poll = now;

      float rc_throttle, rc_steering;
      bool rc_throttle_ok = RcInputReadThrottle(&rc_throttle);
      bool rc_steering_ok = RcInputReadSteering(&rc_steering);

      rc_active = rc_throttle_ok && rc_steering_ok;

      // RC имеет приоритет над Wi-Fi
      if (rc_active) {
        current_throttle = rc_throttle;
        current_steering = rc_steering;
        wifi_active = false;  // RC активен, Wi-Fi команды игнорируются
      }
    }

    // Чтение команд от ESP32 (Wi-Fi)
    if (auto cmd = UartBridgeReceiveCommand()) {
      // Wi-Fi команды принимаются только если RC не активен
      if (!rc_active) {
        current_throttle = cmd->throttle;
        current_steering = cmd->steering;
        wifi_active = true;
      } else {
        wifi_active = false;
      }
    } else {
      // Проверка таймаута Wi-Fi команд
      // (можно добавить таймер последней команды)
      wifi_active = false;
    }

    // Чтение IMU (50 Hz)
    if (now - last_imu_read >= IMU_READ_INTERVAL_MS) {
      last_imu_read = now;
      ImuRead(&imu_data);
    }

    // Обновление failsafe
    if (now - last_failsafe_update >= 10) {  // Каждые 10 мс
      last_failsafe_update = now;
      bool failsafe = FailsafeUpdate(rc_active, wifi_active);

      if (failsafe) {
        // Failsafe активен: нейтраль
        current_throttle = 0.0f;
        current_steering = 0.0f;
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
      ImuConvertToTelem(&imu_data, &telem_data.ax, &telem_data.ay,
                        &telem_data.az, &telem_data.gx, &telem_data.gy,
                        &telem_data.gz);

      // Отправка телеметрии
      UartBridgeSendTelem(telem_data);
    }

    // Небольшая задержка для снижения нагрузки на CPU
    sleep_us(1000);  // 1 мс
  }

  return 0;
}
