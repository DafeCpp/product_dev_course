.PHONY: test test-backend test-frontend type-check

BACKEND_DIR := backend/services/experiment-service
FRONTEND_DIR := frontend/apps/experiment-portal

test: type-check test-backend test-frontend

type-check:
	cd $(BACKEND_DIR) && poetry run mypy src

test-backend:
	cd $(BACKEND_DIR) && poetry run pytest

test-frontend:
	cd $(FRONTEND_DIR) && npm run test

