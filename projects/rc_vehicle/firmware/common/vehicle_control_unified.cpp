#include "vehicle_control_unified.hpp"

#include "config.hpp"
#include "control_loop_processor.hpp"
#include "rc_vehicle_common.hpp"

namespace rc_vehicle {

// ═════════════════════════════════════════════════════════════════════════
// VehicleControlUnified Implementation
// ═════════════════════════════════════════════════════════════════════════

void VehicleControlUnified::SetPlatform(
    std::unique_ptr<VehicleControlPlatform> platform) {
  platform_ = std::move(platform);
}

void VehicleControlUnified::ControlTaskEntry(void* arg) {
  auto* self = static_cast<VehicleControlUnified*>(arg);
  if (self) {
    self->ControlTaskLoop();
  }
}

void VehicleControlUnified::BuildProcessor() {
  if (processor_) return;

  loop_ctx_.emplace(ControlLoopContext{
      *platform_, imu_calib_, madgwick_, ekf_, yaw_ctrl_, pitch_ctrl_,
      slip_ctrl_, oversteer_guard_, kids_processor_, auto_drive_,
      calib_mgr_.get(), stab_mgr_.get(), telem_mgr_.get(), rc_handler_.get(),
      wifi_handler_.get(), imu_handler_.get(), telem_handler_.get(),
      last_loop_hz_});

  processor_ = std::make_unique<ControlLoopProcessor>(*loop_ctx_,
                                                      platform_->GetTimeMs());
}

void VehicleControlUnified::ControlTaskLoop() {
  if (!platform_) return;
  platform_->RegisterTaskWdt();

  BuildProcessor();

  control_task_ready_.store(true, std::memory_order_release);

  uint32_t last_loop = platform_->GetTimeMs();
  while (true) {
    platform_->DelayUntilNextTick(config::ControlLoopConfig::kPeriodMs);
    const uint32_t now = platform_->GetTimeMs();
    processor_->Step(now, now - last_loop);
    last_loop = now;
    platform_->FeedTaskWdt();
  }
}

void VehicleControlUnified::HostStep(uint32_t dt_ms) {
  if (!platform_) return;
  BuildProcessor();
  control_task_ready_.store(true, std::memory_order_release);
  processor_->Step(platform_->GetTimeMs(), dt_ms);
}

bool VehicleControlUnified::StartComOffsetCalibration(
    float target_accel_g, float steering_magnitude, float cruise_duration_sec) {
  if (!stab_mgr_ || !imu_enabled_) return false;
  const auto& calib_data = imu_calib_.GetData();
  return auto_drive_.StartComCalib(target_accel_g, steering_magnitude,
                                   cruise_duration_sec, calib_data.gravity_vec);
}

bool VehicleControlUnified::StartTest(const TestParams& params) {
  if (!stab_mgr_ || !imu_enabled_) return false;
  return auto_drive_.StartTest(params);
}

bool VehicleControlUnified::StartSpeedCalibration(float target_throttle,
                                                  float cruise_duration_sec) {
  if (!imu_enabled_) return false;
  return auto_drive_.StartSpeedCalib(target_throttle, cruise_duration_sec);
}

bool VehicleControlUnified::StartSteeringTrimCalibration(float target_accel_g) {
  if (!stab_mgr_ || !imu_enabled_) return false;
  const auto& cfg = stab_mgr_->GetConfig();
  return auto_drive_.StartTrimCalib(target_accel_g, cfg.steering_trim,
                                    cfg.yaw_rate.steer_to_yaw_rate_dps);
}

void VehicleControlUnified::StartMagCalibration() {
  mag_calib_.Start();
  if (telem_mgr_) {
    telem_mgr_->PushEvent({0, TelemetryEventType::MagCalibStart, 0});
  }
}

void VehicleControlUnified::FinishMagCalibration() {
  const bool was_collecting =
      mag_calib_.GetStatus() == MagCalibStatus::Collecting;

  // Гасим кэшированный mag-семпл ДО Finish(), а не после (ревью PR #308).
  //
  // Этот метод вызывается из HTTP/WS-задачи (ws_command_handlers.cpp,
  // HandleCalibrateMag), а ControlTaskLoop() крутится независимой задачей на
  // другом ядре — атомарности между вызовами здесь нет. Кэш ImuHandler
  // посчитан ТЕКУЩИМ, ещё старым offset, и до инвалидации подаётся в
  // UpdateWithMag() на каждом тике. Всё, что стоит между сменой калибровки и
  // инвалидацией, — это окно, в котором control task может засеять курс по
  // старому offset, а последующее гашение семпла уже не откатит кватернион.
  // Окно это не микроскопическое: SaveMagCalib() ниже — запись в NVS на
  // миллисекунды, то есть сотни тиков control loop.
  //
  // Порядок с гашением впереди безопасен и в обратную сторону: после него
  // FeedMadgwick() уходит в 6DOF, а первое же свежее чтение (≤10 мс) уже
  // применит НОВУЮ калибровку — засев по нему корректен.
  //
  // Гейтим по was_collecting, чтобы не дёргать 6DOF-провал на no-op вызовах
  // Finish() вне сбора. Если сбор был, но калибровка не удалась, цена
  // ошибки — тот же ≤10 мс откат в 6DOF, что и при обычном пропуске чтения.
  if (was_collecting && imu_handler_) imu_handler_->InvalidateMagSample();

  mag_calib_.Finish();
  if (mag_calib_.IsValid()) {
    platform_->SaveMagCalib(mag_calib_.GetData());
    // Инвалидируем опору курса, только если ИМЕННО ЭТОТ вызов реально
    // перевёл калибровку Collecting → Done (data_ действительно перезаписан
    // новым offset). Finish() — no-op вне сбора (status_ тогда не
    // Collecting), а неудачная попытка (мало семплов / плохой radius /
    // NotPlanar) тоже не трогает data_ — в обоих случаях IsValid() может
    // остаться true от СТАРОЙ, уже сохранённой калибровки, и инвалидация
    // была бы ложной: следующая IMU/Forward-калибровка обнулила бы курс без
    // причины, хотя mag-калибровка на самом деле не менялась (review
    // r3630682666, LOS-229).
    if (was_collecting && mag_calib_.GetStatus() == MagCalibStatus::Done) {
      // Apply() дальше будет выдавать другой скорректированный вектор (новый
      // hard-iron offset) — накопленный до этого прогресс сходимости yaw
      // относился к старой калибровке (или к сырым данным) и не годится под
      // новую (LOS-229).
      // Кэшированный семпл уже погашен выше, до Finish(): одного сброса в
      // фильтре мало, потому что ImuHandler отдаёт кэш в UpdateWithMag() на
      // КАЖДОМ тике, обновляя его лишь на 100 Гц чтениях, и следующий же тик
      // снова пометил бы старый вектор пригодным для засева (ревью PR #308,
      // LOS-221).
      madgwick_.InvalidateYawTrust();
    }
  }
  if (telem_mgr_) {
    TelemetryEventType t = mag_calib_.IsValid()
                               ? TelemetryEventType::MagCalibDone
                               : TelemetryEventType::MagCalibFailed;
    telem_mgr_->PushEvent({0, t, 0});
  }
}

void VehicleControlUnified::CancelMagCalibration() {
  mag_calib_.Cancel();
  if (telem_mgr_) {
    telem_mgr_->PushEvent({0, TelemetryEventType::MagCalibCancelled, 0});
  }
}

void VehicleControlUnified::OnWifiCommand(float throttle, float steering) {
  if (platform_) {
    platform_->SendWifiCommand(throttle, steering);
  }
}

std::vector<SelfTestItem> VehicleControlUnified::RunSelfTest() const {
  const SelfTestContext ctx{
      last_loop_hz_, imu_handler_.get(), madgwick_,
      ekf_,          rc_handler_.get(),  wifi_handler_.get(),
      imu_calib_,    telem_mgr_.get(),   platform_ != nullptr,
      inited_};
  return SelfTest::Run(BuildSelfTestInput(ctx));
}

}  // namespace rc_vehicle