# Frontend Workspace

Каталог `projects/frontend/` зеркалирует структуру `projects/backend/`:

```
projects/frontend/
├── common/                  # Общие хуки и типы (frontend-common)
├── apps/                    # Конкретные фронтенд-приложения
│   ├── experiment-portal/   # SPA для Experiment Tracking
│   ├── auth-proxy/          # Fastify BFF
│   └── sensor-simulator/    # SPA для генерации тестовой телеметрии
├── eslint.config.mjs        # Общий ESLint (flat config) для всех пакетов
├── tsconfig.base.json       # Общие strict-настройки TypeScript
└── tsconfig.react.json      # База для React/Vite-пакетов (extends tsconfig.base.json)
```

Новые приложения и библиотеки стоит размещать в соответствующих подпапках, чтобы поддерживать единообразную микросервисную структуру.

## Единый тулчейн

Все пакеты — участники одного npm workspace (корневой `package.json`) с одним
`package-lock.json` в корне репозитория. Отдельных lockfile в приложениях нет.

| Что | Как |
|-----|-----|
| Установка | `npm ci` в корне репозитория |
| Тесты | Vitest во всех пакетах (`npm test`, `npm run test:coverage`) |
| Типы | `tsconfig.json` пакета расширяет `tsconfig.base.json` или `tsconfig.react.json` |
| Линт | `npm run lint` в пакете; конфиг один — `eslint.config.mjs` |

Из корня можно прогнать всё сразу: `npm run type-check`, `npm test`,
`npm run test:coverage` (скрипты запускаются в каждом workspace).

Линт пока проходит чисто и входит в CI для `auth-proxy` и `sensor-simulator`.
В `experiment-portal` и `common` общий конфиг находит ошибки (в основном
правила `react-hooks` v7 и `no-explicit-any`); их чистка — отдельная задача,
после неё линт этих пакетов тоже добавляется в CI.

Docker-образы фронта собираются с контекстом в корне репозитория и ставят
зависимости своего пакета командой `npm ci --workspace=<имя пакета>`.
