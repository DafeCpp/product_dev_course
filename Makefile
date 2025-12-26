.PHONY: test test-backend test-frontend type-check backend-install frontend-install
.PHONY: backend-install
.PHONY: logs logs-follow logs-service logs-proxy logs-auth-service logs-errors
.PHONY: logs-stack logs-stack-up logs-stack-down logs-stack-restart
.PHONY: dev dev-up dev-down dev-restart dev-logs dev-fix dev-clean grafana-reset-password

BACKEND_SERVICES_DIR := projects/backend/services
FRONTEND_DIR := projects/frontend/apps/experiment-portal
OPENAPI_SPEC := openapi/openapi.yaml
# Find all backend services (directories with pyproject.toml)
BACKEND_SERVICES := $(shell find $(BACKEND_SERVICES_DIR) -maxdepth 2 -name "pyproject.toml" -type f | sed 's|/pyproject.toml||' | sort)
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
	@if [ -z "$(BACKEND_SERVICES)" ]; then \
		echo "⚠️  Не найдено ни одного backend сервиса в $(BACKEND_SERVICES_DIR)"; \
		exit 1; \
	fi; \
	for service in $(BACKEND_SERVICES); do \
		echo "📦 Installing dependencies for $$(basename $$service)..."; \
		cd $$service && \
			PY=""; \
			if [ -n "$(PYTHON)" ] && [ -x "$(PYTHON)" ]; then PY="$(PYTHON)"; fi; \
			if [ -z "$$PY" ] && command -v "$(PYTHON)" >/dev/null 2>&1; then PY="$(PYTHON)"; fi; \
			if [ -z "$$PY" ] && [ -x "$$HOME/.pyenv/shims/python3.14" ]; then PY="$$HOME/.pyenv/shims/python3.14"; fi; \
			if [ -z "$$PY" ] && command -v python3.14 >/dev/null 2>&1; then PY="python3.14"; fi; \
			if [ -z "$$PY" ] && command -v python3 >/dev/null 2>&1; then PY="python3"; fi; \
			if [ -z "$$PY" ] && command -v python >/dev/null 2>&1; then PY="python"; fi; \
			if [ -z "$$PY" ]; then \
				echo "❌ Не найден Python интерпретатор (пробовал: $(PYTHON), python3.14, python3, python)."; \
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
			poetry install --with dev || exit 1; \
		cd - > /dev/null; \
	done

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
	@if [ -z "$(BACKEND_SERVICES)" ]; then \
		echo "⚠️  Не найдено ни одного backend сервиса в $(BACKEND_SERVICES_DIR)"; \
		exit 1; \
	fi; \
	failed=0; \
	for service in $(BACKEND_SERVICES); do \
		echo "🔍 Type checking $$(basename $$service)..."; \
		(cd $$service && poetry run mypy src) || failed=1; \
	done; \
	exit $$failed

test-backend: backend-install
	@if [ -z "$(BACKEND_SERVICES)" ]; then \
		echo "⚠️  Не найдено ни одного backend сервиса в $(BACKEND_SERVICES_DIR)"; \
		exit 1; \
	fi; \
	failed=0; \
	for service in $(BACKEND_SERVICES); do \
		echo "🧪 Running tests for $$(basename $$service)..."; \
		(cd $$service && poetry run pytest) || failed=1; \
	done; \
	exit $$failed

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

logs-auth-service:
	docker-compose logs -f --tail=100 auth-service

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
	cd infrastructure/logging && docker-compose -f docker-compose.yml up -d loki promtail grafana
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
	cd infrastructure/logging && docker-compose -f docker-compose.yml stop loki promtail grafana

# Перезапуск стека логирования
logs-stack-restart: logs-stack-down logs-stack-up

# Алиас для запуска
logs-stack: logs-stack-up

# Сброс пароля администратора Grafana
grafana-reset-password:
	@echo "Сброс пароля администратора Grafana..."
	@echo "Используется пароль из переменной GRAFANA_ADMIN_PASSWORD (по умолчанию: admin)"
	cd infrastructure/logging && docker-compose -f docker-compose.yml exec -T grafana grafana cli admin reset-admin-password "$${GRAFANA_ADMIN_PASSWORD:-admin}" 2>&1 | grep -E "(Admin password|successfully|error)" || true
	@echo ""
	@echo "✅ Пароль администратора Grafana сброшен!"
	@echo "👤 Логин: admin"
	@echo "🔑 Пароль: $${GRAFANA_ADMIN_PASSWORD:-admin}"

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
	docker-compose up -d postgres auth-service experiment-service auth-proxy experiment-portal loki promtail grafana
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
	@echo "Для просмотра логов всех dev-сервисов: make dev-logs"
	@echo "Для просмотра логов конкретного сервиса: make logs-service, make logs-proxy, make logs-auth-service, make logs-portal, make logs-postgres"
	@echo "Для просмотра логов через Grafana: make logs-stack"
	@echo ""
	@echo "⚠️  Если возникла ошибка 'ContainerConfig', выполните: make dev-fix"
	@echo "⚠️  Если не получается войти в Grafana, выполните: make grafana-reset-password"

