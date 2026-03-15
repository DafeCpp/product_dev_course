# Рефакторинг системы режимов вождения (Drive Modes)

## Анализ текущей архитектуры

### Проблемы при добавлении нового режима

Сейчас, чтобы добавить новый `DriveMode` (например, `Rally` или `Cruise`), нужно внести изменения минимум в **7 местах**:

1. **`stabilization_config.hpp`** — добавить значение в `enum DriveMode`, новые поля конфигурации, case в `DriveModeToString()`
2. **`stabilization_config.cpp`** — добавить case в `ApplyModeDefaults()` с PID-пресетами
3. **`YawRateController::Process()`** — добавить/изменить проверку `cfg_->mode ==` (сейчас хардкод `Drift`)
4. **`SlipAngleController::Process()`** — аналогично (сейчас хардкод `!= Drift`)
5. **`OversteerGuard::Process()`** — проверка `mode != Drift` для throttle reduction
6. **`vehicle_control_unified.cpp`** — проверка `DirectLaw` в двух местах (pipeline skip, slew rate skip)
7. **`KidsModeProcessor`** — отдельный путь обработки с собственным трекингом `current_mode_`

### Корневые причины

| Проблема | Где проявляется |
|----------|----------------|
| **Mode-aware логика размазана по контроллерам** | Каждый контроллер сам решает, активен ли он для текущего режима через `if (mode == ...)` |
| **Нет абстракции режима** | Режим — это просто `uint8_t` в конфиге, поведение определяется разбросанными `switch`/`if` |
| **Pipeline всегда линейный** | Control loop вызывает все 4 контроллера, каждый внутри решает делать ли что-то |
| **Kids mode — особый случай** | Обрабатывается до pipeline, имеет свой `current_mode_`, дублирует slew rate логику |
| **Конфигурация — монолит** | `StabilizationConfig` содержит параметры всех режимов одновременно |

---

## Предложение: рефакторинг через паттерн Strategy

### Ключевая идея

Каждый `DriveMode` становится **стратегией**, которая объявляет:
- какие контроллеры активны
- какие пресеты PID применять
- какие ограничения (slew rate, лимиты) действуют
- какие safety-фичи включены

### 1. Интерфейс `IDriveModeStrategy`

```cpp
// drive_mode_strategy.hpp
#pragma once

#include "stabilization_config.hpp"

namespace rc_vehicle {

// Forward declarations
class YawRateController;
class PitchCompensator;
class SlipAngleController;
class OversteerGuard;

/**
 * @brief Описание поведения режима — что активно, что нет.
 *
 * Возвращается стратегией, используется в control loop
 * для выбора пути обработки без if/switch по DriveMode.
 */
struct ModeTraits {
  bool yaw_rate_active{true};       // Использовать yaw rate PID
  bool pitch_comp_active{true};     // Использовать pitch compensation
  bool slip_angle_active{false};    // Использовать slip angle PID
  bool oversteer_guard_active{true};// Использовать oversteer detection
  bool oversteer_reduces_throttle{true}; // Oversteer снижает газ (false в Drift)
  bool use_slew_rate{true};         // Применять slew rate к PWM
  bool apply_input_limits{false};   // Лимиты throttle/steering (Kids)
  bool anti_spin_active{false};     // Anti-spin protection (Kids)
};

/**
 * @brief Интерфейс стратегии режима вождения.
 *
 * Каждый DriveMode реализует этот интерфейс.
 * Стратегия stateless — всё состояние остаётся в контроллерах.
 */
class IDriveModeStrategy {
 public:
  virtual ~IDriveModeStrategy() = default;

  /** @brief Идентификатор режима */
  [[nodiscard]] virtual DriveMode GetMode() const noexcept = 0;

  /** @brief Имя режима для логирования */
  [[nodiscard]] virtual const char* GetName() const noexcept = 0;

  /** @brief Описание поведения режима */
  [[nodiscard]] virtual ModeTraits GetTraits() const noexcept = 0;

  /**
   * @brief Применить PID-пресеты к конфигурации.
   * Вызывается при смене режима.
   */
  virtual void ApplyDefaults(StabilizationConfig& cfg) const noexcept = 0;

  /**
   * @brief Пре-обработка команд (лимиты, slew rate).
   * Вызывается перед pipeline. Базовая реализация — no-op.
   * @param throttle команда газа [in/out]
   * @param steering команда руля [in/out]
   * @param dt_ms шаг времени
   */
  virtual void PreProcess(float& throttle, float& steering,
                          uint32_t dt_ms) const noexcept {
    (void)throttle; (void)steering; (void)dt_ms;
  }
};

}  // namespace rc_vehicle
```

### 2. Конкретные стратегии

