#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * Построить кадр COMMAND для отправки на RP2040
 * @param buffer буфер для кадра
 * @param buffer_size размер буфера
 * @param throttle значение газа [-1.0..1.0]
 * @param steering значение руля [-1.0..1.0]
 * @return длина сформированного кадра, 0 при ошибке
 */
size_t ProtocolBuildCommand(uint8_t* buffer, size_t buffer_size, float throttle,
                            float steering);

/**
 * Парсить кадр TELEM от RP2040
 * @param buffer буфер с данными
 * @param buffer_size размер буфера
 * @param telem_data указатель на структуру для данных (будет заполнена)
 * @return длина обработанного кадра, 0 при ошибке
 */
size_t ProtocolParseTelem(const uint8_t* buffer, size_t buffer_size,
                          void* telem_data);

/**
 * Вычислить CRC16 (CRC-16/IBM, Modbus)
 * @param data данные
 * @param length длина данных
 * @return CRC16 значение
 */
uint16_t ProtocolCrc16(const uint8_t* data, size_t length);
