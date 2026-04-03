#include "mag_calibration.hpp"

#include <cfloat>
#include <cmath>
#include <cstring>

// ═════════════════════════════════════════════════════════════════════════
// Jacobi iteration for 3×3 symmetric matrix
// ═════════════════════════════════════════════════════════════════════════

void MagCalibration::Jacobi3x3(float A[3][3], float V[3][3], int max_iter) {
  // V = I
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j)
      V[i][j] = (i == j) ? 1.f : 0.f;

  for (int iter = 0; iter < max_iter; ++iter) {
    // Найти наибольший |A[p][q]|, p != q
    int p = 0, q = 1;
    float max_val = std::fabs(A[0][1]);
    for (int i = 0; i < 3; ++i) {
      for (int j = i + 1; j < 3; ++j) {
        const float v = std::fabs(A[i][j]);
        if (v > max_val) {
          max_val = v;
          p = i;
          q = j;
        }
      }
    }

    if (max_val < 1e-10f) break;  // сошлось

    // Угол вращения
    const float diff = A[q][q] - A[p][p];
    float t;  // tan(θ)
    if (std::fabs(diff) < 1e-12f) {
      t = 1.f;  // θ = π/4
    } else {
      const float phi = diff / (2.f * A[p][q]);
      t = 1.f / (std::fabs(phi) + std::sqrt(phi * phi + 1.f));
      if (phi < 0.f) t = -t;
    }

    const float c = 1.f / std::sqrt(t * t + 1.f);
    const float s = t * c;

    // Обновить A: вращение Гивенса
    const float app = A[p][p], aqq = A[q][q], apq = A[p][q];
    A[p][p] = app - t * apq;
    A[q][q] = aqq + t * apq;
    A[p][q] = 0.f;
    A[q][p] = 0.f;

    for (int r = 0; r < 3; ++r) {
      if (r == p || r == q) continue;
      const float arp = A[r][p], arq = A[r][q];
      A[r][p] = c * arp - s * arq;
      A[p][r] = A[r][p];
      A[r][q] = s * arp + c * arq;
      A[q][r] = A[r][q];
    }

    // Обновить V (столбцы — собственные вектора)
    for (int r = 0; r < 3; ++r) {
      const float vrp = V[r][p], vrq = V[r][q];
      V[r][p] = c * vrp - s * vrq;
      V[r][q] = s * vrp + c * vrq;
    }
  }
}

// ═════════════════════════════════════════════════════════════════════════
// Compute orthonormal basis in the plane perpendicular to normal
// ═════════════════════════════════════════════════════════════════════════

void MagCalibration::ComputeBasis(const float n[3], float e1[3], float e2[3]) {
  // Выбрать вспомогательный вектор, не параллельный n
  float aux[3] = {0.f, 0.f, 1.f};
  const float dot = n[0] * aux[0] + n[1] * aux[1] + n[2] * aux[2];
  if (std::fabs(dot) > 0.9f) {
    aux[0] = 1.f;
    aux[1] = 0.f;
    aux[2] = 0.f;
  }

  // e1 = normalize(n × aux)
  e1[0] = n[1] * aux[2] - n[2] * aux[1];
  e1[1] = n[2] * aux[0] - n[0] * aux[2];
  e1[2] = n[0] * aux[1] - n[1] * aux[0];
  const float len1 =
      std::sqrt(e1[0] * e1[0] + e1[1] * e1[1] + e1[2] * e1[2]);
  if (len1 > 1e-9f) {
    e1[0] /= len1;
    e1[1] /= len1;
    e1[2] /= len1;
  }

  // e2 = n × e1 (уже нормализован, т.к. n и e1 единичные и ортогональные)
  e2[0] = n[1] * e1[2] - n[2] * e1[1];
  e2[1] = n[2] * e1[0] - n[0] * e1[2];
  e2[2] = n[0] * e1[1] - n[1] * e1[0];
}

// ═════════════════════════════════════════════════════════════════════════
// MagCalibration
// ═════════════════════════════════════════════════════════════════════════

void MagCalibration::Start() {
  status_ = MagCalibStatus::Collecting;
  sample_count_ = 0;
  for (int i = 0; i < 3; ++i) {
    min_[i] = FLT_MAX;
    max_[i] = -FLT_MAX;
    sum_[i] = 0.f;
  }
  for (int i = 0; i < 6; ++i) {
    cov_sum_[i] = 0.f;
  }
}

void MagCalibration::FeedSample(const MagData& m) {
  if (status_ != MagCalibStatus::Collecting) {
    return;
  }

  const float v[3] = {m.mx, m.my, m.mz};
  for (int i = 0; i < 3; ++i) {
    if (v[i] < min_[i]) min_[i] = v[i];
    if (v[i] > max_[i]) max_[i] = v[i];
    sum_[i] += v[i];
  }

  // Верхний треугольник: xx, xy, xz, yy, yz, zz
  cov_sum_[0] += v[0] * v[0];
  cov_sum_[1] += v[0] * v[1];
  cov_sum_[2] += v[0] * v[2];
  cov_sum_[3] += v[1] * v[1];
  cov_sum_[4] += v[1] * v[2];
  cov_sum_[5] += v[2] * v[2];

  ++sample_count_;
}

