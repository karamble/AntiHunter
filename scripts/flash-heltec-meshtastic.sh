#!/bin/bash
#
# Flash and configure a Heltec WiFi LoRa 32 V3 with stock Meshtastic
# firmware plus the configuration that pairs it with a Halberd sensor.
#
# A Halberd unit ships as two ESP32-class boards wired together. The
# XIAO ESP32-S3 runs the Halberd firmware (this repo). The Heltec runs
# stock Meshtastic and bridges the XIAO's UART to the LoRa mesh.
#
# The configuration phase is interactive. After every meshtastic --set
# the device reboots, and the script asks the operator to press ENTER
# once the OLED is back on. This catches reboots that take longer than
# expected and lets the operator abort cleanly if something looks off.
#
# Usage:
#   ./scripts/flash-heltec-meshtastic.sh                 # full flash + config
#   ./scripts/flash-heltec-meshtastic.sh --flash-only    # firmware only, skip config
#   ./scripts/flash-heltec-meshtastic.sh --config-only   # config only, skip flash
#   ./scripts/flash-heltec-meshtastic.sh --confirm-config # verify config, no writes
#   ./scripts/flash-heltec-meshtastic.sh -p /dev/ttyUSB1 # specific port
#   ./scripts/flash-heltec-meshtastic.sh -r US           # override LoRa region
#   ./scripts/flash-heltec-meshtastic.sh -v 2.7.15.567b8ea # specific Meshtastic version
#

set -e

# ── colors ───────────────────────────────────────────────────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

log_info()  { echo -e "${GREEN}[INFO]${NC} $1"; }
log_warn()  { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }
log_step()  {
    echo ""
    echo -e "${CYAN}══════════════════════════════════════════════════════════${NC}"
    echo -e "${CYAN}  $1${NC}"
    echo -e "${CYAN}══════════════════════════════════════════════════════════${NC}"
    echo ""
}

# ── flash addresses (Heltec V3 BIGDB_8MB layout) ─────────────────────────────
ADDR_FIRMWARE="0x00"
ADDR_BLEOTA="0x340000"
ADDR_LITTLEFS="0x670000"

# ── halberd defaults ─────────────────────────────────────────────────────────
# Wiring on a Halberd unit: the XIAO ESP32-S3 talks to the Heltec via UART.
# The diginode-v5 PCB names its UART nets MESH_TX and MESH_RX from each
# side's *own* perspective, so MESH_TX wires S3 GPIO5 (out) to Heltec
# GPIO20, and MESH_RX wires S3 GPIO4 (in) to Heltec GPIO19. To turn that
# into a working crossover we swap the Heltec's serial module: it must
# receive on GPIO20 (the wire S3 is driving) and transmit on GPIO19 (the
# wire S3 is listening to).
SERIAL_RXD=20
SERIAL_TXD=19
SERIAL_BAUD_NAME="BAUD_115200"
SERIAL_BAUD_ENUM=11

DEVICE_OWNER_LONG="Halberd"
DEVICE_OWNER_SHORT="HALB"

# Mesh channel PSK. Sourced from halberd/.env (gitignored) so the secret
# is never committed. If unset, the device keeps Meshtastic's PUBLIC
# default channel and the script warns about it.
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
HALBERD_ENV="$(dirname "$SCRIPT_DIR")/.env"
if [ -f "$HALBERD_ENV" ]; then
    set -a
    # shellcheck disable=SC1090
    source "$HALBERD_ENV"
    set +a
fi
MESH_PSK="${MESH_PSK:-}"

# Defaults
PORT=""
REGION="EU_868"
VERSION="2.7.15.567b8ea"   # latest stable as of 2026-05-06, override with -v
FLASH_ONLY=false
CONFIG_ONLY=false
CONFIRM_CONFIG=false
TMPDIR=""

