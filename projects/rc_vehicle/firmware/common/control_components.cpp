#include "control_components.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

#include "config.hpp"
#include "firmware_common/json_writer.hpp"
#include "imu_calibration.hpp"
#include "madgwick_filter.hpp"
#include "mag_calibration.hpp"

namespace rc_vehicle {

namespace {

const char* ZuptStatusToString(ZuptStatus status) {
  switch (status) {
    case ZuptStatus::NotEvaluated:
      return "not_evaluated";
    case ZuptStatus::Applied:
      return "applied";
    case ZuptStatus::ThrottleRejected:
      return "throttle_rejected";
    case ZuptStatus::AccelRejected:
      return "accel_rejected";
    case ZuptStatus::GyroRejected:
      return "gyro_rejected";
  }
  return "unknown";
}

}  // namespace

// ═════════════════════════════════════════════════════════════════════════
// RcInputHandler
// ═════════════════════════════════════════════════════════════════════════

void RcInputHandler::Update(uint32_t now_ms, [[maybe_unused]] uint32_t dt_ms) {
  // Опрос RC с заданной частотой
  if (now_ms - last_poll_ms_ < poll_interval_ms_) {
    return;
  }
  last_poll_ms_ = now_ms;

  // Получить команду от RC-приёмника
  last_command_ = platform_.GetRc();
  active_ = last_command_.has_value();
}

// ═════════════════════════════════════════════════════════════════════════
// WifiCommandHandler
// ═════════════════════════════════════════════════════════════════════════

void WifiCommandHandler::Update(uint32_t now_ms,
                                [[maybe_unused]] uint32_t dt_ms) {
  // Попытаться получить команду из очереди
  auto cmd = platform_.TryReceiveWifiCommand();
  if (cmd) {
    last_command_ = cmd;
    last_cmd_ms_ = now_ms;
  }
  // Кэшируем active-состояние: стабильно в пределах одной итерации control loop
  active_ = last_cmd_ms_ != 0 && (now_ms - last_cmd_ms_) < timeout_ms_;
}

// ═════════════════════════════════════════════════════════════════════════
// ImuHandler
// ═════════════════════════════════════════════════════════════════════════

void ImuHandler::Update(uint32_t now_ms, [[maybe_unused]] uint32_t dt_ms) {
  if (!enabled_) {
    return;
  }

  // Чтение IMU с заданной частотой
  if (now_ms - last_read_ms_ < read_interval_ms_) {
    return;
  }

  const uint32_t prev_read_ms = last_read_ms_;
  last_read_ms_ = now_ms;

  // Прочитать данные IMU
#ifdef RC_PROFILE_LOOP
  // LOS-219/250: время ТОЛЬКО SPI-транзакции(й), отдельно от остального
  // (калибровка/LPF/mag/Madgwick) — см. GetProfSpiUs() в .hpp. Меряем даже
  // при неудаче: сама транзакция уже отработала.
  const uint64_t _spi_t0 = platform_.GetTimeUs();
#endif
  auto imu_data = platform_.ReadImu();
#ifdef RC_PROFILE_LOOP
  {
    const uint64_t _spi_d = platform_.GetTimeUs() - _spi_t0;
    prof_spi_us_ += _spi_d;
    if (_spi_d > prof_spi_max_us_) prof_spi_max_us_ = _spi_d;
  }
#endif
  if (!imu_data) {
    return;
  }

#ifdef RC_PROFILE_LOOP
  const uint64_t _rest_t0 = platform_.GetTimeUs();
#endif
  data_ = *imu_data;

  // Подача семпла в калибровку (если идёт сбор)
  calib_.FeedSample(data_);

  // Сохранить сырые данные акселерометра ДО коррекции bias.
  // Madgwick-фильтр должен видеть истинное направление гравитации в СК датчика,
  // совпадающее с gravity_vec из калибровки. Accel bias включает компоненты
  // наклона (ax,ay mean), из-за чего bias-corrected данные = [0,0,±1]
  // не соответствуют реальному gravity_vec при наклонном монтаже.
  const float raw_ax = data_.ax, raw_ay = data_.ay, raw_az = data_.az;

  // Применить компенсацию bias (если калибровка валидна)
  calib_.Apply(data_);

  // LPF инициализирован в конструкторе — горячий путь без проверок
  filtered_gz_ = lpf_gyro_z_.Step(data_.gz);

  const float dt_sec =
      first_read_ ? (read_interval_ms_ / 1000.0f)
                  : (static_cast<float>(now_ms - prev_read_ms) / 1000.0f);
  first_read_ = false;

  // UpdateMagAndHeading + FeedMadgwick — ДО UpdateVehicleFrame(): иначе на
  // тике, где калибровка становится валидной ровно в момент истечения
  // таймаута устаревшего магнетометра, SetVehicleFrame() прочитал бы
  // filter_.yaw_has_absolute_ref_ ещё с ПРЕДЫДУЩЕГО тика (mag_enabled_ тогда
  // ещё не был инвалидирован) и мог бы закрепить курс, посчитанный по уже
  // замороженному mag-семплу (review r3629768456, LOS-229). В этом порядке
  // к моменту UpdateVehicleFrame() флаг уже отражает состояние текущего
  // тика.
  UpdateMagAndHeading(now_ms);
  FeedMadgwick(raw_ax, raw_ay, raw_az, dt_sec);

  UpdateVehicleFrame();

#ifdef RC_PROFILE_LOOP
  {
    const uint64_t _rest_d = platform_.GetTimeUs() - _rest_t0;
    prof_rest_us_ += _rest_d;
    if (_rest_d > prof_rest_max_us_) prof_rest_max_us_ = _rest_d;
  }
#endif
}

void ImuHandler::UpdateVehicleFrame() {
  // Настроить опорную СК фильтра — только при смене состояния калибровки,
  // чтобы не сбрасывать кватернион Мэджвика каждые 2 мс.
  const bool calib_valid = calib_.IsValid();
  if (calib_valid && !veh_frame_set_) {
    const auto& calib_data = calib_.GetData();
    filter_.SetVehicleFrame(calib_data.gravity_vec,
                            calib_data.accel_forward_vec, true);
    veh_frame_set_ = true;
  } else if (!calib_valid && veh_frame_set_) {
    filter_.SetVehicleFrame(nullptr, nullptr, false);
    veh_frame_set_ = false;
  }
}

void ImuHandler::UpdateMagAndHeading(uint32_t now_ms) {
  // Таймаут устаревания — ДО throttle-гейта опроса ниже и на каждом вызове
  // (IMU-тик, 2 мс), а не только на тиках, где реально идёт опрос
  // магнетометра (100 Гц): иначе IMU-тики между опросами могут уже
  // превысить kMagStaleTimeoutMs по факту, но mag_enabled_ останется true
  // до следующего 10-мс опроса — до 8 мс лишнего доверия к замороженному
  // семплу, в течение которых калибровка могла бы закрепить устаревший курс
  // (review r3629933116, LOS-229).
  if (mag_enabled_ && (now_ms - last_mag_success_ms_) > kMagStaleTimeoutMs) {
    mag_enabled_ = false;
  }

  // Читаем магнетометр на 100 Hz (MMC5983 CMM rate).
  // I2C/SPI транзакция ~350 мкс — не читаем каждые 2 мс.
  if ((now_ms - last_mag_read_ms_) < kMagReadIntervalMs) {
    return;
  }
  last_mag_read_ms_ = now_ms;

#ifdef RC_PROFILE_LOOP
  const uint64_t _mag_t0 = platform_.GetTimeUs();
#endif
  const auto mag_opt = platform_.ReadMag();
#ifdef RC_PROFILE_LOOP
  {
    const uint64_t _mag_d = platform_.GetTimeUs() - _mag_t0;
    prof_mag_us_ += _mag_d;
    if (_mag_d > prof_mag_max_us_) prof_mag_max_us_ = _mag_d;
  }
#endif
  if (!mag_opt) {
    return;
  }
  last_mag_success_ms_ = now_ms;
  mag_data_ = *mag_opt;
  mag_enabled_ = true;

  // Подача нового семпла в калибровку (если идёт сбор)
  if (mag_calib_ && mag_calib_->IsCollecting()) {
    mag_calib_->FeedSample(mag_data_);
  }

  // Калиброванное значение кэшируется: FeedMadgwick использует его на
  // каждом тике 500 Гц до следующего mag-семпла (Apply детерминирован —
  // результат тот же, что пересчёт каждые 2 мс).
  mag_calibrated_ = mag_data_;
  const bool have_calib = mag_calib_ && mag_calib_->IsValid();
  if (have_calib) {
    mag_calib_->Apply(mag_calibrated_);
  }

  if (have_calib) {
    heading_deg_ = ComputePcaHeadingDeg(mag_calibrated_);
  } else {
    // Нет калибровки — fallback: простой atan2 без проекции
    const float h = std::atan2(mag_calibrated_.my, mag_calibrated_.mx) *
                    (180.f / 3.14159265f);
    heading_deg_ = (h < 0.f) ? h + 360.f : h;
  }

  // Установить опорный курс при первом валидном чтении (или после сброса)
  if (!heading_ref_set_) {
    heading_ref_ = heading_deg_;
    heading_ref_set_ = true;
  }
}

float ImuHandler::ComputePcaHeadingDeg(const MagData& mag_cal) const {
  // ── PCA heading: проекция на калибровочную плоскость ──────────────────
  const auto& cd = mag_calib_->GetData();

  // Проекция mag на горизонтальную плоскость (перпендикулярную normal)
  const float dot_n = mag_cal.mx * cd.normal[0] + mag_cal.my * cd.normal[1] +
                      mag_cal.mz * cd.normal[2];
  const float px = mag_cal.mx - dot_n * cd.normal[0];
  const float py = mag_cal.my - dot_n * cd.normal[1];
  const float pz = mag_cal.mz - dot_n * cd.normal[2];

  const float comp1 = px * cd.basis1[0] + py * cd.basis1[1] + pz * cd.basis1[2];
  const float comp2 = px * cd.basis2[0] + py * cd.basis2[1] + pz * cd.basis2[2];

  const float h = std::atan2(comp2, comp1) * (180.f / 3.14159265f);
  return (h < 0.f) ? h + 360.f : h;
}

void ImuHandler::FeedMadgwick(float raw_ax, float raw_ay, float raw_az,
                              float dt_sec) {
  if (!madgwick_enabled_) {
    // Кватернион заморожен (ни Update(), ни UpdateWithMag() не вызываются),
    // но машина могла продолжать двигаться, пока Мэджвик выключен
    // (StabilizationManager::ApplyToFilters переключает это на ходу через
    // cfg.filter.madgwick_enabled). Опору для будущей калибровки нужно
    // заработать заново после повторного включения — иначе
    // IMU/Forward-калибровка могла бы сохранить курс, посчитанный по уже
    // неактуальному кватерниону (review r3630915899, LOS-229).
    filter_.InvalidateYawTrust();
    return;
  }

  if (mag_enabled_) {
    // 9DOF: полный калиброванный mag-вектор в СК датчика (FW-R3).
    // Подаётся каждый тик (включая тики без нового семпла) — предотвращает
    // дрейф yaw между обновлениями магнитометра. Madgwick сам устраняет
    // склонение (bx = sqrt(hx²+hy²)), предварительная проекция не нужна.
    filter_.UpdateWithMag(raw_ax, raw_ay, raw_az, data_.gx, data_.gy, data_.gz,
                          mag_calibrated_.mx, mag_calibrated_.my,
                          mag_calibrated_.mz, dt_sec);
  } else {
    // 6DOF: сырой акселерометр + калиброванный гироскоп
    filter_.Update(raw_ax, raw_ay, raw_az, data_.gx, data_.gy, data_.gz,
                   dt_sec);
  }
}

float ImuHandler::GetRelativeHeadingDeg() const noexcept {
  float delta = heading_deg_ - heading_ref_;
  if (delta > 180.f) delta -= 360.f;
  if (delta <= -180.f) delta += 360.f;
  return delta;
}

void ImuHandler::SetLpfCutoff(float cutoff_hz) {
  if (cutoff_hz < config::LpfConfig::kMinCutoffHz ||
      cutoff_hz > config::LpfConfig::kMaxCutoffHz) {
    return;  // Игнорировать невалидные значения
  }
  const float fs_hz = 1000.f / static_cast<float>(read_interval_ms_);
  lpf_gyro_z_.SetParams(cutoff_hz, fs_hz);
  lpf_gyro_z_.Reset();  // Сброс состояния при изменении параметров
}

// ═════════════════════════════════════════════════════════════════════════
// TelemetryHandler
// ═════════════════════════════════════════════════════════════════════════

void TelemetryHandler::SendTelemetry(uint32_t now_ms,
                                     const TelemetrySnapshot& snap) {
  if (now_ms - last_send_ms_ < send_interval_ms_) {
    return;
  }
  last_send_ms_ = now_ms;

  // FW-RF8: control loop публикует только лёгкий POD-снимок в очередь платформы
  // (детерминированно, без кучи). Построение JSON (cJSON) и отправка по WS
  // выполняются в задаче телеметрии — стоимость/сбой телеметрии физически не
  // достигает 500 Гц цикла.
  //
  // FW-R13: доставку по-прежнему НЕ гейтим по GetWebSocketClientCount() —
  // решение принимает транспорт (WebSocketSendTelem шлёт только реальным
  // WS-fd).
  platform_.PublishTelem(snap);
}

std::string BuildTelemJson(const TelemetrySnapshot& snap) {
  // LOS-252: firmware_common::JsonWriter вместо cJSON — целочисленное
  // форматирование чисел (без printf/sscanf round-trip) и без malloc на
  // узел дерева. Один reserve() на кадр вместо ~160 аллокаций cJSON.
  // Полностью заполненный кадр (все опциональные блоки) — 1023 байта
  // (см. BuildTelemJsonTest.FullSnapshotProducesValidJsonWithAllKeys), 1152
  // с запасом, чтобы не было повторной аллокации+копии на полном кадре.
  // Флаги лимитеров Kids (LOS-13) съели прежний запас до 1024 почти целиком.
  std::string result;
  result.reserve(1152);
  firmware_common::JsonWriter w(result);

  w.BeginObject();
  w.RawStr("type", "telem");
  // Для совместимости: "mcu_pong_ok" = "контроллер жив"
  w.Bool("mcu_pong_ok", true);
  w.Int("uptime_ms", snap.uptime_ms);

  // Link status
  w.BeginObject("link");
  w.Bool("rc_ok", snap.rc_ok);
  w.Bool("wifi_ok", snap.wifi_ok);
  w.Bool("failsafe", snap.failsafe);
  w.EndObject();

  // IMU data (если включен)
  if (snap.imu_enabled) {
    w.BeginObject("imu");
    w.Fixed("ax", snap.imu_data.ax, 3);
    w.Fixed("ay", snap.imu_data.ay, 3);
    w.Fixed("az", snap.imu_data.az, 3);
    w.Fixed("gx", snap.imu_data.gx, 2);
    w.Fixed("gy", snap.imu_data.gy, 2);
    w.Fixed("gz", snap.imu_data.gz, 2);
    w.Fixed("gyro_z_filtered", snap.filtered_gz, 2);
    w.Fixed("forward_accel", snap.forward_accel, 3);

    // Orientation (Madgwick)
    w.BeginObject("orientation");
    w.Fixed("pitch", snap.pitch_deg, 2);
    w.Fixed("roll", snap.roll_deg, 2);
    w.Fixed("yaw", snap.yaw_deg, 2);
    w.EndObject();
    w.EndObject();  // imu

    // Calibration status
    w.BeginObject("calib");
    const char* status_str = "unknown";
    switch (snap.calib_status) {
      case CalibStatus::Idle:
        status_str = "idle";
        break;
      case CalibStatus::Collecting:
        status_str = "collecting";
        break;
      case CalibStatus::Done:
        status_str = "done";
        break;
      case CalibStatus::Failed:
        status_str = "failed";
        break;
    }
    w.RawStr("status", status_str);
    w.Int("stage", snap.calib_stage);
    w.Bool("valid", snap.calib_valid);

    if (snap.calib_valid) {
      const auto& cd = snap.calib_data;
      w.BeginObject("bias");
      w.Fixed("gx", cd.gyro_bias[0], 4);
      w.Fixed("gy", cd.gyro_bias[1], 4);
      w.Fixed("gz", cd.gyro_bias[2], 4);
      w.Fixed("ax", cd.accel_bias[0], 4);
      w.Fixed("ay", cd.accel_bias[1], 4);
      w.Fixed("az", cd.accel_bias[2], 4);
      w.EndObject();

      w.BeginArray("gravity_vec");
      w.FixedElem(cd.gravity_vec[0], 4);
      w.FixedElem(cd.gravity_vec[1], 4);
      w.FixedElem(cd.gravity_vec[2], 4);
      w.EndArray();

      w.BeginArray("forward_vec");
      w.FixedElem(cd.accel_forward_vec[0], 4);
      w.FixedElem(cd.accel_forward_vec[1], 4);
      w.FixedElem(cd.accel_forward_vec[2], 4);
      w.EndArray();
    }
    w.EndObject();  // calib

    // Магнетометр
    if (snap.mag_enabled) {
      w.BeginObject("mag");
      w.Fixed("mx", snap.mag_data.mx, 1);
      w.Fixed("my", snap.mag_data.my, 1);
      w.Fixed("mz", snap.mag_data.mz, 1);
      w.Fixed("heading_deg", snap.heading_deg, 2);
      w.Fixed("heading_rel_deg", snap.heading_rel_deg, 2);
      w.EndObject();
    }

    // EKF: динамическое состояние (vx, vy, r, slip angle)
    if (snap.ekf_available) {
      w.BeginObject("ekf");
      w.Fixed("vx", snap.ekf_vx, 3);
      w.Fixed("vy", snap.ekf_vy, 3);
      w.Fixed("yaw_rate", snap.ekf_yaw_rate, 3);
      w.Fixed("slip_deg", snap.ekf_slip_deg, 2);
      w.Fixed("speed_ms", snap.ekf_speed_ms, 3);
      w.Sci("vx_var", snap.ekf_vx_var);
      w.Sci("vy_var", snap.ekf_vy_var);
      w.Sci("r_var", snap.ekf_r_var);
      w.RawStr("zupt_status", ZuptStatusToString(snap.ekf_zupt_status));
      w.Fixed("speed_meas", snap.ekf_speed_meas, 3);
      w.Bool("diverged", snap.ekf_diverged);
      w.EndObject();
    }

    // Oversteer warning (Phase 4.2)
    if (snap.oversteer_available) {
      w.BeginObject("warn");
      w.Bool("oversteer", snap.oversteer_active);
      w.EndObject();
    }
  }

  // Kids Mode status
  if (snap.kids_mode_active) {
    w.BeginObject("kids_mode");
    w.Bool("active", true);
    w.Bool("anti_spin_active", snap.kids_anti_spin_active);
    w.Bool("accel_limit_active", snap.kids_accel_limit_active);
    w.Bool("speed_limit_active", snap.kids_speed_limit_active);
    w.Bool("limiters_enabled", snap.kids_limiters_enabled);
    w.Fixed("throttle_limit", snap.kids_throttle_limit, 3);
    w.EndObject();
  }

  // RC input (сырые значения с пульта)
  if (snap.rc_ok) {
    w.BeginObject("rc");
    w.Fixed("throttle", snap.rc_throttle, 3);
    w.Fixed("steering", snap.rc_steering, 3);
    w.EndObject();
  }

  // Commanded (до trim/slew)
  w.BeginObject("cmd");
  w.Fixed("throttle", snap.cmd_throttle, 3);
  w.Fixed("steering", snap.cmd_steering, 3);
  w.EndObject();

  // Actuators (после trim/slew)
  w.BeginObject("act");
  w.Fixed("throttle", snap.throttle, 3);
  w.Fixed("steering", snap.steering, 3);
  w.EndObject();

  w.EndObject();  // root
  return result;
}

}  // namespace rc_vehicle
