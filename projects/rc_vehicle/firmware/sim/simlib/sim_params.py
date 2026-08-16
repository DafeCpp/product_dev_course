"""Параметры физ-модели машинки (SimParams).

Значения по умолчанию — порядки величин для небольшой RC-машины; точная подгонка
под реальные логи — в FW-S2.6 (validation/fitting).
"""

from dataclasses import dataclass


@dataclass
class SimParams:
    # ── Геометрия / масса ────────────────────────────────────────────────────
    mass: float = 3.0  # кг
    wheelbase_L: float = 0.30  # м (база)
    com_a: float = 0.15  # м, передняя ось → ЦМ
    com_b: float = 0.15  # м, ЦМ → задняя ось
    Iz: float = 0.05  # кг·м² (момент инерции по рысканью)

    # ── Шины (динамическая модель) ───────────────────────────────────────────
    Caf: float = 60.0  # Н/рад, жёсткость увода передней оси
    Car: float = 60.0  # Н/рад, задней

    # ── Мотор ────────────────────────────────────────────────────────────────
    max_accel: float = 8.0  # м/с² при throttle=1 (после мёртвой зоны)
    motor_tau: float = 0.15  # с, постоянная времени throttle→ускорение
    motor_deadzone: float = 0.05  # breakaway: |throttle| ниже → тяги нет
    drag_coeff: float = 0.8  # 1/с, линейное сопротивление по продольной скорости

    # ── Серво руля ───────────────────────────────────────────────────────────
    servo_max_deg: float = 25.0  # макс. угол руля при steering=±1
    servo_slew_dps: float = 600.0  # град/с, скорость отработки серво

    # ── Ориентация IMU относительно корпуса (град) ───────────────────────────
    imu_pitch_deg: float = 0.0
    imu_roll_deg: float = 0.0

    # ── Сенсоры / среда ──────────────────────────────────────────────────────
    gravity_ms2: float = 9.80665  # м/с²
    mag_field_mga: float = 500.0  # мГс, горизонтальная компонента поля Земли
    gyro_bias_dps: float = 0.0  # смещение гироскопа (опц.)

    # ── Дорожное возбуждение IMU (LOS-287) ─────────────────────────────────
    # sigma(v) = sigma_0 + sigma_per_ms * abs(v). Акселерометр на логах почти
    # декоррелирован уже через один тик телеметрии, поэтому его шум — белый.
    road_ax_sigma_0_g: float = 0.03
    road_ax_sigma_per_ms: float = 0.09
    road_ay_sigma_0_g: float = 0.045
    road_ay_sigma_per_ms: float = 0.19
    road_az_sigma_0_g: float = 0.05
    road_az_sigma_per_ms: float = 0.18

    # gx/gy в дорожных логах имеют короткую колебательную автокорреляцию.
    # Амплитуда также растёт со скоростью, частота/damping задают AR(2)-спектр.
    road_roll_rate_sigma_0_dps: float = 8.0
    road_roll_rate_sigma_per_ms: float = 15.0
    road_pitch_rate_sigma_0_dps: float = 6.0
    road_pitch_rate_sigma_per_ms: float = 12.0
    road_roll_frequency_hz: float = 10.7
    road_pitch_frequency_hz: float = 6.7
    road_attitude_damping: float = 0.28


def fitted_params_2026_07_18() -> SimParams:
    """SimParams, подогнанные под реальные заезды 2026-07-18 (FW-S2.6).

    Тонкая обёртка над реестром профилей (FW-S2.9): числа лежат в
    `simlib/data/profiles/fitted_2026_07_18.json`, отчёт —
    `reports/validation-2026-07-18.md`.
    """
    # Импорт локальный: profiles импортирует sim_params, модульный импорт здесь
    # даст цикл и уронит `import simlib`. Не поднимать наверх файла.
    from .profiles import get_profile

    return get_profile("fitted_2026_07_18")
