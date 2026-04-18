ARG BACKEND_BASE_IMAGE=backend-base:local
FROM ${BACKEND_BASE_IMAGE} AS production

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1

WORKDIR /app

# Copy backend_common directory first (needed for backend-common dependency)
COPY common ./common

WORKDIR /app/services/telemetry-ingest-service

# Copy dependency files (for better layer caching)
COPY services/telemetry-ingest-service/pyproject.toml services/telemetry-ingest-service/poetry.lock services/telemetry-ingest-service/README.md ./

# Install dependencies
RUN poetry install --no-root

# Copy source code
COPY services/telemetry-ingest-service/src ./src
COPY services/telemetry-ingest-service/openapi ./openapi

# Set PYTHONPATH to include src directory
ENV PYTHONPATH=/app/services/telemetry-ingest-service/src

EXPOSE 8003

CMD ["python", "-m", "telemetry_ingest_service.main"]

