ARG BACKEND_BASE_IMAGE=backend-base:local
FROM ${BACKEND_BASE_IMAGE} AS production

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1

WORKDIR /app

# Copy backend_common directory first (needed for backend-common dependency)
COPY common ./common

WORKDIR /app/services/script-service

# Copy dependency files (for better layer caching)
COPY services/script-service/pyproject.toml services/script-service/poetry.lock services/script-service/README.md ./

# Install dependencies (including dev dependencies for hot-reload support)
RUN poetry install --no-root

# Copy source code
COPY services/script-service/src ./src
COPY services/script-service/migrations ./migrations

# Set PYTHONPATH to include src directory
ENV PYTHONPATH=/app/services/script-service/src

EXPOSE 8004

# Default command (can be overridden in docker-compose)
CMD ["python", "-m", "script_service.main"]
