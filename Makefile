.PHONY: test test-backend test-frontend type-check backend-install frontend-install
.PHONY: backend-install
.PHONY: logs logs-follow logs-service logs-proxy logs-errors
.PHONY: logs-stack logs-stack-up logs-stack-down logs-stack-restart

BACKEND_DIR := projects/backend/services/experiment-service
FRONTEND_DIR := projects/frontend/apps/experiment-portal
OPENAPI_SPEC := openapi/openapi.yaml
# Python interpreter to use for Poetry virtualenv.
# Override examples:
#   make PYTHON=/path/to/python backend-install
#   make PYTHON=python3.14 backend-install
#
# IMPORTANT: do NOT resolve to an absolute path at Makefile parse time.
# Many setups (pyenv, asdf) expose Python via shims in $PATH only in interactive shells.
PYTHON ?= python3.14

test: type-check test-backend test-frontend

backend-install:
	@cd $(BACKEND_DIR) && \
		PY=""; \
		if [ -n "$(PYTHON)" ] && [ -x "$(PYTHON)" ]; then PY="$(PYTHON)"; fi; \
		if [ -z "$$PY" ] && command -v "$(PYTHON)" >/dev/null 2>&1; then PY="$(PYTHON)"; fi; \
		if [ -z "$$PY" ] && [ -x "$$HOME/.pyenv/shims/python3.14" ]; then PY="$$HOME/.pyenv/shims/python3.14"; fi; \
		if [ -z "$$PY" ] && command -v python3.14 >/dev/null 2>&1; then PY="python3.14"; fi; \
		if [ -z "$$PY" ] && command -v python3 >/dev/null 2>&1; then PY="python3"; fi; \
		if [ -z "$$PY" ] && command -v python >/dev/null 2>&1; then PY="python"; fi; \
		if [ -z "$$PY" ]; then \
			echo "❌ Не найден Python интерпретатор (пробовал: $$PYTHON, python3.14, python3, python)."; \
			echo "   Этот репозиторий требует Python 3.14+."; \
			echo "   Установите Python 3.14 (например через pyenv) и повторите, или укажите путь: make PYTHON=/path/to/python backend-install"; \
			exit 1; \
		fi; \
		if ! "$$PY" -c 'import sys; raise SystemExit(0 if sys.version_info >= (3, 14) else 1)' >/dev/null 2>&1; then \
			echo "❌ Найден Python, но версия < 3.14: $$($$PY -V 2>&1)"; \
			echo "   Этот репозиторий требует Python 3.14+."; \
			echo "   Установите Python 3.14 (например через pyenv) и повторите, или укажите путь: make PYTHON=/path/to/python3.14 backend-install"; \
			exit 1; \
		fi; \
		poetry env use "$$PY" >/dev/null && \
		poetry install --with dev

frontend-install:
	@cd $(FRONTEND_DIR) && npm ci

type-check: backend-install
	@cd $(BACKEND_DIR) && poetry run mypy src

test-backend: backend-install
	@cd $(BACKEND_DIR) && poetry run pytest

test-frontend: frontend-install
	@cd $(FRONTEND_DIR) && npm run test

.PHONY: generate-sdk
generate-sdk:
	@cd $(BACKEND_DIR) && rm -rf clients/typescript-fetch && \
		poetry run openapi-generator-cli generate \
			-i $(OPENAPI_SPEC) \
			-g typescript-fetch \
			-o clients/typescript-fetch \
			-c openapi/clients/typescript-fetch-config.yaml
	@cd $(BACKEND_DIR) && rm -rf clients/cpp-restsdk && \
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

