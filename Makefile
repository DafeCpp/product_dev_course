.PHONY: test test-backend test-frontend type-check
.PHONY: logs logs-follow logs-service logs-proxy logs-errors
.PHONY: logs-stack logs-stack-up logs-stack-down logs-stack-restart

BACKEND_DIR := backend/services/experiment-service
FRONTEND_DIR := frontend/apps/experiment-portal
OPENAPI_SPEC := openapi/openapi.yaml

test: type-check test-backend test-frontend

type-check:
	cd $(BACKEND_DIR) && poetry run mypy src

test-backend:
	cd $(BACKEND_DIR) && poetry run pytest

test-frontend:
	cd $(FRONTEND_DIR) && npm run test

.PHONY: generate-sdk
generate-sdk:
	cd $(BACKEND_DIR) && rm -rf clients/typescript-fetch && \
		poetry run openapi-generator-cli generate \
			-i $(OPENAPI_SPEC) \
			-g typescript-fetch \
			-o clients/typescript-fetch \
			-c openapi/clients/typescript-fetch-config.yaml
	cd $(BACKEND_DIR) && rm -rf clients/cpp-restsdk && \
		poetry run openapi-generator-cli generate \
			-i $(OPENAPI_SPEC) \
			-g cpp-restsdk \
			-o clients/cpp-restsdk \
			-c openapi/clients/cpp-restsdk-config.yaml

# ============================================
# Логирование
# ============================================

# Просмотр всех логов в реальном времени
logs:
	@echo "Просмотр логов всех сервисов (Ctrl+C для выхода)"
	docker-compose logs -f --tail=50

# Просмотр логов конкретного сервиса
logs-service:
	docker-compose logs -f --tail=100 experiment-service

logs-proxy:
	docker-compose logs -f --tail=100 auth-proxy

logs-portal:
	docker-compose logs -f --tail=100 experiment-portal

logs-postgres:
	docker-compose logs -f --tail=100 postgres

# Просмотр только ошибок
logs-errors:
	docker-compose logs --tail=200 | grep -i "error\|fatal\|exception" --color=always

# ============================================
# Grafana Stack (Loki + Promtail + Grafana)
# ============================================

# Запуск стека логирования
logs-stack-up:
	@echo "Запуск стека логирования (Loki + Promtail + Grafana)..."
	docker-compose -f docker-compose.yml -f docker-compose.logging.yml up -d loki promtail grafana
	@echo ""
	@echo "✅ Стек логирования запущен!"
	@echo "📊 Grafana доступна на http://localhost:3001"
	@echo "👤 Логин: admin"
	@echo "🔑 Пароль: admin (или значение из GRAFANA_ADMIN_PASSWORD в .env)"
	@echo ""
	@echo "Для просмотра логов:"
	@echo "  1. Откройте http://localhost:3001"
	@echo "  2. Перейдите в Explore (иконка компаса)"
	@echo "  3. Выберите datasource 'Loki'"
	@echo "  4. Используйте запрос: {service=\"experiment-service\"} или {container=~\"experiment-.*\"}"

# Остановка стека логирования
logs-stack-down:
	@echo "Остановка стека логирования..."
	docker-compose -f docker-compose.yml -f docker-compose.logging.yml stop loki promtail grafana

# Перезапуск стека логирования
logs-stack-restart: logs-stack-down logs-stack-up

# Алиас для запуска
logs-stack: logs-stack-up

