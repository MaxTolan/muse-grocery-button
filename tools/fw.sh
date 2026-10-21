#!/usr/bin/env bash
# Build and flash the grocery button firmware (Waveshare ESP32-S3-Touch-AMOLED-1.75C).
#
#   tools/fw.sh build      apply our SDK patches, then build
#   tools/fw.sh flash      build, then flash the board over USB
#   tools/fw.sh monitor    show the board's console (Ctrl-] to quit; --no-reset
#                          attaches without restarting the board)
#
# Needs ESP-IDF v6.0.1 (~/esp/esp-idf-v6.0.1, or IDF_EXPORT=/path/to/export.sh),
# your Muse SDK token in sdk_token.local (one line, mgst_...) and your Google
# Cloud API key in google_api_key.local (one line). Both files are git-ignored.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK="$ROOT/third_party/muse-gadget-sdk"
ESP32="$SDK/esp32"
PATCHES="$ROOT/patches/muse-gadget-sdk"
BOARD=s3   # board.sh's name for the 1.75C
BUILD="$ESP32/build-muse-waveshare-s3-175c"
TOKEN_FILE="$ROOT/sdk_token.local"
KEY_FILE="$ROOT/google_api_key.local"
STAMP="$ROOT/build/sdk-patches.stamp"

die() { echo "fw.sh: $*" >&2; exit 1; }

# Files the patches create (git apply --summary: "create mode 100644 path").
created_files() {
    local p
    for p in "$PATCHES"/*.patch; do
        git -C "$SDK" apply --summary "$p" | awk '/create mode/ {print $4}'
    done
}

# The SDK checkout's changes, for telling whether our patch series is in place.
sdk_state() {
    {
        cat "$PATCHES"/*.patch
        git -C "$SDK" diff HEAD
        local f
        for f in $(created_files); do
            echo "== $f"
            cat "$SDK/$f" 2>/dev/null || true
        done
    } | shasum -a 256 | cut -d' ' -f1
}

# Our changes to the SDK live as patches in this repo (the SDK is a pinned
# submodule, not a fork). Later patches build on earlier ones, so the series
# goes on as a whole: unless the checkout is exactly the patched state from
# last time, reset the SDK's tracked files and apply every patch in order.
apply_patches() {
    git -C "$ROOT" submodule update --init third_party/muse-gadget-sdk
    mkdir -p "$ROOT/build"
    if [[ -f "$STAMP" && "$(cat "$STAMP")" == "$(sdk_state)" ]]; then
        return
    fi
    if [[ -n "$(git -C "$SDK" status --porcelain --untracked-files=no)" ]]; then
        echo "fw.sh: resetting the SDK checkout to its pinned commit before patching"
    fi
    git -C "$SDK" checkout -- .
    local f p
    for f in $(created_files); do
        rm -f "$SDK/$f"
    done
    for p in "$PATCHES"/*.patch; do
        git -C "$SDK" apply "$p" || die "$(basename "$p") doesn't apply to the pinned SDK"
        echo "fw.sh: applied $(basename "$p")"
    done
    sdk_state > "$STAMP"
}

# Hand the SDK token and the Google key to the build without printing them or
# letting git see them. A fresh build takes them from a private overlay; an
# existing sdkconfig keeps its own values over defaults, so update the lines
# in place there too.
secrets_overlay() {
    [[ -s "$TOKEN_FILE" ]] || echo "fw.sh: no $TOKEN_FILE: building without an SDK token (gadgets.muse.ai > Account > SDK tokens)" >&2
    [[ -s "$KEY_FILE" ]] || echo "fw.sh: no $KEY_FILE: building without a Google API key (the button can't hear or speak)" >&2
    mkdir -p "$BUILD"
    python3 - "$TOKEN_FILE" "$KEY_FILE" "$BUILD" <<'PY'
import os, re, sys
token_file, key_file, build = sys.argv[1:4]

def read(path):
    try:
        return open(path).read().strip()
    except FileNotFoundError:
        return ""

token, key = read(token_file), read(key_file)
if token and (not re.fullmatch(r"mgst_[A-Za-z0-9_-]*[AEIMQUYcgkosw048]", token) or len(token) != 48):
    sys.exit("fw.sh: sdk_token.local doesn't look like an SDK token (48 characters, mgst_...)")
if key and not re.fullmatch(r"[A-Za-z0-9_-]{30,64}", key):
    sys.exit("fw.sh: google_api_key.local doesn't look like a Google API key (AIza...)")
lines = {
    "CONFIG_GADGET_SDK_TOKEN": f'CONFIG_GADGET_SDK_TOKEN="{token}"\n',
    "CONFIG_GROCERY_GOOGLE_API_KEY": f'CONFIG_GROCERY_GOOGLE_API_KEY="{key}"\n',
}
overlay = os.path.join(build, "sdkconfig.secrets")
fd = os.open(overlay, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
with os.fdopen(fd, "w") as f:
    f.writelines(lines.values())
cfg = os.path.join(build, "sdkconfig")
if os.path.exists(cfg):
    text = new = open(cfg).read()
    for name, line in lines.items():
        new, n = re.subn(rf"^{name}=.*\n", line, new, flags=re.M)
        if not n:
            new += line
    if new != text:
        open(cfg, "w").write(new)
PY
    export MUSE_EXTRA_DEFAULTS="$ROOT/firmware/sdkconfig.grocery;$BUILD/sdkconfig.secrets"
    export MUSE_EXTRA_COMPONENT_DIRS="$ROOT/components"
}

idf_env() {
    if [[ -n "${IDF_EXPORT:-}" ]]; then
        # shellcheck disable=SC1090
        . "$IDF_EXPORT" >/dev/null 2>&1
    elif ! command -v idf.py >/dev/null 2>&1; then
        local d
        for d in "$HOME/.espressif/esp-idf-v6.0.1" "$HOME/esp/esp-idf-v6.0.1"; do
            # shellcheck disable=SC1091
            [[ -f "$d/export.sh" ]] && { . "$d/export.sh" >/dev/null 2>&1; break; }
        done
    fi
    command -v idf.py >/dev/null 2>&1 || die "ESP-IDF v6.0.1 not found (see docs/06-firmware.md)"
}

build() {
    apply_patches
    secrets_overlay
    "$ESP32/tools/muse/board.sh" build "$BOARD"
}

case "${1:-}" in
build)
    build
    ;;
flash)
    build
    "$ESP32/tools/muse/board.sh" flash "$BOARD" "${2:-}"
    ;;
monitor)
    # tools/fw.sh monitor [PORT] [--no-reset ...]: options go to idf.py monitor.
    shift
    sel=""
    if [[ $# -gt 0 && "$1" != -* ]]; then
        sel="$1"
        shift
    fi
    idf_env
    port="$(python3 "$ESP32/tools/muse/ports.py" "$BOARD" $sel)"
    cd "$ESP32" && exec idf.py -B "$BUILD" -p "$port" monitor "$@"
    ;;
*)
    sed -n '2,10p' "$0"
    exit 2
    ;;
esac
