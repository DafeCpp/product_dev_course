#!/usr/bin/env bash
# ============================================
# Первоначальная настройка VM в Yandex Cloud
# ============================================
#
# Запускать один раз после terraform apply:
#   scp scripts/setup-vm.sh deploy@<VM_IP>:~
#   ssh deploy@<VM_IP> 'bash ~/setup-vm.sh'
#
set -euo pipefail

APP_DIR="/opt/experiment-tracking"

echo "==> Создаём рабочую директорию..."
sudo mkdir -p "$APP_DIR"
sudo chown "$(whoami):$(whoami)" "$APP_DIR"

echo "==> Устанавливаем Docker и Docker Compose (если не установлены)..."
if ! command -v docker &>/dev/null; then
  curl -fsSL https://get.docker.com | sudo sh
  sudo usermod -aG docker "$(whoami)"
  echo "Docker установлен. Перезайдите в SSH для применения групп."
fi

if ! docker compose version &>/dev/null; then
  echo "Docker Compose plugin не найден — устанавливаем..."
  sudo apt-get update
  sudo apt-get install -y docker-compose-plugin
fi

echo "==> Устанавливаем Yandex Cloud CLI..."
if ! command -v yc &>/dev/null; then
  curl -sSL https://storage.yandexcloud.net/yandexcloud-yc/install.sh | bash
  export PATH="$HOME/yandex-cloud/bin:$PATH"
fi

echo "==> Настраиваем авторизацию в Container Registry..."
yc container registry configure-docker

echo "==> Загружаем SSL-сертификат Yandex для PostgreSQL..."
mkdir -p "$APP_DIR/certs"
wget -q "https://storage.yandexcloud.net/cloud-certs/CA.pem" -O "$APP_DIR/certs/yandex-ca.pem"

echo "==> Создаём структуру директорий..."
mkdir -p "$APP_DIR/infrastructure/logging/grafana/provisioning"
mkdir -p "$APP_DIR/infrastructure/logging/grafana/dashboards"

cat <<'INSTRUCTIONS'

============================================
  VM готова! Следующие шаги:
============================================

1. Скопируйте файлы на сервер:

   scp docker-compose.prod.yml deploy@<VM_IP>:/opt/experiment-tracking/
   scp env.production.example deploy@<VM_IP>:/opt/experiment-tracking/.env
   scp -r infrastructure/logging/ deploy@<VM_IP>:/opt/experiment-tracking/infrastructure/

2. Отредактируйте .env на сервере:

   ssh deploy@<VM_IP>
   nano /opt/experiment-tracking/.env

3. Запустите:

   cd /opt/experiment-tracking
   docker compose -f docker-compose.prod.yml pull
   docker compose -f docker-compose.prod.yml up -d

4. Проверьте:

   docker compose -f docker-compose.prod.yml ps
   curl http://localhost/

INSTRUCTIONS
