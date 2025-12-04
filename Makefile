.PHONY: test test-backend test-frontend type-check

BACKEND_DIR := backend/services/experiment-service
FRONTEND_DIR := frontend/apps/experiment-portal
OPENAPI_SPEC := $(BACKEND_DIR)/openapi/openapi.yaml

test: type-check test-backend test-frontend

type-check:
	cd $(BACKEND_DIR) && poetry run mypy src

test-backend:
	cd $(BACKEND_DIR) && poetry run pytest

test-frontend:
	cd $(FRONTEND_DIR) && npm run test

.PHONY: generate-sdk
generate-sdk:
	cd $(BACKEND_DIR) && rm -rf clients/typescript-fetch && \
		poetry run openapi-generator-cli generate \
			-i $(OPENAPI_SPEC) \
			-g typescript-fetch \
			-o clients/typescript-fetch \
			-c openapi/clients/typescript-fetch-config.yaml
	cd $(BACKEND_DIR) && rm -rf clients/cpp-restsdk && \
		poetry run openapi-generator-cli generate \
			-i $(OPENAPI_SPEC) \
			-g cpp-restsdk \
			-o clients/cpp-restsdk \
			-c openapi/clients/cpp-restsdk-config.yaml

