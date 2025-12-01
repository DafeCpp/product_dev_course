# Backend Workspace

Каталог `backend/` хранит общий код для микросервисов итогового проекта.

```
backend/
├── common/     # Общие компоненты (DTO, middlewares, тестовые утилиты)
├── libs/       # Шаримые библиотеки (клиенты сервисов, бэкенд SDK)
└── services/   # Конкретные микросервисы (experiment-service, auth, ...)
```

- `common/` и `libs/` пока содержат заглушки — сюда будут попадать переиспользуемые куски кода.
- `services/experiment-service` — актуальный каркас Experiment Service (aiohttp + asyncpg + testsuite), ранее расположенный в `backend-project/experiment-service`.

Новые сервисы стоит создавать внутри `backend/services/<service-name>` с той же структурой (Poetry, Dockerfile, testsuite и т.д.), чтобы единообразно разворачивать микросервисную экосистему.

