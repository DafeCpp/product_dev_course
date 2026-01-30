#include "platform.hpp"

#include <stdint.h>

#if defined(STM32F1)
#include <stm32f1xx.h>
#elif defined(STM32F4)
#include <stm32f4xx.h>
#elif defined(STM32G4)
#include <stm32g4xx.h>
#endif

static volatile uint32_t s_millis = 0;

extern "C" void SysTick_Handler(void) { s_millis++; }

uint32_t platform_get_time_ms(void) { return s_millis; }

void platform_delay_ms(uint32_t ms) {
  uint32_t end = s_millis + ms;
  while (s_millis < end) {
    __NOP();
  }
}

void platform_init(void) {
  // SysTick: 1 ms. SystemCoreClock задаётся в system_stm32f1xx.c (и др.) после SystemInit()
  if (SysTick_Config(SystemCoreClock / 1000u) != 0u) {
    for (;;) { __NOP(); }
  }
  NVIC_SetPriority(SysTick_IRQn, 0x80u);
}
