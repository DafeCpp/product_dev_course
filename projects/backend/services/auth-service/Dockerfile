ARG BACKEND_BASE_IMAGE=backend-base:local
FROM ${BACKEND_BASE_IMAGE} AS production

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1

WORKDIR /app

# Copy backend_common directory first (needed for backend-common dependency)
COPY common ./common

WORKDIR /app/services/auth-service

# Copy dependency files (for better layer caching)
COPY services/auth-service/pyproject.toml services/auth-service/poetry.lock services/auth-service/README.md ./

# Install dependencies
RUN poetry install --no-root

# Copy source code
COPY services/auth-service/src ./src
COPY services/auth-service/bin ./bin
COPY services/auth-service/migrations ./migrations

# Set PYTHONPATH to include src directory
ENV PYTHONPATH=/app/services/auth-service/src

EXPOSE 8001

# Default command
CMD ["python", "-m", "auth_service.main"]

