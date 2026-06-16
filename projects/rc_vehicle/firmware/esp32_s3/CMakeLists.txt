# Минимальный CMakeLists.txt для ESP-IDF проекта (ESP32-S3)
cmake_minimum_required(VERSION 3.16)

# Включаем ESP-IDF
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Отладочный override интервала отправки телеметрии (мс).
# Без флага — дефолт из config.hpp (50 мс / 20 Гц), поведение не меняется.
#   idf.py build -DRC_TELEM_SEND_INTERVAL_MS=10   # 100 Гц
#
# ВАЖНО: НЕ передавать это через -DCMAKE_CXX_FLAGS=... — ESP-IDF подменяет
# CMAKE_CXX_FLAGS своим response-файлом на этапе configure, и пользовательское
# значение теряется. COMPILE_DEFINITIONS IDF не трогает, поэтому флаг доезжает
# до компиляции всех компонентов. Свойство задаётся ДО project().
if(DEFINED RC_TELEM_SEND_INTERVAL_MS)
  idf_build_set_property(COMPILE_DEFINITIONS
    "RC_TELEM_SEND_INTERVAL_MS=${RC_TELEM_SEND_INTERVAL_MS}" APPEND)
endif()

# Определение проекта
project(rc_vehicle_esp32_s3)

