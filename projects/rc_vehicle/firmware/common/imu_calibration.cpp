#include "imu_calibration.hpp"

#include <cmath>
#include <cstring>

void ImuCalibration::ResetAccumulators() {
  collected_ = 0;
  std::memset(sum_, 0, sizeof(sum_));
  std::memset(sum_sq_, 0, sizeof(sum_sq_));
}

void ImuCalibration::StartCalibration(CalibMode mode, int num_samples) {
  mode_ = mode;
  target_samples_ = num_samples > 0 ? num_samples : 1000;
  status_ = CalibStatus::Collecting;
  ResetAccumulators();
}

void ImuCalibration::FeedSample(const ImuData& raw) {
  if (status_ != CalibStatus::Collecting) return;

  const double vals[6] = {raw.gx, raw.gy, raw.gz, raw.ax, raw.ay, raw.az};

  for (int i = 0; i < 6; ++i) {
    sum_[i] += vals[i];
    sum_sq_[i] += vals[i] * vals[i];
  }
  ++collected_;

  if (collected_ >= target_samples_) {
    if (Finalize()) {
      status_ = CalibStatus::Done;
    } else {
      status_ = CalibStatus::Failed;
    }
  }
}

bool ImuCalibration::Finalize() {
  if (collected_ == 0) return false;

  const double n = static_cast<double>(collected_);
  double mean[6];
  double var[6];

  for (int i = 0; i < 6; ++i) {
    mean[i] = sum_[i] / n;
    var[i] = (sum_sq_[i] / n) - (mean[i] * mean[i]);
  }

  // Проверка: гироскоп не должен показывать вращение (variance < threshold)
  for (int i = 0; i < 3; ++i) {
    if (var[i] > static_cast<double>(kGyroVarianceThreshold)) return false;
  }

  // Если Full — проверить и акселерометр
  if (mode_ == CalibMode::Full) {
    for (int i = 3; i < 6; ++i) {
      if (var[i] > static_cast<double>(kAccelVarianceThreshold)) return false;
    }
  }

  // Gyro bias = среднее значение в покое (идеал = 0)
  data_.gyro_bias[0] = static_cast<float>(mean[0]);
  data_.gyro_bias[1] = static_cast<float>(mean[1]);
  data_.gyro_bias[2] = static_cast<float>(mean[2]);

  // Accel bias = отклонение от идеального g-вектора (0, 0, +1.0g)
  if (mode_ == CalibMode::Full) {
    data_.accel_bias[0] = static_cast<float>(mean[3]);         // ideal = 0
    data_.accel_bias[1] = static_cast<float>(mean[4]);         // ideal = 0
    data_.accel_bias[2] = static_cast<float>(mean[5] - 1.0);  // ideal = 1.0g
  }

  data_.valid = true;
  return true;
}

void ImuCalibration::Apply(ImuData& data) const {
  if (!data_.valid) return;

  data.gx -= data_.gyro_bias[0];
  data.gy -= data_.gyro_bias[1];
  data.gz -= data_.gyro_bias[2];

  data.ax -= data_.accel_bias[0];
  data.ay -= data_.accel_bias[1];
  data.az -= data_.accel_bias[2];
}

void ImuCalibration::SetData(const ImuCalibData& data) {
  // Валидация: bias не должен быть слишком большим
  for (int i = 0; i < 3; ++i) {
    if (std::fabs(data.gyro_bias[i]) > kMaxGyroBias) {
      data_.valid = false;
      return;
    }
    if (std::fabs(data.accel_bias[i]) > kMaxAccelBias) {
      data_.valid = false;
      return;
    }
  }

  data_ = data;
}
