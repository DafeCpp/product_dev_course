#include "uart_bridge.hpp"

#include <string.h>

#include "config.hpp"
#include "hardware/uart.h"
#include "pico/stdlib.h"
#include "protocol.hpp"

static uint8_t uart_rx_buffer[UART_BUF_SIZE];
static size_t uart_rx_buffer_pos = 0;

int UartBridgeInit(void) {
  // Инициализация UART
  uart_init(UART_ID, UART_BAUD_RATE);

  // Настройка GPIO
  gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
  gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);

  // Очистка буфера
  uart_rx_buffer_pos = 0;
  memset(uart_rx_buffer, 0, sizeof(uart_rx_buffer));

  return 0;
}

int UartBridgeSendTelem(const void *telem_data) {
  if (telem_data == NULL) {
    return -1;
  }

  uint8_t frame_buffer[32];
  size_t frame_len = ProtocolBuildTelem(frame_buffer, sizeof(frame_buffer),
                                        (const TelemetryData *)telem_data);

  if (frame_len == 0) {
    return -1;
  }

  // Отправка кадра
  uart_write_blocking(UART_ID, frame_buffer, frame_len);

  return 0;
}

bool UartBridgeReceiveCommand(float *throttle, float *steering) {
  if (throttle == NULL || steering == NULL) {
    return false;
  }

  // Чтение доступных данных
  while (uart_is_readable(UART_ID) &&
         uart_rx_buffer_pos < sizeof(uart_rx_buffer)) {
    uart_rx_buffer[uart_rx_buffer_pos++] = uart_getc(UART_ID);
  }

  // Поиск начала кадра
  int frame_start = ProtocolFindFrameStart(uart_rx_buffer, uart_rx_buffer_pos);
  if (frame_start < 0) {
    // Кадр не найден, очищаем буфер если он переполнен
    if (uart_rx_buffer_pos >= sizeof(uart_rx_buffer) - 1) {
      uart_rx_buffer_pos = 0;
    }
    return false;
  }

  // Сдвигаем буфер к началу кадра
  if (frame_start > 0) {
    memmove(uart_rx_buffer, &uart_rx_buffer[frame_start],
            uart_rx_buffer_pos - frame_start);
    uart_rx_buffer_pos -= frame_start;
  }

  // Парсинг кадра (минимальная длина кадра COMMAND = 16 байт)
  if (uart_rx_buffer_pos < 16) {
    return false; // Недостаточно данных
  }

  size_t parsed_len = ProtocolParseCommand(uart_rx_buffer, uart_rx_buffer_pos,
                                           throttle, steering);

  if (parsed_len > 0) {
    // Удаляем обработанный кадр из буфера
    memmove(uart_rx_buffer, &uart_rx_buffer[parsed_len],
            uart_rx_buffer_pos - parsed_len);
    uart_rx_buffer_pos -= parsed_len;
    return true;
  }

  // Если кадр невалидный, удаляем первый байт и продолжаем поиск
  if (uart_rx_buffer_pos > 0) {
    memmove(uart_rx_buffer, &uart_rx_buffer[1], uart_rx_buffer_pos - 1);
    uart_rx_buffer_pos--;
  }

  return false;
}
