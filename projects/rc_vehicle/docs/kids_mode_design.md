# Kids Mode (Детский режим) — Техническое задание

## 1. Обзор

**Kids Mode** — специальный режим управления RC-машиной, предназначенный для безопасной передачи управления ребёнку. Режим ограничивает максимальную скорость ("душит газ") и включает максимальную помощь в управлении для предотвращения потери контроля.

### Ключевые решения
- **Источник управления**: RC-пульт и Wi-Fi (ограничения применяются к обоим источникам)
- **Переключение режима**: только через WebSocket API (смартфон/веб-интерфейс)
- **Возрастные пресеты**: да (toddler, child, preteen)

### Цели
- **Безопасность**: ограничение максимальной скорости машины
- **Простота управления**: максимальная стабилизация и помощь
- **Плавность**: мягкие реакции на резкие движения стиков
- **Защита от заноса**: агрессивное предотвращение потери контроля

## 2. Функциональные требования

### 2.1 Ограничение газа (Throttle Limiting)

| Параметр | Описание | Диапазон | По умолчанию |
|----------|----------|----------|--------------|
| `throttle_limit` | Максимальный газ [0..1] | 0.1 – 1.0 | 0.3 (30%) |
| `reverse_limit` | Максимальный задний ход [0..1] | 0.1 – 1.0 | 0.2 (20%) |

**Логика**:
```
throttle_out = clamp(throttle_in * throttle_limit, -reverse_limit, throttle_limit)
```

### 2.2 Ограничение руля (Steering Limiting)

| Параметр | Описание | Диапазон | По умолчанию |
|----------|----------|----------|--------------|
| `steering_limit` | Максимальный угол поворота [0..1] | 0.3 – 1.0 | 0.7 (70%) |

**Логика**:
```
steering_out = clamp(steering_in, -steering_limit, steering_limit)
```

### 2.3 Усиленный Slew Rate (Плавность)

| Параметр | Описание | Диапазон | По умолчанию |
|----------|----------|----------|--------------|
| `slew_throttle` | Скорость изменения газа [/сек] | 0.1 – 2.0 | 0.3 |
| `slew_steering` | Скорость изменения руля [/сек] | 0.2 – 3.0 | 0.5 |

Сравнение с обычным режимом:
- Normal: throttle 0.5/s, steering 1.0/s
- Kids: throttle 0.3/s, steering 0.5/s

### 2.4 Усиленная стабилизация

В Kids Mode автоматически включаются и усиливаются:

| Функция | Поведение в Kids Mode |
|---------|----------------------|
| Yaw Rate Control | Включён, kp увеличен на 50% |
| Pitch Compensation | Включена |
| Oversteer Guard | Включён, throttle_reduction = 0.5 (50%) |
| Adaptive PID | Включён |

### 2.5 Защита от резких манёвров

| Параметр | Описание | По умолчанию |
|----------|----------|--------------|
| `anti_spin_enabled` | Автоматическое снижение газа при заносе | true |
| `anti_spin_threshold_deg` | Порог угла заноса для срабатывания | 10° |
| `anti_spin_reduction` | Снижение газа при срабатывании | 0.7 (70%) |

## 3. Архитектура

### 3.1 Расширение DriveMode

```cpp
enum class DriveMode : uint8_t {
  Normal = 0,  // Базовый контроль рыскания
  Sport = 1,   // Агрессивные параметры
  Drift = 2,   // Управление дрифтом
  Kids = 3     // Детский режим (NEW)
};
```

### 3.2 Структура KidsModeConfig

```cpp
/**
 * @brief Конфигурация детского режима
 */
struct KidsModeConfig {
  /** Максимальный газ вперёд [0.1..1.0] */
  float throttle_limit{0.3f};

  /** Максимальный задний ход [0.1..1.0] */
  float reverse_limit{0.2f};

  /** Максимальный угол поворота [0.3..1.0] */
  float steering_limit{0.7f};

  /** Скорость изменения газа [/сек] */
  float slew_throttle{0.3f};

  /** Скорость изменения руля [/сек] */
  float slew_steering{0.5f};

  /** Включить защиту от заноса */
  bool anti_spin_enabled{true};

  /** Порог угла заноса для anti-spin [градусы] */
  float anti_spin_threshold_deg{10.0f};

  /** Снижение газа при anti-spin [0..1] */
  float anti_spin_reduction{0.7f};

  [[nodiscard]] bool IsValid() const noexcept;
  void Clamp() noexcept;
};
```

