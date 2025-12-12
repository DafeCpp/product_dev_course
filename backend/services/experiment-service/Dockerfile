FROM python:3.11-slim AS base

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1

WORKDIR /app

# Install system dependencies required for building Python packages
# postgresql-dev provides pg_config needed for psycopg2
# gcc, g++, python3-dev needed for compiling C/Cython extensions (asyncpg)
# libpq-dev provides PostgreSQL client libraries
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

# Copy dependency files first (for better layer caching)
COPY pyproject.toml README.md ./

# Install dependencies (including dev dependencies for hot-reload support)
# In production, this can be optimized by using --no-dev flag
RUN poetry install --no-root

# Copy source code
COPY src ./src

# Set PYTHONPATH to include src directory
ENV PYTHONPATH=/app/src

EXPOSE 8002

# Default command (can be overridden in docker-compose)
CMD ["python", "-m", "experiment_service.main"]

