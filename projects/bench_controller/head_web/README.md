# Bench Head: страница отладки (Ф4, LOS-187)

Статическая страница без сборки и внешних зависимостей (ES-модуль, canvas).
Блоки: S1 узлы и шина, S2 ручная уставка, S3 live-телеметрия, S4 останов
и статусы, S5 журнал. Все команды уходят только по WebSocket `/ws` с
`cmd_id`; ответ приходит как `acks` в снимке (схема:
`docs/experiment-control/bench-command-schema.md`).

Раздача страницы из прошивки (softAP, механизм ассетов LOS-184) — точка
интеграции A-1; здесь страница проверяется целиком против mock-WS.

## Запуск против mock-WS

```bash
cd projects/bench_controller/head_web/mock_ws
python3 -m venv .venv && . .venv/bin/activate
pip install -r requirements.txt
python mock_server.py            # http://localhost:8090/
```

Сценарии (флаги `mock_server.py`):

| Флаг | Эффект |
|---|---|
| `--bus-mode monitor` (или `bring_up_master`) | шина в режиме монитора, форма S2 заблокирована |
| `--no-frames` | на шине нет кадров (индикатор в S1) |
| `--program` | идёт программа нагружения, ручной ввод заблокирован |
| `--link-loss-at N` | через N с пропадает связь: `grace` → `ramp_down` → `safe_hold` |
| `--estop-chain ok\|open\|unknown` | состояние цепи e-stop |
| `--host`, `--port` | адрес сервера |

Кнопка **stop (soft)** программная и не является аварийным остановом.

## Тесты

```bash
npm ci && npm test                       # страница (vitest, Node >= 24)
cd mock_ws && python -m pytest tests/    # модель mock-WS
```
