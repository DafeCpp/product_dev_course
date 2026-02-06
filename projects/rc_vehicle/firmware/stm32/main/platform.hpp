#pragma once

#include <stdint.h>

uint32_t PlatformGetTimeMs();
void PlatformDelayMs(uint32_t ms);
void PlatformInit();
