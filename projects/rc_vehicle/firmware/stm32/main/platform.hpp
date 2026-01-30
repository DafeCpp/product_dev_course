#pragma once

#include <stdint.h>

uint32_t platform_get_time_ms(void);
void platform_delay_ms(uint32_t ms);
void platform_init(void);
