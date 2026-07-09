#pragma once

namespace bench {

/**
 * @brief Упрощённая модель гидравлического канала нагружения
 *
 * Цепочка: команда клапану → золотник (апериодическое звено 1-го
 * порядка, τ ≈ 8 мс — динамика штатной электроники Atos) → расход ∝
 * положению золотника → скорость поршня → перемещение штока →
 * усилие = жёсткость_образца · перемещение (образец как пружина).
 *
 * Разрушение образца моделируется падением жёсткости до остаточной
 * доли (TriggerFailure) — сила проседает при том же перемещении, как
 * при реальном разрыве.
 *
 * Модель используется и в C++ (ctest, эмулятор на ESP32), и зеркально
 * в Python (sim/benchsim/plant.py); согласованность проверяется общим
 * тест-вектором.
 */
class HydraulicPlantModel {
 public:
  struct Config {
    float spool_tau_s{0.008f};        ///< Постоянная времени золотника
    float piston_speed_mm_s{400.0f};  ///< Скорость поршня при полном открытии
    float stiffness_n_mm{5000.0f};    ///< Жёсткость образца
    float position_limit_mm{80.0f};   ///< Ход штока (механический упор)
  };

  struct State {
    float spool{0.0f};        ///< Положение золотника [-1..1]
    float position_mm{0.0f};  ///< Перемещение штока
    float force_n{0.0f};      ///< Усилие на образце
  };

  HydraulicPlantModel() = default;
  explicit HydraulicPlantModel(const Config& config) : config_(config) {}

  /**
   * @brief Шаг модели
   * @param valve_cmd Команда клапану [-1..1]
   * @param dt_sec Шаг времени
   * @return Новое состояние
   */
  State Step(float valve_cmd, float dt_sec) noexcept;

  /// Разрушение образца: жёсткость падает до residual_fraction от исходной
  void TriggerFailure(float residual_fraction = 0.02f) noexcept {
    stiffness_scale_ = residual_fraction;
  }

  [[nodiscard]] const State& GetState() const noexcept { return state_; }
  [[nodiscard]] bool Failed() const noexcept {
    return stiffness_scale_ < 1.0f;
  }

 private:
  Config config_{};
  State state_{};
  float stiffness_scale_{1.0f};
};

}  // namespace bench