# Остановка фронтенда, бэкенда, auth-service, auth-proxy и Grafana
dev-down:
	@echo "Остановка фронтенда, бэкенда, auth-service, auth-proxy и Grafana..."
	docker-compose stop postgres auth-service experiment-service auth-proxy experiment-portal loki promtail grafana
	@echo "✅ Сервисы остановлены"

# Перезапуск фронтенда, бэкенда, auth-service, auth-proxy и Grafana
dev-restart: dev-down dev-up

# Просмотр логов всех dev-сервисов
dev-logs:
	@echo "Просмотр логов всех dev-сервисов (Ctrl+C для выхода)"
	docker-compose logs -f --tail=50 postgres auth-service experiment-service auth-proxy experiment-portal loki promtail grafana

# Исправление ошибки ContainerConfig (удаление проблемных контейнеров и пересоздание)
dev-fix:
	@echo "Исправление ошибки ContainerConfig..."
	@echo "Остановка всех dev-сервисов..."
	docker-compose stop postgres auth-service experiment-service auth-proxy experiment-portal loki promtail grafana 2>/dev/null || true
	@echo "Удаление проблемных контейнеров..."
	@docker ps -a --filter "name=experiment-service" --format "{{.ID}}" | xargs -r docker rm -f 2>/dev/null || true
	@docker ps -a --filter "name=auth-service" --format "{{.ID}}" | xargs -r docker rm -f 2>/dev/null || true
	@docker ps -a --filter "name=auth-proxy" --format "{{.ID}}" | xargs -r docker rm -f 2>/dev/null || true
	@docker ps -a --filter "name=experiment-portal" --format "{{.ID}}" | xargs -r docker rm -f 2>/dev/null || true
	@docker ps -a --filter "name=grafana" --format "{{.ID}}" | xargs -r docker rm -f 2>/dev/null || true
	@docker ps -a --filter "name=loki" --format "{{.ID}}" | xargs -r docker rm -f 2>/dev/null || true
	@echo "Удаление контейнеров с префиксом проекта..."
	docker-compose rm -f postgres auth-service experiment-service auth-proxy experiment-portal loki promtail grafana 2>/dev/null || true
	@echo "Очистка неиспользуемых образов..."
	@docker image prune -f >/dev/null 2>&1 || true
	@echo "✅ Очистка завершена. Запускаю сервисы заново..."
	@$(MAKE) dev-up

# Алиас для запуска
dev: dev-up

# Очистка всех данных в dev (база данных, логи)
# ⚠️  ВНИМАНИЕ: Эта команда удалит все данные из базы данных и все логи!
dev-clean:
	@echo "⚠️  ВНИМАНИЕ: Эта команда удалит все данные из базы данных и все логи!"
	@echo "Остановка всех dev-сервисов..."
	@docker-compose stop postgres auth-service experiment-service auth-proxy experiment-portal loki promtail grafana 2>/dev/null || true
	@cd infrastructure/logging && docker-compose -f docker-compose.yml stop loki promtail grafana 2>/dev/null || true
	@echo "Удаление контейнеров..."
	@docker-compose rm -f postgres auth-service experiment-service auth-proxy experiment-portal loki promtail grafana 2>/dev/null || true
	@cd infrastructure/logging && docker-compose -f docker-compose.yml rm -f loki promtail grafana 2>/dev/null || true
	@echo "Удаление volumes (база данных и логи)..."
	@docker volume rm -f $${POSTGRES_DATA_VOLUME:-experiment-postgres-data} 2>/dev/null || true
	@docker volume rm -f $${LOKI_DATA_VOLUME:-experiment-loki-data} 2>/dev/null || true
	@docker volume rm -f $${GRAFANA_DATA_VOLUME:-experiment-grafana-data} 2>/dev/null || true
	@echo "✅ Все данные очищены!"
	@echo ""
	@echo "💡 Для запуска сервисов заново выполните: make dev-up"

# ============================================
# Миграции базы данных
# ============================================

# Применение миграций auth-service
auth-migrate:
	@echo "Применение миграций auth-service..."
	@docker-compose exec -T auth-service python -m bin.migrate --database-url "$${AUTH_DATABASE_URL:-postgresql://postgres:postgres@postgres:5432/auth_db}" || \
		docker-compose exec auth-service python -m bin.migrate --database-url "$${AUTH_DATABASE_URL:-postgresql://postgres:postgres@postgres:5432/auth_db}"
	@echo "✅ Миграции применены"

# Создание базы данных auth_db (если не существует)
auth-create-db:
	@echo "Создание базы данных auth_db..."
	@docker-compose exec -T postgres psql -U postgres -d postgres -c "SELECT 1 FROM pg_database WHERE datname = 'auth_db'" | grep -q 1 && \
		echo "✅ База данных auth_db уже существует" || \
		(docker-compose exec -T postgres psql -U postgres -d postgres -c "CREATE DATABASE auth_db;" && \
		echo "✅ База данных auth_db создана")

# Инициализация auth-service (создание БД + миграции)
auth-init: auth-create-db auth-migrate
	@echo "✅ Auth-service инициализирован"

