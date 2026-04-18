#pragma once

// Stub for ESP-IDF logging macros — used in host-side GTests
// All macros expand to no-op; tests verify behavior via MockPlatform::Log()

#define ESP_LOGE(tag, fmt, ...) ((void)0)
#define ESP_LOGW(tag, fmt, ...) ((void)0)
#define ESP_LOGI(tag, fmt, ...) ((void)0)
#define ESP_LOGD(tag, fmt, ...) ((void)0)
#define ESP_LOGV(tag, fmt, ...) ((void)0)
