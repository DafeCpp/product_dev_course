#pragma once

#include <stddef.h>
#include <stdint.h>

// Структура для данных телеметрии
struct TelemetryData {
  uint16_t seq;
  uint8_t status;     // bit0: rc_ok, bit1: wifi_ok, bit2: failsafe_active
  int16_t ax, ay, az; // Акселерометр (mg)
  int16_t gx, gy, gz; // Гироскоп (mdps)
};

/**
 * Построить кадр TELEM для отправки на ESP32
 * @param buffer буфер для кадра
 * @param buffer_size размер буфера
 * @param telem_data указатель на данные телеметрии
 * @return длина сформированного кадра, 0 при ошибке
 */
size_t ProtocolBuildTelem(uint8_t *buffer, size_t buffer_size,
                          const TelemetryData *telem_data);

/**
 * Парсить кадр COMMAND от ESP32
 * @param buffer буфер с данными
 * @param buffer_size размер буфера
 * @param throttle указатель для значения газа (будет заполнен)
 * @param steering указатель для значения руля (будет заполнен)
 * @return длина обработанного кадра, 0 при ошибке
 */
size_t ProtocolParseCommand(const uint8_t *buffer, size_t buffer_size,
                            float *throttle, float *steering);

/**
 * Вычислить CRC16 (CRC-16/IBM, Modbus)
 * @param data данные
 * @param length длина данных
 * @return CRC16 значение
 */
uint16_t ProtocolCrc16(const uint8_t *data, size_t length);

/**
 * Найти начало кадра в буфере (префикс AA 55)
 * @param buffer буфер для поиска
 * @param buffer_size размер буфера
 * @return индекс начала кадра, -1 если не найден
 */
int ProtocolFindFrameStart(const uint8_t *buffer, size_t buffer_size);
