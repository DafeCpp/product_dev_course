#include "config.hpp"
#include "failsafe.hpp"
#include "imu.hpp"
#include "platform.hpp"
#include "protocol.hpp"
#include "pwm_control.hpp"
#include "rc_input.hpp"
#include "slew_rate.hpp"
#include "uart_bridge.hpp"

#if defined(STM32F1)
#include <stm32f1xx.h>
#elif defined(STM32F4)
#include <stm32f4xx.h>
#elif defined(STM32G4)
#include <stm32g4xx.h>
#endif

int main(void) {
  SystemInit();
  PlatformInit();

  PwmControlInit();
  RcInputInit();
  ImuInit();
  UartBridgeInit();
  FailsafeInit();

  uint32_t last_pwm = PlatformGetTimeMs();
  uint32_t last_rc = PlatformGetTimeMs();
  uint32_t last_imu = PlatformGetTimeMs();
  uint32_t last_telem = PlatformGetTimeMs();
  uint32_t last_failsafe = PlatformGetTimeMs();
  uint32_t last_wifi_cmd_ms = 0;

  // commanded_* — что хотим (RC/Wi‑Fi), applied_* — что реально подаём на PWM
  // (slew-rate)
  float commanded_throttle = 0.0f;
  float commanded_steering = 0.0f;
  float applied_throttle = 0.0f;
  float applied_steering = 0.0f;
  bool rc_active = false;
  bool wifi_active = false;

  ImuData imu_data = {0};
  TelemetryData telem_data = {0};
  uint16_t telem_seq = 0;

  while (1) {
    uint32_t now = PlatformGetTimeMs();

    if (now - last_pwm >= PWM_UPDATE_INTERVAL_MS) {
      uint32_t dt = now - last_pwm;
      last_pwm = now;
      applied_throttle = ApplySlewRate(commanded_throttle, applied_throttle,
                                       SLEW_RATE_THROTTLE_MAX_PER_SEC, dt);
      applied_steering = ApplySlewRate(commanded_steering, applied_steering,
                                       SLEW_RATE_STEERING_MAX_PER_SEC, dt);
      PwmControlSetThrottle(applied_throttle);
      PwmControlSetSteering(applied_steering);
    }

    if (now - last_rc >= RC_IN_POLL_INTERVAL_MS) {
      last_rc = now;
      auto rc_thr = RcInputReadThrottle();
      auto rc_str = RcInputReadSteering();
      rc_active = rc_thr.has_value() && rc_str.has_value();
      if (rc_active) {
        commanded_throttle = *rc_thr;
        commanded_steering = *rc_str;
      }
    }

    while (UartBridgeReceivePing()) {
      UartBridgeSendPong();
    }

    if (auto cmd = UartBridgeReceiveCommand()) {
      if (!rc_active) {
        commanded_throttle = cmd->throttle;
        commanded_steering = cmd->steering;
        last_wifi_cmd_ms = now;
      }
    }
    wifi_active =
        (!rc_active) && ((now - last_wifi_cmd_ms) < WIFI_CMD_TIMEOUT_MS);

    if (now - last_imu >= IMU_READ_INTERVAL_MS) {
      last_imu = now;
      ImuRead(imu_data);
    }

    if (now - last_failsafe >= 10) {
      last_failsafe = now;
      if (FailsafeUpdate(rc_active, wifi_active)) {
        commanded_throttle = 0.0f;
        commanded_steering = 0.0f;
        applied_throttle = 0.0f;
        applied_steering = 0.0f;
        PwmControlSetNeutral();
      }
    }

    if (now - last_telem >= TELEM_SEND_INTERVAL_MS) {
      last_telem = now;
      telem_data.seq = telem_seq++;
      telem_data.status = 0;
      if (rc_active) telem_data.status |= 0x01;
      if (wifi_active) telem_data.status |= 0x02;
      if (FailsafeIsActive()) telem_data.status |= 0x04;
      ImuConvertToTelem(imu_data, telem_data.ax, telem_data.ay, telem_data.az,
                        telem_data.gx, telem_data.gy, telem_data.gz);
      UartBridgeSendTelem(telem_data);
    }

    PlatformDelayMs(1);
  }

  return 0;
}
