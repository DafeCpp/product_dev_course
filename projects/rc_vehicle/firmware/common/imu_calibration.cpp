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

bool ImuCalibration::StartForwardCalibration(int num_samples) {
  if (!data_.valid) return false;
  double g2 = static_cast<double>(data_.gravity_vec[0]) * data_.gravity_vec[0] +
              static_cast<double>(data_.gravity_vec[1]) * data_.gravity_vec[1] +
              static_cast<double>(data_.gravity_vec[2]) * data_.gravity_vec[2];
  if (g2 < 1e-6) return false;
  mode_ = CalibMode::Forward;
  target_samples_ = num_samples > 0 ? num_samples : 2000;
  status_ = CalibStatus::Collecting;
  collected_ = 0;
  std::memset(sum_linear_, 0, sizeof(sum_linear_));
  first_linear_set_ = false;
  return true;
}

int ImuCalibration::GetCalibStage() const {
  if (status_ != CalibStatus::Collecting) return 0;
  return (mode_ == CalibMode::Forward) ? 2 : 1;
}

void ImuCalibration::FeedSample(const ImuData& raw) {
  if (status_ != CalibStatus::Collecting) return;

  if (mode_ == CalibMode::Forward) {
    // Линейное ускорение = откалиброванный accel − вектор g (в g)
    float ax_cal = raw.ax - data_.accel_bias[0];
    float ay_cal = raw.ay - data_.accel_bias[1];
    float az_cal = raw.az - data_.accel_bias[2];
    float lx = ax_cal - data_.gravity_vec[0];
    float ly = ay_cal - data_.gravity_vec[1];
    float lz = az_cal - data_.gravity_vec[2];
    float mag2 = lx * lx + ly * ly + lz * lz;
    if (mag2 >= kLinearAccelThreshold * kLinearAccelThreshold) {
      if (!first_linear_set_) {
        first_linear_[0] = lx;
        first_linear_[1] = ly;
        first_linear_[2] = lz;
        first_linear_set_ = true;
      }
      sum_linear_[0] += lx;
      sum_linear_[1] += ly;
      sum_linear_[2] += lz;
    }
    ++collected_;
    if (collected_ >= target_samples_) {
      if (FinalizeForward()) {
        status_ = CalibStatus::Done;
      } else {
        status_ = CalibStatus::Failed;
      }
    }
    return;
  }

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

  // Accel bias и вектор g в СК датчика (этап 1: стояние на месте).
  if (mode_ == CalibMode::Full) {
    const double expected_az = (mean[5] >= 0.0) ? 1.0 : -1.0;
    data_.accel_bias[0] = static_cast<float>(mean[3]);
    data_.accel_bias[1] = static_cast<float>(mean[4]);
    data_.accel_bias[2] = static_cast<float>(mean[5] - expected_az);
    // Вектор g в СК датчика (направление при покое)
    double g2 = mean[3] * mean[3] + mean[4] * mean[4] + mean[5] * mean[5];
    constexpr double kMinG2 = 1e-6;
    if (g2 >= kMinG2) {
      double g = std::sqrt(g2);
      data_.gravity_vec[0] = static_cast<float>(mean[3] / g);
      data_.gravity_vec[1] = static_cast<float>(mean[4] / g);
      data_.gravity_vec[2] = static_cast<float>(mean[5] / g);
    }
  }

  data_.valid = true;
  return true;
}

bool ImuCalibration::FinalizeForward() {
  double n2 = sum_linear_[0] * sum_linear_[0] + sum_linear_[1] * sum_linear_[1] + sum_linear_[2] * sum_linear_[2];
  constexpr double kMinNorm2 = 1e-8;
  if (n2 < kMinNorm2) return false;
  double n = std::sqrt(n2);
  float fx = static_cast<float>(sum_linear_[0] / n);
  float fy = static_cast<float>(sum_linear_[1] / n);
  float fz = static_cast<float>(sum_linear_[2] / n);
  // Знак: совпадаем с первым значимым ускорением (считаем его «вперёд»)
  if (first_linear_set_) {
    float dot = fx * first_linear_[0] + fy * first_linear_[1] + fz * first_linear_[2];
    if (dot < 0.f) {
      fx = -fx;
      fy = -fy;
      fz = -fz;
    }
  }
  data_.accel_forward_vec[0] = fx;
  data_.accel_forward_vec[1] = fy;
  data_.accel_forward_vec[2] = fz;
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

float ImuCalibration::GetForwardAccel(const ImuData& data) const {
  return data.ax * data_.accel_forward_vec[0] +
         data.ay * data_.accel_forward_vec[1] +
         data.az * data_.accel_forward_vec[2];
}

void ImuCalibration::SetForwardDirection(float fx, float fy, float fz) {
  double n2 = static_cast<double>(fx) * fx + static_cast<double>(fy) * fy + static_cast<double>(fz) * fz;
  constexpr double kMinNorm2 = 1e-6;
  if (n2 < kMinNorm2) {
    data_.accel_forward_vec[0] = 1.f;
    data_.accel_forward_vec[1] = 0.f;
    data_.accel_forward_vec[2] = 0.f;
    return;
  }
  double n = std::sqrt(n2);
  data_.accel_forward_vec[0] = static_cast<float>(fx / n);
  data_.accel_forward_vec[1] = static_cast<float>(fy / n);
  data_.accel_forward_vec[2] = static_cast<float>(fz / n);
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
  auto normalize_forward = [](float* v) {
    double n2 = static_cast<double>(v[0]) * v[0] + static_cast<double>(v[1]) * v[1] + static_cast<double>(v[2]) * v[2];
    if (n2 >= 1e-6) {
      double n = std::sqrt(n2);
      v[0] = static_cast<float>(v[0] / n);
      v[1] = static_cast<float>(v[1] / n);
      v[2] = static_cast<float>(v[2] / n);
    } else {
      v[0] = 1.f; v[1] = 0.f; v[2] = 0.f;
    }
  };
  auto normalize_gravity = [](float* v) {
    double n2 = static_cast<double>(v[0]) * v[0] + static_cast<double>(v[1]) * v[1] + static_cast<double>(v[2]) * v[2];
    if (n2 >= 1e-6) {
      double n = std::sqrt(n2);
      v[0] = static_cast<float>(v[0] / n);
      v[1] = static_cast<float>(v[1] / n);
      v[2] = static_cast<float>(v[2] / n);
    } else {
      v[0] = 0.f; v[1] = 0.f; v[2] = 1.f;
    }
  };
  normalize_forward(data_.accel_forward_vec);
  normalize_gravity(data_.gravity_vec);
}
