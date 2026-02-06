cmake_minimum_required(VERSION 3.13)
project(rc_vehicle_stm32 C CXX ASM)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# MCU по умолчанию: cmake -DMCU=STM32F411CE ..
if(NOT DEFINED MCU)
  set(MCU STM32F103C8)
endif()

# Конфиг платы (задаёт CUBE_PATH_VAR, CUBE_DEVICE, CMSIS_DEVICE_DIR, HAL_DRIVER_DIR и т.д.)
set(BOARD_CMAKE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/boards/${MCU}.cmake")
if(NOT EXISTS "${BOARD_CMAKE}")
  message(FATAL_ERROR "Неизвестный MCU: ${MCU}. Доступные: STM32F103C8, STM32F401CE, STM32F411CE, STM32G431CB")
endif()
include(${BOARD_CMAKE})

# Путь к STM32Cube пакету (по имени переменной из конфига платы)
if(NOT DEFINED ${CUBE_PATH_VAR})
  if(DEFINED ENV{${CUBE_PATH_VAR}})
    set(${CUBE_PATH_VAR} $ENV{${CUBE_PATH_VAR}})
  else()
    message(FATAL_ERROR
      "${CUBE_PATH_VAR} не задан. Клонируйте репозиторий Cube и укажите путь:\n"
      "  git clone --recursive https://github.com/STMicroelectronics/STM32CubeF1.git\n"
      "  cmake -B build -DMCU=${MCU} -D${CUBE_PATH_VAR}=$PWD/STM32CubeF1 ..\n"
      "  или: export ${CUBE_PATH_VAR}=/path/to/STM32CubeF1")
  endif()
endif()
set(CUBE_ROOT ${${CUBE_PATH_VAR}})
if(NOT IS_DIRECTORY "${CUBE_ROOT}/Drivers")
  message(FATAL_ERROR "Не найден каталог Drivers в ${CUBE_ROOT}. Проверьте путь к STM32Cube.")
endif()

# Поддержка LL-драйверов по семейству
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/cube_ll_sources.cmake)

# CMSIS: ядро (ARM) и устройство (ST). В GitHub Cube подмодули: CMSIS, cmsis_device_*.
set(CMSIS_CORE_DIR ${CUBE_ROOT}/Drivers/CMSIS)
set(CMSIS_DEVICE_DIR_FULL ${CUBE_ROOT}/Drivers/${CMSIS_DEVICE_DIR})
# Fallback: полный пакет с st.com использует Drivers/CMSIS/Device/ST/STM32F1xx
if(NOT EXISTS "${CMSIS_DEVICE_DIR_FULL}/Include")
  if(MCU_DEFINE STREQUAL "STM32F1")
    set(CMSIS_DEVICE_DIR_FULL ${CUBE_ROOT}/Drivers/CMSIS/Device/ST/STM32F1xx)
  elseif(MCU_DEFINE STREQUAL "STM32F4")
    set(CMSIS_DEVICE_DIR_FULL ${CUBE_ROOT}/Drivers/CMSIS/Device/ST/STM32F4xx)
  elseif(MCU_DEFINE STREQUAL "STM32G4")
    set(CMSIS_DEVICE_DIR_FULL ${CUBE_ROOT}/Drivers/CMSIS/Device/ST/STM32G4xx)
  endif()
endif()
set(CMSIS_DEVICE_INC ${CMSIS_DEVICE_DIR_FULL}/Include)
set(CMSIS_DEVICE_SRC ${CMSIS_DEVICE_DIR_FULL}/Source/Templates)
set(CMSIS_DEVICE_GCC ${CMSIS_DEVICE_SRC}/gcc)
set(HAL_DRIVER_DIR_FULL ${CUBE_ROOT}/Drivers/${HAL_DRIVER_DIR})
set(HAL_DRIVER_INC ${HAL_DRIVER_DIR_FULL}/Inc)
set(HAL_DRIVER_SRC ${HAL_DRIVER_DIR_FULL}/Src)

# Компилятор
set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)

# C++26 (если поддерживается компилятором), иначе C++23.
set(RC_VEHICLE_CXX_STANDARD 23)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND
   CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 14)
  set(RC_VEHICLE_CXX_STANDARD 26)
endif()
set(CMAKE_CXX_STANDARD ${RC_VEHICLE_CXX_STANDARD})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

