#include "vehicle_ekf.hpp"

#include <cstring>

namespace rc_vehicle {

namespace {
constexpr float kPi = 3.14159265358979f;
constexpr float kRadToDeg = 180.0f / kPi;
constexpr float kMinSpeedThreshold = 0.3f;
}  // namespace

// ═════════════════════════════════════════════════════════════════════════
// Конструктор и инициализация
// ═════════════════════════════════════════════════════════════════════════

VehicleEkf::VehicleEkf(NoiseParams params) noexcept : params_(params) {
  InitP();
}

void VehicleEkf::InitP() noexcept {
  memset(P_, 0, sizeof(P_));
  P_[0]  = 1.0f;  // P[0][0] = var(vx)
  P_[5]  = 1.0f;  // P[1][1] = var(vy)
  P_[10] = 1.0f;  // P[2][2] = var(r)
  P_[15] = 1.0f;  // P[3][3] = var(ψ) ≈ 1 rad² → быстрая сходимость к первому mag
}

void VehicleEkf::Reset() noexcept {
  x_[0] = x_[1] = x_[2] = x_[3] = 0.0f;
  InitP();
}

void VehicleEkf::SetState(float vx, float vy, float r) noexcept {
  x_[0] = vx;
  x_[1] = vy;
  x_[2] = r;
}

void VehicleEkf::SetYaw(float yaw_rad) noexcept {
  x_[3] = WrapAngle(yaw_rad);
}

// ═════════════════════════════════════════════════════════════════════════
// Шаг предсказания (bicycle model + интеграция курса)
// ═════════════════════════════════════════════════════════════════════════

void VehicleEkf::Predict(float ax, float ay, float dt) noexcept {
  if (dt <= 0.0f) {
    return;
  }

  const float vx = x_[0];
  const float vy = x_[1];
  const float r  = x_[2];
  const float psi = x_[3];

  const float vy_damp = 1.0f - params_.vy_decay_hz * dt;

  // ── Нелинейная модель f(x, u) ─────────────────────────────────────────
  x_[0] = vx + dt * (ax + r * vy);
  x_[1] = vy * vy_damp + dt * (ay - r * vx);
  x_[2] = r;
  x_[3] = WrapAngle(psi + r * dt);

  // ── Якобиан F (4×4, row-major) ────────────────────────────────────────
  //
  // F = [ 1,       dt·r,   dt·vy,  0 ]
  //     [ -dt·r,   vy_dmp, -dt·vx, 0 ]
  //     [ 0,       0,      1,      0 ]
  //     [ 0,       0,      dt,     1 ]
  //
  // ∂ψ/∂r = dt, остальные частные производные ψ нулевые.

  float F[16] = {
      1.0f,    dt * r,   dt * vy,  0.0f,
      -dt * r, vy_damp,  -dt * vx, 0.0f,
      0.0f,    0.0f,     1.0f,     0.0f,
      0.0f,    0.0f,     dt,       1.0f,
  };

  // ── P = F * P * F^T + Q ───────────────────────────────────────────────
  float FP[16], Ft[16], FPFt[16];
  MatMul4x4(F, P_, FP);
  MatTranspose4x4(F, Ft);
  MatMul4x4(FP, Ft, FPFt);

  FPFt[0]  += params_.q_vx;
  FPFt[5]  += params_.q_vy;
  FPFt[10] += params_.q_r;
  FPFt[15] += params_.q_psi;

  memcpy(P_, FPFt, sizeof(P_));
  SymmetrizeP(P_);
  ClampP();
}

// ═════════════════════════════════════════════════════════════════════════
// Шаг обновления по gyro_z: H = [0, 0, 1, 0]
// ═════════════════════════════════════════════════════════════════════════

void VehicleEkf::UpdateGyroZ(float gz) noexcept {
  const float innov = gz - x_[2];

  // S = P[2][2] + R_gz = P_[10] + r_gz
  const float S = P_[10] + params_.r_gz;
  if (S < kPDiagMin) {
    return;
  }

  // K[i] = P_[i*4+2] / S  (столбец 2 матрицы P)
  const float K0 = P_[2]  / S;  // P[0][2]
  const float K1 = P_[6]  / S;  // P[1][2]
  const float K2 = P_[10] / S;  // P[2][2]
  const float K3 = P_[14] / S;  // P[3][2]

  x_[0] += K0 * innov;
  x_[1] += K1 * innov;
  x_[2] += K2 * innov;
  x_[3] = WrapAngle(x_[3] + K3 * innov);

  // P = (I - K*H)*P: строка i: P[i][j] -= K[i] * P[2][j]
  const float K[4] = {K0, K1, K2, K3};
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      P_[i * 4 + j] -= K[i] * P_[2 * 4 + j];
    }
  }

  SymmetrizeP(P_);
  ClampP();
}

// ═════════════════════════════════════════════════════════════════════════
// Шаг обновления по курсу магнитометра: H = [0, 0, 0, 1]
// ═════════════════════════════════════════════════════════════════════════

void VehicleEkf::UpdateHeading(float heading_rad) noexcept {
  const float z = WrapAngle(heading_rad);

  // Инновация с обёрткой угла: корректно обрабатывает переход через ±π
  const float innov = std::atan2(std::sin(z - x_[3]),
                                 std::cos(z - x_[3]));

  // S = P[3][3] + R_heading = P_[15] + r_heading
  const float S = P_[15] + params_.r_heading;
  if (S < kPDiagMin) {
    return;
  }

  // K[i] = P_[i*4+3] / S  (столбец 3 матрицы P)
  const float K0 = P_[3]  / S;  // P[0][3]
  const float K1 = P_[7]  / S;  // P[1][3]
  const float K2 = P_[11] / S;  // P[2][3]
  const float K3 = P_[15] / S;  // P[3][3]

  x_[0] += K0 * innov;
  x_[1] += K1 * innov;
  x_[2] += K2 * innov;
  x_[3] = WrapAngle(x_[3] + K3 * innov);

  // P = (I - K*H)*P: строка i: P[i][j] -= K[i] * P[3][j]
  const float K[4] = {K0, K1, K2, K3};
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      P_[i * 4 + j] -= K[i] * P_[3 * 4 + j];
    }
  }

  SymmetrizeP(P_);
  ClampP();
}

