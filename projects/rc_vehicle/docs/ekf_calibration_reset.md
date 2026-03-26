# EKF Reset After IMU Calibration

## Проблема

После калибровки IMU скорость в EKF (Extended Kalman Filter) не обнуляется сразу. Это происходит потому, что:

1. **Калибровка обновляет только смещения датчиков**:
   - Gyro offset (смещение гироскопа)
   - Accel offset (смещение акселерометра)
   - Gravity vector (вектор гравитации)
   - Forward vector (вектор направления вперед)

2. **EKF продолжает работать со своим накопленным состоянием**:
   - `vx` (продольная скорость)
   - `vy` (боковая скорость)
   - `r` (угловая скорость рыскания)

3. **Скорость постепенно адаптируется** к новым калибровочным данным через фильтр Калмана.

## Пример из лога

```
I (578130) imu_nvs: Saved calib: gyro=[-0.400, -0.028, 0.679] accel=[0.3945, 0.4342, 0.2123]
I (578134) platform_esp32: Calibration done, saved to NVS
I (582953) platform_esp32: EKF: vx=0.71 vy=7.12 m/s  slip=84.3 deg  ← Скорость не обнулилась
I (587954) platform_esp32: EKF: vx=0.70 vy=7.17 m/s  slip=84.4 deg  ← Медленно меняется
I (592953) platform_esp32: EKF: vx=0.70 vy=7.20 m/s  slip=84.4 deg
```

## Решение

Добавлен **автоматический сброс EKF** после успешной калибровки:

### Изменения в коде

1. **[`CalibrationManager`](../firmware/common/calibration_manager.hpp:24)** теперь принимает указатель на EKF:
   ```cpp
   CalibrationManager(VehicleControlPlatform& platform,
                      ImuCalibration& imu_calib,
                      MadgwickFilter& madgwick,
                      VehicleEkf* ekf = nullptr);  // ← Новый параметр
   ```

2. **[`ProcessCompletion()`](../firmware/common/calibration_manager.cpp:59)** вызывает [`Reset()`](../firmware/common/vehicle_ekf.hpp:64) после калибровки:
   ```cpp
   if (status == CalibStatus::Done) {
     // ... сохранение калибровки ...

     // Сбросить EKF, чтобы скорость обнулилась
     if (ekf_) {
       ekf_->Reset();
       platform_.Log(LogLevel::Info, "EKF state reset after calibration");
     }
   }
   ```

3. **[`VehicleControlUnified::Init()`](../firmware/common/vehicle_control_unified.cpp:253)** передает указатель на EKF:
   ```cpp
   calib_mgr_.reset(new CalibrationManager(*platform_, imu_calib_, madgwick_, &ekf_));
   ```

## Результат

После калибровки:
- ✅ Скорость (`vx`, `vy`) обнуляется мгновенно
- ✅ Угол заноса (`slip_angle`) сбрасывается в 0
- ✅ Ковариационная матрица `P` возвращается к начальным значениям
- ✅ EKF начинает оценку с чистого листа

## Технические детали

### Метод [`VehicleEkf::Reset()`](../firmware/common/vehicle_ekf.hpp:64)

```cpp
void VehicleEkf::Reset() noexcept {
  // Обнулить вектор состояния
  x_[0] = 0.0f;  // vx
  x_[1] = 0.0f;  // vy
  x_[2] = 0.0f;  // r

  // Восстановить начальную ковариацию
  InitP();
}
```

### Почему это безопасно?

1. **Калибровка выполняется в покое** — машина стоит неподвижно
2. **Реальная скорость = 0** — сброс EKF соответствует физической реальности
3. **Failsafe уже сбрасывает EKF** — это проверенный механизм (строка 130 в [`ControlTaskLoop()`](../firmware/common/vehicle_control_unified.cpp:130))

## Альтернативные подходы (не реализованы)

1. **Постепенное затухание** — добавить большой шум процесса на короткое время
2. **Условный сброс** — сбрасывать только если скорость > порога
3. **Сброс только скоростей** — оставить `r` (yaw rate) без изменений

Текущее решение (полный сброс) выбрано как самое простое и надежное.

## См. также

- [`vehicle_ekf.hpp`](../firmware/common/vehicle_ekf.hpp) — реализация EKF
- [`calibration_manager.hpp`](../firmware/common/calibration_manager.hpp) — менеджер калибровки
- [`imu_calibration.hpp`](../firmware/common/imu_calibration.hpp) — калибровка IMU