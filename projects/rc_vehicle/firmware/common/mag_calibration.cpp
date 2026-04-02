#include "mag_calibration.hpp"

#include <cfloat>
#include <cmath>

void MagCalibration::Start() {
  status_ = MagCalibStatus::Collecting;
  sample_count_ = 0;
  // Инициализируем min/max противоположными экстремумами
  for (int i = 0; i < 3; ++i) {
    min_[i] = FLT_MAX;
    max_[i] = -FLT_MAX;
  }
}

void MagCalibration::FeedSample(const MagData& m) {
  if (status_ != MagCalibStatus::Collecting) {
    return;
  }

  const float vals[3] = {m.mx, m.my, m.mz};
  for (int i = 0; i < 3; ++i) {
    if (vals[i] < min_[i]) min_[i] = vals[i];
    if (vals[i] > max_[i]) max_[i] = vals[i];
  }
  ++sample_count_;
}

void MagCalibration::Finish() {
  if (status_ != MagCalibStatus::Collecting) {
    return;
  }

  if (sample_count_ < kMinSamples) {
    status_ = MagCalibStatus::Failed;
    return;
  }

  // Вычисляем средний радиус как среднее из полу-размахов по каждой оси
  float radius_sum = 0.f;
  float offsets[3]{};
  for (int i = 0; i < 3; ++i) {
    offsets[i] = (max_[i] + min_[i]) * 0.5f;
    radius_sum += (max_[i] - min_[i]) * 0.5f;
  }
  const float avg_radius = radius_sum / 3.f;

  if (avg_radius < kMinRadius || avg_radius > kMaxRadius) {
    status_ = MagCalibStatus::Failed;
    return;
  }

  for (int i = 0; i < 3; ++i) {
    data_.offset[i] = offsets[i];
  }
  data_.valid = true;
  status_ = MagCalibStatus::Done;
}

void MagCalibration::Cancel() {
  status_ = MagCalibStatus::Idle;
  sample_count_ = 0;
}

void MagCalibration::Apply(MagData& m) const {
  if (!data_.valid) {
    return;
  }
  m.mx -= data_.offset[0];
  m.my -= data_.offset[1];
  m.mz -= data_.offset[2];
}

void MagCalibration::SetData(const MagCalibData& d) {
  data_ = d;
  if (d.valid) {
    status_ = MagCalibStatus::Done;
  }
}