# ── arg parsing ──────────────────────────────────────────────────────────────
usage() {
    cat <<EOF
Usage: $(basename "$0") [options]

Options:
  -p, --port PORT     Heltec serial port (default: auto-detect /dev/ttyUSB*)
  -r, --region RGN    LoRa region (default: ${REGION}). Examples: EU_868, US, CN
  -v, --version VER   Meshtastic version tag (default: ${VERSION})
  --flash-only        Flash firmware, skip configuration
  --config-only       Configure already-flashed Heltec, skip firmware flash
  --confirm-config    Verify configuration without flashing or writing
  -h, --help          Show this help

Examples:
  $(basename "$0")                            # full flash + halberd config
  $(basename "$0") --flash-only               # just flash stock Meshtastic
  $(basename "$0") --config-only              # apply halberd config to existing flash
  $(basename "$0") -r US -p /dev/ttyUSB1      # US region, specific port
EOF
    exit 0
}

while [ $# -gt 0 ]; do
    case "$1" in
        -p|--port)        PORT="$2";        shift 2;;
        -r|--region)      REGION="$2";      shift 2;;
        -v|--version)     VERSION="$2";     shift 2;;
        --flash-only)     FLASH_ONLY=true;  shift;;
        --config-only)    CONFIG_ONLY=true; shift;;
        --confirm-config) CONFIRM_CONFIG=true; shift;;
        -h|--help)        usage;;
        *) log_error "Unknown arg: $1"; usage;;
    esac
done

cleanup() {
    if [ -n "$TMPDIR" ] && [ -d "$TMPDIR" ]; then
        rm -rf "$TMPDIR"
    fi
    return 0
}
trap cleanup EXIT

# ── tool checks ──────────────────────────────────────────────────────────────
require_tool() {
    if ! command -v "$1" >/dev/null 2>&1; then
        log_error "Required tool '$1' not found in PATH."
        log_error "Install: $2"
        exit 1
    fi
}

if ! $CONFIRM_CONFIG && ! $CONFIG_ONLY; then
    require_tool esptool.py "pip install esptool"
    require_tool curl "apt install curl"
    require_tool unzip "apt install unzip"
fi
if ! $FLASH_ONLY; then
    require_tool meshtastic "pip install meshtastic"
fi

# ── port autodetect ──────────────────────────────────────────────────────────
if [ -z "$PORT" ]; then
    for candidate in /dev/ttyUSB0 /dev/ttyUSB1 /dev/ttyUSB2; do
        if [ -e "$candidate" ]; then
            PORT="$candidate"
            log_info "Auto-detected Heltec port: $PORT"
            break
        fi
    done
    if [ -z "$PORT" ]; then
        log_error "No /dev/ttyUSB* device found. Plug in the Heltec or pass -p PORT."
        exit 1
    fi
fi

if [ ! -e "$PORT" ]; then
    log_error "Port $PORT does not exist."
    exit 1
fi

# ── PSK warning ──────────────────────────────────────────────────────────────
if [ -z "$MESH_PSK" ] && ! $FLASH_ONLY && ! $CONFIRM_CONFIG; then
    log_step "Step 0: PSK check"
    log_warn "MESH_PSK is not set. The Heltec will join Meshtastic's PUBLIC default channel."
    log_warn "To bind it to a private mesh, drop MESH_PSK=\"base64:<256-bit-key>\" into halberd/.env"
    log_warn "(gitignored) and re-run --config-only."
fi

