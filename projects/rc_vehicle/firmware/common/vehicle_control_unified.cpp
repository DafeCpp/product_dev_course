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

void VehicleControlUnified::ControlTaskLoop() {
  if (!platform_) return;
  platform_->RegisterTaskWdt();

  const ControlLoopContext ctx{
      *platform_,       imu_calib_,        madgwick_,    ekf_,
      yaw_ctrl_,        pitch_ctrl_,        slip_ctrl_,   oversteer_guard_,
      kids_processor_,  auto_drive_,
      calib_mgr_.get(), stab_mgr_.get(),    telem_mgr_.get(),
      rc_handler_.get(), wifi_handler_.get(), imu_handler_.get(),
      telem_handler_.get(), last_loop_hz_};

  const uint32_t start = platform_->GetTimeMs();
  ControlLoopProcessor processor(ctx, start);

  control_task_ready_.store(true, std::memory_order_release);

  uint32_t last_loop = start;
  while (true) {
    platform_->DelayUntilNextTick(config::ControlLoopConfig::kPeriodMs);
    const uint32_t now = platform_->GetTimeMs();
    processor.Step(now, now - last_loop);
    last_loop = now;
    platform_->FeedTaskWdt();
  }
}

bool VehicleControlUnified::StartComOffsetCalibration(
    float target_accel_g, float steering_magnitude,
    float cruise_duration_sec) {
  if (!stab_mgr_ || !imu_enabled_) return false;
  const auto& calib_data = imu_calib_.GetData();
  return auto_drive_.StartComCalib(target_accel_g, steering_magnitude,
                                   cruise_duration_sec,
                                   calib_data.gravity_vec);
}

bool VehicleControlUnified::StartTest(const TestParams& params) {
  if (!stab_mgr_ || !imu_enabled_) return false;
  return auto_drive_.StartTest(params);
}

bool VehicleControlUnified::StartSteeringTrimCalibration(
    float target_accel_g) {
  if (!stab_mgr_ || !imu_enabled_) return false;
  const auto& cfg = stab_mgr_->GetConfig();
  return auto_drive_.StartTrimCalib(target_accel_g, cfg.steering_trim,
                                    cfg.yaw_rate.steer_to_yaw_rate_dps);
}

void VehicleControlUnified::OnWifiCommand(float throttle, float steering) {
  if (platform_) {
    platform_->SendWifiCommand(throttle, steering);
  }
}

std::vector<SelfTestItem> VehicleControlUnified::RunSelfTest() const {
  const SelfTestContext ctx{last_loop_hz_,   imu_handler_.get(),
                            madgwick_,       ekf_,
                            rc_handler_.get(), wifi_handler_.get(),
                            imu_calib_,      telem_mgr_.get(),
                            platform_ != nullptr, inited_};
  return SelfTest::Run(BuildSelfTestInput(ctx));
}

}  // namespace rc_vehicle