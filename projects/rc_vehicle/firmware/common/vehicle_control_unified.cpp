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
    // Отложенные WS-запросы, которые безопасно применять только здесь.
    ProcessMagCalibRequests();
    processor_->Step(now, now - last_loop);
    last_loop = now;
    platform_->FeedTaskWdt();
  }
}

void VehicleControlUnified::HostStep(uint32_t dt_ms) {
  if (!platform_) return;
  BuildProcessor();
  control_task_ready_.store(true, std::memory_order_release);
  ProcessMagCalibRequests();
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

void VehicleControlUnified::PublishMagCalibState() {
  const char* status = "idle";
  switch (mag_calib_.GetStatus()) {
    case MagCalibStatus::Idle:
      status = "idle";
      break;
    case MagCalibStatus::Collecting:
      status = "collecting";
      break;
    case MagCalibStatus::Done:
      status = "done";
      break;
    case MagCalibStatus::Failed:
      status = "failed";
      break;
  }
  const char* fail_reason = mag_calib_.GetFailReasonStr();

  // Обе строки — литералы со статическим временем жизни, поэтому отдавать
  // указатель из-под мьютекса безопасно.
  std::lock_guard<std::mutex> lock(mag_state_mutex_);
  mag_status_pub_ = status;
  mag_fail_reason_pub_ = fail_reason;
}

bool VehicleControlUnified::QueueMagCalibRequest(MagCalibRequest req) {
  // Вызывается из HTTP/WS-задачи (ws_command_handlers.cpp, HandleCalibrateMag),
  // а ControlTaskLoop() — независимая задача. Ничего из mag_calib_/madgwick_/
  // imu_handler_ отсюда трогать нельзя: все они непрерывно читаются и пишутся
  // control loop'ом на 500 Гц и не потокобезопасны. Только ставим запрос в
  // очередь — по образцу CalibrationManager::SetForwardDirection() (PR #290).
  //
  // Откладываются ВСЕ три команды, а не только finish. Отложить одну означало
  // бы гонку между ней и оставшимися синхронными: control loop, забрав запрос
  // из очереди, доходит до mag_calib_ уже вне мьютекса, и Start() с чужой
  // задачи успевал бы в этот зазор подменить сессию под уже начатым
  // завершением. Никакой гейт на стороне HTTP этого не закрывает — окно
  // сужается, но остаётся (три итерации ревью PR #308 ровно об этом).
  //
  // Очередь, а не одна ячейка: команды применяются в том же порядке, в каком
  // пришли по WS, поэтому наблюдаемое поведение совпадает с прежним
  // синхронным.
  //
  // Переполнения (>kMagCalibQueueSize команд за один тик, 2 мс) от человека
  // в UI не ждём, но автоматический клиент способен и на такое, поэтому оно
  // не проглатывается: false доходит до WS-обработчика и до клиента в
  // ok=false. Молча отброшенный cancel оставил бы калибровку собирать семплы
  // вечно при том, что клиент считает команду принятой (ревью PR #308).
  std::lock_guard<std::mutex> lock(mag_state_mutex_);
  if (mag_request_count_ >= kMagCalibQueueSize) return false;
  mag_requests_[mag_request_count_++] = req;
  return true;
}

bool VehicleControlUnified::StartMagCalibration() {
  return QueueMagCalibRequest(MagCalibRequest::Start);
}

bool VehicleControlUnified::FinishMagCalibration() {
  return QueueMagCalibRequest(MagCalibRequest::Finish);
}

bool VehicleControlUnified::CancelMagCalibration() {
  return QueueMagCalibRequest(MagCalibRequest::Cancel);
}

bool VehicleControlUnified::EraseMagCalibration() {
  return QueueMagCalibRequest(MagCalibRequest::Erase);
}

void VehicleControlUnified::ProcessMagCalibRequests() {
  MagCalibRequest batch[kMagCalibQueueSize];
  size_t count = 0;
  {
    std::lock_guard<std::mutex> lock(mag_state_mutex_);
    count = mag_request_count_;
    for (size_t i = 0; i < count; ++i) batch[i] = mag_requests_[i];
    mag_request_count_ = 0;
  }
  if (count == 0) return;

  for (size_t i = 0; i < count; ++i) {
    switch (batch[i]) {
      case MagCalibRequest::Start:
        ApplyMagCalibStart();
        break;
      case MagCalibRequest::Finish:
        ApplyMagCalibFinish();
        break;
      case MagCalibRequest::Cancel:
        ApplyMagCalibCancel();
        break;
      case MagCalibRequest::Erase:
        ApplyMagCalibErase();
        break;
    }
  }

  // Публикуем один раз на пачку: промежуточные статусы внутри неё всё равно
  // ненаблюдаемы — она применяется целиком в пределах одного тика.
  PublishMagCalibState();
}

void VehicleControlUnified::ApplyMagCalibStart() {
  mag_calib_.Start();
  if (telem_mgr_) {
    telem_mgr_->PushEvent({0, TelemetryEventType::MagCalibStart, 0});
  }
}

void VehicleControlUnified::ApplyMagCalibCancel() {
  mag_calib_.Cancel();
  if (telem_mgr_) {
    telem_mgr_->PushEvent({0, TelemetryEventType::MagCalibCancelled, 0});
  }
}

void VehicleControlUnified::ApplyMagCalibErase() {
  // Через ту же очередь — только ради порядка относительно finish. Сам
  // mag_calib_ стирание не трогает: калибровка в памяти доживает до
  // перезагрузки, как и раньше.
  const bool ok = platform_->EraseMagCalib();
  platform_->Log(
      ok ? LogLevel::Info : LogLevel::Warning,
      ok ? "Mag calibration erased from NVS" : "Mag calibration erase FAILED");
}

void VehicleControlUnified::ApplyMagCalibFinish() {
  const bool was_collecting =
      mag_calib_.GetStatus() == MagCalibStatus::Collecting;

  mag_calib_.Finish();

  // Всё это исполняется на потоке control loop, поэтому чужих тиков между
  // шагами нет: последовательность атомарна и по отношению к
  // UpdateMagAndHeading()/FeedMadgwick(), и по отношению к start/cancel —
  // те приходят через ту же очередь. Порядок шагов внутри свободен — именно
  // поэтому инвалидации сдвинуты СЮДА, за Finish(), где уже известен его
  // исход.
  //
  // Сбрасываем что-либо, только если ИМЕННО ЭТОТ вызов реально перевёл
  // калибровку Collecting → Done, т.е. data_ перезаписан новым offset.
  // Finish() — no-op вне сбора (status_ тогда не Collecting), а неудачная
  // попытка (мало семплов / плохой radius / NotPlanar) тоже не трогает
  // data_. В обоих случаях IsValid() может остаться true от СТАРОЙ, уже
  // сохранённой калибровки, и сброс был бы ложным: yaw сходился к ней же и
  // остаётся действительным, а обнулять его — значит на 12 с лишить
  // следующую IMU/Forward-калибровку опоры preserve_yaw и заставить её
  // засевать курс по одному мгновенному семплу (review r3630682666,
  // LOS-229; ревью PR #308).
  const bool recalibrated =
      was_collecting && mag_calib_.GetStatus() == MagCalibStatus::Done;

  if (recalibrated) {
    // Кэш ImuHandler посчитан ещё старым offset на предыдущем тике и
    // оставался бы пригодным для засева до следующего 100 Гц чтения.
    if (imu_handler_) imu_handler_->InvalidateMagSample();
    // Apply() дальше будет выдавать другой скорректированный вектор (новый
    // hard-iron offset) — накопленный прогресс сходимости yaw относился к
    // старой калибровке (или к сырым данным) и не годится под новую
    // (LOS-229). Одного сброса в фильтре мало: ImuHandler отдаёт кэш в
    // UpdateWithMag() на КАЖДОМ тике, обновляя его лишь на 100 Гц чтениях,
    // и следующий же тик снова пометил бы старый вектор пригодным.
    madgwick_.InvalidateYawTrust();
  }

  if (mag_calib_.IsValid()) {
    platform_->SaveMagCalib(mag_calib_.GetData());
  }

  if (telem_mgr_) {
    TelemetryEventType t = mag_calib_.IsValid()
                               ? TelemetryEventType::MagCalibDone
                               : TelemetryEventType::MagCalibFailed;
    telem_mgr_->PushEvent({0, t, 0});
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