```cpp
// drive_modes.hpp
#pragma once

#include "drive_mode_strategy.hpp"

namespace rc_vehicle {

class NormalModeStrategy final : public IDriveModeStrategy {
 public:
  [[nodiscard]] DriveMode GetMode() const noexcept override {
    return DriveMode::Normal;
  }
  [[nodiscard]] const char* GetName() const noexcept override {
    return "Normal";
  }
  [[nodiscard]] ModeTraits GetTraits() const noexcept override {
    return {.yaw_rate_active = true,
            .pitch_comp_active = true,
            .slip_angle_active = false,
            .oversteer_guard_active = true,
            .oversteer_reduces_throttle = true,
            .use_slew_rate = true};
  }
  void ApplyDefaults(StabilizationConfig& cfg) const noexcept override {
    cfg.yaw_rate.pid = {0.10f, 0.0f, 0.005f, 0.5f, 0.3f};
    cfg.yaw_rate.steer_to_yaw_rate_dps = 90.0f;
    // ... остальные defaults из текущего ApplyModeDefaults()
  }
};

class SportModeStrategy final : public IDriveModeStrategy {
 public:
  [[nodiscard]] DriveMode GetMode() const noexcept override {
    return DriveMode::Sport;
  }
  [[nodiscard]] const char* GetName() const noexcept override {
    return "Sport";
  }
  [[nodiscard]] ModeTraits GetTraits() const noexcept override {
    return {.yaw_rate_active = true,
            .pitch_comp_active = true,
            .slip_angle_active = false,
            .oversteer_guard_active = true,
            .oversteer_reduces_throttle = true,
            .use_slew_rate = true};
  }
  void ApplyDefaults(StabilizationConfig& cfg) const noexcept override {
    cfg.yaw_rate.pid = {0.20f, 0.01f, 0.010f, 0.7f, 0.5f};
    cfg.yaw_rate.steer_to_yaw_rate_dps = 120.0f;
    // ... агрессивные параметры
  }
};

class DriftModeStrategy final : public IDriveModeStrategy {
 public:
  [[nodiscard]] DriveMode GetMode() const noexcept override {
    return DriveMode::Drift;
  }
  [[nodiscard]] const char* GetName() const noexcept override {
    return "Drift";
  }
  [[nodiscard]] ModeTraits GetTraits() const noexcept override {
    return {.yaw_rate_active = false,        // Руль — за водителем
            .pitch_comp_active = true,
            .slip_angle_active = true,       // Контроль заноса
            .oversteer_guard_active = true,
            .oversteer_reduces_throttle = false, // Занос ожидаем
            .use_slew_rate = true};
  }
  void ApplyDefaults(StabilizationConfig& cfg) const noexcept override {
    cfg.slip_angle.pid = {0.05f, 0.0f, 0.002f, 0.3f, 0.2f};
    // ...
  }
};

class KidsModeStrategy final : public IDriveModeStrategy {
 public:
  [[nodiscard]] DriveMode GetMode() const noexcept override {
    return DriveMode::Kids;
  }
  [[nodiscard]] const char* GetName() const noexcept override {
    return "Kids";
  }
  [[nodiscard]] ModeTraits GetTraits() const noexcept override {
    return {.yaw_rate_active = true,
            .pitch_comp_active = true,
            .slip_angle_active = false,
            .oversteer_guard_active = true,
            .oversteer_reduces_throttle = true,
            .use_slew_rate = true,
            .apply_input_limits = true,      // Лимиты вход. сигнала
            .anti_spin_active = true};       // Anti-spin
  }
  void ApplyDefaults(StabilizationConfig& cfg) const noexcept override {
    cfg.yaw_rate.pid = {0.15f, 0.0f, 0.008f, 0.5f, 0.3f};
    // Более консервативные параметры
  }
  // Kids mode имеет PreProcess для лимитов
  void PreProcess(float& throttle, float& steering,
                  uint32_t dt_ms) const noexcept override;
};

class DirectLawStrategy final : public IDriveModeStrategy {
 public:
  [[nodiscard]] DriveMode GetMode() const noexcept override {
    return DriveMode::DirectLaw;
  }
  [[nodiscard]] const char* GetName() const noexcept override {
    return "DirectLaw";
  }
  [[nodiscard]] ModeTraits GetTraits() const noexcept override {
    return {.yaw_rate_active = false,
            .pitch_comp_active = false,
            .slip_angle_active = false,
            .oversteer_guard_active = false,
            .oversteer_reduces_throttle = false,
            .use_slew_rate = false};        // Без slew rate
  }
  void ApplyDefaults(StabilizationConfig& cfg) const noexcept override {
    // DirectLaw не использует PID — ничего не меняем
  }
};

}  // namespace rc_vehicle
```

### 3. Реестр режимов `DriveModeRegistry`

