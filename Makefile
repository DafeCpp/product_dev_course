.PHONY: test test-backend test-frontend test-telemetry-cli type-check backend-install frontend-install
.PHONY: backend-install
.PHONY: logs logs-follow logs-service logs-proxy logs-auth-service logs-errors
.PHONY: logs-stack logs-stack-up logs-stack-down logs-stack-restart
.PHONY: dev dev-up dev-down dev-restart dev-rebuild dev-rebuild-changed dev-logs dev-status dev-fix dev-clean grafana-reset-password

BACKEND_SERVICES_DIR := projects/backend/services
BACKEND_DIR := projects/backend/services/experiment-service
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
DOCKER_BUILDKIT ?= 1
COMPOSE_DOCKER_CLI_BUILD ?= 1
DOCKER_BUILD_ENV := DOCKER_BUILDKIT=$(DOCKER_BUILDKIT) COMPOSE_DOCKER_CLI_BUILD=$(COMPOSE_DOCKER_CLI_BUILD)
# Таймаут HTTP-запросов к Docker daemon (секунды). Увеличьте при медленной сборке/запуске контейнеров.
COMPOSE_HTTP_TIMEOUT ?= 120
BACKEND_BASE_DOCKERFILE := projects/backend/Dockerfile.base
BACKEND_BASE_HASH := $(shell sha256sum $(BACKEND_BASE_DOCKERFILE) 2>/dev/null | awk '{print $$1}')
BACKEND_DEV_SERVICES := auth-service experiment-service telemetry-ingest-service
# Backend tests require TimescaleDB (telemetry tables are hypertables).
# By default we run tests against the docker-compose postgres container and
# dynamically discover its credentials and mapped host port (so local .env works).
TEST_POSTGRESQL_DSN ?=

test: type-check test-backend test-telemetry-cli test-frontend

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
	PG_TEST_DSN="$(TEST_POSTGRESQL_DSN)"; \
	if [ -z "$$PG_TEST_DSN" ]; then \
		echo "🐘 Starting TimescaleDB (postgres) for backend tests..."; \
		docker-compose up -d postgres >/dev/null 2>&1 || { \
			echo "❌ docker-compose failed to start postgres"; \
			echo "   If you see 'Permission denied' on the Docker socket, add your user to the docker group:"; \
			echo "   sudo usermod -aG docker \$$USER && newgrp docker"; \
			echo "   Or run tests against an existing Postgres: make test-backend TEST_POSTGRESQL_DSN='postgresql://user:pass@host:5432/dbname'"; \
			exit 1; \
		}; \
		for i in $$(seq 1 60); do \
			docker-compose exec -T postgres pg_isready -U postgres -d postgres >/dev/null 2>&1 && break; \
			sleep 0.5; \
		done; \
		if ! docker-compose exec -T postgres pg_isready -U postgres -d postgres >/dev/null 2>&1; then \
			echo "❌ Postgres is not ready"; \
			docker-compose logs --tail=80 postgres; \
			exit 1; \
		fi; \
		hostport="$$(docker-compose port postgres 5432 2>/dev/null | tail -n 1 | sed 's/.*://')"; \
		if [ -z "$$hostport" ]; then hostport=5433; fi; \
		pg_user="$$(docker-compose exec -T postgres sh -lc 'printf \"%s\" \"$${POSTGRES_USER:-postgres}\"' | sed 's/\"//g')"; \
		pg_pass="$$(docker-compose exec -T postgres sh -lc 'printf \"%s\" \"$${POSTGRES_PASSWORD:-postgres}\"' | sed 's/\"//g')"; \
		docker-compose exec -T postgres psql -U postgres -d postgres -c "ALTER USER \"$${pg_user}\" WITH PASSWORD '$${pg_pass}';" >/dev/null 2>&1 || true; \
		PG_TEST_DSN="postgresql://$${pg_user}:$${pg_pass}@localhost:$${hostport}/postgres"; \
	fi; \
	echo "🔌 Using PostgreSQL DSN for tests: $$(echo $$PG_TEST_DSN | sed -E 's#(postgresql://[^:]+:)[^@]+@#\\1***@#')"; \
	failed=0; \
	for service in $(BACKEND_SERVICES); do \
		echo "🧪 Running tests for $$(basename $$service)..."; \
		(cd $$service && poetry run pytest --postgresql "$$PG_TEST_DSN") || failed=1; \
	done; \
	exit $$failed