# Флаги C/C++
set(CMAKE_C_FLAGS "${MCU_CFLAGS} -fdata-sections -ffunction-sections -Wall -Wextra")
set(CMAKE_CXX_FLAGS "${MCU_CFLAGS} -fdata-sections -ffunction-sections -Wall -Wextra")

# Linker script (GCC)
set(LD_SCRIPT_PATH ${CMSIS_DEVICE_GCC}/${LD_SCRIPT})
if(NOT EXISTS "${LD_SCRIPT_PATH}")
  # Альтернативное расположение в некоторых пакетах
  set(LD_SCRIPT_PATH ${CMSIS_DEVICE_SRC}/${LD_SCRIPT})
endif()
if(NOT EXISTS "${LD_SCRIPT_PATH}")
  message(FATAL_ERROR "Linker script не найден: ${LD_SCRIPT}. Проверьте CMSIS Device в ${CUBE_ROOT}")
endif()
set(CMAKE_EXE_LINKER_FLAGS "${MCU_CFLAGS} -Wl,--gc-sections -T${LD_SCRIPT_PATH} -nostartfiles -Wl,--no-warn-flags")

# Системный и стартовый файл (C + ASM). Имя system_* по семейству: f1xx, f4xx, g4xx
if(MCU_DEFINE STREQUAL "STM32F1")
  set(SYSTEM_C_FILE system_stm32f1xx.c)
elseif(MCU_DEFINE STREQUAL "STM32F4")
  set(SYSTEM_C_FILE system_stm32f4xx.c)
elseif(MCU_DEFINE STREQUAL "STM32G4")
  set(SYSTEM_C_FILE system_stm32g4xx.c)
endif()
set(SYSTEM_FILE ${CMSIS_DEVICE_SRC}/${SYSTEM_C_FILE})
set(STARTUP_FILE ${CMSIS_DEVICE_GCC}/${STARTUP_ASM})
if(NOT EXISTS "${STARTUP_FILE}")
  set(STARTUP_FILE ${CMSIS_DEVICE_SRC}/${STARTUP_ASM})
endif()

set(RC_VEHICLE_COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../common)

add_executable(rc_vehicle_stm32.elf
  main/main.cpp
  main/platform.cpp
  main/pwm_control.cpp
  main/rc_input.cpp
  main/imu.cpp
  main/spi_stm32.cpp
  main/failsafe.cpp
  main/uart_bridge.cpp
  ${RC_VEHICLE_COMMON_DIR}/protocol.cpp
  ${RC_VEHICLE_COMMON_DIR}/uart_bridge_base.cpp
  ${RC_VEHICLE_COMMON_DIR}/mpu6050_spi.cpp
  ${RC_VEHICLE_COMMON_DIR}/failsafe_core.cpp
  ${SYSTEM_FILE}
  ${STARTUP_FILE}
)

# LL-драйверы (только те .c, которые есть в пакете)
foreach(SRC ${CUBE_LL_SOURCES})
  if(EXISTS "${HAL_DRIVER_SRC}/${SRC}")
    target_sources(rc_vehicle_stm32.elf PRIVATE ${HAL_DRIVER_SRC}/${SRC})
  endif()
endforeach()

target_include_directories(rc_vehicle_stm32.elf PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/main
  ${RC_VEHICLE_COMMON_DIR}
  ${CMSIS_CORE_DIR}/Include
  ${CMSIS_DEVICE_INC}
  ${HAL_DRIVER_INC}
)

target_compile_definitions(rc_vehicle_stm32.elf PRIVATE
  ${CUBE_DEVICE}
  USE_FULL_LL_DRIVER
  HSE_VALUE=${HSE_VALUE}
  ${MCU_DEFINE}
  MCU_${MCU}
)

# Генерация .bin и .hex
add_custom_command(TARGET rc_vehicle_stm32.elf POST_BUILD
  COMMAND arm-none-eabi-objcopy -O binary rc_vehicle_stm32.elf rc_vehicle_stm32.bin
  COMMAND arm-none-eabi-objcopy -O ihex rc_vehicle_stm32.elf rc_vehicle_stm32.hex
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

message(STATUS "MCU: ${MCU} (${MCU_DEFINE}), STM32Cube: ${CUBE_ROOT}")
