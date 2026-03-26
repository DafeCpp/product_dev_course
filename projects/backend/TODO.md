# TODO

## Архитектура и инфраструктура

### Разделение управления схемами БД от сервисов

**Проблема:** В настоящее время сервисы (auth-service, experiment-service) применяют миграции БД при старте приложения в функции `apply_migrations_on_startup`.

**Требование:** Сервисы не должны работать со схемами БД напрямую. Схемы должны применяться отдельно, например:
- При создании контейнера (init container в Kubernetes)
- Отдельным шагом в docker-compose
- Отдельным миграционным job'ом

**Задачи:**
- [x] Удалить функцию `apply_migrations_on_startup` из `auth-service/src/auth_service/main.py`
- [x] Удалить функцию `apply_migrations_on_startup` из `experiment-service/src/experiment_service/main.py`
- [x] Удалить вызов `app.on_startup.append(apply_migrations_on_startup)` из обоих сервисов
- [x] Настроить применение миграций через отдельный init container (`auth-migrate`, `experiment-migrate`)
- [x] Обновить docker-compose.yml — добавлены сервисы `auth-migrate` и `experiment-migrate`
- [ ] Обновить документацию по развертыванию

