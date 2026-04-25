#!/bin/bash
#
# Build cognator WASM for browser use.
# Requires a Go toolchain (1.25+ matching go.mod). No Emscripten.
#
# Usage:
#   ./web/build-wasm.sh
#
# Artifacts written to web/:
#   cognator.wasm   — the compiled module (includes the embedded
#                      descriptive merkmal bundle)
#   wasm_exec.js    — Go runtime glue, copied from GOROOT
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

echo "Building cognator WASM for browser..."

cd "$REPO_DIR"

# Ensure the embedded merkmal bundle is in sync with the canonical
# fixture. This lets the source of truth stay in tests/fixtures and
# avoids a second place to update when the bundle version changes.
rsync -a --delete \
    tests/fixtures/merkmal-models/ \
    cmd/cognatorwasm/embedded/descriptive/

GOOS=js GOARCH=wasm go build \
    -trimpath -ldflags="-s -w" \
    -o "$SCRIPT_DIR/cognator.wasm" \
    ./cmd/cognatorwasm

# Copy wasm_exec.js from GOROOT. Go ships this under lib/wasm/ since
# 1.24; older layouts used misc/wasm/. Support both.
if [[ -f "$(go env GOROOT)/lib/wasm/wasm_exec.js" ]]; then
    cp "$(go env GOROOT)/lib/wasm/wasm_exec.js" "$SCRIPT_DIR/"
elif [[ -f "$(go env GOROOT)/misc/wasm/wasm_exec.js" ]]; then
    cp "$(go env GOROOT)/misc/wasm/wasm_exec.js" "$SCRIPT_DIR/"
else
    echo "error: wasm_exec.js not found under GOROOT" >&2
    exit 1
fi

echo ""
echo "WASM build complete:"
ls -lh "$SCRIPT_DIR/cognator.wasm" "$SCRIPT_DIR/wasm_exec.js"
echo ""
echo "To test: cd web && python3 -m http.server 8080"