# ── Step 1: download firmware (skipped on --config-only / --confirm-config) ──
if ! $CONFIG_ONLY && ! $CONFIRM_CONFIG; then
    log_step "Step 1: download Meshtastic ${VERSION} firmware-esp32s3 bundle"
    TMPDIR="$(mktemp -d)"
    ZIP_NAME="firmware-esp32s3-${VERSION}.zip"
    ZIP_URL="https://github.com/meshtastic/firmware/releases/download/v${VERSION}/${ZIP_NAME}"
    log_info "Downloading $ZIP_URL"
    if ! curl -fsSL -o "$TMPDIR/$ZIP_NAME" "$ZIP_URL"; then
        log_error "Download failed. Check the version tag exists at https://github.com/meshtastic/firmware/releases"
        exit 1
    fi
    unzip -q "$TMPDIR/$ZIP_NAME" -d "$TMPDIR/extracted"

    # Release asset naming: the main factory image is `firmware-heltec-v3-<ver>.bin`
    # (no .factory.bin suffix since ~2.7). The `*-update.bin` sibling is the OTA
    # delta image and must be excluded — flashing it at 0x00 overwrites the
    # bootloader and bricks the device until it is re-flashed in boot mode.
    FIRMWARE_BIN=$(find "$TMPDIR/extracted" -name "firmware-heltec-v3-*.bin" ! -name "*-update.bin" | head -1)
    BLEOTA_BIN=$(find "$TMPDIR/extracted" -name "bleota-s3.bin" | head -1)
    LITTLEFS_BIN=$(find "$TMPDIR/extracted" -name "littlefs-heltec-v3-*.bin" | head -1)
    if [ -z "$FIRMWARE_BIN" ] || [ -z "$BLEOTA_BIN" ] || [ -z "$LITTLEFS_BIN" ]; then
        log_error "Expected binaries not found in the firmware bundle."
        log_error "  firmware: $FIRMWARE_BIN"
        log_error "  bleota:   $BLEOTA_BIN"
        log_error "  littlefs: $LITTLEFS_BIN"
        exit 1
    fi
    log_info "Firmware: $(basename "$FIRMWARE_BIN")"
    log_info "BLE OTA:  $(basename "$BLEOTA_BIN")"
    log_info "LittleFS: $(basename "$LITTLEFS_BIN")"

    log_step "Step 2: erase + flash"
    log_info "Erasing flash..."
    esptool.py --port "$PORT" erase_flash
    log_info "Writing firmware @ $ADDR_FIRMWARE"
    esptool.py --port "$PORT" write_flash "$ADDR_FIRMWARE" "$FIRMWARE_BIN"
    log_info "Writing BLE OTA @ $ADDR_BLEOTA"
    esptool.py --port "$PORT" write_flash "$ADDR_BLEOTA" "$BLEOTA_BIN"
    log_info "Writing LittleFS @ $ADDR_LITTLEFS"
    esptool.py --port "$PORT" write_flash "$ADDR_LITTLEFS" "$LITTLEFS_BIN"
    log_info "Flash complete. Device is rebooting."
fi

# ── interactive helpers ──────────────────────────────────────────────────────
# wait_for_device: blocks on ENTER from the operator, then probes the device
# with `meshtastic --info`. If the probe fails, asks again. Type 'abort' to
# bail out cleanly. Use this between every meshtastic --set call so the
# operator can confirm each post-set reboot finished before the next command.
wait_for_device() {
    while true; do
        echo -en "${YELLOW}[WAIT]${NC} Press ENTER when the Heltec OLED is active (or type 'abort'): "
        read -r response
        if [ "$response" = "abort" ]; then
            log_error "Aborted by operator"
            exit 1
        fi
        if meshtastic --port "$PORT" --info >/dev/null 2>&1; then
            log_info "Device responding"
            return 0
        fi
        log_warn "Device not responding yet. Wait for the OLED and try again."
    done
}

# mesh_cmd: log the action, run the meshtastic command, then hand control
# back to the operator via wait_for_device. Most --set commands trigger a
# reboot, which can take 5 to 15 seconds depending on the setting.
mesh_cmd() {
    local description="$1"
    shift
    log_info "$description"
    if ! meshtastic --port "$PORT" "$@" 2>&1; then
        log_warn "Command may have failed. Device may be rebooting."
    fi
    wait_for_device
}

