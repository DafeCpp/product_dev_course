#!/usr/bin/env bash
# ============================================
# Откат деплоя на предыдущий тег образа
# ============================================
#
# Использование:
#   ./scripts/rollback.sh <IMAGE_TAG>
#
# Переменные окружения:
#   VM_HOST     - IP или hostname VM (обязательно)
#   VM_USER     - SSH пользователь (по умолчанию: deploy)
#   REGISTRY_ID - ID Container Registry (обязательно)
#
# Пример:
#   VM_HOST=84.201.xxx.xxx REGISTRY_ID=crp... ./scripts/rollback.sh abc1234
#
set -euo pipefail

IMAGE_TAG="${1:-}"
VM_USER="${VM_USER:-deploy}"
APP_DIR="/opt/experiment-tracking"

if [[ -z "${IMAGE_TAG}" ]]; then
  echo "ERROR: IMAGE_TAG is required"
  echo "Usage: VM_HOST=<ip> REGISTRY_ID=<id> $0 <IMAGE_TAG>"
  echo ""
  echo "Available tags can be found with:"
  echo "  yc container image list --repository-name <registry-id>/auth-service"
  exit 1
fi

if [[ -z "${VM_HOST:-}" ]]; then
  echo "ERROR: VM_HOST is not set"
  exit 1
fi

if [[ -z "${REGISTRY_ID:-}" ]]; then
  echo "ERROR: REGISTRY_ID is not set"
  exit 1
fi

CR_REGISTRY="cr.yandex/${REGISTRY_ID}"

echo "==> Rolling back to tag: ${IMAGE_TAG}"
echo "==> Target: ${VM_USER}@${VM_HOST}"
echo ""

read -r -p "Are you sure you want to rollback to ${IMAGE_TAG}? [y/N] " confirm
if [[ "${confirm}" != "y" && "${confirm}" != "Y" ]]; then
  echo "Rollback cancelled."
  exit 0
fi

ssh "${VM_USER}@${VM_HOST}" bash -s <<REMOTE
set -euo pipefail
cd ${APP_DIR}

echo "==> Current IMAGE_TAG:"
grep "^IMAGE_TAG=" .env || echo "(not set)"

echo ""
echo "==> Switching to: ${IMAGE_TAG}"
sed -i "s|^IMAGE_TAG=.*|IMAGE_TAG=${IMAGE_TAG}|" .env

echo "==> Pulling images for tag ${IMAGE_TAG}..."
docker compose -f docker-compose.prod.yml pull

echo "==> Restarting services..."
docker compose -f docker-compose.prod.yml up -d --remove-orphans

sleep 10

echo ""
echo "==> Service status after rollback:"
docker compose -f docker-compose.prod.yml ps

echo ""
echo "==> Rollback to ${IMAGE_TAG} complete!"
REMOTE

echo ""
echo "==> Verifying health..."
for i in $(seq 1 6); do
  STATUS=$(curl -s -o /dev/null -w "%{http_code}" "http://${VM_HOST}/" || true)
  if [ "$STATUS" = "200" ]; then
    echo "==> Health check passed (attempt $i)"
    echo "==> Rollback successful! App: http://${VM_HOST}/"
    exit 0
  fi
  echo "Attempt $i: status=$STATUS, retrying in 10s..."
  sleep 10
done

echo "WARNING: Health check did not pass after rollback. Check logs:"
echo "  ssh ${VM_USER}@${VM_HOST} 'cd ${APP_DIR} && docker compose -f docker-compose.prod.yml logs --tail=50'"
exit 1