test-telemetry-cli:
	@echo "🧪 Running tests for telemetry-cli..."
	@cd projects/telemetry_cli && \
		PY=""; \
		if [ -n "$(PYTHON)" ] && [ -x "$(PYTHON)" ]; then PY="$(PYTHON)"; fi; \
		if [ -z "$$PY" ] && command -v "$(PYTHON)" >/dev/null 2>&1; then PY="$(PYTHON)"; fi; \
		if [ -z "$$PY" ] && [ -x "$$HOME/.pyenv/shims/python3.14" ]; then PY="$$HOME/.pyenv/shims/python3.14"; fi; \
		if [ -z "$$PY" ] && command -v python3.14 >/dev/null 2>&1; then PY="python3.14"; fi; \
		if [ -z "$$PY" ] && command -v python3 >/dev/null 2>&1; then PY="python3"; fi; \
		if [ -z "$$PY" ] && command -v python >/dev/null 2>&1; then PY="python"; fi; \
		if [ -z "$$PY" ]; then \
			echo "❌ Не найден Python интерпретатор для telemetry-cli тестов."; \
			exit 1; \
		fi; \
		if [ ! -x ".venv/bin/python" ]; then \
			echo "📦 Creating venv for telemetry-cli tests..."; \
			"$$PY" -m venv .venv || exit 1; \
			.venv/bin/python -m pip install -U pip >/dev/null || exit 1; \
			.venv/bin/pip install -e . >/dev/null || exit 1; \
		fi; \
		PYTHONPATH=src .venv/bin/python -m unittest discover -s tests -p "test_*.py" -q

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
# Grafana Stack (Loki + Alloy + Grafana)
# ============================================

# Запуск стека логирования
logs-stack-up:
	@echo "Запуск стека логирования (Loki + Alloy + Grafana)..."
	cd infrastructure/logging && docker-compose -f docker-compose.yml up -d loki alloy grafana
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
	cd infrastructure/logging && docker-compose -f docker-compose.yml stop loki alloy grafana

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
	@$(MAKE) backend-base-check || $(MAKE) backend-base
	@if [ ! -f docker-compose.override.yml ]; then \
		echo "⚠️  Файл docker-compose.override.yml не найден. Создаю из примера..."; \
		cp docker-compose.override.yml.example docker-compose.override.yml 2>/dev/null || true; \
	fi
	@if [ ! -f .env ]; then \
		echo "⚠️  Файл .env не найден. Создаю из примера..."; \
		cp env.docker.example .env 2>/dev/null || true; \
	fi
	COMPOSE_HTTP_TIMEOUT=$(COMPOSE_HTTP_TIMEOUT) docker-compose up -d postgres auth-service experiment-service telemetry-ingest-service auth-proxy experiment-portal sensor-simulator loki alloy grafana
	@echo ""
	@echo "✅ Сервисы запущены!"
	@echo "🌐 Фронтенд доступен на http://localhost:3000"
	@echo "🧪 Sensor Simulator доступен на http://localhost:8082"
	@echo "🔧 Бэкенд API доступен на http://localhost:8002"
	@echo "📡 Telemetry Ingest доступен на http://localhost:8003"
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
	docker-compose stop postgres auth-service experiment-service telemetry-ingest-service auth-proxy experiment-portal sensor-simulator loki alloy grafana
	@echo "✅ Сервисы остановлены"

# Перезапуск фронтенда, бэкенда, auth-service, auth-proxy и Grafana
dev-restart: dev-down dev-up