# ── Step 3: configure (skipped on --flash-only) ──────────────────────────────
if ! $FLASH_ONLY && ! $CONFIRM_CONFIG; then
    log_step "Step 3: configure halberd-specific settings"
    log_info "Each --set triggers a reboot. Press ENTER after each one when the OLED is back."

    log_info "Initial sync. Wait for the freshly-flashed Heltec to come up."
    wait_for_device

    mesh_cmd "Set LoRa region to $REGION" --set lora.region "$REGION"

    if [ -n "$MESH_PSK" ]; then
        mesh_cmd "Apply mesh channel PSK from halberd/.env" \
            --ch-set psk "$MESH_PSK" --ch-index 0
    else
        log_warn "Skipping channel PSK. Heltec stays on Meshtastic public default channel."
    fi

    # GPS not present on the Heltec in a Halberd unit (the Heltec is purely
    # a mesh-radio bridge). Mark the GPS config so Meshtastic does not waste
    # cycles polling a non-existent module.
    mesh_cmd "Mark GPS as NOT_PRESENT (Heltec has no GPS in a Halberd unit)" \
        --set position.gps_mode NOT_PRESENT

    mesh_cmd "Set telemetry broadcast interval to 30 min (minimum)" \
        --set telemetry.device_update_interval 1800

    # Serial module routes TEXTMSG bytes on GPIO rxd/txd (not USB), so
    # override_console_serial_port stays FALSE. The USB port remains the
    # debug console, which is what local config tools (this script,
    # `meshtastic --port` queries, etc.) talk to. The XIAO ESP32-S3 is
    # wired to the Heltec on GPIO19/20 and exchanges TEXTMSG over those
    # pins, fully independent of the USB console. The rxd/txd values
    # below intentionally counter-rotate the PCB net names — see the
    # SERIAL_RXD/SERIAL_TXD comment near the top of this script.
    mesh_cmd "Configure serial module (TEXTMSG, baud $SERIAL_BAUD_NAME, rxd=$SERIAL_RXD txd=$SERIAL_TXD)" \
        --set serial.enabled true \
        --set serial.echo false \
        --set serial.rxd "$SERIAL_RXD" \
        --set serial.txd "$SERIAL_TXD" \
        --set serial.baud "$SERIAL_BAUD_NAME" \
        --set serial.timeout 0 \
        --set serial.mode TEXTMSG \
        --set serial.override_console_serial_port false

    mesh_cmd "Set device owner: $DEVICE_OWNER_LONG / $DEVICE_OWNER_SHORT" \
        --set-owner "$DEVICE_OWNER_LONG" --set-owner-short "$DEVICE_OWNER_SHORT"

    # The Heltec has no role in BLE detection on a Halberd unit. The XIAO
    # does the BLE scanning. Disabling Heltec BLE keeps the radio quiet so
    # it does not advertise itself or interfere with nearby BLE work.
    mesh_cmd "Disable Bluetooth (XIAO does BLE on a Halberd unit)" \
        --set bluetooth.enabled false
fi

# ── Step 4: verify config ────────────────────────────────────────────────────
log_step "Step 4: verify config"

echo -en "${YELLOW}[WAIT]${NC} Press ENTER to start config verification (or type 'abort'): "
read -r response
if [ "$response" = "abort" ]; then
    log_error "Aborted by operator"
    exit 1
fi

log_info "Waiting for Heltec to come online..."
if ! meshtastic --port "$PORT" --info >/dev/null 2>&1; then
    wait_for_device
fi

# Lifted from gotailme/scripts/flash-heltec-meshtastic.sh (proven working
# in the existing fleet). Matches against the full --get output line
# rather than parsing out the value, which works regardless of whether
# the CLI emits "key: value" or "key = value" formatting. The patterns
# accept either the symbolic name or the numeric enum value.
verify_get() {
    local key="$1"
    local expected="$2"
    local output
    output=$(meshtastic --port "$PORT" --get "$key" 2>&1) || {
        log_warn "  FAIL $key (could not read from device)"
        return 1
    }
    local actual
    actual=$(echo "$output" | grep -i "$key" | sed 's/.*= *//')
    if [ -z "$actual" ]; then
        log_warn "  FAIL $key (no value in output)"
        return 1
    fi
    if echo "$actual" | grep -qi "$expected"; then
        log_info "  OK   $key = $actual"
        return 0
    fi
    log_warn "  FAIL $key = $actual (expected: $expected)"
    return 1
}

