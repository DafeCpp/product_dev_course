#include "vehicle_ekf.hpp"

#include <cstring>

namespace rc_vehicle {

namespace {
constexpr float kPi = 3.14159265358979f;
constexpr float kRadToDeg = 180.0f / kPi;
constexpr float kMinSpeedThreshold = 0.3f;  // м/с: ниже этого EKF без датчиков колёс ненадёжен
}  // namespace

// ═════════════════════════════════════════════════════════════════════════
// Конструктор и инициализация
// ═════════════════════════════════════════════════════════════════════════

VehicleEkf::VehicleEkf(NoiseParams params) noexcept : params_(params) {
  InitP();
}

void VehicleEkf::InitP() noexcept {
  memset(P_, 0, sizeof(P_));
  P_[0] = 1.0f;  // P[0][0] = var(vx)
  P_[4] = 1.0f;  // P[1][1] = var(vy)
  P_[8] = 1.0f;  // P[2][2] = var(r)
}

void VehicleEkf::Reset() noexcept {
  x_[0] = x_[1] = x_[2] = 0.0f;
  InitP();
}

void VehicleEkf::SetState(float vx, float vy, float r) noexcept {
  x_[0] = vx;
  x_[1] = vy;
  x_[2] = r;
}

// ═════════════════════════════════════════════════════════════════════════
// Шаг предсказания (bicycle model)
// ═════════════════════════════════════════════════════════════════════════

void VehicleEkf::Predict(float ax, float ay, float dt) noexcept {
  if (dt <= 0.0f) {
    return;
  }

  const float vx = x_[0];
  const float vy = x_[1];
  const float r = x_[2];

  // ── Нелинейная модель f(x, u) ─────────────────────────────────────────
  //
  // Из уравнений движения в СК машины:
  //   ax_imu = vx_dot - r * vy  →  vx_dot = ax_imu + r * vy
  //   ay_imu = vy_dot + r * vx  →  vy_dot = ay_imu - r * vx
  //   r — random walk (корректируется по gyro_z в шаге обновления)
  //
  // Демпфирование vy: моделирует боковую жёсткость шин.
  // При нормальном повороте ay≈r*vx → vy_dot≈0, демпфирование удерживает vy≈0.
  // При реальном заносе (ay<r*vx, нет бокового усилия) vy нарастает несмотря
  // на демпфирование.

  const float vy_damp = 1.0f - params_.vy_decay_hz * dt;

  x_[0] = vx + dt * (ax + r * vy);
  x_[1] = vy * vy_damp + dt * (ay - r * vx);
  x_[2] = r;

  // ── Якобиан F = df/dx (вычисляется в точке x до обновления) ──────────
  //
  // F = [ 1,          dt*r,   dt*vy ]
  //     [ -dt*r,   vy_damp,  -dt*vx ]
  //     [ 0,          0,      1     ]

  float F[9] = {
      1.0f,    dt * r,  dt * vy,   //
      -dt * r, vy_damp, -dt * vx,  //
      0.0f,    0.0f,    1.0f       //
  };

  // ── P = F * P * F^T + Q ───────────────────────────────────────────────
  float FP[9], Ft[9], FPFt[9];
  MatMul3x3(F, P_, FP);
  MatTranspose3x3(F, Ft);
  MatMul3x3(FP, Ft, FPFt);

  // Добавить диагональный шум процесса Q
  FPFt[0] += params_.q_vx;
  FPFt[4] += params_.q_vy;
  FPFt[8] += params_.q_r;

  memcpy(P_, FPFt, sizeof(P_));
  SymmetrizeP(P_);
  ClampP();
}

// ═════════════════════════════════════════════════════════════════════════
// Шаг обновления по gyro_z
// ═════════════════════════════════════════════════════════════════════════

void VehicleEkf::UpdateGyroZ(float gz) noexcept {
  // H = [0, 0, 1]  →  измерение r
  // Инновация: y = gz - r
  const float innov = gz - x_[2];

  // S = H * P * H^T + R = P[2][2] + R_gz.
  // ClampP() гарантирует P[2][2] >= kPDiagMin = 1e-6f, r_gz >= 0 →
  // S >= kPDiagMin > 0 всегда. Порог по kPDiagMin защищает только от
  // r_gz < 0 (ошибка конфигурации), штатный 1e-9f никогда не срабатывал.
  const float S = P_[8] + params_.r_gz;
  if (S < kPDiagMin) {
    return;
  }

  // K = P * H^T / S = P-столбец 2, делённый на S
  //   K = [P[0][2], P[1][2], P[2][2]]^T / S
  const float K0 = P_[2] / S;  // K для vx
  const float K1 = P_[5] / S;  // K для vy
  const float K2 = P_[8] / S;  // K для r

  // x = x + K * y
  x_[0] += K0 * innov;
  x_[1] += K1 * innov;
  x_[2] += K2 * innov;

  // P = (I - K * H) * P
  //
  // K * H = [[0, 0, K0],   →  I - K*H = [[1,  0,  -K0],
  //          [0, 0, K1],                  [0,  1,  -K1],
  //          [0, 0, K2]]                  [0,  0,  1-K2]]
  //
  // Строки результата:
  //   row i: P_row_i - K_i * P_row_2

  for (int j = 0; j < 3; ++j) {
    const float p2j = P_[6 + j];  // P[2][j]
    P_[0 + j] -= K0 * p2j;
    P_[3 + j] -= K1 * p2j;
    P_[6 + j] *= (1.0f - K2);
  }

  SymmetrizeP(P_);
  ClampP();
}

// ═════════════════════════════════════════════════════════════════════════
// Zero Velocity Update (ZUPT)
// ═════════════════════════════════════════════════════════════════════════

void VehicleEkf::ScalarZeroUpdate(int col, float r) noexcept {
  // Скалярное Kalman-обновление: z = 0, H = e_col^T.
  //
  // Joseph form P_new[i][j] = P[i][j] − K[i]·Pcol[j] − K[j]·Pcol[i] + K[i]·K[j]·S
  // получена аналитически для H = e_col^T, где K[i] = P[i][col] / S.
  // Обходим верхний треугольник и зеркалируем — снапшот только столбца Pcol.

  const float S = P_[col * 3 + col] + r;
  if (S < kPDiagMin) return;

  // Снапшот столбца col (P симметрична, поэтому P[i][col] = P[col][i])
  const float Pcol[3] = {P_[col], P_[3 + col], P_[6 + col]};
  const float K[3] = {Pcol[0] / S, Pcol[1] / S, Pcol[2] / S};

  // Состояние: x += K * (-x[col])
  const float innov = -x_[col];
  x_[0] += K[0] * innov;
  x_[1] += K[1] * innov;
  x_[2] += K[2] * innov;

  // P: обходим верхний треугольник, зеркалируем нижний
  for (int i = 0; i < 3; ++i) {
    for (int j = i; j < 3; ++j) {
      const float val =
          P_[i * 3 + j] - K[i] * Pcol[j] - K[j] * Pcol[i] + K[i] * K[j] * S;
      P_[i * 3 + j] = val;
      P_[j * 3 + i] = val;
    }
  }
}

void VehicleEkf::UpdateZeroVelocity(float r_zupt) noexcept {
  // Два последовательных скалярных обновления Kalman с нулевым измерением.
  // Joseph form гарантирует сохранение положительной полуопределённости P
  // при частых вызовах (в отличие от упрощённой P_new = (I−KH)·P).
  ScalarZeroUpdate(0, r_zupt);  // H = [1,0,0], z = 0 → vx → 0
  ScalarZeroUpdate(1, r_zupt);  // H = [0,1,0], z = 0 → vy → 0
  ClampP();
}

// ═════════════════════════════════════════════════════════════════════════
// Угол заноса
// ═════════════════════════════════════════════════════════════════════════

float VehicleEkf::GetSlipAngleRad() const noexcept {
  if (GetSpeedMs() < kMinSpeedThreshold) {
    return 0.0f;
  }
  // При сильном отрицательном vx (< -kMinSpeedThreshold) EKF без датчиков
  // колёс не может надёжно оценить угол заноса: вектор скорости инвертирован,
  // что даёт atan2 ≈ 120–180°. При vx ∈ (-kMinSpeedThreshold, 0] значение
  // может быть артефактом float-точности, но atan2 всё ещё даёт корректный
  // угол (±90° при vx≈0, vy≠0), поэтому guard только для явного заднего хода.
  if (x_[0] < -kMinSpeedThreshold) {
    return 0.0f;
  }
  return std::atan2(x_[1], x_[0]);
}

float VehicleEkf::GetSlipAngleDeg() const noexcept {
  return GetSlipAngleRad() * kRadToDeg;
}

// ═════════════════════════════════════════════════════════════════════════
// Матричные операции (3×3, row-major)
// ═════════════════════════════════════════════════════════════════════════

void VehicleEkf::MatMul3x3(const float A[9], const float B[9],
                            float C[9]) noexcept {
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j) {
      float s = 0.0f;
      for (int k = 0; k < 3; ++k) {
        s += A[i * 3 + k] * B[k * 3 + j];
      }
      C[i * 3 + j] = s;
    }
  }
}

