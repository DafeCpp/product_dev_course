.PHONY: test test-backend test-frontend type-check backend-install frontend-install
.PHONY: backend-install
.PHONY: logs logs-follow logs-service logs-proxy logs-errors
.PHONY: logs-stack logs-stack-up logs-stack-down logs-stack-restart
.PHONY: dev dev-up dev-down dev-restart

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
NODE ?= node
FRONTEND_NODE_IMAGE ?= node:24-alpine

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
	@cd $(FRONTEND_DIR) && \
		if ! command -v "$(NODE)" >/dev/null 2>&1; then \
			if command -v docker >/dev/null 2>&1; then \
				echo "⚠️  Не найден Node.js локально — запускаю npm ci в Docker ($(FRONTEND_NODE_IMAGE))."; \
				docker run --rm -v "$$(pwd)":/repo -w /repo/$(FRONTEND_DIR) $(FRONTEND_NODE_IMAGE) sh -lc "npm ci --no-audit --no-fund --loglevel=error"; \
				exit 0; \
			fi; \
			echo "❌ Не найден Node.js (пробовал: $(NODE))."; \
			echo "   Фронтенд требует Node.js 24 (LTS) или новее."; \
			echo "   Установите Node LTS и повторите, или укажите бинарник: make NODE=/path/to/node test-frontend"; \
			echo "   Альтернатива: установите Docker и используйте 'make test-frontend-docker'."; \
			exit 1; \
		fi; \
		if ! "$(NODE)" -e 'const [maj]=process.versions.node.split(\".\"); process.exit(Number(maj) >= 24 ? 0 : 1)' >/dev/null 2>&1; then \
			if command -v docker >/dev/null 2>&1; then \
				echo "⚠️  Node.js слишком старый: $$($(NODE) -v 2>&1) — запускаю npm ci в Docker ($(FRONTEND_NODE_IMAGE))."; \
				docker run --rm -v "$$(pwd)":/repo -w /repo/$(FRONTEND_DIR) $(FRONTEND_NODE_IMAGE) sh -lc "npm ci --no-audit --no-fund --loglevel=error"; \
				exit 0; \
			fi; \
			echo "❌ Node.js слишком старый: $$($(NODE) -v 2>&1)"; \
			echo "   Фронтенд требует Node.js 24 (LTS) или новее."; \
			echo "   Подсказка: если используете nvm — выполните: nvm install --lts && nvm use --lts"; \
			echo "   Альтернатива: установите Docker и используйте 'make test-frontend-docker'."; \
			exit 1; \
		fi; \
		npm ci --no-audit --no-fund --loglevel=error

type-check: backend-install
	@cd $(BACKEND_DIR) && poetry run mypy src

test-backend: backend-install
	@cd $(BACKEND_DIR) && poetry run pytest

test-frontend: frontend-install
	@cd $(FRONTEND_DIR) && \
		if command -v "$(NODE)" >/dev/null 2>&1 && "$(NODE)" -e 'const [maj]=process.versions.node.split(\".\"); process.exit(Number(maj) >= 24 ? 0 : 1)' >/dev/null 2>&1; then \
			npm run test; \
			exit 0; \
		fi; \
		if command -v docker >/dev/null 2>&1; then \
			echo "⚠️  Запускаю frontend тесты в Docker ($(FRONTEND_NODE_IMAGE))."; \
			docker run --rm -v "$$(pwd)":/repo -w /repo/$(FRONTEND_DIR) $(FRONTEND_NODE_IMAGE) sh -lc "npm ci --no-audit --no-fund --loglevel=error && npm run test"; \
			exit 0; \
		fi; \
		echo "❌ Нужен Node.js 24+ или Docker для запуска frontend тестов."; \
		exit 1

.PHONY: test-frontend-docker
test-frontend-docker:
	@docker run --rm -v "$$(pwd)":/repo -w /repo/$(FRONTEND_DIR) $(FRONTEND_NODE_IMAGE) sh -lc "npm ci --no-audit --no-fund --loglevel=error && npm run test"

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

# ============================================
# Локальная отладка (Frontend + Backend + Auth Service + Auth Proxy + Grafana)
# ============================================

# Запуск фронтенда, бэкенда, auth-service, auth-proxy и Grafana для локальной отладки
dev-up:
	@echo "Запуск фронтенда, бэкенда, auth-service, auth-proxy и Grafana для локальной отладки..."
	@if [ ! -f docker-compose.override.yml ]; then \
		echo "⚠️  Файл docker-compose.override.yml не найден. Создаю из примера..."; \
		cp docker-compose.override.yml.example docker-compose.override.yml 2>/dev/null || true; \
	fi
	@if [ ! -f .env ]; then \
		echo "⚠️  Файл .env не найден. Создаю из примера..."; \
		cp env.docker.example .env 2>/dev/null || true; \
	fi
	@if [ -f docker-compose.override.yml ]; then \
		docker-compose up -d postgres auth-service experiment-service auth-proxy experiment-portal loki grafana; \
	else \
		docker-compose -f docker-compose.yml -f docker-compose.logging.yml up -d postgres auth-service experiment-service auth-proxy experiment-portal loki grafana; \
	fi
	@echo ""
	@echo "✅ Сервисы запущены!"
	@echo "🌐 Фронтенд доступен на http://localhost:3000"
	@echo "🔧 Бэкенд API доступен на http://localhost:8002"
	@echo "🔐 Auth Proxy доступен на http://localhost:8080"
	@echo "🔑 Auth Service доступен на http://localhost:8001"
	@echo ""
	@echo "👤 Пользователь по умолчанию:"
	@echo "   Username: admin"
	@echo "   Password: admin123"
	@echo "   ⚠️  Требуется смена пароля при первом входе!"
	@echo ""
	@echo "💡 Для регистрации нового пользователя используйте:"
	@echo "   curl -X POST http://localhost:8001/auth/register \\"
	@echo "     -H 'Content-Type: application/json' \\"
	@echo "     -d '{\"username\":\"testuser\",\"email\":\"test@example.com\",\"password\":\"testpass123\"}'"
	@echo "📊 Grafana доступна на http://localhost:3001"
	@echo ""
	@echo ""
	@echo "👤 Grafana логин: admin"
	@echo "🔑 Grafana пароль: admin (или значение из GRAFANA_ADMIN_PASSWORD в .env)"
	@echo ""
	@echo "Для просмотра логов: make logs-service, make logs-portal или make logs-stack"

# Остановка фронтенда, бэкенда, auth-service, auth-proxy и Grafana
dev-down:
	@echo "Остановка фронтенда, бэкенда, auth-service, auth-proxy и Grafana..."
	@if [ -f docker-compose.override.yml ]; then \
		docker-compose stop postgres auth-service experiment-service auth-proxy experiment-portal loki grafana; \
	else \
		docker-compose -f docker-compose.yml -f docker-compose.logging.yml stop postgres auth-service experiment-service auth-proxy experiment-portal loki grafana; \
	fi
	@echo "✅ Сервисы остановлены"

# Перезапуск фронтенда, бэкенда, auth-service, auth-proxy и Grafana
dev-restart: dev-down dev-up

# Алиас для запуска
dev: dev-up

