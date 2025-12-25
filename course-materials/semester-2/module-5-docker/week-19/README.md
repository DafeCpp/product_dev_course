# Неделя 19: CI/CD и деплой с Docker

## Цели недели
- Настроить CI/CD pipeline с Docker
- Изучить deployment strategies
- Научиться безопасно деплоить приложения
- Автоматизировать деплой через GitHub Actions
- Освоить best practices для production

## Теоретическая часть

### Что такое CI/CD?

**CI (Continuous Integration)** - автоматическая сборка и тестирование кода при каждом коммите.

**CD (Continuous Deployment/Delivery)** - автоматический деплой приложения после успешной сборки.

```
┌──────────┐
│   Git    │ Push commit
└────┬─────┘
     │
     ▼
┌─────────────────┐
│  GitHub Actions │ CI Pipeline
│  1. Checkout    │
│  2. Build       │
│  3. Test        │
│  4. Lint        │
└────┬────────────┘
     │ ✅ Success
     ▼
┌─────────────────┐
│  Build Image    │
│  Push to Registry│
└────┬────────────┘
     │
     ▼
┌─────────────────┐
│   Deploy        │ CD Pipeline
│   to Production │
└─────────────────┘
```

### CI/CD с Docker

**Преимущества:**
- ✅ Консистентные окружения
- ✅ Воспроизводимые сборки
- ✅ Легкий деплой
- ✅ Изоляция зависимостей

## GitHub Actions для Docker

### Базовая структура

```yaml
# .github/workflows/ci.yml
name: CI Pipeline

on:
  push:
    branches: [main, develop]
  pull_request:
    branches: [main]

jobs:
  test:
    runs-on: ubuntu-latest

    steps:
      - uses: actions/checkout@v3

      - name: Set up Python
        uses: actions/setup-python@v4
        with:
          python-version: '3.14'

      - name: Install dependencies
        run: |
          pip install -r requirements.txt
          pip install pytest pytest-cov

      - name: Run tests
        run: |
          pytest --cov=src --cov-report=xml

      - name: Run linters
        run: |
          pip install flake8 black mypy
          flake8 src
          black --check src
          mypy src
```

### Сборка и публикация Docker образа

```yaml
# .github/workflows/docker.yml
name: Build and Push Docker Image

on:
  push:
    branches: [main]
    tags:
      - 'v*'

env:
  REGISTRY: ghcr.io  # GitHub Container Registry
  IMAGE_NAME: ${{ github.repository }}

jobs:
  build-and-push:
    runs-on: ubuntu-latest
    permissions:
      contents: read
      packages: write

    steps:
      - name: Checkout code
        uses: actions/checkout@v3

      - name: Set up Docker Buildx
        uses: docker/setup-buildx-action@v2

      - name: Log in to Container Registry
        uses: docker/login-action@v2
        with:
          registry: ${{ env.REGISTRY }}
          username: ${{ github.actor }}
          password: ${{ secrets.GITHUB_TOKEN }}

      - name: Extract metadata
        id: meta
        uses: docker/metadata-action@v4
        with:
          images: ${{ env.REGISTRY }}/${{ env.IMAGE_NAME }}
          tags: |
            type=ref,event=branch
            type=ref,event=pr
            type=semver,pattern={{version}}
            type=semver,pattern={{major}}.{{minor}}
            type=sha

      - name: Build and push
        uses: docker/build-push-action@v4
        with:
          context: .
          push: true
          tags: ${{ steps.meta.outputs.tags }}
          labels: ${{ steps.meta.outputs.labels }}
          cache-from: type=gha
          cache-to: type=gha,mode=max
```

### Полный CI/CD Pipeline

