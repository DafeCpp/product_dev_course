ARG BACKEND_BASE_IMAGE=backend-base:local
FROM ${BACKEND_BASE_IMAGE} AS production

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1

WORKDIR /app

# Copy backend_common directory first (needed for backend-common dependency)
COPY common ./common

WORKDIR /app/services/config-service

# Copy dependency files (for better layer caching)
COPY services/config-service/pyproject.toml services/config-service/poetry.lock services/config-service/README.md ./

# Install dependencies
RUN poetry install --no-root

# Copy source code
COPY services/config-service/src ./src
COPY services/config-service/bin ./bin
COPY services/config-service/migrations ./migrations

# Set PYTHONPATH to include src directory and backend-common
ENV PYTHONPATH=/app/services/config-service/src:/app/common/src

EXPOSE 8004

CMD ["python", "-m", "config_service.main"]
