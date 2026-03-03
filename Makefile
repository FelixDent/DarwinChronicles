.PHONY: build build-release test bench clean format run run-example run-sandbox help

BUILD_DIR    := build
RELEASE_DIR  := build-release

help: ## Show this help
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | \
		awk 'BEGIN {FS = ":.*?## "}; {printf "  \033[36m%-16s\033[0m %s\n", $$1, $$2}'

build: ## Build in Debug mode
	cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug
	cmake --build $(BUILD_DIR) -j$$(nproc 2>/dev/null || echo 4)

build-release: ## Build in Release mode
	cmake -B $(RELEASE_DIR) -DCMAKE_BUILD_TYPE=Release
	cmake --build $(RELEASE_DIR) -j$$(nproc 2>/dev/null || echo 4)

test: build ## Run unit tests
	cd $(BUILD_DIR) && ctest --output-on-failure

bench: ## Build and run benchmarks
	cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release -DDARWIN_BUILD_BENCHES=ON
	cmake --build $(BUILD_DIR) -j$$(nproc 2>/dev/null || echo 4)
	$(BUILD_DIR)/benches/darwin_benchmarks

clean: ## Remove build directories
	rm -rf $(BUILD_DIR) $(RELEASE_DIR)

format: ## Format all source files with clang-format
	find include src tests benches examples sandboxes -name '*.h' -o -name '*.cpp' | \
		xargs clang-format -i

run: build ## Build and run the main executable
	$(BUILD_DIR)/darwin

run-example: build ## Run an example (usage: make run-example EXAMPLE=earth_like)
	$(BUILD_DIR)/examples/$(EXAMPLE)

run-sandbox: build ## Run a sandbox (usage: make run-sandbox SANDBOX=worldgen)
	$(BUILD_DIR)/sandboxes/$(SANDBOX)/sandbox_$(SANDBOX)
