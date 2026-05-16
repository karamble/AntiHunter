# Halberd build + flash Makefile.
#
# Two pieces of hardware ship with each Halberd unit:
#   1. XIAO ESP32-S3   runs the Halberd firmware (this repo, halberd-full / halberd-headless)
#   2. Heltec V3       runs stock Meshtastic and bridges the XIAO's UART to the LoRa mesh
#
# The XIAO targets wrap PlatformIO. The Heltec targets wrap
# `scripts/flash-heltec-meshtastic.sh` which downloads stock Meshtastic
# and applies the configuration that pairs the Heltec with the XIAO.
#
# Run `make help` for a list of all targets.

CPPCHECK := cppcheck
CPPCHECK_FLAGS := --enable=all --std=c++17 \
	--suppress=missingIncludeSystem \
	--suppress=missingInclude \
	--suppress=checkersReport \
	--suppress=unusedFunction \
	--suppress=normalCheckLevelMaxBranches \
	--suppress=checkLevelNormal \
	--suppress=unmatchedSuppression \
	--inline-suppr \
	-DPROGMEM= \
	--error-exitcode=1

FULL_SRC := Halberd/full/src
HEADLESS_SRC := Halberd/headless/src
EXCLUDE := -i Halberd/full/src/wifi.c -i Halberd/full/src/opendroneid.c \
	-i Halberd/headless/src/wifi.c -i Halberd/headless/src/opendroneid.c

# XIAO ESP32-S3 defaults. Override on the command line, e.g.
#   make flash-full XIAO_PORT=/dev/ttyACM1
XIAO_PORT ?= /dev/ttyACM0

# XIAO ESP32-C5 (Halberd v5 coprocessor) defaults. The C5 is built with
# ESP-IDF v6.0.1 (installed at IDF_PATH); the targets below source export.sh
# from that path before invoking idf.py. Override IDF_PATH on the command
# line if your checkout lives elsewhere.
IDF_PATH ?= $(HOME)/go/src/github.com/espressif/esp-idf
C5_DIR := Halberd/c5
C5_PORT ?= /dev/ttyACM1

# Heltec defaults forwarded to scripts/flash-heltec-meshtastic.sh.
# HELTEC_PORT empty triggers the script's auto-detect of /dev/ttyUSB*.
HELTEC_PORT ?=
HELTEC_REGION ?= EU_868
HELTEC_VERSION ?= 2.7.15.567b8ea

HELTEC_FLAGS := -r $(HELTEC_REGION) -v $(HELTEC_VERSION)
ifneq ($(HELTEC_PORT),)
HELTEC_FLAGS += -p $(HELTEC_PORT)
endif

.PHONY: help
help: ## Show this help.
	@awk 'BEGIN {FS = ":.*?## "} /^[a-zA-Z0-9_-]+:.*?## / {printf "  %-22s %s\n", $$1, $$2}' $(MAKEFILE_LIST)

.PHONY: lint lint-full lint-headless
lint: lint-full lint-headless ## Run cppcheck on both variants.

lint-full: ## Run cppcheck on the full variant.
	$(CPPCHECK) $(CPPCHECK_FLAGS) $(EXCLUDE) $(FULL_SRC)/

lint-headless: ## Run cppcheck on the headless variant.
	$(CPPCHECK) $(CPPCHECK_FLAGS) $(EXCLUDE) $(HEADLESS_SRC)/

# ── XIAO ESP32-S3 (Halberd firmware) ───────────────────────────────────────
.PHONY: build build-full build-headless flash-full flash-headless clean

build: build-full build-headless ## Build both Halberd variants without flashing.

build-full: ## Build halberd-full firmware.
	pio run -e halberd-full

build-headless: ## Build halberd-headless firmware.
	pio run -e halberd-headless

flash-full: ## Flash XIAO ESP32-S3 with halberd-full firmware (default port /dev/ttyACM0).
	@if [ ! -e "$(XIAO_PORT)" ]; then \
		echo "Port $(XIAO_PORT) not found. Plug in the XIAO or override with XIAO_PORT=..."; \
		exit 1; \
	fi
	pio run -e halberd-full -t upload --upload-port $(XIAO_PORT)

