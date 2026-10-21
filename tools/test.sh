#!/usr/bin/env bash
# Host tests for the grocery button's conversation logic (no board, no ESP-IDF):
#
#   tools/test.sh
#
# Builds tests/test_grocery.c with the address and undefined-behaviour
# sanitizers and runs it.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/components/grocery_button"
OUT="$ROOT/build/test"

# Some Command Line Tools releases ship a newest macOS SDK their own linker
# can't read ("tapi error: malformed file"). If the default SDK can't link a
# trivial program, fall back to the newest one that can.
pick_macos_sdk() {
    [[ "$(uname)" == Darwin && -z "${SDKROOT:-}" ]] || return 0
    local probe
    probe="$(mktemp -d)"
    echo 'int main(void){return 0;}' > "$probe/t.c"
    if ! cc "$probe/t.c" -o "$probe/t" 2>/dev/null; then
        local sdk
        for sdk in $(ls -d /Library/Developer/CommandLineTools/SDKs/MacOSX[0-9]*.sdk 2>/dev/null | sort -rV); do
            if SDKROOT="$sdk" cc "$probe/t.c" -o "$probe/t" 2>/dev/null; then
                export SDKROOT="$sdk"
                echo "test.sh: default macOS SDK can't link; using $(basename "$sdk")" >&2
                break
            fi
        done
    fi
    rm -rf "$probe"
}

pick_macos_sdk
mkdir -p "$OUT"
cc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    -I"$SRC/include" \
    "$ROOT/tests/test_grocery.c" \
    "$SRC/grocery_parse.c" "$SRC/grocery_script.c" "$SRC/grocery_outbox.c" "$SRC/grocery_speech_util.c" \
    -lm -o "$OUT/test_grocery"
"$OUT/test_grocery"
