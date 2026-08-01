include(${CMAKE_CURRENT_LIST_DIR}/../../../../firmware_common/sources.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../../../firmware_common/esp32/sources_esp32.cmake)

idf_component_register(
    SRCS
        "main.cpp"
        "stabilization_config_json.cpp"
        "ws_command_registry.cpp"
        "ws_command_handlers.cpp"
        "../../esp32_common/rc_http_routes.cpp"
        "../../common/stabilization_config.cpp"
        "../../common/stabilization_pipeline.cpp"
        "../../common/drive_modes.cpp"
        "../../common/drive_mode_registry.cpp"
        "../../common/kids_mode_processor.cpp"
        "../../common/vehicle_control_unified.cpp"
        "../../common/vehicle_control_unified_init.cpp"
        "../../common/auto_drive_coordinator.cpp"
        "../../common/test_runner.cpp"
        "../../common/speed_calibration.cpp"
        "../../common/com_offset_calibration.cpp"
        "../../common/steering_trim_calibration.cpp"
        "../../common/telemetry_builder.cpp"
        "../../common/diagnostics_reporter.cpp"
        "../../common/control_loop_helpers.cpp"
        "../../common/control_loop_processor.cpp"
        "../../common/self_test.cpp"
        "../../common/calibration_manager.cpp"
        "../../common/stabilization_manager.cpp"
        "../../common/telemetry_manager.cpp"
        "../../common/telemetry_log.cpp"
        "../../common/telemetry_event_log.cpp"
        "../../common/telemetry_config_snapshot.cpp"
        "../../common/motion_driver.cpp"
        "../../common/vehicle_ekf.cpp"
        "../../common/vehicle_state_estimator.cpp"
        "../../common/tilt_estimator.cpp"
        "vehicle_control_platform_esp32.cpp"
        "pwm_control.cpp"
        "rc_input.cpp"
        "spi_esp32.cpp"
        "imu.cpp"
        "mag.cpp"
        "../../common/mpu6050_spi.cpp"
        "../../common/lsm6ds3_spi.cpp"
        "../../common/mmc5983_spi.cpp"
        "../../esp32_common/mmc5983_i2c.cpp"
        "../../common/mag_calibration.cpp"
        "../../common/failsafe.cpp"
        "../../common/control_components.cpp"
        ${FIRMWARE_COMMON_SOURCES}
        ${FIRMWARE_COMMON_ESP32_SOURCES}
        "../../common/imu_calibration.cpp"
        "../../common/madgwick_filter.cpp"
        "../../common/lpf_butterworth.cpp"
        "../../esp32_common/imu_calibration_nvs.cpp"
        "../../esp32_common/mag_calibration_nvs.cpp"
        "../../esp32_common/stabilization_config_nvs.cpp"
        "../../esp32_common/crash_logger.cpp"
        "../../esp32_common/udp_telem_sender.cpp"
    INCLUDE_DIRS
        "."
        "../../common"
        ${FIRMWARE_COMMON_INCLUDE_DIRS}
        ${FIRMWARE_COMMON_ESP32_INCLUDE_DIRS}
        "../../esp32_common"
    PRIV_REQUIRES
        esp_wifi
        esp_http_server
        nvs_flash
        # ESP-IDF v6 splits drivers into esp_driver_* components.
        esp_driver_gpio
        esp_driver_ledc
        esp_driver_spi
        esp_driver_i2c
        lwip
        freertos
        cjson
        esp_timer
)

# C++26 (если поддерживается компилятором), иначе C++23.
set(RC_VEHICLE_CXX_STANDARD 26)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND
   CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 14)
    set(RC_VEHICLE_CXX_STANDARD 26)
endif()

set_property(TARGET ${COMPONENT_LIB} PROPERTY CXX_STANDARD ${RC_VEHICLE_CXX_STANDARD})
set_property(TARGET ${COMPONENT_LIB} PROPERTY CXX_STANDARD_REQUIRED ON)
set_property(TARGET ${COMPONENT_LIB} PROPERTY CXX_EXTENSIONS ON)
