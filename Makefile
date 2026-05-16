# OpenXRay automation for macOS (arm64 / x86_64).
# Thin wrapper around CMake + Homebrew that captures everything needed for a
# baseline run (stdout, engine log, macOS crash report) into a session dir.
# See CLAUDE.md, notes/progress.md, notes/apple-silicon.md for context.

SHELL := /bin/bash

BUILD_DIR    ?= build
BUILD_TYPE   ?= Mixed
PARALLEL     ?= 4
FSGAME_LTX   ?=
EXTRA_ARGS   ?= -nointro
SESSION_DIR  ?= notes/session-$(shell date +%Y%m%d-%H%M%S)
HOST_ARCH    := $(shell uname -m)

CONFIG_STAMP := $(BUILD_DIR)/CMakeCache.txt

.DEFAULT_GOAL := help
.PHONY: help setup check-configure-prereqs configure build run all clean rebuild

help: ## Show this help and current settings
	@echo "OpenXRay macOS automation"
	@echo
	@echo "Targets:"
	@awk 'BEGIN {FS = ":.*?## "} /^[a-zA-Z][a-zA-Z_-]*:.*?## / {printf "  %-12s %s\n", $$1, $$2}' $(MAKEFILE_LIST)
	@echo
	@echo "Variables (override with VAR=value):"
	@echo "  BUILD_DIR    = $(BUILD_DIR)"
	@echo "  BUILD_TYPE   = $(BUILD_TYPE)  (Debug | Mixed | Release | ReleaseMasterGold)"
	@echo "  PARALLEL     = $(PARALLEL)"
	@echo "  FSGAME_LTX   = $(FSGAME_LTX)  (required for 'run')"
	@echo "  EXTRA_ARGS   = $(EXTRA_ARGS)"
	@echo "  SESSION_DIR  = $(SESSION_DIR)"
	@echo "  HOST_ARCH    = $(HOST_ARCH)"

setup: ## Install brew deps, update git submodules, verify toolchain
	@echo "==> Host architecture: $(HOST_ARCH)"
	@if [ "$(HOST_ARCH)" != "arm64" ] && [ "$(HOST_ARCH)" != "x86_64" ]; then \
		echo "ERROR: unsupported host arch '$(HOST_ARCH)'"; exit 1; \
	fi
	@command -v brew >/dev/null || { echo "ERROR: Homebrew not installed"; exit 1; }
	@echo "==> brew bundle install"
	brew bundle install --file=Brewfile
	@echo "==> git submodule update --init --recursive"
	git submodule update --init --recursive
	@echo "==> Toolchain check"
	@for tool in clang cmake; do \
		p=$$(command -v $$tool || true); \
		if [ -z "$$p" ]; then echo "ERROR: $$tool not found in PATH"; exit 1; fi; \
		echo "  $$tool -> $$p"; \
		file "$$p" | sed 's/^/    /'; \
	done

check-configure-prereqs:
	@command -v cmake >/dev/null || { echo "ERROR: cmake not in PATH (run 'make setup')"; exit 1; }
	@[ -f Externals/luabind-deboostified/CMakeLists.txt ] || \
		{ echo "ERROR: submodules missing (run 'make setup' or 'git submodule update --init --recursive')"; exit 1; }

$(CONFIG_STAMP): | check-configure-prereqs
	cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DCMAKE_UNITY_BUILD=ON

configure: $(CONFIG_STAMP) ## Run cmake configure step (idempotent)

build: configure ## Build and verify the binary is native Mach-O for HOST_ARCH
	cmake --build $(BUILD_DIR) --parallel $(PARALLEL)
	@echo "==> Verifying built binary"
	@bin=$$(find $(BUILD_DIR) -name xr_3da -type f | head -1); \
	if [ -z "$$bin" ]; then echo "ERROR: xr_3da not found under $(BUILD_DIR)"; exit 1; fi; \
	echo "  binary: $$bin"; \
	file "$$bin"; \
	if ! file "$$bin" | grep -q "Mach-O 64-bit executable $(HOST_ARCH)"; then \
		echo "ERROR: binary is not Mach-O 64-bit $(HOST_ARCH) — check your toolchain"; \
		exit 1; \
	fi