# Пересборка контейнеров и запуск dev-сервисов
dev-rebuild: dev-down
	@echo "Пересборка контейнеров dev-сервисов..."
	@$(MAKE) backend-base-no-cache
	@set -e; \
	if docker buildx version >/dev/null 2>&1; then \
		$(DOCKER_BUILD_ENV) docker-compose build --no-cache postgres auth-service experiment-service telemetry-ingest-service auth-proxy experiment-portal sensor-simulator loki alloy grafana; \
	else \
		echo "⚠️  buildx не найден — docker-compose build без BuildKit."; \
		DOCKER_BUILDKIT=0 COMPOSE_DOCKER_CLI_BUILD=0 docker-compose build --no-cache postgres auth-service experiment-service telemetry-ingest-service auth-proxy experiment-portal sensor-simulator loki alloy grafana; \
	fi
	@$(MAKE) dev-up

# Пересборка только тех образов, которые затронуты изменениями в git
dev-rebuild-changed:
	@set -e; \
	if ! command -v git >/dev/null 2>&1; then \
		echo "❌ git не найден — невозможно определить измененные файлы."; \
		exit 1; \
	fi; \
	changed_files="$$(git status --porcelain=v1 -uall | awk '{print $$2}')"; \
	if [ -z "$$changed_files" ]; then \
		echo "✅ Изменений не найдено — пересборка не требуется."; \
		exit 0; \
	fi; \
	services=""; \
	base_changed=0; \
	if echo "$$changed_files" | grep -q '^projects/backend/Dockerfile.base$$'; then \
		base_changed=1; \
		services="$$services $(BACKEND_DEV_SERVICES)"; \
	fi; \
	if echo "$$changed_files" | grep -q '^projects/backend/common/'; then \
		services="$$services $(BACKEND_DEV_SERVICES)"; \
	fi; \
	if echo "$$changed_files" | grep -q '^projects/backend/services/auth-service/'; then \
		services="$$services auth-service"; \
	fi; \
	if echo "$$changed_files" | grep -q '^projects/backend/services/experiment-service/'; then \
		services="$$services experiment-service"; \
	fi; \
	if echo "$$changed_files" | grep -q '^projects/backend/services/telemetry-ingest-service/'; then \
		services="$$services telemetry-ingest-service"; \
	fi; \
	if echo "$$changed_files" | grep -q '^projects/frontend/apps/auth-proxy/'; then \
		services="$$services auth-proxy"; \
	fi; \
	if echo "$$changed_files" | grep -q '^projects/frontend/apps/experiment-portal/'; then \
		services="$$services experiment-portal"; \
	fi; \
	if echo "$$changed_files" | grep -q '^projects/frontend/apps/sensor-simulator/'; then \
		services="$$services sensor-simulator"; \
	fi; \
	services="$$(echo $$services | tr ' ' '\n' | awk 'NF' | sort -u | tr '\n' ' ')"; \
	if [ -z "$$services" ]; then \
		echo "✅ Изменения не затрагивают сборку образов — пересборка не требуется."; \
		exit 0; \
	fi; \
	if echo "$$services" | grep -q -E '(^| )(auth-service|experiment-service|telemetry-ingest-service)( |$$)'; then \
		if [ "$$base_changed" = "1" ]; then \
			$(MAKE) backend-base-no-cache; \
		else \
			$(MAKE) backend-base-check || $(MAKE) backend-base; \
		fi; \
	fi; \
	echo "Пересборка измененных сервисов: $$services"; \
	if docker buildx version >/dev/null 2>&1; then \
		$(DOCKER_BUILD_ENV) docker-compose build $$services; \
	else \
		echo "⚠️  buildx не найден — docker-compose build без BuildKit."; \
		DOCKER_BUILDKIT=0 COMPOSE_DOCKER_CLI_BUILD=0 docker-compose build $$services; \
	fi; \
	docker-compose up -d $$services

