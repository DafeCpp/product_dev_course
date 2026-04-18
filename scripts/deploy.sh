#!/usr/bin/env bash
# ============================================
# Ручной деплой на VM (без CI/CD)
# ============================================
#
# Использование:
#   ./scripts/deploy.sh [IMAGE_TAG]
#
# Переменные окружения:
#   VM_HOST     - IP или hostname VM (обязательно)
#   VM_USER     - SSH пользователь (по умолчанию: deploy)
#   REGISTRY_ID - ID Container Registry в Yandex Cloud (обязательно)
#
# Примеры:
#   VM_HOST=84.201.xxx.xxx REGISTRY_ID=crp... ./scripts/deploy.sh
#   VM_HOST=84.201.xxx.xxx REGISTRY_ID=crp... ./scripts/deploy.sh abc1234
#
set -euo pipefail

IMAGE_TAG="${1:-$(git rev-parse --short HEAD)}"
VM_USER="${VM_USER:-deploy}"
CR_REGISTRY="cr.yandex/${REGISTRY_ID}"
APP_DIR="/opt/experiment-tracking"

if [[ -z "${VM_HOST:-}" ]]; then
  echo "ERROR: VM_HOST is not set"
  echo "Usage: VM_HOST=<ip> REGISTRY_ID=<id> $0 [IMAGE_TAG]"
  exit 1
fi

if [[ -z "${REGISTRY_ID:-}" ]]; then
  echo "ERROR: REGISTRY_ID is not set"
  echo "Usage: VM_HOST=<ip> REGISTRY_ID=<id> $0 [IMAGE_TAG]"
  exit 1
fi

echo "==> Deploying tag: ${IMAGE_TAG}"
echo "==> Target: ${VM_USER}@${VM_HOST}"
echo "==> Registry: ${CR_REGISTRY}"
echo ""

SERVICES=(
  "auth-service"
  "experiment-service"
  "telemetry-ingest-service"
  "auth-proxy"
  "experiment-portal"
  "sensor-simulator"
)

# --- 1. Собираем и пушим образы ---
echo "==> Building and pushing images..."

for SVC in "${SERVICES[@]}"; do
  case "$SVC" in
    auth-service|experiment-service|telemetry-ingest-service)
      CONTEXT="./projects/backend"
      DOCKERFILE="services/${SVC}/Dockerfile"
      ;;
    auth-proxy|experiment-portal|sensor-simulator)
      CONTEXT="./projects/frontend/apps/${SVC}"
      DOCKERFILE="Dockerfile"
      ;;
  esac

  echo "  Building ${SVC}..."
  docker build \
    -t "${CR_REGISTRY}/${SVC}:${IMAGE_TAG}" \
    -t "${CR_REGISTRY}/${SVC}:latest" \
    -f "${CONTEXT}/${DOCKERFILE}" \
    --target production \
    "${CONTEXT}"

  echo "  Pushing ${SVC}..."
  docker push "${CR_REGISTRY}/${SVC}:${IMAGE_TAG}"
  docker push "${CR_REGISTRY}/${SVC}:latest"
done

# --- 2. Копируем файлы на VM ---
echo "==> Syncing files to VM..."
scp docker-compose.prod.yml "${VM_USER}@${VM_HOST}:${APP_DIR}/"
scp -r infrastructure/logging/ "${VM_USER}@${VM_HOST}:${APP_DIR}/infrastructure/"

# --- 3. Деплоим на VM ---
echo "==> Deploying on VM..."
ssh "${VM_USER}@${VM_HOST}" bash -s <<REMOTE
set -euo pipefail
cd ${APP_DIR}

# Обновляем тег
sed -i "s|^IMAGE_TAG=.*|IMAGE_TAG=${IMAGE_TAG}|" .env
sed -i "s|^CR_REGISTRY=.*|CR_REGISTRY=${CR_REGISTRY}|" .env

# Авторизуемся в CR
yc container registry configure-docker

# Пуллим и рестартуем
docker compose -f docker-compose.prod.yml pull
docker compose -f docker-compose.prod.yml up -d --remove-orphans

echo ""
echo "==> Service status:"
docker compose -f docker-compose.prod.yml ps

# Чистим старые образы
docker image prune -f
REMOTE

echo ""
echo "==> Deploy complete! Tag: ${IMAGE_TAG}"
echo "==> App URL: http://${VM_HOST}/"
