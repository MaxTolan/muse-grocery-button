#!/usr/bin/env bash
# Renders the round status screen in each state to docs/previews/ (headless,
# no board): tools/preview.sh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build/preview"

# The same fallback as tools/test.sh for a macOS SDK the linker can't read.
if [[ "$(uname)" == Darwin && -z "${SDKROOT:-}" ]]; then
    probe="$(mktemp -d)"
    echo 'int main(void){return 0;}' > "$probe/t.c"
    if ! cc "$probe/t.c" -o "$probe/t" 2>/dev/null; then
        for sdk in $(ls -d /Library/Developer/CommandLineTools/SDKs/MacOSX[0-9]*.sdk 2>/dev/null | sort -rV); do
            if SDKROOT="$sdk" cc "$probe/t.c" -o "$probe/t" 2>/dev/null; then
                export SDKROOT="$sdk"
                break
            fi
        done
    fi
    rm -rf "$probe"
fi

cmake -S "$ROOT/tools/preview" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$BUILD" -j >/dev/null
mkdir -p "$ROOT/docs/previews"
"$BUILD/grocery_preview" "$ROOT/docs/previews"
