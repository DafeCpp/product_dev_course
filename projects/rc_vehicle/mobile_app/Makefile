.PHONY: help deps test analyze check build-apk build-apk-release clean run icons

FLUTTER ?= flutter
DART ?= dart

help: ## Show this help
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | sort | \
		awk 'BEGIN {FS = ":.*?## "}; {printf "  \033[36m%-20s\033[0m %s\n", $$1, $$2}'

# --- Setup ---

deps: ## Install/update dependencies
	$(FLUTTER) pub get

# --- Quality ---

test: ## Run all tests
	$(FLUTTER) test

test-v: ## Run tests with verbose output
	$(FLUTTER) test --reporter expanded

test-coverage: ## Run tests with coverage report
	$(FLUTTER) test --coverage
	@echo "Coverage report: coverage/lcov.info"
	@command -v genhtml >/dev/null 2>&1 && \
		genhtml coverage/lcov.info -o coverage/html && \
		echo "HTML report: coverage/html/index.html" || \
		echo "Install lcov for HTML report: sudo apt install lcov"

analyze: ## Run dart analyzer
	$(FLUTTER) analyze

format: ## Format all Dart files
	$(DART) format lib test

format-check: ## Check formatting without modifying files
	$(DART) format --set-exit-if-changed lib test

check: analyze format-check test ## Run all checks (analyze + format + test)

# --- Build ---

build-apk: ## Build debug APK
	$(FLUTTER) build apk --debug

build-apk-release: ## Build release APK
	$(FLUTTER) build apk --release

build-bundle: ## Build release AAB (for Play Store)
	$(FLUTTER) build appbundle --release

# --- Run ---

run: ## Run on connected device
	$(FLUTTER) run

run-release: ## Run release build on connected device
	$(FLUTTER) run --release

devices: ## List connected devices
	$(FLUTTER) devices

# --- Utilities ---

icons: ## Generate app icons (requires flutter_launcher_icons in dev_dependencies)
	$(DART) run flutter_launcher_icons

clean: ## Clean build artifacts
	$(FLUTTER) clean
	rm -rf coverage

rebuild: clean deps ## Clean and reinstall everything