### 3.3 Интеграция в StabilizationConfig

```cpp
struct StabilizationConfig {
  // ... existing fields ...

  /** Конфигурация детского режима */
  KidsModeConfig kids_mode;

  // ... rest of struct ...
};
```

### 3.4 Enum для возрастных пресетов

```cpp
/**
 * @brief Возрастные пресеты для Kids Mode
 */
enum class KidsPreset : uint8_t {
  Custom = 0,   // Пользовательские настройки
  Toddler = 1,  // 3-5 лет: очень медленно, максимальная помощь
  Child = 2,    // 6-9 лет: умеренно, хорошая помощь
  Preteen = 3   // 10-12 лет: быстрее, базовая помощь
};
```

### 3.5 Новый компонент: KidsModeProcessor

```cpp
/**
 * @brief Процессор детского режима
 *
 * Применяет ограничения газа, руля и slew rate,
 * а также усиленную защиту от заноса.
 */
class KidsModeProcessor {
 public:
  void Init(const StabilizationConfig& cfg, const VehicleEkf& ekf,
            const ImuHandler* imu);

  /**
   * @brief Применить ограничения Kids Mode
   * @param throttle Команда газа [in/out]
   * @param steering Команда руля [in/out]
   * @param dt_ms Шаг времени
   */
  void Process(float& throttle, float& steering, uint32_t dt_ms) noexcept;

  /** @brief Проверить, активен ли Kids Mode */
  [[nodiscard]] bool IsActive() const noexcept;

  /** @brief Проверить, сработала ли защита anti-spin */
  [[nodiscard]] bool IsAntiSpinActive() const noexcept;

  void Reset() noexcept;

 private:
  const StabilizationConfig* cfg_{nullptr};
  const VehicleEkf* ekf_{nullptr};
  const ImuHandler* imu_{nullptr};

  float smoothed_throttle_{0.0f};
  float smoothed_steering_{0.0f};
  bool anti_spin_active_{false};
};
```

## 4. Поток данных

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         Control Loop (500 Hz)                           │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  RC Input ─────┐                                                        │
│                ├──► SelectControlSource (RC > WiFi priority)            │
│  WiFi Input ───┘                                                        │
│       │                                                                 │
│       ▼                                                                 │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │ KidsModeProcessor (if mode == Kids)                              │   │
│  │  ├─ Apply throttle_limit / reverse_limit to ANY source           │   │
│  │  ├─ Apply steering_limit                                         │   │
│  │  ├─ Apply enhanced slew rate                                     │   │
│  │  └─ Anti-spin protection (reduce throttle if slip > threshold)   │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│       │                                                                 │
│       ▼                                                                 │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │ YawRateController (enhanced gains in Kids mode)                  │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│       │                                                                 │
│       ▼                                                                 │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │ PitchCompensator (always enabled in Kids mode)                   │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│       │                                                                 │
│       ▼                                                                 │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │ OversteerGuard (aggressive settings in Kids mode)                │   │
│  └─────────────────────────────────────────────────────────────────┘   │
│       │                                                                 │
│       ▼                                                                 │
│  PWM Output                                                             │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

## 5. WebSocket API

### 5.1 Получение конфигурации Kids Mode

**Запрос:**
```json
{
  "type": "get_stab_config"
}
```

**Ответ (расширенный):**
```json
{
  "type": "stab_config",
  "enabled": true,
  "mode": 3,
  "mode_name": "kids",
  "kids_mode": {
    "throttle_limit": 0.3,
    "reverse_limit": 0.2,
    "steering_limit": 0.7,
    "slew_throttle": 0.3,
    "slew_steering": 0.5,
    "anti_spin_enabled": true,
    "anti_spin_threshold_deg": 10.0,
    "anti_spin_reduction": 0.7
  }
}
```

### 5.2 Установка Kids Mode

**Запрос:**
```json
{
  "type": "set_stab_config",
  "mode": 3,
  "kids_mode": {
    "throttle_limit": 0.25,
    "reverse_limit": 0.15
  }
}
```