// ═════════════════════════════════════════════════════════════════════════
// Zero Velocity Update (ZUPT)
// ═════════════════════════════════════════════════════════════════════════

void VehicleEkf::ScalarZeroUpdate(int col, float r) noexcept {
  // Скалярное Kalman-обновление: z = 0, H = e_col^T.
  // Joseph form: P_new[i][j] = P[i][j] − K[i]·Pcol[j] − K[j]·Pcol[i] + K[i]·K[j]·S
  const float S = P_[col * 4 + col] + r;
  if (S < kPDiagMin) return;

  // Снапшот столбца col (P симметрична: P[i][col] == P[col][i])
  const float Pcol[4] = {P_[col], P_[4 + col], P_[8 + col], P_[12 + col]};
  const float K[4] = {Pcol[0] / S, Pcol[1] / S, Pcol[2] / S, Pcol[3] / S};

  const float innov = -x_[col];
  x_[0] += K[0] * innov;
  x_[1] += K[1] * innov;
  x_[2] += K[2] * innov;
  x_[3] = WrapAngle(x_[3] + K[3] * innov);

  // Joseph form: верхний треугольник + зеркало
  for (int i = 0; i < 4; ++i) {
    for (int j = i; j < 4; ++j) {
      const float val =
          P_[i * 4 + j] - K[i] * Pcol[j] - K[j] * Pcol[i] + K[i] * K[j] * S;
      P_[i * 4 + j] = val;
      P_[j * 4 + i] = val;
    }
  }
}

void VehicleEkf::UpdateZeroVelocity(float r_zupt) noexcept {
  ScalarZeroUpdate(0, r_zupt);  // vx → 0
  ScalarZeroUpdate(1, r_zupt);  // vy → 0
  ClampP();
}

// ═════════════════════════════════════════════════════════════════════════
// Высокоуровневое обновление из IMU
// ═════════════════════════════════════════════════════════════════════════

void VehicleEkf::UpdateFromImu(float ax_g, float ay_g, float az_g,
                               float gz_dps, float dt_sec) noexcept {
  constexpr float kG = 9.80665f;
  constexpr float kDegToRad = kPi / 180.0f;

  Predict(ax_g * kG, ay_g * kG, dt_sec);
  UpdateGyroZ(gz_dps * kDegToRad);

  const float accel_mag =
      std::sqrt(ax_g * ax_g + ay_g * ay_g + az_g * az_g);
  constexpr float kZuptAccelThresh = 0.05f;
  constexpr float kZuptGyroThresh = 3.0f;
  if (std::abs(accel_mag - 1.0f) < kZuptAccelThresh &&
      std::abs(gz_dps) < kZuptGyroThresh) {
    UpdateZeroVelocity(0.1f);
  }
}

// ═════════════════════════════════════════════════════════════════════════
// Угол заноса
// ═════════════════════════════════════════════════════════════════════════

float VehicleEkf::GetSlipAngleRad() const noexcept {
  if (GetSpeedMs() < kMinSpeedThreshold) {
    return 0.0f;
  }
  if (x_[0] < -kMinSpeedThreshold) {
    return 0.0f;
  }
  return std::atan2(x_[1], x_[0]);
}

float VehicleEkf::GetSlipAngleDeg() const noexcept {
  return GetSlipAngleRad() * kRadToDeg;
}

float VehicleEkf::GetYawDeg() const noexcept {
  return x_[3] * kRadToDeg;
}

// ═════════════════════════════════════════════════════════════════════════
// Вспомогательные функции
// ═════════════════════════════════════════════════════════════════════════

float VehicleEkf::WrapAngle(float a) noexcept {
  // Быстрая нормализация в [-π, π]
  while (a >  kPi) a -= 2.0f * kPi;
  while (a < -kPi) a += 2.0f * kPi;
  return a;
}

void VehicleEkf::MatMul4x4(const float A[16], const float B[16],
                            float C[16]) noexcept {
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      float s = 0.0f;
      for (int k = 0; k < 4; ++k) {
        s += A[i * 4 + k] * B[k * 4 + j];
      }
      C[i * 4 + j] = s;
    }
  }
}

void VehicleEkf::MatTranspose4x4(const float A[16], float At[16]) noexcept {
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      At[j * 4 + i] = A[i * 4 + j];
    }
  }
}

void VehicleEkf::ClampP() noexcept {
  for (int i = 0; i < 16; ++i) {
    if (!std::isfinite(P_[i])) {
      InitP();
      return;
    }
  }
  // Диагональные индексы 4×4: 0, 5, 10, 15
  const int diag[4] = {0, 5, 10, 15};
  for (int d : diag) {
    if (P_[d] > kPDiagMax) P_[d] = kPDiagMax;
    if (P_[d] < kPDiagMin) P_[d] = kPDiagMin;
  }
}

void VehicleEkf::SymmetrizeP(float P[16]) noexcept {
  for (int i = 0; i < 4; ++i) {
    for (int j = i + 1; j < 4; ++j) {
      const float avg = 0.5f * (P[i * 4 + j] + P[j * 4 + i]);
      P[i * 4 + j] = avg;
      P[j * 4 + i] = avg;
    }
  }
}

}  // namespace rc_vehicle
