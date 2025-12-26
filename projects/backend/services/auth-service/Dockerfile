FROM python:3.14-slim AS base

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1

WORKDIR /app

# Install system dependencies required for building Python packages
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

# Install dependencies
RUN poetry install --no-root

# Copy source code
COPY src ./src
COPY bin ./bin

# Set PYTHONPATH to include src directory
ENV PYTHONPATH=/app/src

EXPOSE 8001

# Default command
CMD ["python", "-m", "auth_service.main"]