run: build ## Launch xr_3da with FSGAME_LTX=... and capture logs to SESSION_DIR
	@if [ -z "$(FSGAME_LTX)" ]; then \
		echo "ERROR: FSGAME_LTX is empty. Example:"; \
		echo "  make run FSGAME_LTX=/Users/you/Games/STALKER-CoP/fsgame.ltx"; \
		exit 1; \
	fi
	@if [ ! -f "$(FSGAME_LTX)" ]; then \
		echo "ERROR: FSGAME_LTX=$(FSGAME_LTX) does not exist"; exit 1; \
	fi
	@set -o pipefail; \
	bin=$$(find $(BUILD_DIR) -name xr_3da -type f | head -1); \
	if [ -z "$$bin" ]; then echo "ERROR: xr_3da not found under $(BUILD_DIR)"; exit 1; fi; \
	abs_bin=$$(cd "$$(dirname "$$bin")" && pwd)/$$(basename "$$bin"); \
	mkdir -p "$(SESSION_DIR)"; \
	echo "==> Session: $(SESSION_DIR)"; \
	{ \
		echo "## sw_vers"; sw_vers; \
		echo; echo "## uname -a"; uname -a; \
		echo; echo "## arch (host)"; echo "$(HOST_ARCH)"; \
		echo; echo "## file <bin>"; file "$$abs_bin"; \
		echo; echo "## git HEAD"; git rev-parse HEAD 2>/dev/null || echo "(no git)"; \
		echo; echo "## git status --short"; git status --short 2>/dev/null || true; \
	} > "$(SESSION_DIR)/system.txt"; \
	touch "$(SESSION_DIR)/.start"; \
	echo "==> Running: $$abs_bin -fsltx $(FSGAME_LTX) $(EXTRA_ARGS)"; \
	rc=0; \
	( cd "$$(dirname "$$abs_bin")" && ./xr_3da -fsltx "$(FSGAME_LTX)" $(EXTRA_ARGS) ) 2>&1 \
		| tee "$(SESSION_DIR)/stdout.log" \
		|| rc=$$?; \
	echo "==> Process exited with code: $$rc"; \
	logs_dir="$$(dirname "$(FSGAME_LTX)")/logs"; \
	if [ -d "$$logs_dir" ]; then \
		echo "==> Copying engine logs from $$logs_dir"; \
		cp -R "$$logs_dir" "$(SESSION_DIR)/engine-logs"; \
	else \
		echo "==> No engine logs dir at $$logs_dir"; \
	fi; \
	echo "==> Looking for new crash reports under ~/Library/Logs/DiagnosticReports"; \
	new_reports=$$(find "$$HOME/Library/Logs/DiagnosticReports" -name 'xr_3da*.ips' -newer "$(SESSION_DIR)/.start" 2>/dev/null || true); \
	if [ -n "$$new_reports" ]; then \
		mkdir -p "$(SESSION_DIR)/crash-reports"; \
		echo "$$new_reports" | while read -r r; do cp "$$r" "$(SESSION_DIR)/crash-reports/"; done; \
		echo "  copied $$(echo "$$new_reports" | wc -l | tr -d ' ') crash report(s)"; \
	else \
		echo "  none"; \
	fi; \
	echo; \
	echo "==> Summary"; \
	echo "  session : $(SESSION_DIR)"; \
	echo "  exit    : $$rc"; \
	echo "  crashes : $$([ -d '$(SESSION_DIR)/crash-reports' ] && ls '$(SESSION_DIR)/crash-reports' | wc -l | tr -d ' ' || echo 0)"; \
	exit $$rc

all: setup build run ## Run setup + build + run end-to-end

clean: ## Remove the build directory
	rm -rf $(BUILD_DIR)

rebuild: clean build ## Wipe build directory and rebuild from scratch