void MagCalibration::Finish() {
  if (status_ != MagCalibStatus::Collecting) {
    return;
  }

  fail_reason_ = MagCalibFailReason::None;

  if (sample_count_ < kMinSamples) {
    status_ = MagCalibStatus::Failed;
    fail_reason_ = MagCalibFailReason::TooFewSamples;
    return;
  }

  // ── Hard iron offset (min/max) ──────────────────────────────────────────
  float radius_sum = 0.f;
  float offsets[3]{};
  for (int i = 0; i < 3; ++i) {
    offsets[i] = (max_[i] + min_[i]) * 0.5f;
    radius_sum += (max_[i] - min_[i]) * 0.5f;
  }
  const float avg_radius = radius_sum / 3.f;

  if (avg_radius < kMinRadius) {
    status_ = MagCalibStatus::Failed;
    fail_reason_ = MagCalibFailReason::RadiusTooSmall;
    return;
  }
  if (avg_radius > kMaxRadius) {
    status_ = MagCalibStatus::Failed;
    fail_reason_ = MagCalibFailReason::RadiusTooLarge;
    return;
  }

  // ── PCA: матрица ковариации ─────────────────────────────────────────────
  const float n_inv = 1.f / static_cast<float>(sample_count_);
  const float mean[3] = {sum_[0] * n_inv, sum_[1] * n_inv, sum_[2] * n_inv};

  // cov_sum хранит: [xx, xy, xz, yy, yz, zz]
  // C[i][j] = cov_sum[k]/N - mean[i]*mean[j]
  float C[3][3];
  C[0][0] = cov_sum_[0] * n_inv - mean[0] * mean[0];
  C[0][1] = cov_sum_[1] * n_inv - mean[0] * mean[1];
  C[0][2] = cov_sum_[2] * n_inv - mean[0] * mean[2];
  C[1][0] = C[0][1];
  C[1][1] = cov_sum_[3] * n_inv - mean[1] * mean[1];
  C[1][2] = cov_sum_[4] * n_inv - mean[1] * mean[2];
  C[2][0] = C[0][2];
  C[2][1] = C[1][2];
  C[2][2] = cov_sum_[5] * n_inv - mean[2] * mean[2];

  // ── Якоби → собственные значения (диагональ C) и вектора (столбцы V) ────
  float V[3][3];
  Jacobi3x3(C, V);

  // Собственные значения = C[0][0], C[1][1], C[2][2] после Якоби
  float eigenvalues[3] = {C[0][0], C[1][1], C[2][2]};

  // Найти индексы: min, mid, max
  int idx_min = 0, idx_max = 0;
  for (int i = 1; i < 3; ++i) {
    if (eigenvalues[i] < eigenvalues[idx_min]) idx_min = i;
    if (eigenvalues[i] > eigenvalues[idx_max]) idx_max = i;
  }
  const int idx_mid = 3 - idx_min - idx_max;

  // ── Валидация планарности ───────────────────────────────────────────────
  // λ_min / λ_mid < kPlanarityThreshold — данные лежат в плоскости
  if (eigenvalues[idx_mid] > 1e-6f) {
    const float ratio = eigenvalues[idx_min] / eigenvalues[idx_mid];
    if (ratio > kPlanarityThreshold) {
      status_ = MagCalibStatus::Failed;
      fail_reason_ = MagCalibFailReason::NotPlanar;
      return;
    }
  }

  // ── Сохранить результат ─────────────────────────────────────────────────
  for (int i = 0; i < 3; ++i) {
    data_.offset[i] = offsets[i];
    data_.normal[i] = V[i][idx_min];  // столбец V с наименьшим λ
  }

  // Нормализовать normal (Якоби должен дать единичный, но на всякий случай)
  const float nlen = std::sqrt(data_.normal[0] * data_.normal[0] +
                               data_.normal[1] * data_.normal[1] +
                               data_.normal[2] * data_.normal[2]);
  if (nlen > 1e-9f) {
    data_.normal[0] /= nlen;
    data_.normal[1] /= nlen;
    data_.normal[2] /= nlen;
  }

  ComputeBasis(data_.normal, data_.basis1, data_.basis2);

  data_.valid = true;
  status_ = MagCalibStatus::Done;
}

const char* MagCalibration::GetFailReasonStr() const noexcept {
  switch (fail_reason_) {
    case MagCalibFailReason::TooFewSamples:
      return "too_few_samples";
    case MagCalibFailReason::RadiusTooSmall:
      return "radius_too_small";
    case MagCalibFailReason::RadiusTooLarge:
      return "radius_too_large";
    case MagCalibFailReason::NotPlanar:
      return "not_planar";
    default:
      return "none";
  }
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
    // Пересчитать basis из normal (на случай загрузки из NVS v1 без basis)
    ComputeBasis(data_.normal, data_.basis1, data_.basis2);
    status_ = MagCalibStatus::Done;
  }
}