VERIFY_FAIL=0
verify_get "lora.region" "${REGION}\|3"                            || VERIFY_FAIL=1
verify_get "serial.enabled" "true\|True"                           || VERIFY_FAIL=1
verify_get "serial.baud" "${SERIAL_BAUD_NAME}\|${SERIAL_BAUD_ENUM}" || VERIFY_FAIL=1
verify_get "serial.mode" "TEXTMSG\|3"                              || VERIFY_FAIL=1
verify_get "serial.rxd" ": *${SERIAL_RXD}\$"                       || VERIFY_FAIL=1
verify_get "serial.txd" ": *${SERIAL_TXD}\$"                       || VERIFY_FAIL=1
verify_get "position.gps_mode" "NOT_PRESENT\|2"                    || VERIFY_FAIL=1
verify_get "telemetry.device_update_interval" "1800"               || VERIFY_FAIL=1
verify_get "bluetooth.enabled" "false\|False"                      || VERIFY_FAIL=1

# Channel PSK lives in --info, not --get. With MESH_PSK set, the default
# AQ== PSK is a failure. Without it, the default is the expected state.
CH_OUTPUT=$(meshtastic --port "$PORT" --info 2>&1) || true
if [ -n "$MESH_PSK" ]; then
    if echo "$CH_OUTPUT" | grep -q "psk.*AQ=="; then
        log_warn "  FAIL Channel PSK is default (AQ==) but MESH_PSK was set"
        VERIFY_FAIL=1
    elif echo "$CH_OUTPUT" | grep -q "psk"; then
        log_info "  OK   Channel PSK is set (non-default)"
    else
        log_warn "  WARN Could not verify channel PSK"
    fi
else
    if echo "$CH_OUTPUT" | grep -q "psk.*AQ=="; then
        log_info "  OK   Channel PSK is Meshtastic default (no MESH_PSK set)"
    elif echo "$CH_OUTPUT" | grep -q "psk"; then
        log_info "  OK   Channel PSK is non-default (existing config preserved)"
    else
        log_warn "  WARN Could not verify channel PSK"
    fi
fi

# Owner names are exposed via --info, not --get. Parse the User block.
OWNER_LONG_ACTUAL=$(echo "$CH_OUTPUT" | grep -E "longName|long_name" | head -1 | sed 's/.*: *//' | tr -d '",')
OWNER_SHORT_ACTUAL=$(echo "$CH_OUTPUT" | grep -E "shortName|short_name" | head -1 | sed 's/.*: *//' | tr -d '",')
if [ "$OWNER_LONG_ACTUAL" = "$DEVICE_OWNER_LONG" ]; then
    log_info "  OK   owner.long = $OWNER_LONG_ACTUAL"
else
    log_warn "  FAIL owner.long = $OWNER_LONG_ACTUAL (expected: $DEVICE_OWNER_LONG)"
    VERIFY_FAIL=1
fi
if [ "$OWNER_SHORT_ACTUAL" = "$DEVICE_OWNER_SHORT" ]; then
    log_info "  OK   owner.short = $OWNER_SHORT_ACTUAL"
else
    log_warn "  FAIL owner.short = $OWNER_SHORT_ACTUAL (expected: $DEVICE_OWNER_SHORT)"
    VERIFY_FAIL=1
fi

if [ "$VERIFY_FAIL" -ne 0 ]; then
    log_warn "Some config values did not match expected. Inspect manually:"
    log_warn "  meshtastic --port $PORT --info"
    exit 1
fi

log_info "All halberd-specific config values verified."
log_step "Done"
log_info "Port: $PORT"
log_info "Owner: $DEVICE_OWNER_LONG / $DEVICE_OWNER_SHORT"
log_info "Region: $REGION"
log_info "Mesh PSK: $([ -n "$MESH_PSK" ] && echo "set (private channel)" || echo "unset (PUBLIC channel)")"
