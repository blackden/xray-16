# OpenXRay automation for macOS (arm64 / x86_64).
# Thin wrapper around CMake + Homebrew that captures everything needed for a
# baseline run (stdout, engine log, macOS crash report) into a session dir.
# See CLAUDE.md, notes/progress.md, notes/apple-silicon.md for context.

SHELL := /bin/bash

# Per-developer overrides. .env is gitignored; copy .env.example to .env.
-include .env

BUILD_DIR    ?= build
BUILD_TYPE   ?= Mixed
PARALLEL     ?= 4
GAME_DIR     ?= $(HOME)/Games/STALKER-CoP
GAMEDATA_SRC ?= $(GAME_DIR)
FSGAME_LTX   ?= $(GAME_DIR)/fsgame.ltx
EXTRA_ARGS   ?= -nointro
SESSION_DIR  ?= notes/session-$(shell date +%Y%m%d-%H%M%S)
HOST_ARCH    := $(shell uname -m)
STEAM_LOGIN  ?=
APPID        ?= 41700

CONFIG_STAMP := $(BUILD_DIR)/CMakeCache.txt

INSTALL_APP_DIR  ?= /Applications/OpenXRay.app
RELEASE_BIN      := bin/$(HOST_ARCH)/ReleaseMasterGold/xr_3da
DIST_APP         := dist/OpenXRay.app

.DEFAULT_GOAL := help
.PHONY: help setup check-configure-prereqs configure build build-release profile run run-lldb all clean rebuild install-game link-gamedata codesign bundle package install all-in-one test test-encoding

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
	@echo "  GAME_DIR     = $(GAME_DIR)  (where steamcmd installs the game)"
	@echo "  GAMEDATA_SRC = $(GAMEDATA_SRC)  (source for 'all-in-one'; defaults to GAME_DIR)"
	@echo "  FSGAME_LTX   = $(FSGAME_LTX)  (defaults to GAME_DIR/fsgame.ltx)"
	@echo "  EXTRA_ARGS   = $(EXTRA_ARGS)"
	@echo "  SESSION_DIR  = $(SESSION_DIR)"
	@echo "  HOST_ARCH    = $(HOST_ARCH)"
	@echo "  STEAM_LOGIN  = $(STEAM_LOGIN)  (required for 'install-game')"
	@echo "  APPID        = $(APPID)  (41700 = Call of Pripyat, 20510 = Clear Sky)"

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
	@[ -f Externals/luabind/CMakeLists.txt ] || \
		{ echo "ERROR: submodules missing (run 'make setup' or 'git submodule update --init --recursive')"; exit 1; }

$(CONFIG_STAMP): | check-configure-prereqs
	cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DCMAKE_UNITY_BUILD=ON

configure: $(CONFIG_STAMP) ## Run cmake configure step (idempotent)

