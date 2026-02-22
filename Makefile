# Spectra — Developer Makefile
# Usage: make [target] [BUILD_TYPE=Release|Debug] [BUILD_DIR=build]

BUILD_TYPE ?= Release
BUILD_DIR  ?= build
PREFIX     ?= /usr/local
NPROC      := $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

.PHONY: configure build test install package clean format check-format \
        pip-dev pip-test help

help: ## Show this help
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | \
		awk 'BEGIN {FS = ":.*?## "}; {printf "  \033[36m%-16s\033[0m %s\n", $$1, $$2}'

# ─── C++ ─────────────────────────────────────────────────────────────────────

configure: ## Configure CMake build
	cmake -B $(BUILD_DIR) \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
		-DSPECTRA_BUILD_TESTS=ON \
		-DSPECTRA_BUILD_EXAMPLES=ON

build: configure ## Build the project
	cmake --build $(BUILD_DIR) -j$(NPROC)

test: build ## Run all unit tests
	cd $(BUILD_DIR) && ctest --output-on-failure -j$(NPROC)

install: build ## Install to PREFIX (default /usr/local)
	cmake --install $(BUILD_DIR) --prefix $(PREFIX)

package: build ## Create packages (deb, rpm, tgz) via CPack
	cd $(BUILD_DIR) && cpack -G TGZ && cpack -G DEB || true

clean: ## Remove build directory
	rm -rf $(BUILD_DIR)

# ─── Formatting ──────────────────────────────────────────────────────────────

format: ## Format all C++ files
	./format_project.sh

check-format: ## Check C++ formatting (no changes)
	./format_project.sh --check

# ─── Python ──────────────────────────────────────────────────────────────────

pip-dev: ## Install Python package in dev mode
	cd python && pip install -e ".[dev]"

pip-test: ## Run Python tests
	cd python && pytest tests/ -v

# ─── Convenience ─────────────────────────────────────────────────────────────

all: build test ## Build and test everything

rebuild: clean build ## Clean rebuild

debug: ## Build in Debug mode
	$(MAKE) build BUILD_TYPE=Debug

release: ## Build in Release mode
	$(MAKE) build BUILD_TYPE=Release

version: ## Print current version
	@cat version.txt