void VehicleEkf::MatTranspose3x3(const float A[9], float At[9]) noexcept {
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j) {
      At[j * 3 + i] = A[i * 3 + j];
    }
  }
}

void VehicleEkf::ClampP() noexcept {
  // Если хоть один элемент P не конечен — сброс к InitP()
  for (int i = 0; i < 9; ++i) {
    if (!std::isfinite(P_[i])) {
      InitP();
      return;
    }
  }
  // Ограничить диагональные элементы [kPDiagMin, kPDiagMax]
  for (int i = 0; i < 3; ++i) {
    const int d = i * 3 + i;
    if (P_[d] > kPDiagMax) P_[d] = kPDiagMax;
    if (P_[d] < kPDiagMin) P_[d] = kPDiagMin;
  }
}

void VehicleEkf::SymmetrizeP(float P[9]) noexcept {
  // Принудительная симметризация для численной стабильности:
  // P[i][j] = P[j][i] = (P[i][j] + P[j][i]) / 2
  for (int i = 0; i < 3; ++i) {
    for (int j = i + 1; j < 3; ++j) {
      const float avg = 0.5f * (P[i * 3 + j] + P[j * 3 + i]);
      P[i * 3 + j] = avg;
      P[j * 3 + i] = avg;
    }
  }
}

void VehicleEkf::UpdateFromImu(float ax_g, float ay_g, float az_g,
                               float gz_dps, float dt_sec) noexcept {
  constexpr float kG = 9.80665f;
  constexpr float kDegToRad = kPi / 180.0f;

  // Predict: ax,ay в g → м/с²
  Predict(ax_g * kG, ay_g * kG, dt_sec);

  // Update: gz в °/с → рад/с
  UpdateGyroZ(gz_dps * kDegToRad);

  // ZUPT: если |a| ≈ 1g и |gyro_z| мал → машина стоит
  const float accel_mag =
      std::sqrt(ax_g * ax_g + ay_g * ay_g + az_g * az_g);
  constexpr float kZuptAccelThresh = 0.05f;  // ||a| - 1g| < 0.05g
  constexpr float kZuptGyroThresh = 3.0f;    // |gyro_z| < 3 dps
  if (std::abs(accel_mag - 1.0f) < kZuptAccelThresh &&
      std::abs(gz_dps) < kZuptGyroThresh) {
    UpdateZeroVelocity(0.1f);
  }
}

}  // namespace rc_vehicle