build: configure ## Build and verify the binary is native Mach-O for HOST_ARCH
	cmake --build $(BUILD_DIR) --parallel $(PARALLEL)
	@echo "==> Verifying built binary"
	@bin=$$(find bin -name xr_3da -type f 2>/dev/null | head -1); \
	if [ -z "$$bin" ]; then echo "ERROR: xr_3da not found under bin/"; exit 1; fi; \
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
	bin=$$(find bin -name xr_3da -type f 2>/dev/null | head -1); \
	if [ -z "$$bin" ]; then echo "ERROR: xr_3da not found under bin/"; exit 1; fi; \
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
	fs_root="$$(dirname "$(FSGAME_LTX)")"; \
	copied_any=0; \
	for logs_dir in "$$fs_root/_appdata_/logs" "$$fs_root/logs"; do \
		if [ -d "$$logs_dir" ]; then \
			echo "==> Copying engine logs from $$logs_dir"; \
			mkdir -p "$(SESSION_DIR)/engine-logs"; \
			cp -R "$$logs_dir"/* "$(SESSION_DIR)/engine-logs/" 2>/dev/null || true; \
			copied_any=1; \
		fi; \
	done; \
	if [ "$$copied_any" = "0" ]; then \
		echo "==> No engine logs found under $$fs_root/{_appdata_/logs,logs}"; \
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

run-lldb: build ## Launch xr_3da under lldb to capture a backtrace on crash
	@if [ -z "$(FSGAME_LTX)" ] || [ ! -f "$(FSGAME_LTX)" ]; then \
		echo "ERROR: FSGAME_LTX must point to an existing fsgame.ltx"; exit 1; \
	fi
	@bin=$$(find bin -name xr_3da -type f 2>/dev/null | head -1); \
	if [ -z "$$bin" ]; then echo "ERROR: xr_3da not found under bin/"; exit 1; fi; \
	abs_bin=$$(cd "$$(dirname "$$bin")" && pwd)/$$(basename "$$bin"); \
	mkdir -p "$(SESSION_DIR)"; \
	abs_session=$$(cd "$(SESSION_DIR)" && pwd); \
	touch "$$abs_session/.start"; \
	echo "==> lldb session: $$abs_session"; \
	echo "==> lldb runs in batch mode: 'bt all' fires on crash, then quits."; \
	echo "    Full lldb+stdout transcript -> $$abs_session/lldb.log"; \
	cd "$$(dirname "$$abs_bin")" && \
		lldb --batch \
		     -o "process launch -- -fsltx $(FSGAME_LTX) $(EXTRA_ARGS)" \
		     -k "bt all" \
		     -k "quit" \
		     -- ./xr_3da 2>&1 | tee "$$abs_session/lldb.log"

install-game: ## Install CoP/CS via steamcmd into GAME_DIR (needs STEAM_LOGIN)
	@if [ -z "$(STEAM_LOGIN)" ]; then \
		echo "ERROR: STEAM_LOGIN is empty. Examples:"; \
		echo "  make install-game STEAM_LOGIN=yourname"; \
		echo "  make install-game STEAM_LOGIN=yourname APPID=20510 GAME_DIR=\$$HOME/Games/STALKER-CS"; \
		exit 1; \
	fi
	@command -v steamcmd >/dev/null || { \
		echo "ERROR: steamcmd not in PATH. Install with: brew install --cask steamcmd"; \
		exit 1; \
	}
	STEAM_LOGIN="$(STEAM_LOGIN)" INSTALL_DIR="$(GAME_DIR)" APPID="$(APPID)" \
		./scripts/mac/install-cop-steamcmd.sh

codesign: ## Ad-hoc sign xr_3da with get-task-allow so macOS writes .ips on crash
	@bin=$$(find bin -name xr_3da -type f 2>/dev/null | head -1); \
	if [ -z "$$bin" ]; then echo "ERROR: xr_3da not found under bin/"; exit 1; fi; \
	abs_bin=$$(cd "$$(dirname "$$bin")" && pwd)/$$(basename "$$bin"); \
	echo "==> codesign --force -s - --entitlements scripts/mac/debug.entitlements $$abs_bin"; \
	codesign --force -s - --entitlements scripts/mac/debug.entitlements "$$abs_bin"; \
	echo "==> Verifying entitlements on $$abs_bin"; \
	codesign --display --entitlements - "$$abs_bin" 2>&1 | sed 's/^/    /'

link-gamedata: ## Symlink res/gamedata into GAME_DIR (GL shaders, OpenXRay configs)
	@target="$(GAME_DIR)/gamedata"; \
	source="$(CURDIR)/res/gamedata"; \
	if [ ! -d "$$source" ]; then \
		echo "ERROR: $$source does not exist"; exit 1; \
	fi; \
	if [ ! -d "$(GAME_DIR)" ]; then \
		echo "ERROR: GAME_DIR=$(GAME_DIR) does not exist (run 'make install-game' first)"; exit 1; \
	fi; \
	if [ -L "$$target" ]; then \
		echo "==> Replacing existing symlink at $$target"; rm "$$target"; \
	elif [ -e "$$target" ]; then \
		echo "ERROR: $$target exists and is not a symlink — refusing to overwrite"; exit 1; \
	fi; \
	ln -s "$$source" "$$target"; \
	echo "==> Linked $$target -> $$source"

all: setup build run ## Run setup + build + run end-to-end

test: test-encoding ## Run regression checks + safe_append unit test (no xrCore link)
	@echo "==> Static regression checks"
	@tests/regression_checks.sh
	@echo
	@echo "==> Compiling safe_append unit test"
	@mkdir -p build-tests
	@clang++ -std=c++17 -Wall -Wextra tests/safe_append_test.cpp -o build-tests/safe_append_test
	@echo "==> Running safe_append unit test"
	@build-tests/safe_append_test

test-encoding: ## Run UTF-8 / cp1251 characterization tests (no xrCore link)
	@mkdir -p build-tests
	@echo "==> Compiling encoding characterization tests"
	@clang++ -std=c++17 -Wall -Wextra -Werror tests/utf8_validator_test.cpp -o build-tests/utf8_validator_test
	@clang++ -std=c++17 -Wall -Wextra -Werror tests/cp1251_roundtrip_test.cpp -liconv -o build-tests/cp1251_roundtrip_test
	@clang++ -std=c++17 -Wall -Wextra -Werror tests/mb_decode_test.cpp -o build-tests/mb_decode_test
	@clang++ -std=c++17 -Wall -Wextra -Werror tests/utf8_decode_test.cpp -o build-tests/utf8_decode_test
	@echo "==> Running utf8_validator_test"
	@build-tests/utf8_validator_test
	@echo "==> Running cp1251_roundtrip_test"
	@build-tests/cp1251_roundtrip_test
	@echo "==> Running mb_decode_test"
	@build-tests/mb_decode_test
	@echo "==> Running utf8_decode_test"
	@build-tests/utf8_decode_test
	@echo "==> Verifying fixtures are well-formed"
	@iconv -f UTF-8 -t UTF-8 < tests/fixtures/encoding/phrase.utf8 > /dev/null \
		&& echo "  ✓ phrase.utf8 is valid UTF-8" \
		|| { echo "  ✗ phrase.utf8 is NOT valid UTF-8"; exit 1; }
	@! iconv -f UTF-8 -t UTF-8 < tests/fixtures/encoding/phrase.cp1251 > /dev/null 2>&1 \
		&& echo "  ✓ phrase.cp1251 is correctly NOT valid UTF-8" \
		|| { echo "  ✗ phrase.cp1251 is unexpectedly valid UTF-8 (regenerate?)"; exit 1; }

build-release: ## Compile the ReleaseMasterGold binary into bin/$(HOST_ARCH)/ReleaseMasterGold/
	@$(MAKE) build BUILD_TYPE=ReleaseMasterGold BUILD_DIR=build-release

profile: ## Build with Tracy enabled (Mixed config) -- captures frame markers/ZoneScoped
	@command -v tracy >/dev/null || command -v Tracy >/dev/null || { \
		echo "WARNING: 'tracy' GUI client not found on PATH."; \
		echo "         Install with: brew install tracy"; \
		echo "         (build will continue; the engine sends data over TCP to the GUI.)"; \
	}
	@if [ ! -f build-profile/CMakeCache.txt ]; then \
		cmake -B build-profile -DCMAKE_BUILD_TYPE=Mixed -DCMAKE_UNITY_BUILD=ON -DXRAY_ENABLE_TRACY=ON; \
	fi
	cmake --build build-profile --parallel $(PARALLEL)
	@echo "==> Tracy-enabled binary:"
	@find bin -name xr_3da -type f -newer build-profile/CMakeCache.txt 2>/dev/null | head -1 | sed 's/^/  /'
	@echo "==> Run: 'tracy' (GUI), then launch xr_3da -- it broadcasts on TCP 8086 by default."

bundle: ## Assemble dist/OpenXRay.app from an existing release binary (no compile; idempotent)
	@if [ ! -f "$(RELEASE_BIN)" ]; then \
		echo "ERROR: $(RELEASE_BIN) not found. Run 'make build-release' first."; \
		exit 1; \
	fi
	@if [ -f "$(DIST_APP)/Contents/MacOS/xr_3da" ] && \
		[ "$(DIST_APP)/Contents/MacOS/xr_3da" -nt "$(RELEASE_BIN)" ]; then \
		echo "==> $(DIST_APP) is newer than $(RELEASE_BIN) — skip bundle (already up to date)"; \
		exit 0; \
	fi
	@BUILD_TYPE=ReleaseMasterGold \
		HOST_ARCH=$(HOST_ARCH) \
		DEFAULT_FSGAME_LTX="$(FSGAME_LTX)" \
		APP_VERSION="$$(git describe --always --dirty 2>/dev/null || echo dev)" \
		scripts/mac/package_app.sh

package: build-release bundle ## Compile release + assemble dist/OpenXRay.app

install: build-release bundle ## Hot-swap xr_3da + launcher script into $(INSTALL_APP_DIR) (fast iter)
	@if [ ! -d "$(INSTALL_APP_DIR)" ]; then \
		echo "ERROR: $(INSTALL_APP_DIR) does not exist."; \
		echo "       Run 'make package' first, then drag $(DIST_APP) to /Applications/,"; \
		echo "       or override with INSTALL_APP_DIR=/path/to/OpenXRay.app."; \
		exit 1; \
	fi
	@target_bin="$(INSTALL_APP_DIR)/Contents/MacOS/xr_3da"; \
	target_launcher="$(INSTALL_APP_DIR)/Contents/MacOS/OpenXRay"; \
	src_bin="$(RELEASE_BIN)"; \
	src_launcher="$(DIST_APP)/Contents/MacOS/OpenXRay"; \
	bin_up_to_date=0; launcher_up_to_date=0; \
	[ -f "$$target_bin" ] && [ "$$target_bin" -nt "$$src_bin" ] && bin_up_to_date=1; \
	[ -f "$$target_launcher" ] && [ "$$target_launcher" -nt "$$src_launcher" ] && launcher_up_to_date=1; \
	if [ "$$bin_up_to_date" = "1" ] && [ "$$launcher_up_to_date" = "1" ]; then \
		echo "==> $(INSTALL_APP_DIR) is up to date — nothing to install"; \
		exit 0; \
	fi; \
	if [ "$$bin_up_to_date" != "1" ]; then \
		echo "==> Copying $$src_bin -> $$target_bin"; \
		cp "$$src_bin" "$$target_bin"; \
		echo "==> Re-codesigning binary with debug entitlements"; \
		codesign --force --sign - --entitlements scripts/mac/debug.entitlements "$$target_bin" >/dev/null; \
	fi; \
	if [ "$$launcher_up_to_date" != "1" ]; then \
		echo "==> Copying launcher $$src_launcher -> $$target_launcher"; \
		cp "$$src_launcher" "$$target_launcher"; \
	fi; \
	echo "==> Installed. Launch: open '$(INSTALL_APP_DIR)'"

all-in-one: build-release ## Bundle .app + game data side-by-side into dist/OpenXRay-AllInOne.dmg
	@if [ ! -f "$(GAMEDATA_SRC)/fsgame.ltx" ]; then \
		echo "ERROR: GAMEDATA_SRC=$(GAMEDATA_SRC) has no fsgame.ltx."; \
		echo "       Pass GAMEDATA_SRC=/path/to/STALKER-CoP,"; \
		echo "       or run 'make install-game STEAM_LOGIN=...' first."; \
		exit 1; \
	fi
	@GAMEDATA_SRC="$(GAMEDATA_SRC)" \
		HOST_ARCH=$(HOST_ARCH) \
		BUILD_TYPE=ReleaseMasterGold \
		APP_VERSION="$$(git describe --always --dirty 2>/dev/null || echo dev)" \
		scripts/mac/package_all_in_one.sh

clean: ## Remove the build and binary output directories
	rm -rf $(BUILD_DIR) build-release bin dist

rebuild: clean build ## Wipe build directory and rebuild from scratch
