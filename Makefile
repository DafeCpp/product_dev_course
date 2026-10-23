.PHONY: bench-firmware-test check-prod-wiring test test-backend test-deploy-contract test-frontend test-telemetry-cli type-check backend-install frontend-install
.PHONY: test-coverage test-coverage-backend test-coverage-frontend test-coverage-firmware
.PHONY: help backend-install dev-clean-all dev-seed-demo dev-reset-admin backend-base backend-base-no-cache backend-base-check
.PHONY: logs logs-follow logs-service logs-proxy logs-auth-service logs-portal logs-postgres logs-errors
.PHONY: logs-stack logs-stack-up logs-stack-down logs-stack-restart
.PHONY: dev dev-up dev-down dev-restart dev-rebuild dev-rebuild-changed dev-logs dev-status dev-fix dev-clean dev-seed grafana-reset-password

.DEFAULT_GOAL := test
SHELL := /bin/bash

BACKEND_SERVICES_DIR := projects/backend/services
BACKEND_COMMON_DIR := projects/backend/common
BACKEND_DIR := projects/backend/services/experiment-service
OPENAPI_SPEC := openapi/openapi.yaml
# Find all backend services (directories with pyproject.toml)
BACKEND_SERVICES := $(patsubst %/pyproject.toml,%,$(sort $(wildcard $(BACKEND_SERVICES_DIR)/*/pyproject.toml)))
BACKEND_PROJECTS := $(BACKEND_COMMON_DIR) $(BACKEND_SERVICES)
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
DOCKER_BUILDKIT ?= 1
COMPOSE_DOCKER_CLI_BUILD ?= 1
# Таймаут HTTP-запросов к Docker daemon (секунды). Увеличьте при медленной сборке/запуске контейнеров.
# Prefer Docker Compose v2 (docker compose) over legacy v1 (docker-compose).
# v1.29.2 is incompatible with newer Docker Engine ('ContainerConfig' KeyError).
DOCKER_COMPOSE ?= $(shell if docker compose version >/dev/null 2>&1; then echo "docker compose"; else echo "docker-compose"; fi)
COMPOSE_HTTP_TIMEOUT ?= 120
BACKEND_BASE_DOCKERFILE := projects/backend/Dockerfile.base
BACKEND_BASE_HASH := $(shell sha256sum $(BACKEND_BASE_DOCKERFILE) 2>/dev/null | awk '{print $$1}')
BACKEND_DEV_SERVICES := auth-service experiment-service telemetry-ingest-service
# All services managed by `make dev-*` targets
DEV_ALL_SERVICES := postgres redis auth-service experiment-service telemetry-ingest-service config-service auth-proxy experiment-portal sensor-simulator loki alloy grafana
# Default dev credentials for `make dev-seed`
DEV_ADMIN_USER     ?= admin
DEV_ADMIN_EMAIL    ?= admin@example.com
DEV_ADMIN_PASSWORD ?= Admin123
DEV_BOOTSTRAP_SECRET ?= dev-bootstrap-secret
AUTH_SERVICE_URL        ?= http://localhost:8001
EXPERIMENT_SERVICE_URL  ?= http://localhost:8002
TELEMETRY_SERVICE_URL   ?= http://localhost:8003
# Backend tests require TimescaleDB (telemetry tables are hypertables).
# By default we run tests against the docker-compose postgres container and
# dynamically discover its credentials and mapped host port (so local .env works).
TEST_POSTGRESQL_DSN ?=

export PYTHON NODE FRONTEND_NODE_IMAGE DOCKER_COMPOSE COMPOSE_HTTP_TIMEOUT
export DOCKER_BUILDKIT COMPOSE_DOCKER_CLI_BUILD BACKEND_BASE_DOCKERFILE BACKEND_BASE_HASH FORCE_BACKEND_BASE
export BACKEND_SERVICES BACKEND_PROJECTS BACKEND_COMMON_DIR BACKEND_DEV_SERVICES DEV_ALL_SERVICES TEST_POSTGRESQL_DSN
export DEV_ADMIN_USER DEV_ADMIN_EMAIL DEV_ADMIN_PASSWORD DEV_BOOTSTRAP_SECRET AUTH_SERVICE_URL MAKE

# Репозиторные скрипты (scripts/) используют PyYAML; ставим его в отдельный venv,
# чтобы не требовать глобальных пакетов.
TOOLS_VENV := .venv-tools
$(TOOLS_VENV)/.ready: scripts/requirements-tools.txt
	@python3 -m venv $(TOOLS_VENV)
	@$(TOOLS_VENV)/bin/pip install -q -r scripts/requirements-tools.txt
	@touch $@

help: ## Show available commands
	@bash scripts/show-help.sh $(MAKEFILE_LIST)

test: type-check test-backend test-telemetry-cli test-frontend ## Run type checks, backend, CLI and frontend tests

test-deploy-contract: $(TOOLS_VENV)/.ready ## Run script and deployment contract tests
	@echo "🧪 Running Make command and production contract tests..."
	@$(TOOLS_VENV)/bin/python -m unittest discover -s scripts/tests -p "test_*.py" -v

check-prod-wiring: $(TOOLS_VENV)/.ready ## Check dev/prod compose drift against infrastructure/production-wiring.yml
	@$(TOOLS_VENV)/bin/python scripts/check_prod_wiring.py

backend-install: ## Install backend dependencies (Python 3.14+)
	@bash scripts/backend.sh install

frontend-install: ## Install npm workspace dependencies
	@bash scripts/frontend.sh frontend-install

type-check: backend-install ## Run backend type checks
	@bash scripts/backend.sh type-check

test-backend: backend-install ## Run backend tests (TimescaleDB required)
	@bash scripts/backend.sh test

# Host-тесты прошивки контроллера стенда (GTest); не входят в make test —
# запускаются явно (как test-coverage-firmware у rc_vehicle)
bench-firmware-test: ## Run bench-controller host tests
	$(MAKE) -C projects/bench_controller/firmware test

test-telemetry-cli: ## Run telemetry CLI tests
	@bash scripts/backend.sh telemetry-cli

# Vitest во всех frontend workspace (common, experiment-portal, auth-proxy, sensor-simulator).
test-frontend: frontend-install ## Run frontend workspace tests
	@bash scripts/frontend.sh test-frontend

.PHONY: test-frontend-docker
test-frontend-docker: ## Run frontend tests in Docker
	@bash scripts/frontend.sh test-frontend-docker

# ============================================
# Coverage
# ============================================

# Запуск всех coverage-репортов (backend + frontend + firmware).
test-coverage: test-coverage-backend test-coverage-frontend test-coverage-firmware ## Generate backend, frontend and firmware coverage

# Coverage по всем backend сервисам (pytest-cov). Использует ту же логику postgres, что и test-backend.
test-coverage-backend: backend-install ## Generate backend coverage
	@bash scripts/backend.sh coverage

# Coverage по фронту (vitest --coverage во всех frontend workspace).
test-coverage-frontend: frontend-install ## Generate frontend coverage
	@bash scripts/frontend.sh test-coverage-frontend

# Coverage прошивки (host-сборка GTest с lcov).
test-coverage-firmware: ## Generate firmware host coverage
	@bash scripts/firmware-coverage.sh

.PHONY: generate-sdk
generate-sdk: ## Regenerate API clients
	@cd $(BACKEND_DIR) && rm -rf clients/typescript-fetch && \
		poetry --directory=. run openapi-generator-cli generate \
			-i $(OPENAPI_SPEC) \
			-g typescript-fetch \
			-o clients/typescript-fetch \
			-c openapi/clients/typescript-fetch-config.yaml
	@cd $(BACKEND_DIR) && rm -rf clients/cpp-restsdk && \
		poetry --directory=. run openapi-generator-cli generate \
			-i $(OPENAPI_SPEC) \
			-g cpp-restsdk \
			-o clients/cpp-restsdk \
			-c openapi/clients/cpp-restsdk-config.yaml
	@npm --prefix projects/frontend/apps/experiment-portal run generate:types

# ============================================
# Логирование
# ============================================

# Просмотр всех логов в реальном времени
logs: ## Follow all service logs
	@echo "Просмотр логов всех сервисов (Ctrl+C для выхода)"
	$(DOCKER_COMPOSE) logs -f --tail=50

# Просмотр логов конкретного сервиса
logs-service: ## Follow experiment-service logs
	$(DOCKER_COMPOSE) logs -f --tail=100 experiment-service

logs-proxy: ## Follow auth-proxy logs
	$(DOCKER_COMPOSE) logs -f --tail=100 auth-proxy

logs-auth-service: ## Follow auth-service logs
	$(DOCKER_COMPOSE) logs -f --tail=100 auth-service

logs-portal: ## Follow portal logs
	$(DOCKER_COMPOSE) logs -f --tail=100 experiment-portal

logs-postgres: ## Follow PostgreSQL logs
	$(DOCKER_COMPOSE) logs -f --tail=100 postgres

# Просмотр только ошибок
logs-errors: ## Show recent errors
	$(DOCKER_COMPOSE) logs --tail=200 | grep -i "error\|fatal\|exception" --color=always

# ============================================
# Grafana Stack (Loki + Alloy + Grafana)
# ============================================

# Запуск стека логирования
logs-stack-up: ## Start the standalone logging stack
	@echo "Запуск стека логирования (Loki + Alloy + Grafana)..."
	cd infrastructure/logging && $(DOCKER_COMPOSE) -f docker-compose.yml up -d loki alloy grafana
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
logs-stack-down: ## Stop the standalone logging stack
	@echo "Остановка стека логирования..."
	cd infrastructure/logging && $(DOCKER_COMPOSE) -f docker-compose.yml stop loki alloy grafana

# Перезапуск стека логирования
logs-stack-restart: logs-stack-down logs-stack-up ## Restart the standalone logging stack

# Алиас для запуска
logs-stack: logs-stack-up ## Start the standalone logging stack

# Сброс пароля администратора Grafana
grafana-reset-password: ## Reset the local Grafana password
	@echo "Сброс пароля администратора Grafana..."
	@echo "Используется пароль из переменной GRAFANA_ADMIN_PASSWORD (по умолчанию: admin)"
	cd infrastructure/logging && $(DOCKER_COMPOSE) -f docker-compose.yml exec -T grafana grafana cli admin reset-admin-password "$${GRAFANA_ADMIN_PASSWORD:-admin}" 2>&1 | grep -E "(Admin password|successfully|error)" || true
	@echo ""
	@echo "✅ Пароль администратора Grafana сброшен!"
	@echo "👤 Логин: admin"
	@echo "🔑 Пароль: $${GRAFANA_ADMIN_PASSWORD:-admin}"

# ============================================
# Локальная отладка (Frontend + Backend + Auth Service + Auth Proxy + Grafana)
# ============================================

# Запуск фронтенда, бэкенда, auth-service, auth-proxy и Grafana для локальной отладки
dev-up: ## Start and seed the development stack
	@bash scripts/dev.sh dev-up

# Заполнение БД демо-данными: 2 проекта, 5 датчиков, 3 эксперимента, 5 запусков, телеметрия.
# Требует: pip install requests (или pipx run requests)
dev-seed-demo: dev-seed ## Create demo projects, sensors and experiments
	@echo "Заполнение БД демо-данными..."
	@python3 scripts/seed_demo.py \
		--auth-url $(AUTH_SERVICE_URL) \
		--exp-url $(EXPERIMENT_SERVICE_URL) \
		--telem-url $(TELEMETRY_SERVICE_URL)

# Создание dev-пользователя (superadmin) через bootstrap API.
# Идемпотентно: если пользователь уже создан, просто пропускает.
dev-seed: ## Create the development admin user
	@echo "Ожидание готовности auth-service..."
	@for i in $$(seq 1 30); do \
		if curl -sf $(AUTH_SERVICE_URL)/health >/dev/null 2>&1; then break; fi; \
		if [ $$i -eq 30 ]; then echo "❌ auth-service не отвечает"; exit 1; fi; \
		sleep 1; \
	done
	@echo "Создание dev-пользователя $(DEV_ADMIN_USER)..."
	@status=$$(curl -s -o /dev/null -w '%{http_code}' -X POST $(AUTH_SERVICE_URL)/auth/admin/bootstrap \
		-H 'Content-Type: application/json' \
		-d '{"bootstrap_secret":"$(DEV_BOOTSTRAP_SECRET)","username":"$(DEV_ADMIN_USER)","email":"$(DEV_ADMIN_EMAIL)","password":"$(DEV_ADMIN_PASSWORD)"}'); \
	if [ "$$status" = "201" ]; then \
		echo "✅ Пользователь $(DEV_ADMIN_USER) создан (superadmin)"; \
	elif [ "$$status" = "409" ]; then \
		echo "ℹ️  Пользователь $(DEV_ADMIN_USER) уже существует — используй 'make dev-reset-admin' чтобы сбросить пароль на $(DEV_ADMIN_PASSWORD)"; \
	else \
		echo "⚠️  Bootstrap вернул HTTP $$status (возможно, пользователь уже создан)"; \
	fi

# Сброс пароля superadmin на дефолтный dev-пароль.
# Используй когда пароль неизвестен после make dev-seed.
dev-reset-admin: ## Reset the development admin password
	@echo "Сброс пароля $(DEV_ADMIN_USER) на $(DEV_ADMIN_PASSWORD)..."
	@$(DOCKER_COMPOSE) exec -T auth-service sh -c 'printf "%s\n" "import asyncio, asyncpg, os" "from auth_service.services.password import hash_password" "async def reset():" "    url = os.environ.get(\"DATABASE_URL\", \"\")" "    conn = await asyncpg.connect(url)" "    h = hash_password(\"$(DEV_ADMIN_PASSWORD)\")" "    r = await conn.fetchrow(\"UPDATE users SET hashed_password=\$$1, password_change_required=false WHERE username=\$$2 RETURNING id\", h, \"$(DEV_ADMIN_USER)\")" "    await conn.close()" "    return r" "r = asyncio.run(reset())" "print(\"✅ Пароль сброшен\" if r else \"❌ Пользователь не найден\")" | python3'

# Остановка фронтенда, бэкенда, auth-service, auth-proxy и Grafana
dev-down: ## Stop development services
	@echo "Остановка фронтенда, бэкенда, auth-service, auth-proxy и Grafana..."
	$(DOCKER_COMPOSE) stop $(DEV_ALL_SERVICES)
	@echo "✅ Сервисы остановлены"

# Перезапуск фронтенда, бэкенда, auth-service, auth-proxy и Grafana
dev-restart: dev-down dev-up ## Restart development services

# Пересборка контейнеров и запуск dev-сервисов
dev-rebuild: dev-down ## Rebuild development images without cache
	@bash scripts/dev.sh dev-rebuild

# Пересборка только тех образов, которые затронуты изменениями в git
dev-rebuild-changed: ## Rebuild images affected by local git changes
	@bash scripts/dev.sh dev-rebuild-changed

# Пересборка только одного backend сервиса (make dev-auth-service и т.д.)
dev-%: ## Rebuild one backend service (e.g. dev-auth-service)
	@bash scripts/dev.sh dev-service "$*"

backend-base: ## Build the backend base image if outdated
	@bash scripts/dev.sh backend-base

backend-base-no-cache: ## Rebuild the backend base image without cache
	@bash scripts/dev.sh backend-base-no-cache

backend-base-check: ## Check the backend base image hash
	@bash scripts/dev.sh backend-base-check

# Просмотр логов всех dev-сервисов
dev-logs: ## Follow development service logs
	@echo "Просмотр логов всех dev-сервисов (Ctrl+C для выхода)"
	$(DOCKER_COMPOSE) logs -f --tail=50 $(DEV_ALL_SERVICES)

# Статус dev-сервисов (какие контейнеры запущены)
dev-status: ## Show development service status
	@echo "Статус dev-сервисов:"
	@$(DOCKER_COMPOSE) ps $(DEV_ALL_SERVICES) 2>/dev/null || $(DOCKER_COMPOSE) ps

# Пересоздание dev-контейнеров без удаления постоянных данных
dev-fix: ## Recreate development containers, retaining data
	@bash scripts/dev.sh dev-fix

# Алиас для запуска
dev: dev-up ## Start the development stack

# Очистка всех данных в dev (база данных, логи)
# ⚠️  ВНИМАНИЕ: Эта команда удалит все данные из базы данных и все логи!
dev-clean: ## Delete development containers and data
	@bash scripts/dev.sh dev-clean

# Полная очистка всех контейнеров проекта (включая остановленные)
# ⚠️  Удаляются контейнеры, volumes и локально собранные образы Compose-проектов.
dev-clean-all: ## Delete development containers, data and local images
	@bash scripts/dev.sh dev-clean-all

# ============================================
# Миграции базы данных
# ============================================

# Применение миграций auth-service
auth-migrate: ## Apply auth-service migrations
	@echo "Применение миграций auth-service..."
	@$(DOCKER_COMPOSE) exec -T auth-service python -m bin.migrate --database-url "$${AUTH_DATABASE_URL:-postgresql://auth_user:auth_password@postgres:5432/auth_db}" || \
		$(DOCKER_COMPOSE) exec auth-service python -m bin.migrate --database-url "$${AUTH_DATABASE_URL:-postgresql://auth_user:auth_password@postgres:5432/auth_db}"
	@echo "✅ Миграции применены"

# Создание базы данных auth_db (если не существует)
auth-create-db: ## Create auth_db
	@echo "Создание базы данных auth_db..."
	@$(DOCKER_COMPOSE) exec -T postgres psql -U postgres -d postgres -c "SELECT 1 FROM pg_database WHERE datname = 'auth_db'" | grep -q 1 && \
		echo "✅ База данных auth_db уже существует" || \
		($(DOCKER_COMPOSE) exec -T postgres psql -U postgres -d postgres -c "CREATE DATABASE auth_db;" && \
		echo "✅ База данных auth_db создана")

# Seed initial admin user (if not exists)
auth-seed: ## Seed the initial admin
	@echo "Создание первого админа (если не существует)..."
	@$(DOCKER_COMPOSE) exec -T auth-service python -m bin.seed \
		--database-url "$${AUTH_DATABASE_URL:-postgresql://auth_user:auth_password@postgres:5432/auth_db}" \
		--username "$${ADMIN_USERNAME:-admin}" \
		--email "$${ADMIN_EMAIL:-admin@example.com}" \
		--password "$${ADMIN_PASSWORD}" || true
	@echo "✅ Seed completed"

# Инициализация auth-service (создание БД + миграции + первый админ)
auth-init: auth-create-db auth-migrate auth-seed ## Initialize auth-service
	@echo "✅ Auth-service инициализирован"
# Применение миграций experiment-service
experiment-migrate: ## Apply experiment-service migrations
	@echo "Применение миграций experiment-service..."
	@$(DOCKER_COMPOSE) exec -T experiment-service python -m bin.migrate --database-url "$${EXPERIMENT_DATABASE_URL:-postgresql://experiment_user:experiment_password@postgres:5432/experiment_db}" || \
		$(DOCKER_COMPOSE) exec experiment-service python -m bin.migrate --database-url "$${EXPERIMENT_DATABASE_URL:-postgresql://experiment_user:experiment_password@postgres:5432/experiment_db}"
	@echo "✅ Миграции применены"

# Применение миграций telemetry-ingest-service (использует ту же experiment_db)
telemetry-ingest-migrate: ## Apply telemetry migrations
	@echo "Применение миграций telemetry-ingest-service..."
	@$(DOCKER_COMPOSE) exec -T telemetry-ingest-service python -m bin.migrate --database-url "$${EXPERIMENT_DATABASE_URL:-postgresql://experiment_user:experiment_password@postgres:5432/experiment_db}" || \
		$(DOCKER_COMPOSE) exec telemetry-ingest-service python -m bin.migrate --database-url "$${EXPERIMENT_DATABASE_URL:-postgresql://experiment_user:experiment_password@postgres:5432/experiment_db}"
	@echo "✅ Миграции telemetry-ingest-service применены"

# Нагрузочное тестирование telemetry-ingest (см. projects/backend/services/telemetry-ingest-service/loadtest/README.md)
# Пример: make telemetry-loadtest ARGS="run baseline"
.PHONY: telemetry-loadtest
telemetry-loadtest: ## Run telemetry load test (ARGS="run baseline")
	@cd projects/backend/services/telemetry-ingest-service && \
		PYTHONPATH=loadtest poetry run python -m ingest_loadtest $(ARGS)

# Создание базы данных script-service
script-create-db: ## Create script_db
	@echo "Создание базы данных script_db..."
	@$(DOCKER_COMPOSE) exec -T postgres psql -U postgres -d postgres -c "SELECT 1 FROM pg_database WHERE datname = 'script_db'" | grep -q 1 && \
		echo "✅ База данных script_db уже существует" || \
		($(DOCKER_COMPOSE) exec -T postgres psql -U postgres -d postgres -c "CREATE DATABASE script_db;" && \
		echo "✅ База данных script_db создана")

# Применение миграций script-service
script-migrate: ## Apply script-service migrations
	@echo "Применение миграций script-service..."
	@$(DOCKER_COMPOSE) exec -T script-service python -m bin.migrate --database-url "$${SCRIPT_DATABASE_URL:-postgresql://script_user:script_password@postgres:5432/script_db}" || \
		$(DOCKER_COMPOSE) exec script-service python -m bin.migrate --database-url "$${SCRIPT_DATABASE_URL:-postgresql://script_user:script_password@postgres:5432/script_db}"
	@echo "✅ Миграции script-service применены"

# Инициализация script-service (создание БД + миграции)
script-init: script-create-db script-migrate ## Initialize script-service
	@echo "✅ Script-service инициализирован"

# Создание базы данных config_db (если не существует)
config-create-db: ## Create config_db
	@echo "Создание базы данных config_db..."
	@$(DOCKER_COMPOSE) exec -T postgres psql -U postgres -d postgres -c "SELECT 1 FROM pg_database WHERE datname = 'config_db'" | grep -q 1 && \
		echo "✅ База данных config_db уже существует" || \
		($(DOCKER_COMPOSE) exec -T postgres psql -U postgres -d postgres -c "CREATE DATABASE config_db;" && \
		echo "✅ База данных config_db создана")

# Применение миграций config-service
config-migrate: ## Apply config-service migrations
	@echo "Применение миграций config-service..."
	@$(DOCKER_COMPOSE) exec -T config-service python -m bin.migrate --database-url "$${CONFIG_DATABASE_URL:-postgresql://config_user:config_password@postgres:5432/config_db}" || \
		$(DOCKER_COMPOSE) exec config-service python -m bin.migrate --database-url "$${CONFIG_DATABASE_URL:-postgresql://config_user:config_password@postgres:5432/config_db}"
	@echo "✅ Миграции config-service применены"

# Инициализация config-service (создание БД + миграции)
config-init: config-create-db config-migrate ## Initialize config-service
	@echo "✅ Config-service инициализирован"

# ============================================
# Production Deploy (Yandex Cloud)
# ============================================

.PHONY: infra-init infra-plan infra-apply infra-destroy

infra-init: ## Initialize Terraform
	@cd infrastructure/yandex-cloud && terraform init

infra-plan: ## Plan infrastructure changes
	@cd infrastructure/yandex-cloud && terraform plan

infra-apply: ## Apply infrastructure changes
	@cd infrastructure/yandex-cloud && terraform apply

infra-destroy: ## Destroy production infrastructure (explicit confirmation required)
	@if [ "$(CONFIRM_PRODUCTION_DESTROY)" != "destroy-production" ]; then \
		echo "ОТКАЗ: production teardown требует CONFIRM_PRODUCTION_DESTROY=destroy-production"; \
		echo "Сначала выполните процедуру снятия prevent_destroy из docs/deployment-yandex-cloud.md"; \
		exit 1; \
	fi
	@echo "ВНИМАНИЕ: удаляется ВСЯ инфраструктура в Yandex Cloud!"
	@cd infrastructure/yandex-cloud && terraform destroy

.PHONY: mvp-demo-check
mvp-demo-check: dev-up auth-init experiment-migrate telemetry-ingest-migrate ## Run the MVP end-to-end demo check
	@bash scripts/mvp_demo_check.sh
