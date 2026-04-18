ARG BACKEND_BASE_IMAGE=backend-base:local
FROM ${BACKEND_BASE_IMAGE} AS production

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1

WORKDIR /app

# Copy backend_common directory first (needed for backend-common dependency)
COPY common ./common

WORKDIR /app/services/experiment-service

# Copy dependency files (for better layer caching)
COPY services/experiment-service/pyproject.toml services/experiment-service/poetry.lock services/experiment-service/README.md ./

# Install dependencies (including dev dependencies for hot-reload support)
# In production, this can be optimized by using --no-dev flag
RUN poetry install --no-root

# Copy source code
COPY services/experiment-service/src ./src
COPY services/experiment-service/bin ./bin
COPY services/experiment-service/migrations ./migrations

# Set PYTHONPATH to include src directory
ENV PYTHONPATH=/app/services/experiment-service/src

EXPOSE 8002

# Default command (can be overridden in docker-compose)
CMD ["python", "-m", "experiment_service.main"]

