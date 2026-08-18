# Golden-фикстуры replay (FW-S2.2)

| Файл | Происхождение | Сценарий | Что проверяем |
|------|---------------|----------|---------------|
| `golden_synth_drive.csv` | **синтетика** (`gen_golden.py`, clean-signal профиль `fitted_2026_07_18`) | разгон + синусоида руля, 400 кадров @100 Гц | инварианты replay (нет NaN, throttle/steering ∈ [-1,1], EKF не расходится) |
| `golden_real_static_tilt.csv` | `test_runs/telemetry_log_night_test_2.csv`, строки 9300:10400; извлечено 2026-08-18 | ровный pre-roll для startup-калибровки, затем статический наклон около 20°, 12.3 с | наклон в replay-выходе и circular σ PCA `heading_deg` |
| `golden_real_failsafe_reconstructed.csv` | `test_runs/telemetry_log_auto_forward_2.csv`, строки 0:300; извлечено 2026-08-18 | неподвижная машина, источники управления отсутствуют | failsafe и нейтраль PWM |
| `golden_real_oversteer_2026_08_02.csv` | `test_runs/telemetry_log_day_02_08_26.csv`, строки 19300:19650, запись 2026-08-02, `drive_mode=2` | реальный Drift-манёвр, исходное срабатывание 209346–209721 мс | `oversteer_active` внутри размеченной фазы |
| `golden_real_straight_marker.csv` | `test_runs/telemetry_log_auto_forward_2.csv`, строки 2535:2763, `test_marker=1`; извлечено 2026-08-18 | движущаяся автоматическая прямая, 2.5 с | команда газа авто-теста и ограниченный дрейф курса |
| `golden_real_reverse_2026_07_18.csv` | `test_runs/telemetry_log_forward_back.csv`, строки 1800:2200, запись 2026-07-18 | задний ход с почти нулевым рулевым вводом | отсутствие внесённого стабилизатором руля |

Синтетическая фикстура использует динамику, откалиброванную по реальным заездам
LOS-32, но не содержит сами логи или дорожный шум. Поэтому она детерминирована,
самодостаточна и сохраняет clean-signal контракт replay из LOS-287.
Формат CSV совпадает с прошивкой (`telemetry_log_*.csv`), поэтому тот же
загрузчик (`simlib.load_telemetry_csv`) работает и на реальных поездках.

Вырезки воспроизводятся скриптом `extract_real_golden.py`; полные исходные логи
остаются локальными и исключены из Git. Колонка `replay_phase` отделяет контекст
от участка сценарного ассерта.
Перед `sim_host` срезы ≈100 Гц интерполируются на равномерные тики
2 мс: прошивка получает примерно пять `HostStep()` на каждую строку
телеметрии, как в production control loop 500 Гц. Для сверки с
`replay_phase` берётся ближайший 2-мс output к времени исходной строки.

Во всех реальных источниках IMU установлен перевёрнуто: покой даёт `az≈−1 g`,
а ось X совпадает с направлением движения. Replay поэтому загружает сохранённый
vehicle frame `gravity=(0,0,-1)`, `forward=(1,0,0)` с нулевыми bias (логи уже
bias-корректированы). `RotateToVehicleFrame()` приводит accel и gyro к
каноническому кадру с `az≈+1 g`; identity-калибровка используется только старой
синтетической fixture.

Исходные логи не содержат `rc_present`/`wifi_present`, поэтому failsafe-кейс
использует реальные сенсоры неподвижного эпизода, но состояние отсутствующих
источников управления реконструировано колонками `rc_present=0` и
`wifi_present=0`. Для oversteer из записанного config snapshot воспроизводятся
`warn_enabled=true`, пороги 10°/30°·с⁻¹ и снижение газа 0.7.
Для straight replay по записанному `test_marker=1` восстанавливается auto-test
`straight` с target acceleration 0.1 g и duration 3 с, а также WebSocket keepalive
без ручной Wi-Fi команды. Эти дополнения явно относятся к replay и не
выдаются за поля исходной телеметрии.

Для tilt-эпизода saved magnetometer calibration восстановлена из
планарного поворота в том же `telemetry_log_night_test_2.csv`, строки
9800:11800, тем же hard-iron min/max + PCA-алгоритмом, что и
`MagCalibration::Finish()`. Планарность выборки λmin/λmid=0.044;
offset=(335.907, 56.915, 447.906) мГс, field strength=208.505 мГс. Replay
передаёт offset, normal и оба basis-вектора в `sim_host` до `Init()`,
поэтому `heading_deg` идёт через `ComputePcaHeadingDeg()`, а не fallback
`atan2(my, mx)`.
Фикстура также хранит исходный `dt_ms` первого кадра, чтобы
холодный replay не терял фактический интервал на границе вырезки.

Решение о хранении полных логов и Git LFS остаётся в FW-S2.7.
