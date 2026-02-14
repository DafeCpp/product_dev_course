#include "vehicle_control.hpp"

#include <stdlib.h>

#include "cJSON.h"
#include "config.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "failsafe.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "imu.hpp"
#include "pwm_control.hpp"
#include "rc_input.hpp"
#include "rc_vehicle_common.hpp"
#include "slew_rate.hpp"
#include "websocket_server.hpp"

static const char* TAG = "vehicle_control";

static constexpr uint32_t CONTROL_TASK_STACK = 6144;
static constexpr UBaseType_t CONTROL_TASK_PRIORITY = 5;

struct WifiCmd {
  float throttle{0.0f};
  float steering{0.0f};
};

static QueueHandle_t s_cmd_queue = nullptr;
static bool s_rc_enabled = false;
static bool s_imu_enabled = false;
static bool s_inited = false;

static uint32_t NowMs() { return (uint32_t)(esp_timer_get_time() / 1000); }

static void vehicle_control_task(void* arg) {
  (void)arg;

  // commanded_* — что хотим (RC/Wi‑Fi), applied_* — что реально подаём на PWM
  // (slew-rate)
  float commanded_throttle = 0.0f;
  float commanded_steering = 0.0f;
  float applied_throttle = 0.0f;
  float applied_steering = 0.0f;

  bool rc_active = false;
  bool wifi_active = false;

  uint32_t last_pwm_update = NowMs();
  uint32_t last_rc_poll = NowMs();
  uint32_t last_imu_read = NowMs();
  uint32_t last_telem_send = NowMs();
  uint32_t last_failsafe_update = NowMs();
  uint32_t last_wifi_cmd_ms = 0;

  ImuData imu_data = {0};

  while (1) {
    const uint32_t now = NowMs();

    // Опрос RC-in (50 Hz)
    if (s_rc_enabled && (now - last_rc_poll >= RC_IN_POLL_INTERVAL_MS)) {
      last_rc_poll = now;

      auto rc_throttle = RcInputReadThrottle();
      auto rc_steering = RcInputReadSteering();
      rc_active = rc_throttle.has_value() && rc_steering.has_value();

      // RC имеет приоритет над Wi-Fi
      if (rc_active) {
        commanded_throttle = *rc_throttle;
        commanded_steering = *rc_steering;
      }
    } else if (!s_rc_enabled) {
      rc_active = false;
    }

    // Чтение команд от Wi‑Fi (WebSocket)
    WifiCmd cmd;
    if (s_cmd_queue && xQueueReceive(s_cmd_queue, &cmd, 0) == pdTRUE) {
      // Wi‑Fi команды принимаются только если RC не активен
      if (!rc_active) {
        commanded_throttle = cmd.throttle;
        commanded_steering = cmd.steering;
        last_wifi_cmd_ms = now;
      }
    }

    // Wi‑Fi активен, если команда приходила недавно и RC не активен
    wifi_active = (!rc_active) && (last_wifi_cmd_ms != 0) &&
                  ((now - last_wifi_cmd_ms) < WIFI_CMD_TIMEOUT_MS);

    // Чтение IMU (50 Hz)
    if (s_imu_enabled && (now - last_imu_read >= IMU_READ_INTERVAL_MS)) {
      last_imu_read = now;
      (void)ImuRead(imu_data);  // IMU опционален; ошибок не фаталим
    }

    // Обновление failsafe
    if (now - last_failsafe_update >= 10) {  // Каждые 10 мс
      last_failsafe_update = now;
      if (FailsafeUpdate(rc_active, wifi_active)) {
        // Failsafe активен: нейтраль
        commanded_throttle = 0.0f;
        commanded_steering = 0.0f;
        applied_throttle = 0.0f;
        applied_steering = 0.0f;
        PwmControlSetNeutral();
      }
    }

    // Обновление PWM (50 Hz)
    if (now - last_pwm_update >= PWM_UPDATE_INTERVAL_MS) {
      const uint32_t dt_ms = now - last_pwm_update;
      last_pwm_update = now;

      applied_throttle = ApplySlewRate(commanded_throttle, applied_throttle,
                                       SLEW_RATE_THROTTLE_MAX_PER_SEC, dt_ms);
      applied_steering = ApplySlewRate(commanded_steering, applied_steering,
                                       SLEW_RATE_STEERING_MAX_PER_SEC, dt_ms);

      (void)PwmControlSetThrottle(applied_throttle);
      (void)PwmControlSetSteering(applied_steering);
    }

    // Отправка телеметрии (20 Hz)
    if (now - last_telem_send >= TELEM_SEND_INTERVAL_MS) {
      last_telem_send = now;

      // Если клиентов нет — не аллоцируем JSON зря.
      if (WebSocketGetClientCount() == 0) {
        vTaskDelay(pdMS_TO_TICKS(1));
        continue;
      }

      cJSON* root = cJSON_CreateObject();
      if (root) {
        cJSON_AddStringToObject(root, "type", "telem");

        // Для совместимости с текущим UI: “mcu_pong_ok” = “контроллер жив”.
        cJSON_AddBoolToObject(root, "mcu_pong_ok", true);

        cJSON* link = cJSON_CreateObject();
        if (link) {
          cJSON_AddBoolToObject(link, "rc_ok", rc_active);
          cJSON_AddBoolToObject(link, "wifi_ok", wifi_active);
          cJSON_AddBoolToObject(link, "failsafe", FailsafeIsActive());
          cJSON_AddItemToObject(root, "link", link);
        }

        if (s_imu_enabled) {
          cJSON* imu = cJSON_CreateObject();
          if (imu) {
            cJSON_AddNumberToObject(imu, "ax", imu_data.ax);
            cJSON_AddNumberToObject(imu, "ay", imu_data.ay);
            cJSON_AddNumberToObject(imu, "az", imu_data.az);
            cJSON_AddNumberToObject(imu, "gx", imu_data.gx);
            cJSON_AddNumberToObject(imu, "gy", imu_data.gy);
            cJSON_AddNumberToObject(imu, "gz", imu_data.gz);
            cJSON_AddItemToObject(root, "imu", imu);
          }
        }

        cJSON* act = cJSON_CreateObject();
        if (act) {
          cJSON_AddNumberToObject(act, "throttle", applied_throttle);
          cJSON_AddNumberToObject(act, "steering", applied_steering);
          cJSON_AddItemToObject(root, "act", act);
        }

        char* json_str = cJSON_PrintUnformatted(root);
        if (json_str) {
          (void)WebSocketSendTelem(json_str);
          free(json_str);
        }
        cJSON_Delete(root);
      }
    }

    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

esp_err_t VehicleControlInit(void) {
  if (s_inited) return ESP_OK;

  if (PwmControlInit() != 0) {
    ESP_LOGE(TAG, "Failed to initialize PWM");
    return ESP_FAIL;
  }

  // RC-in опционален
  if (RcInputInit() == 0) {
    s_rc_enabled = true;
  } else {
    s_rc_enabled = false;
    ESP_LOGW(TAG, "RC input init failed — continuing without RC-in");
  }

  // IMU опционален
  if (ImuInit() == 0) {
    s_imu_enabled = true;
  } else {
    s_imu_enabled = false;
    const int who = ImuGetLastWhoAmI();
    ESP_LOGW(TAG, "IMU init failed — continuing without IMU");
    if (who >= 0) {
      ESP_LOGW(TAG,
               "IMU WHO_AM_I=0x%02X (expected 0x68 MPU-6050 or 0x70 MPU-6500)",
               who);
    } else {
      ESP_LOGW(TAG,
               "IMU SPI read failed — check wiring: CS=%d, SCK=%d, MOSI=%d, "
               "MISO=%d, 3V3/GND",
               (int)IMU_SPI_CS_PIN, (int)IMU_SPI_SCK_PIN, (int)IMU_SPI_MOSI_PIN,
               (int)IMU_SPI_MISO_PIN);
    }
  }

  FailsafeInit();

  s_cmd_queue = xQueueCreate(1, sizeof(WifiCmd));
  if (s_cmd_queue == nullptr) {
    ESP_LOGE(TAG, "Failed to create Wi-Fi command queue");
    return ESP_FAIL;
  }

  BaseType_t created =
      xTaskCreate(vehicle_control_task, "vehicle_ctrl", CONTROL_TASK_STACK,
                  NULL, CONTROL_TASK_PRIORITY, NULL);
  if (created != pdPASS) {
    ESP_LOGE(TAG, "Failed to create vehicle control task");
    return ESP_FAIL;
  }

  s_inited = true;
  ESP_LOGI(TAG, "Vehicle control started");
  return ESP_OK;
}

void VehicleControlOnWifiCommand(float throttle, float steering) {
  if (s_cmd_queue == nullptr) return;

  WifiCmd cmd = {
      .throttle = rc_vehicle::ClampNormalized(throttle),
      .steering = rc_vehicle::ClampNormalized(steering),
  };

  // Перезаписываем последнюю команду (частота ~50 Hz).
  (void)xQueueOverwrite(s_cmd_queue, &cmd);
}
