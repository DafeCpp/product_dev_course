FROM python:3.14-slim AS base

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1

WORKDIR /app

# Install system dependencies required for building Python packages (asyncpg)
RUN apt-get update && apt-get install -y --no-install-recommends \
    postgresql-client \
    libpq-dev \
    gcc \
    g++ \
    python3-dev \
    && rm -rf /var/lib/apt/lists/*

# Install poetry
RUN pip install --upgrade pip \
    && pip install poetry \
    && poetry config virtualenvs.create false

# Copy backend_common directory first (needed for backend-common dependency)
COPY common ./common
# Copy dependency files (for better layer caching)
COPY services/telemetry-ingest-service/pyproject.toml services/telemetry-ingest-service/README.md ./

# Fix path to backend-common in pyproject.toml (relative path changes after copying to /app)
RUN sed -i 's|path = \"../../common\"|path = \"./common\"|g' pyproject.toml

# Install dependencies
RUN poetry install --no-root

# Copy source code
COPY services/telemetry-ingest-service/src ./src
COPY services/telemetry-ingest-service/openapi ./openapi

# Set PYTHONPATH to include src directory
ENV PYTHONPATH=/app/src

EXPOSE 8003

CMD ["python", "-m", "telemetry_ingest_service.main"]

