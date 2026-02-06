#include "spi_stm32.hpp"

#include "board_pins.hpp"

#if defined(STM32F1)
#include <stm32f1xx.h>
#include <stm32f1xx_ll_gpio.h>
#include <stm32f1xx_ll_rcc.h>
#include <stm32f1xx_ll_spi.h>
#elif defined(STM32F4)
#include <stm32f4xx.h>
#include <stm32f4xx_ll_gpio.h>
#include <stm32f4xx_ll_rcc.h>
#include <stm32f4xx_ll_spi.h>
#elif defined(STM32G4)
#include <stm32g4xx.h>
#include <stm32g4xx_ll_gpio.h>
#include <stm32g4xx_ll_rcc.h>
#include <stm32g4xx_ll_spi.h>
#endif


#define SPI_NCS_PIN_MASK (1U << SPI_NCS_PIN)
#define SPI_SCK_PIN_MASK (1U << SPI_SCK_PIN)
#define SPI_MISO_PIN_MASK (1U << SPI_MISO_PIN)
#define SPI_MOSI_PIN_MASK (1U << SPI_MOSI_PIN)

#if defined(STM32F1)
#define RCC_GPIO_SPI_PORT LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_GPIOB)
#define RCC_SPI_PERIPH LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_SPI2)
#elif defined(STM32F4)
#define RCC_GPIO_SPI_PORT LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOB)
#define RCC_SPI_PERIPH LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_SPI2)
#elif defined(STM32G4)
#define RCC_GPIO_SPI_PORT LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOB)
#define RCC_SPI_PERIPH LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_SPI2)
#endif

static bool spi_initialized = false;

static void cs_low(void) {
  LL_GPIO_ResetOutputPin(SPI_NCS_PORT, SPI_NCS_PIN_MASK);
}

static void cs_high(void) {
  LL_GPIO_SetOutputPin(SPI_NCS_PORT, SPI_NCS_PIN_MASK);
}

static void wait_txe(void) {
  while (!LL_SPI_IsActiveFlag_TXE(SPI_PERIPH))
    ;
}

static void wait_rxne(void) {
  while (!LL_SPI_IsActiveFlag_RXNE(SPI_PERIPH))
    ;
}

static void wait_not_busy(void) {
  while (LL_SPI_IsActiveFlag_BSY(SPI_PERIPH))
    ;
}

int SpiStm32::Init() {
  if (spi_initialized)
    return 0;

  RCC_GPIO_SPI_PORT;
  RCC_SPI_PERIPH;

  LL_GPIO_SetPinMode(SPI_NCS_PORT, SPI_NCS_PIN_MASK, LL_GPIO_MODE_OUTPUT);
  LL_GPIO_SetOutputPin(SPI_NCS_PORT, SPI_NCS_PIN_MASK);

#if defined(STM32F1)
  LL_GPIO_SetPinSpeed(SPI_NCS_PORT, SPI_NCS_PIN_MASK, LL_GPIO_SPEED_FREQ_LOW);
  LL_GPIO_SetPinMode(SPI_SCK_PORT, SPI_SCK_PIN_MASK, LL_GPIO_MODE_ALTERNATE);
  LL_GPIO_SetPinSpeed(SPI_SCK_PORT, SPI_SCK_PIN_MASK, LL_GPIO_SPEED_FREQ_HIGH);
  LL_GPIO_SetPinMode(SPI_MISO_PORT, SPI_MISO_PIN_MASK, LL_GPIO_MODE_FLOATING);
  LL_GPIO_SetPinMode(SPI_MOSI_PORT, SPI_MOSI_PIN_MASK, LL_GPIO_MODE_ALTERNATE);
  LL_GPIO_SetPinSpeed(SPI_MOSI_PORT, SPI_MOSI_PIN_MASK, LL_GPIO_SPEED_FREQ_HIGH);
#else
  LL_GPIO_SetPinOutputType(SPI_NCS_PORT, SPI_NCS_PIN_MASK,
                           LL_GPIO_OUTPUT_PUSHPULL);
  LL_GPIO_SetPinMode(SPI_SCK_PORT, SPI_SCK_PIN_MASK, LL_GPIO_MODE_ALTERNATE);
  LL_GPIO_SetPinMode(SPI_MISO_PORT, SPI_MISO_PIN_MASK, LL_GPIO_MODE_ALTERNATE);
  LL_GPIO_SetPinMode(SPI_MOSI_PORT, SPI_MOSI_PIN_MASK, LL_GPIO_MODE_ALTERNATE);
  LL_GPIO_SetAFPin_8_15(SPI_SCK_PORT, SPI_SCK_PIN_MASK, LL_GPIO_AF_5);
  LL_GPIO_SetAFPin_8_15(SPI_MISO_PORT, SPI_MISO_PIN_MASK, LL_GPIO_AF_5);
  LL_GPIO_SetAFPin_8_15(SPI_MOSI_PORT, SPI_MOSI_PIN_MASK, LL_GPIO_AF_5);
#endif

  LL_SPI_InitTypeDef init = {0};
  init.TransferDirection = LL_SPI_FULL_DUPLEX;
  init.Mode = LL_SPI_MODE_MASTER;
  init.DataWidth = LL_SPI_DATAWIDTH_8BIT;
  init.ClockPolarity = LL_SPI_POLARITY_LOW;
  init.ClockPhase = LL_SPI_PHASE_1EDGE;
  init.NSS = LL_SPI_NSS_SOFT;
  init.BaudRate = LL_SPI_BAUDRATEPRESCALER_DIV32;
  init.BitOrder = LL_SPI_MSB_FIRST;
  LL_SPI_Init(SPI_PERIPH, &init);
  LL_SPI_Enable(SPI_PERIPH);

  spi_initialized = true;
  return 0;
}

int SpiStm32::Transfer(std::span<const uint8_t> tx, std::span<uint8_t> rx) {
  if (tx.size() == 0 || tx.size() != rx.size()) return -1;

  cs_low();
  for (size_t i = 0; i < tx.size(); i++) {
    wait_txe();
    LL_SPI_TransmitData8(SPI_PERIPH, tx[i]);
    wait_rxne();
    rx[i] = LL_SPI_ReceiveData8(SPI_PERIPH);
  }
  wait_not_busy();
  cs_high();
  return 0;
}
