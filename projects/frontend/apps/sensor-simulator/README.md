# Sensor Simulator (Web)

Небольшое отдельное SPA-приложение для симуляции поведения датчиков и отправки телеметрии в `telemetry-ingest-service`.

## Запуск в Docker Compose

Сервис добавлен в корневой `docker-compose.yml` как `sensor-simulator`.

- UI: `http://localhost:8082`
- Внутри контейнера nginx проксирует:
  - `/telemetry/*` → `telemetry-ingest-service:8003/*` (same-origin, без CORS)

## Dev-режим (локально, без Docker)

```bash
cd projects/frontend/apps/sensor-simulator
npm ci
npm run dev -- --host 0.0.0.0 --port 3006
```

В dev-режиме по умолчанию запросы идут на `/telemetry/...`, поэтому удобнее запускать через Docker (nginx-прокси).

