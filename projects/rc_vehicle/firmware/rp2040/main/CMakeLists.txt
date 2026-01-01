add_executable(rc_vehicle_rp2040
    main.cpp
    pwm_control.cpp
    rc_input.cpp
    imu.cpp
    failsafe.cpp
    uart_bridge.cpp
    protocol.cpp
)

# Установка стандарта C++23
set_target_properties(rc_vehicle_rp2040 PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)

target_link_libraries(rc_vehicle_rp2040
    pico_stdlib
    hardware_pwm
    hardware_uart
    hardware_i2c
    hardware_gpio
)

# Включение USB Serial для отладки
pico_enable_stdio_usb(rc_vehicle_rp2040 1)
pico_enable_stdio_uart(rc_vehicle_rp2040 0)

# Создание .uf2 файла для прошивки
pico_add_extra_outputs(rc_vehicle_rp2040)

