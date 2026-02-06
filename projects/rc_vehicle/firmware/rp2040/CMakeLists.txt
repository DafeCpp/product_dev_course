# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

cmake_minimum_required(VERSION 3.13)

# Включаем Pico SDK
include(pico_sdk_import.cmake)

project(rc_vehicle_rp2040)

# Инициализация Pico SDK
pico_sdk_init()

# C++26 (если поддерживается компилятором), иначе C++23.
set(RC_VEHICLE_CXX_STANDARD 23)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND
   CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 14)
  set(RC_VEHICLE_CXX_STANDARD 26)
endif()

set(CMAKE_CXX_STANDARD ${RC_VEHICLE_CXX_STANDARD})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

# Добавляем main компонент
add_subdirectory(main)