flash-headless: ## Flash XIAO ESP32-S3 with halberd-headless firmware.
	@if [ ! -e "$(XIAO_PORT)" ]; then \
		echo "Port $(XIAO_PORT) not found. Plug in the XIAO or override with XIAO_PORT=..."; \
		exit 1; \
	fi
	pio run -e halberd-headless -t upload --upload-port $(XIAO_PORT)

clean: ## Remove PlatformIO build artifacts.
	pio run -t clean

# ── XIAO ESP32-C5 (Halberd v5 coprocessor, ESP-IDF) ────────────────────────
# idf.py is wrapped by sourcing ESP-IDF's export.sh, which requires bash.
# Each recipe shells out to bash explicitly so the rest of the Makefile can
# stay on POSIX sh.
.PHONY: c5-build c5-flash c5-monitor c5-clean

# Common preamble: fail loudly if IDF_PATH doesn't point at an IDF checkout.
IDF_CHECK = test -f "$(IDF_PATH)/export.sh" || { \
	echo "ESP-IDF not found at IDF_PATH=$(IDF_PATH)."; \
	echo "Install v6.0.1 there, or run: IDF_PATH=/your/path make $@"; \
	exit 1; \
}

c5-build: ## Build the C5 firmware (ESP-IDF v6.0.1, target esp32c5).
	@$(IDF_CHECK)
	@bash -c '. "$(IDF_PATH)/export.sh" >/dev/null && cd "$(C5_DIR)" && idf.py build'

c5-flash: ## Flash the C5 firmware (default port /dev/ttyACM1, override C5_PORT=...).
	@$(IDF_CHECK)
	@if [ ! -e "$(C5_PORT)" ]; then \
		echo "Port $(C5_PORT) not found. Plug in the C5 or override with C5_PORT=..."; \
		exit 1; \
	fi
	@bash -c '. "$(IDF_PATH)/export.sh" >/dev/null && cd "$(C5_DIR)" && idf.py -p $(C5_PORT) flash'

c5-monitor: ## Open idf.py monitor on the C5 (Ctrl-] to exit).
	@$(IDF_CHECK)
	@bash -c '. "$(IDF_PATH)/export.sh" >/dev/null && cd "$(C5_DIR)" && idf.py -p $(C5_PORT) monitor'

c5-clean: ## Remove C5 build artifacts (idf.py fullclean).
	@$(IDF_CHECK)
	@bash -c '. "$(IDF_PATH)/export.sh" >/dev/null && cd "$(C5_DIR)" && idf.py fullclean'

# ── Heltec V3 (stock Meshtastic + halberd config) ──────────────────────────
.PHONY: heltec-flash heltec-flash-only heltec-config-only heltec-confirm-config

heltec-flash: ## Flash + configure Heltec V3 with stock Meshtastic for a Halberd unit.
	@bash scripts/flash-heltec-meshtastic.sh $(HELTEC_FLAGS)

heltec-flash-only: ## Flash stock Meshtastic on the Heltec, skip configuration.
	@bash scripts/flash-heltec-meshtastic.sh $(HELTEC_FLAGS) --flash-only

heltec-config-only: ## Apply halberd configuration to an already-flashed Heltec.
	@bash scripts/flash-heltec-meshtastic.sh $(HELTEC_FLAGS) --config-only

heltec-confirm-config: ## Verify the Heltec has the expected halberd configuration.
	@bash scripts/flash-heltec-meshtastic.sh $(HELTEC_FLAGS) --confirm-config

# ── Combined ───────────────────────────────────────────────────────────────
.PHONY: flash-unit
flash-unit: flash-full heltec-flash ## Flash both the XIAO (halberd-full) and the Heltec for one Halberd unit.
	@echo "Halberd unit fully flashed: XIAO with halberd-full, Heltec with stock Meshtastic + halberd config."