```cpp
// drive_mode_registry.hpp
#pragma once

#include <array>
#include "drive_mode_strategy.hpp"
#include "drive_modes.hpp"

namespace rc_vehicle {

/**
 * @brief Реестр стратегий режимов вождения.
 *
 * Статический массив стратегий — без аллокаций, O(1) поиск по DriveMode.
 * Добавление нового режима: 1) создать класс, 2) добавить в массив.
 */
class DriveModeRegistry {
 public:
  /**
   * @brief Получить стратегию по DriveMode
   * @return Ссылка на стратегию (никогда nullptr — fallback на Normal)
   */
  static const IDriveModeStrategy& Get(DriveMode mode) noexcept {
    const auto idx = static_cast<size_t>(mode);
    if (idx < kStrategies.size() && kStrategies[idx] != nullptr) {
      return *kStrategies[idx];
    }
    return kNormal;  // fallback
  }

 private:
  static const NormalModeStrategy kNormal;
  static const SportModeStrategy kSport;
  static const DriftModeStrategy kDrift;
  static const KidsModeStrategy kKids;
  static const DirectLawStrategy kDirectLaw;

  // Индексация совпадает с enum DriveMode
  static constexpr std::array<const IDriveModeStrategy*, 5> kStrategies = {
      &kNormal, &kSport, &kDrift, &kKids, &kDirectLaw};
};

}  // namespace rc_vehicle
```

### 4. Упрощённый control loop

Вместо текущего кода с if/switch по `DriveMode`:

```cpp
// vehicle_control_unified.cpp — после рефакторинга

// Получаем стратегию для текущего режима — один раз за итерацию
const auto& strategy = DriveModeRegistry::Get(stab_mgr_->GetConfig().mode);
const auto traits = strategy.GetTraits();

// Pre-processing (лимиты Kids Mode, etc.)
strategy.PreProcess(commanded_throttle, commanded_steering, dt_ms);

// Stabilization pipeline — управляется traits, без if(mode==)
const float stab_w = stab_mgr_->GetStabilizationWeight();
const float mode_w = stab_mgr_->GetModeTransitionWeight();

if (traits.yaw_rate_active)
  yaw_ctrl_.Process(commanded_steering, stab_w, mode_w, dt_ms);

if (traits.pitch_comp_active)
  pitch_ctrl_.Process(commanded_throttle, stab_w);

if (traits.slip_angle_active)
  slip_ctrl_.Process(commanded_throttle, stab_w, mode_w, dt_ms);

if (traits.oversteer_guard_active)
  oversteer_guard_.Process(commanded_throttle, dt_ms, traits.oversteer_reduces_throttle);

// PWM output
if (traits.use_slew_rate) {
  UpdatePwmWithSlewRate(now, commanded_throttle, ...);
} else {
  applied_throttle = commanded_throttle;
  applied_steering = commanded_steering;
  platform_->SetPwm(applied_throttle, applied_steering);
}
```

### 5. Контроллеры становятся mode-agnostic

Убираем из контроллеров все проверки `cfg_->mode`:

```cpp
// YawRateController::Process() — до рефакторинга:
if (cfg_->mode == DriveMode::Drift) return;  // УБРАТЬ

// OversteerGuard::Process() — до рефакторинга:
if (cfg_->mode != DriveMode::Drift) {        // УБРАТЬ
  throttle *= (1.0f - reduction);
}

// После: передаём reduces_throttle через параметр от traits
void OversteerGuard::Process(float& throttle, uint32_t dt_ms,
                             bool reduce_throttle) noexcept;
```

---

## Что нужно сделать для добавления нового режима

### До рефакторинга: 7+ файлов, рассыпанная логика

### После рефакторинга: 3 точки изменения

1. Создать класс `RallyModeStrategy` в `drive_modes.hpp` (~20 строк)
2. Добавить значение в `enum DriveMode`
3. Добавить в `DriveModeRegistry::kStrategies`

Pipeline, контроллеры, PWM — не трогаем.

---

## Резюме изменений

| Компонент | Что меняется |
|-----------|-------------|
| **Новый:** `IDriveModeStrategy` + `ModeTraits` | Интерфейс стратегии, декларативное описание поведения |
| **Новый:** `DriveModeRegistry` | Статический реестр, O(1) lookup, zero-alloc |
| **Новый:** конкретные стратегии | Normal, Sport, Drift, Kids, DirectLaw |
| `YawRateController` | Убрать `if (mode == Drift)` |
| `SlipAngleController` | Убрать `if (mode != Drift)` |
| `OversteerGuard` | Убрать `if (mode != Drift)`, принимать `bool reduce_throttle` параметром |
| `VehicleControlUnified` | Заменить if/switch на `traits`-driven pipeline |
| `KidsModeProcessor` | Интегрировать через `KidsModeStrategy::PreProcess()`, убрать отдельный `current_mode_` |
| `StabilizationConfig` | `ApplyModeDefaults()` делегирует в `DriveModeRegistry::Get(mode).ApplyDefaults()` |

---

## Ограничения и гарантии

- **NVS-совместимость**: структура `StabilizationConfig` не меняется, NVS-миграция не нужна.
- **Realtime-safe**: стратегии stateless, `ModeTraits` — POD-тип, копируется на стеке. Никаких аллокаций в control loop.
- **Тесты**: каждую стратегию можно тестировать изолированно через `GetTraits()` + `ApplyDefaults()`.
