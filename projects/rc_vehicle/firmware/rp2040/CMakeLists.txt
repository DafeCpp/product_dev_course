cmake_minimum_required(VERSION 3.13)

# Включаем Pico SDK
include(pico_sdk_import.cmake)

project(rc_vehicle_rp2040)

# Инициализация Pico SDK
pico_sdk_init()

# Добавляем main компонент
add_subdirectory(main)