```yaml
# .github/workflows/deploy.yml
name: CI/CD Pipeline

on:
  push:
    branches: [main, develop]
  pull_request:
    branches: [main]

env:
  REGISTRY: ghcr.io
  IMAGE_NAME: ${{ github.repository }}

jobs:
  # Job 1: Tests
  test:
    runs-on: ubuntu-latest

    services:
      postgres:
        image: postgres:15-alpine
        env:
          POSTGRES_USER: test_user
          POSTGRES_PASSWORD: test_pass
          POSTGRES_DB: testdb
        options: >-
          --health-cmd pg_isready
          --health-interval 10s
          --health-timeout 5s
          --health-retries 5
        ports:
          - 5432:5432

      redis:
        image: redis:7-alpine
        options: >-
          --health-cmd "redis-cli ping"
          --health-interval 10s
          --health-timeout 5s
          --health-retries 5
        ports:
          - 6379:6379

    steps:
      - uses: actions/checkout@v3

      - name: Set up Python
        uses: actions/setup-python@v4
        with:
          python-version: '3.14'
          cache: 'pip'

      - name: Install dependencies
        run: |
          pip install -r requirements.txt
          pip install pytest pytest-cov pytest-asyncio

      - name: Run tests
        env:
          DATABASE_URL: postgresql://test_user:test_pass@localhost:5432/testdb
          REDIS_URL: redis://localhost:6379/0
        run: |
          pytest --cov=src --cov-report=xml --cov-report=html

      - name: Upload coverage
        uses: codecov/codecov-action@v3
        with:
          file: ./coverage.xml
          flags: unittests
          name: codecov-umbrella

      - name: Run linters
        run: |
          pip install flake8 black mypy
          flake8 src --max-line-length=100 --extend-ignore=E203
          black --check src
          mypy src --ignore-missing-imports

  # Job 2: Build Docker Image
  build:
    runs-on: ubuntu-latest
    needs: test
    if: github.event_name == 'push'

    steps:
      - uses: actions/checkout@v3

      - name: Set up Docker Buildx
        uses: docker/setup-buildx-action@v2

      - name: Log in to Container Registry
        uses: docker/login-action@v2
        with:
          registry: ${{ env.REGISTRY }}
          username: ${{ github.actor }}
          password: ${{ secrets.GITHUB_TOKEN }}

      - name: Extract metadata
        id: meta
        uses: docker/metadata-action@v4
        with:
          images: ${{ env.REGISTRY }}/${{ env.IMAGE_NAME }}
          tags: |
            type=ref,event=branch
            type=sha,prefix={{branch}}-
            type=raw,value=latest,enable={{is_default_branch}}

      - name: Build and push
        uses: docker/build-push-action@v4
        with:
          context: .
          push: true
          tags: ${{ steps.meta.outputs.tags }}
          labels: ${{ steps.meta.outputs.labels }}
          cache-from: type=gha
          cache-to: type=gha,mode=max
          platforms: linux/amd64

  # Job 3: Security Scan
  security:
    runs-on: ubuntu-latest
    needs: build
    if: github.event_name == 'push' && github.ref == 'refs/heads/main'

    steps:
      - uses: actions/checkout@v3

      - name: Run Trivy vulnerability scanner
        uses: aquasecurity/trivy-action@master
        with:
          image-ref: ${{ env.REGISTRY }}/${{ env.IMAGE_NAME }}:main
          format: 'sarif'
          output: 'trivy-results.sarif'

      - name: Upload Trivy results to GitHub Security
        uses: github/codeql-action/upload-sarif@v2
        with:
          sarif_file: 'trivy-results.sarif'

  # Job 4: Deploy
  deploy:
    runs-on: ubuntu-latest
    needs: [build, security]
    if: github.event_name == 'push' && github.ref == 'refs/heads/main'
    environment:
      name: production
      url: https://api.example.com

    steps:
      - name: Deploy to production
        run: |
          echo "Deploying ${{ env.REGISTRY }}/${{ env.IMAGE_NAME }}:main"
          # Здесь команды для деплоя (SSH, kubectl, и т.д.)
```

## Deployment Strategies

### 1. Blue-Green Deployment

**Концепция:**
- Два идентичных окружения (Blue и Green)
- Одно активно, другое простаивает
- Новую версию деплоим на неактивное
- Переключаем трафик после проверки

```
┌─────────────┐
│   Users     │
└──────┬──────┘
       │
       ▼
┌─────────────┐
│Load Balancer│
└───┬─────┬───┘
    │     │
    ▼     ▼
┌─────┐ ┌─────┐
│Blue │ │Green│ (new version)
│ v1  │ │ v2  │
└─────┘ └─────┘
```

**Преимущества:**
- ✅ Нулевое downtime
- ✅ Быстрый rollback
- ✅ Тестирование перед переключением

**Недостатки:**
- ❌ Удвоенное потребление ресурсов
- ❌ Сложность синхронизации данных

### 2. Rolling Deployment

**Концепция:**
- Постепенная замена инстансов
- Старые и новые работают одновременно
- Load balancer распределяет трафик

```
Старые инстансы → Новые инстансы
Instance 1 (v1) → Instance 1 (v2) ✅
Instance 2 (v1) → Instance 2 (v2) ✅
Instance 3 (v1) → Instance 3 (v2) ✅
```

**Преимущества:**
- ✅ Минимальное потребление ресурсов
- ✅ Постепенный rollout
- ✅ Возможность остановить деплой

**Недостатки:**
- ⚠️ Временная несовместимость версий
- ⚠️ Сложнее rollback

### 3. Canary Deployment

**Концепция:**
- Новая версия разворачивается на малой части инстансов
- Мониторинг метрик
- Постепенное увеличение трафика на новую версию

