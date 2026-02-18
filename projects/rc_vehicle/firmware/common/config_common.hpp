#pragma once

// Общие параметры, не зависящие от конкретной платформы/пинов.
// Подключается из platform config.hpp.

// Таймаут активности Wi‑Fi команд (если команда не приходила недавно — wifi_active=false).
#ifndef WIFI_CMD_TIMEOUT_MS
#define WIFI_CMD_TIMEOUT_MS 250
#endif

// Тайминги control loop (можно переопределить в platform config.hpp).
#ifndef CONTROL_LOOP_PERIOD_MS
#define CONTROL_LOOP_PERIOD_MS 2
#endif
#ifndef PWM_UPDATE_INTERVAL_MS
#define PWM_UPDATE_INTERVAL_MS 20
#endif
#ifndef RC_IN_POLL_INTERVAL_MS
#define RC_IN_POLL_INTERVAL_MS 20
#endif
#ifndef IMU_READ_INTERVAL_MS
#define IMU_READ_INTERVAL_MS 2
#endif
#ifndef TELEM_SEND_INTERVAL_MS
#define TELEM_SEND_INTERVAL_MS 50
#endif
#ifndef SLEW_RATE_THROTTLE_MAX_PER_SEC
#define SLEW_RATE_THROTTLE_MAX_PER_SEC 0.5f
#endif
#ifndef SLEW_RATE_STEERING_MAX_PER_SEC
#define SLEW_RATE_STEERING_MAX_PER_SEC 1.0f
#endif