**Ответ:**
```json
{
  "type": "set_stab_config_ack",
  "ok": true,
  "mode": 3,
  "mode_name": "kids",
  "kids_mode": {
    "throttle_limit": 0.25,
    "reverse_limit": 0.15,
    "steering_limit": 0.7,
    "slew_throttle": 0.3,
    "slew_steering": 0.5,
    "anti_spin_enabled": true,
    "anti_spin_threshold_deg": 10.0,
    "anti_spin_reduction": 0.7
  }
}
```

### 5.3 Быстрое включение Kids Mode

**Запрос:**
```json
{
  "type": "set_kids_mode",
  "enabled": true,
  "throttle_limit": 0.3
}
```

**Ответ:**
```json
{
  "type": "set_kids_mode_ack",
  "ok": true,
  "enabled": true,
  "throttle_limit": 0.3
}
```

## 6. Телеметрия

### 6.1 Расширение телеметрии

```json
{
  "type": "telem",
  "kids_mode": {
    "active": true,
    "throttle_limited": true,
    "anti_spin_active": false,
    "effective_throttle_limit": 0.3
  }
}
```

## 7. Предустановки (Presets)

### 7.1 Возрастные предустановки

| Preset | Возраст | throttle_limit | reverse_limit | steering_limit | slew_throttle | slew_steering | anti_spin_threshold |
|--------|---------|----------------|---------------|----------------|---------------|---------------|---------------------|
| `toddler` | 3-5 лет | 0.15 | 0.10 | 0.5 | 0.2 | 0.3 | 5° |
| `child` | 6-9 лет | 0.30 | 0.20 | 0.7 | 0.3 | 0.5 | 10° |
| `preteen` | 10-12 лет | 0.50 | 0.35 | 0.85 | 0.4 | 0.7 | 15° |

### 7.2 Реализация пресетов

```cpp
void KidsModeConfig::ApplyPreset(KidsPreset preset) noexcept {
  switch (preset) {
    case KidsPreset::Toddler:
      throttle_limit = 0.15f;
      reverse_limit = 0.10f;
      steering_limit = 0.5f;
      slew_throttle = 0.2f;
      slew_steering = 0.3f;
      anti_spin_threshold_deg = 5.0f;
      anti_spin_reduction = 0.8f;
      break;
    case KidsPreset::Child:
      throttle_limit = 0.30f;
      reverse_limit = 0.20f;
      steering_limit = 0.7f;
      slew_throttle = 0.3f;
      slew_steering = 0.5f;
      anti_spin_threshold_deg = 10.0f;
      anti_spin_reduction = 0.7f;
      break;
    case KidsPreset::Preteen:
      throttle_limit = 0.50f;
      reverse_limit = 0.35f;
      steering_limit = 0.85f;
      slew_throttle = 0.4f;
      slew_steering = 0.7f;
      anti_spin_threshold_deg = 15.0f;
      anti_spin_reduction = 0.5f;
      break;
    default:
      break;  // Custom — не меняем
  }
}
```

### 7.3 API для предустановок

**Запрос:**
```json
{
  "type": "set_kids_preset",
  "preset": "child"
}
```

**Ответ:**
```json
{
  "type": "set_kids_preset_ack",
  "ok": true,
  "preset": "child",
  "kids_mode": {
    "throttle_limit": 0.30,
    "reverse_limit": 0.20,
    "steering_limit": 0.70,
    "slew_throttle": 0.30,
    "slew_steering": 0.50,
    "anti_spin_enabled": true,
    "anti_spin_threshold_deg": 10.0,
    "anti_spin_reduction": 0.70
  }
}
```

### 7.4 Получение списка пресетов

**Запрос:**
```json
{
  "type": "get_kids_presets"
}
```

**Ответ:**
```json
{
  "type": "kids_presets",
  "presets": [
    {"id": "toddler", "name": "Малыш (3-5 лет)", "throttle_limit": 0.15},
    {"id": "child", "name": "Ребёнок (6-9 лет)", "throttle_limit": 0.30},
    {"id": "preteen", "name": "Подросток (10-12 лет)", "throttle_limit": 0.50}
  ],
  "current": "child"
}
```

## 8. Особенности работы с источниками управления

### 8.1 Поддержка RC и Wi-Fi

В Kids Mode ограничения применяются к **любому источнику управления** (RC или Wi-Fi). Приоритет источников сохраняется стандартным: RC > Wi-Fi.

```cpp
// В ControlTaskLoop после SelectControlSource:
if (cfg.mode == DriveMode::Kids) {
  // Применить ограничения Kids Mode к команде от ЛЮБОГО источника
  kids_processor_.Process(commanded_throttle, commanded_steering, dt_ms);
}
```