# Пересборка только одного backend сервиса (make dev-auth-service и т.д.)
dev-%:
	@service="$*"; \
	if ! echo "$(BACKEND_DEV_SERVICES)" | tr ' ' '\n' | grep -qx "$$service"; then \
		echo "❌ Неизвестный backend сервис: $$service"; \
		echo "   Доступные: $(BACKEND_DEV_SERVICES)"; \
		exit 1; \
	fi; \
	$(MAKE) backend-base-check || $(MAKE) backend-base; \
	if [ ! -f docker-compose.override.yml ]; then \
		echo "⚠️  Файл docker-compose.override.yml не найден. Создаю из примера..."; \
		cp docker-compose.override.yml.example docker-compose.override.yml 2>/dev/null || true; \
	fi; \
	if [ ! -f .env ]; then \
		echo "⚠️  Файл .env не найден. Создаю из примера..."; \
		cp env.docker.example .env 2>/dev/null || true; \
	fi; \
	echo "Пересборка $$service..."; \
	if docker buildx version >/dev/null 2>&1; then \
		$(DOCKER_BUILD_ENV) docker-compose build $$service; \
	else \
		echo "⚠️  buildx не найден — docker-compose build без BuildKit."; \
		DOCKER_BUILDKIT=0 COMPOSE_DOCKER_CLI_BUILD=0 docker-compose build $$service; \
	fi; \
	docker-compose up -d $$service

backend-base:
	@set -e; \
	if [ "$(FORCE_BACKEND_BASE)" != "1" ] && docker image inspect backend-base:local >/dev/null 2>&1; then \
		current_hash="$$(docker image inspect backend-base:local --format '{{ index .Config.Labels "org.opencontainers.image.base.dockerfile-hash" }}' 2>/dev/null)"; \
		if [ "$$current_hash" = "$(BACKEND_BASE_HASH)" ] && [ -n "$$current_hash" ]; then \
			echo "Базовый backend образ уже актуален — пропускаю сборку."; \
			exit 0; \
		fi; \
	fi; \
	echo "Сборка базового backend образа..."; \
	if docker buildx version >/dev/null 2>&1; then \
		$(DOCKER_BUILD_ENV) docker build -f $(BACKEND_BASE_DOCKERFILE) --build-arg BASE_IMAGE_HASH=$(BACKEND_BASE_HASH) -t backend-base:local projects/backend; \
	else \
		echo "⚠️  buildx не найден — собираю без BuildKit."; \
		DOCKER_BUILDKIT=0 COMPOSE_DOCKER_CLI_BUILD=0 docker build -f $(BACKEND_BASE_DOCKERFILE) --build-arg BASE_IMAGE_HASH=$(BACKEND_BASE_HASH) -t backend-base:local projects/backend; \
	fi

backend-base-no-cache:
	@echo "Пересборка базового backend образа без кэша..."
	@set -e; \
	if docker buildx version >/dev/null 2>&1; then \
		$(DOCKER_BUILD_ENV) docker build --no-cache -f $(BACKEND_BASE_DOCKERFILE) --build-arg BASE_IMAGE_HASH=$(BACKEND_BASE_HASH) -t backend-base:local projects/backend; \
	else \
		echo "⚠️  buildx не найден — собираю без BuildKit."; \
		DOCKER_BUILDKIT=0 COMPOSE_DOCKER_CLI_BUILD=0 docker build --no-cache -f $(BACKEND_BASE_DOCKERFILE) --build-arg BASE_IMAGE_HASH=$(BACKEND_BASE_HASH) -t backend-base:local projects/backend; \
	fi

backend-base-check:
	@set -e; \
	if ! docker image inspect backend-base:local >/dev/null 2>&1; then \
		echo "❌ Базовый backend образ не найден."; \
		exit 1; \
	fi; \
	current_hash="$$(docker image inspect backend-base:local --format '{{ index .Config.Labels "org.opencontainers.image.base.dockerfile-hash" }}' 2>/dev/null)"; \
	if [ -z "$$current_hash" ]; then \
		echo "⚠️  В образе нет метки хэша. Пересоберите: make backend-base"; \
		exit 2; \
	fi; \
	if [ "$$current_hash" = "$(BACKEND_BASE_HASH)" ]; then \
		echo "✅ Базовый backend образ актуален."; \
	else \
		echo "⚠️  Базовый backend образ устарел. Текущий хэш: $$current_hash"; \
		echo "    Актуальный хэш: $(BACKEND_BASE_HASH)"; \
		exit 3; \
	fi

# Просмотр логов всех dev-сервисов
dev-logs:
	@echo "Просмотр логов всех dev-сервисов (Ctrl+C для выхода)"
	docker-compose logs -f --tail=50 postgres auth-service experiment-service telemetry-ingest-service auth-proxy experiment-portal sensor-simulator loki alloy grafana