```
100% трафика → 90% v1, 10% v2 → 50% v1, 50% v2 → 100% v2
```

**Преимущества:**
- ✅ Минимальный риск
- ✅ Тестирование на реальном трафике
- ✅ Возможность быстрого отката

### 4. Recreate Deployment

**Концепция:**
- Останавливаем все старые инстансы
- Разворачиваем новые
- Простой подход

**Преимущества:**
- ✅ Простота
- ✅ Нет проблем с совместимостью версий

**Недостатки:**
- ❌ Downtime во время деплоя
- ❌ Нет возможности rollback

## Production Setup

### Безопасность

#### 1. Не храните секреты в образах

**❌ ПЛОХО:**
```dockerfile
ENV SECRET_KEY=my_secret_key
ENV DATABASE_PASSWORD=password123
```

**✅ ПРАВИЛЬНО:**
```yaml
# docker-compose.prod.yml
services:
  api:
    environment:
      - SECRET_KEY=${SECRET_KEY}  # Из переменных окружения
      - DATABASE_PASSWORD=${DB_PASSWORD}
```

#### 2. Используйте secrets

**Docker Secrets:**
```yaml
services:
  api:
    secrets:
      - db_password
      - jwt_secret

secrets:
  db_password:
    external: true
  jwt_secret:
    file: ./secrets/jwt_secret.txt
```

#### 3. Non-root user

```dockerfile
# Всегда используйте non-root user
RUN useradd -m -u 1000 appuser
USER appuser
```

#### 4. Минимизация образа

```dockerfile
# Multi-stage build
FROM python:3.14-slim as builder
# ... build ...

FROM python:3.14-slim
COPY --from=builder /root/.local /root/.local
# Только runtime, без build tools
```

#### 5. Security scanning

```yaml
# В CI/CD
- name: Run Trivy scan
  uses: aquasecurity/trivy-action@master
  with:
    image-ref: myapp:latest
    severity: 'CRITICAL,HIGH'
```

### Мониторинг и логирование

```yaml
services:
  api:
    logging:
      driver: "json-file"
      options:
        max-size: "10m"
        max-file: "3"
    healthcheck:
      test: ["CMD", "curl", "-f", "http://localhost:8000/health"]
      interval: 30s
      timeout: 10s
      retries: 3
```

### Resource Limits

```yaml
services:
  api:
    deploy:
      resources:
        limits:
          cpus: '1.0'
          memory: 512M
        reservations:
          cpus: '0.5'
          memory: 256M
    restart: always
```

### Networking

```yaml
networks:
  frontend:
    driver: bridge
  backend:
    driver: bridge
    internal: true  # Без доступа наружу
```

## Автоматический деплой

### Пример: Deploy на VPS через SSH

```yaml
# .github/workflows/deploy.yml
name: Deploy to Production

on:
  push:
    branches: [main]

jobs:
  deploy:
    runs-on: ubuntu-latest
    environment: production

    steps:
      - uses: actions/checkout@v3

      - name: Build image
        uses: docker/build-push-action@v4
        with:
          context: .
          push: false
          tags: myapp:latest
          load: true

      - name: Deploy via SSH
        uses: appleboy/scp-action@master
        with:
          host: ${{ secrets.HOST }}
          username: ${{ secrets.USERNAME }}
          key: ${{ secrets.SSH_KEY }}
          source: "docker-compose.prod.yml"
          target: "/opt/myapp/"

      - name: Execute deploy commands
        uses: appleboy/ssh-action@master
        with:
          host: ${{ secrets.HOST }}
          username: ${{ secrets.USERNAME }}
          key: ${{ secrets.SSH_KEY }}
          script: |
            cd /opt/myapp
            docker-compose pull
            docker-compose up -d --no-deps --build api
            docker-compose exec -T api python manage.py migrate
```

### Пример: Deploy в Docker Swarm

```yaml
name: Deploy to Swarm

on:
  push:
    branches: [main]

jobs:
  deploy:
    runs-on: ubuntu-latest

    steps:
      - uses: actions/checkout@v3

      - name: Deploy to Swarm
        uses: docker/build-push-action@v4
        with:
          context: .
          push: true
          tags: registry.example.com/myapp:latest

      - name: SSH and update service
        uses: appleboy/ssh-action@master
        with:
          host: ${{ secrets.SWARM_MANAGER }}
          username: ${{ secrets.USERNAME }}
          key: ${{ secrets.SSH_KEY }}
          script: |
            docker service update --image registry.example.com/myapp:latest myapp
```

## Docker Registry

### GitHub Container Registry (ghcr.io)