**Преимущества:**
- Родитель может управлять с телефона в Kids Mode (ограниченная скорость)
- Ребёнок может управлять с RC-пульта (ограниченная скорость)
- Переключение между источниками не снимает ограничения

### 8.2 Failsafe в Kids Mode

- Failsafe работает стандартно (250 мс без активного источника → нейтраль)
- При потере RC-сигнала Wi-Fi становится резервным источником (как обычно)
- Ограничения Kids Mode применяются к любому активному источнику

## 9. Изменения в файлах

### 9.1 Новые файлы

| Файл | Описание |
|------|----------|
| `firmware/common/kids_mode_processor.hpp` | Заголовок KidsModeProcessor |
| `firmware/common/kids_mode_processor.cpp` | Реализация KidsModeProcessor |
| `docs/kids_mode_design.md` | Этот документ |

### 9.2 Модифицируемые файлы

| Файл | Изменения |
|------|-----------|
| `firmware/common/stabilization_config.hpp` | Добавить DriveMode::Kids, KidsModeConfig |
| `firmware/common/stabilization_config.cpp` | Реализация KidsModeConfig::IsValid/Clamp |
| `firmware/common/vehicle_control_unified.hpp` | Добавить KidsModeProcessor |
| `firmware/common/vehicle_control_unified.cpp` | Интеграция KidsModeProcessor в control loop |
| `firmware/esp32_s3/main/ws_command_handlers.cpp` | Обработка kids_mode в JSON |
| `firmware/esp32_s3/main/stabilization_config_json.hpp` | JSON сериализация kids_mode |
| `firmware/esp32_s3/main/stabilization_config_json.cpp` | JSON сериализация kids_mode |
| `firmware/esp32_common/stabilization_config_nvs.hpp` | NVS хранение kids_mode |
| `firmware/common/control_components.cpp` | Телеметрия kids_mode |
| `docs/stabilization/README.md` | Документация Kids Mode |

## 10. Тестирование

### 10.1 Unit Tests

- [ ] `KidsModeConfig::IsValid()` — валидация параметров
- [ ] `KidsModeConfig::Clamp()` — ограничение параметров
- [ ] `KidsModeProcessor::Process()` — применение лимитов
- [ ] Anti-spin логика при различных углах заноса

### 10.2 Integration Tests

- [ ] Переключение Normal → Kids → Normal
- [ ] Сохранение/загрузка конфигурации из NVS
- [ ] WebSocket API: get/set kids_mode
- [ ] Телеметрия в Kids Mode

### 10.3 Manual Tests

- [ ] Реальное управление с ограниченным газом
- [ ] Проверка плавности (slew rate)
- [ ] Срабатывание anti-spin на скользкой поверхности
- [ ] Переключение режимов во время движения

## 11. Безопасность

### 11.1 Failsafe в Kids Mode

- При потере связи (RC/WiFi) — стандартный failsafe (газ=0, руль=центр)
- Kids Mode не влияет на failsafe timeout (250 мс)

### 11.2 Ограничения

- Kids Mode нельзя отключить через RC-пульт (только через WebSocket)
- При переключении из Kids Mode в другой режим — плавный переход (fade)

## 12. Roadmap

### Phase 1: MVP (1-2 дня)
- [x] Дизайн и документация
- [ ] KidsModeConfig структура + KidsPreset enum
- [ ] DriveMode::Kids
- [ ] Базовое ограничение throttle/steering
- [ ] WebSocket API (set_stab_config с mode=3)

### Phase 2: Стабилизация и RC-only (1 день)
- [ ] KidsModeProcessor
- [ ] Интеграция в control loop
- [ ] Логика "только RC" для Kids Mode
- [ ] Усиленные параметры стабилизации

### Phase 3: Anti-spin и Presets (1 день)
- [ ] Anti-spin логика
- [ ] Возрастные пресеты (toddler/child/preteen)
- [ ] WebSocket API для пресетов
- [ ] Телеметрия anti-spin

### Phase 4: Тестирование (0.5 дня)
- [ ] Unit tests
- [ ] Integration tests
- [ ] Manual testing с реальным RC

---

**Версия документа**: 1.0
**Дата создания**: 2026-03-05
**Автор**: AI Assistant
**Статус**: Draft