# Статус dev-сервисов (какие контейнеры запущены)
dev-status:
	@echo "Статус dev-сервисов:"
	@docker-compose ps postgres auth-service experiment-service telemetry-ingest-service auth-proxy experiment-portal sensor-simulator loki alloy grafana 2>/dev/null || docker-compose ps

# Исправление ошибки ContainerConfig (удаление проблемных контейнеров и пересоздание)
dev-fix:
	@echo "Исправление ошибки ContainerConfig..."
	@echo "Остановка всех dev-сервисов..."
	docker-compose stop postgres auth-service experiment-service telemetry-ingest-service auth-proxy experiment-portal sensor-simulator loki alloy grafana 2>/dev/null || true
	@echo "Удаление проблемных контейнеров..."
	@docker ps -a --filter "name=experiment-service" --format "{{.ID}}" | xargs -r docker rm -f 2>/dev/null || true
	@docker ps -a --filter "name=auth-service" --format "{{.ID}}" | xargs -r docker rm -f 2>/dev/null || true
	@docker ps -a --filter "name=telemetry-ingest-service" --format "{{.ID}}" | xargs -r docker rm -f 2>/dev/null || true
	@docker ps -a --filter "name=auth-proxy" --format "{{.ID}}" | xargs -r docker rm -f 2>/dev/null || true
	@docker ps -a --filter "name=experiment-portal" --format "{{.ID}}" | xargs -r docker rm -f 2>/dev/null || true
	@docker ps -a --filter "name=sensor-simulator" --format "{{.ID}}" | xargs -r docker rm -f 2>/dev/null || true
	@docker ps -a --filter "name=grafana" --format "{{.ID}}" | xargs -r docker rm -f 2>/dev/null || true
	@docker ps -a --filter "name=loki" --format "{{.ID}}" | xargs -r docker rm -f 2>/dev/null || true
	@docker ps -a --filter "name=backend-postgres" --format "{{.ID}}" | xargs -r docker rm -f 2>/dev/null || true
	@echo "Удаление контейнеров с префиксом проекта..."
	docker-compose rm -f postgres auth-service experiment-service telemetry-ingest-service auth-proxy experiment-portal sensor-simulator loki alloy grafana 2>/dev/null || true
	@echo "Удаление volume PostgreSQL для пересоздания с правильным паролем..."
	@docker volume rm -f $${POSTGRES_DATA_VOLUME:-backend-postgres-data} 2>/dev/null || true
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
	@docker-compose stop postgres auth-service experiment-service telemetry-ingest-service auth-proxy experiment-portal sensor-simulator loki alloy grafana 2>/dev/null || true
	@cd infrastructure/logging && docker-compose -f docker-compose.yml stop loki alloy grafana 2>/dev/null || true
	@echo "Удаление контейнеров..."
	@docker-compose rm -f postgres auth-service experiment-service telemetry-ingest-service auth-proxy experiment-portal sensor-simulator loki alloy grafana 2>/dev/null || true
	@cd infrastructure/logging && docker-compose -f docker-compose.yml rm -f loki alloy grafana 2>/dev/null || true
	@echo "Удаление volumes (база данных и логи)..."
	@docker volume rm -f $${POSTGRES_DATA_VOLUME:-backend-postgres-data} 2>/dev/null || true
	@docker volume rm -f $${LOKI_DATA_VOLUME:-experiment-loki-data} 2>/dev/null || true
	@docker volume rm -f $${GRAFANA_DATA_VOLUME:-experiment-grafana-data} 2>/dev/null || true
	@echo "✅ Все данные очищены!"
	@echo ""
	@echo "💡 Для запуска сервисов заново выполните: make dev-up"

