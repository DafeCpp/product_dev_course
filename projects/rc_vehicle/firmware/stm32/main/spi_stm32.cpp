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

int SpiBusStm32::Init() {
  if (inited_) return 0;
  if (spi_initialized) {
    inited_ = true;
    return 0;
  }

  RCC_GPIO_SPI_PORT;
  RCC_SPI_PERIPH;

#if defined(STM32F1)
  LL_GPIO_SetPinMode(SPI_SCK_PORT, SPI_SCK_PIN_MASK, LL_GPIO_MODE_ALTERNATE);
  LL_GPIO_SetPinSpeed(SPI_SCK_PORT, SPI_SCK_PIN_MASK, LL_GPIO_SPEED_FREQ_HIGH);
  LL_GPIO_SetPinMode(SPI_MISO_PORT, SPI_MISO_PIN_MASK, LL_GPIO_MODE_FLOATING);
  LL_GPIO_SetPinMode(SPI_MOSI_PORT, SPI_MOSI_PIN_MASK, LL_GPIO_MODE_ALTERNATE);
  LL_GPIO_SetPinSpeed(SPI_MOSI_PORT, SPI_MOSI_PIN_MASK, LL_GPIO_SPEED_FREQ_HIGH);
#else
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
  inited_ = true;
  return 0;
}

SpiDeviceStm32::SpiDeviceStm32(SpiBusStm32 &bus)
    : SpiDeviceStm32(bus, static_cast<void *>(SPI_NCS_PORT), SPI_NCS_PIN) {}

SpiDeviceStm32::SpiDeviceStm32(SpiBusStm32 &bus, void *cs_port,
                               unsigned int cs_pin)
    : bus_(bus), cs_port_(cs_port), cs_pin_mask_(1U << cs_pin) {}

void SpiDeviceStm32::CsLow() {
  LL_GPIO_ResetOutputPin(static_cast<GPIO_TypeDef *>(cs_port_), cs_pin_mask_);
}

void SpiDeviceStm32::CsHigh() {
  LL_GPIO_SetOutputPin(static_cast<GPIO_TypeDef *>(cs_port_), cs_pin_mask_);
}

int SpiDeviceStm32::Init() {
  if (inited_) return 0;
  if (bus_.Init() != 0) return -1;

  auto *port = static_cast<GPIO_TypeDef *>(cs_port_);
  LL_GPIO_SetPinMode(port, cs_pin_mask_, LL_GPIO_MODE_OUTPUT);
  LL_GPIO_SetOutputPin(port, cs_pin_mask_);

#if defined(STM32F1)
  LL_GPIO_SetPinSpeed(port, cs_pin_mask_, LL_GPIO_SPEED_FREQ_LOW);
#else
  LL_GPIO_SetPinOutputType(port, cs_pin_mask_, LL_GPIO_OUTPUT_PUSHPULL);
#endif

  inited_ = true;
  return 0;
}

int SpiDeviceStm32::Transfer(std::span<const uint8_t> tx,
                             std::span<uint8_t> rx) {
  if (!inited_) return -1;
  if (tx.size() == 0 || tx.size() != rx.size()) return -1;

  CsLow();
  for (size_t i = 0; i < tx.size(); i++) {
    wait_txe();
    LL_SPI_TransmitData8(SPI_PERIPH, tx[i]);
    wait_rxne();
    rx[i] = LL_SPI_ReceiveData8(SPI_PERIPH);
  }
  wait_not_busy();
  CsHigh();
  return 0;
}
