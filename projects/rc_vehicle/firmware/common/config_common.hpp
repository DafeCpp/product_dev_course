#pragma once

// Общие параметры, не зависящие от конкретной платформы/пинов.
// Подключается из platform config.hpp.

// Таймаут активности Wi‑Fi команд (если команда не приходила недавно — wifi_active=false).
#ifndef WIFI_CMD_TIMEOUT_MS
#define WIFI_CMD_TIMEOUT_MS 250
#endif

