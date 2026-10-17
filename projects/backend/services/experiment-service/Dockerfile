ARG BACKEND_BASE_IMAGE=backend-base:local
ARG PYTHON_IMAGE=python:3.14-slim

# ---- builder: ставим prod-зависимости в venv (без dev-группы) ----
FROM ${BACKEND_BASE_IMAGE} AS builder

RUN python -m venv /opt/venv
ENV VIRTUAL_ENV=/opt/venv \
    PATH=/opt/venv/bin:$PATH

WORKDIR /app

# backend_common нужен как path-зависимость
COPY common ./common

WORKDIR /app/services/experiment-service

# Файлы зависимостей отдельно для кэша слоёв
COPY services/experiment-service/pyproject.toml services/experiment-service/poetry.lock services/experiment-service/README.md ./

RUN poetry install --no-root --without dev

# ---- production: только runtime, без gcc/poetry/dev-зависимостей, не root ----
FROM ${PYTHON_IMAGE} AS production

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    VIRTUAL_ENV=/opt/venv \
    PATH=/opt/venv/bin:$PATH

RUN apt-get update && apt-get install -y --no-install-recommends libpq5 \
    && rm -rf /var/lib/apt/lists/* \
    && groupadd --system --gid 10001 app \
    && useradd --system --uid 10001 --gid app --home-dir /app --shell /usr/sbin/nologin app \
    && mkdir -p /var/cache/config-service \
    && chown app:app /var/cache/config-service

COPY --from=builder /opt/venv /opt/venv

WORKDIR /app
COPY --chown=app:app common ./common

WORKDIR /app/services/experiment-service

COPY --chown=app:app services/experiment-service/src ./src
COPY --chown=app:app services/experiment-service/bin ./bin
COPY --chown=app:app services/experiment-service/migrations ./migrations

ENV PYTHONPATH=/app/services/experiment-service/src:/app/common/src

USER app

EXPOSE 8002

CMD ["python", "-m", "experiment_service.main"]