**Публикация:**
```bash
# Логин
echo $GITHUB_TOKEN | docker login ghcr.io -u USERNAME --password-stdin

# Тег
docker tag myapp:latest ghcr.io/username/myapp:latest

# Публикация
docker push ghcr.io/username/myapp:latest
```

**Использование в docker-compose:**
```yaml
services:
  api:
    image: ghcr.io/username/myapp:latest
    pull_policy: always
```

### Docker Hub

```yaml
- name: Login to Docker Hub
  uses: docker/login-action@v2
  with:
    username: ${{ secrets.DOCKER_USERNAME }}
    password: ${{ secrets.DOCKER_PASSWORD }}
```

### Private Registry

```yaml
- name: Login to Private Registry
  uses: docker/login-action@v2
  with:
    registry: registry.example.com
    username: ${{ secrets.REGISTRY_USERNAME }}
    password: ${{ secrets.REGISTRY_PASSWORD }}
```

## Best Practices для Production

### 1. Используйте теги правильно

```yaml
tags: |
  type=ref,event=branch        # main, develop
  type=sha,prefix={{branch}}-  # main-abc123
  type=semver,pattern={{version}}  # v1.2.3
  type=raw,value=latest,enable={{is_default_branch}}
```

### 2. Кэширование layers

```yaml
cache-from: type=gha
cache-to: type=gha,mode=max
```

### 3. Multi-platform builds

```yaml
platforms: linux/amd64,linux/arm64
```

### 4. Health checks

```yaml
services:
  api:
    healthcheck:
      test: ["CMD", "curl", "-f", "http://localhost:8000/health"]
      interval: 30s
      timeout: 10s
      retries: 3
```

### 5. Graceful shutdown

```python
# В приложении
import signal
import asyncio

async def shutdown(signal, app):
    """Graceful shutdown."""
    print(f"Received {signal.name}")
    app['shutdown'] = True

    # Закрываем connections
    await app['db_pool'].close()
    await app['redis'].close()

    # Даем время завершить текущие запросы
    await asyncio.sleep(2)

# Регистрация handlers
for sig in (signal.SIGTERM, signal.SIGINT):
    loop.add_signal_handler(sig, lambda: asyncio.create_task(shutdown(sig, app)))
```

### 6. Logging

```yaml
logging:
  driver: "json-file"
  options:
    max-size: "10m"
    max-file: "3"
    labels: "production"
```

## Rollback Strategy

### Автоматический rollback при ошибках

```yaml
deploy:
  restart: always
  rollback_config:
    parallelism: 1
    delay: 10s
    failure_action: rollback
    monitor: 60s
```

### Ручной rollback

```bash
# Откат на предыдущую версию
docker service update --rollback myapp

# Или через docker-compose
docker-compose pull myapp:previous-version
docker-compose up -d
```

## Мониторинг деплоя

### Health checks в CI/CD

```yaml
- name: Wait for health check
  run: |
    timeout 300 bash -c 'until curl -f http://api.example.com/health; do sleep 5; done'

- name: Run smoke tests
  run: |
    pytest tests/smoke/
```

### Метрики деплоя

```yaml
- name: Send deployment notification
  uses: 8398a7/action-slack@v3
  with:
    status: custom
    custom_payload: |
      {
        "text": "Deployment successful",
        "attachments": [{
          "color": "good",
          "fields": [{
            "title": "Version",
            "value": "${{ github.sha }}",
            "short": true
          }]
        }]
      }
```

## Дополнительные материалы

### Полезные ссылки
- [GitHub Actions Documentation](https://docs.github.com/en/actions)
- [Docker Build Push Action](https://github.com/docker/build-push-action)
- [Deployment Strategies](https://martinfowler.com/bliki/BlueGreenDeployment.html)

### Инструменты
- [Trivy](https://github.com/aquasecurity/trivy) - Security scanning
- [Hadolint](https://github.com/hadolint/hadolint) - Dockerfile linter
- [Docker Scout](https://docs.docker.com/scout/) - Image analysis

### Статьи
- [CI/CD Best Practices](https://www.docker.com/blog/best-practices-for-using-docker-for-ci-cd/)
- [Deployment Patterns](https://thenewstack.io/deployment-patterns/)

## Вопросы для самопроверки

1. В чем разница между CI и CD?
2. Какая deployment strategy лучше для zero-downtime?
3. Как обеспечить безопасность при деплое?
4. Зачем нужны health checks в деплое?
5. Как организовать rollback при проблемах?

## Следующая неделя

На [Неделе 20](../../module-6-microservices/week-20/README.md) начнем модуль по микросервисам: изучим монолит vs микросервисы и DDD! 🚀

---

**Удачи с CI/CD! 🚀**