# Полная очистка всех контейнеров проекта (включая остановленные)
# ⚠️  ВНИМАНИЕ: Эта команда удалит ВСЕ контейнеры проекта, volumes и неиспользуемые образы!
dev-clean-all:
	@echo "⚠️  ВНИМАНИЕ: Эта команда удалит ВСЕ контейнеры проекта, volumes и неиспользуемые образы!"
	@echo "Остановка всех контейнеров проекта..."
	@docker-compose down 2>/dev/null || true
	@cd infrastructure/logging && docker-compose -f docker-compose.yml down 2>/dev/null || true
	@echo "Удаление всех контейнеров проекта (включая остановленные)..."
	@docker ps -a --filter "name=backend-postgres" --filter "name=auth-service" --filter "name=experiment-service" --filter "name=telemetry-ingest-service" --filter "name=auth-proxy" --filter "name=experiment-portal" --filter "name=sensor-simulator" --filter "name=loki" --filter "name=alloy" --filter "name=grafana" --format "{{.ID}}" | xargs -r docker rm -f 2>/dev/null || true
	@docker-compose rm -f 2>/dev/null || true
	@cd infrastructure/logging && docker-compose -f docker-compose.yml rm -f 2>/dev/null || true
	@echo "Удаление volumes (база данных и логи)..."
	@docker volume rm -f $${POSTGRES_DATA_VOLUME:-backend-postgres-data} 2>/dev/null || true
	@docker volume rm -f $${LOKI_DATA_VOLUME:-experiment-loki-data} 2>/dev/null || true
	@docker volume rm -f $${GRAFANA_DATA_VOLUME:-experiment-grafana-data} 2>/dev/null || true
	@echo "Удаление неиспользуемых образов проекта..."
	@docker images --filter "reference=*auth-service*" --filter "reference=*experiment-service*" --filter "reference=*telemetry-ingest-service*" --filter "reference=*auth-proxy*" --filter "reference=*experiment-portal*" --format "{{.ID}}" | xargs -r docker rmi -f 2>/dev/null || true
	@docker image prune -f >/dev/null 2>&1 || true
	@echo "✅ Полная очистка завершена!"
	@echo ""
	@echo "💡 Для запуска сервисов заново выполните: make dev-up"

# ============================================
# Миграции базы данных
# ============================================

# Применение миграций auth-service
auth-migrate:
	@echo "Применение миграций auth-service..."
	@docker-compose exec -T auth-service python -m bin.migrate --database-url "$${AUTH_DATABASE_URL:-postgresql://auth_user:auth_password@postgres:5432/auth_db}" || \
		docker-compose exec auth-service python -m bin.migrate --database-url "$${AUTH_DATABASE_URL:-postgresql://auth_user:auth_password@postgres:5432/auth_db}"
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
# Применение миграций experiment-service
experiment-migrate:
	@echo "Применение миграций experiment-service..."
	@docker-compose exec -T experiment-service python -m bin.migrate --database-url "$${EXPERIMENT_DATABASE_URL:-postgresql://experiment_user:experiment_password@postgres:5432/experiment_db}" || \
		docker-compose exec experiment-service python -m bin.migrate --database-url "$${EXPERIMENT_DATABASE_URL:-postgresql://experiment_user:experiment_password@postgres:5432/experiment_db}"
	@echo "✅ Миграции применены"

# ============================================
# Production Deploy (Yandex Cloud)
# ============================================

.PHONY: infra-init infra-plan infra-apply infra-destroy deploy deploy-manual

infra-init:
	@cd infrastructure/yandex-cloud && terraform init

infra-plan:
	@cd infrastructure/yandex-cloud && terraform plan

infra-apply:
	@cd infrastructure/yandex-cloud && terraform apply

infra-destroy:
	@echo "ВНИМАНИЕ: удалит ВСЮ инфраструктуру в Yandex Cloud!"
	@cd infrastructure/yandex-cloud && terraform destroy

deploy:
	@if [ -z "$(VM_HOST)" ] || [ -z "$(REGISTRY_ID)" ]; then \
		echo "Usage: make deploy VM_HOST=<ip> REGISTRY_ID=<id> [IMAGE_TAG=<tag>]"; \
		exit 1; \
	fi
	@./scripts/deploy.sh $(IMAGE_TAG)

.PHONY: mvp-demo-check
mvp-demo-check: dev-up auth-init experiment-migrate
	@bash scripts/mvp_demo_check.sh
