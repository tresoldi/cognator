#!/bin/bash
#
# Build cognator WASM for browser use from the C implementation.
# Requires Emscripten (`emcc`) and the vendored merkmal C runtime used by the
# Python package.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
MERKMAL_DIR="${COGNATOR_MERKMAL_SOURCE_DIR:-$REPO_DIR/python/vendor/merkmal}"

if ! command -v emcc >/dev/null 2>&1; then
    echo "error: emcc not found; install/activate Emscripten before building C/WASM" >&2
    exit 1
fi
if [[ ! -f "$MERKMAL_DIR/include/merkmal.h" ]]; then
    echo "error: merkmal C source not found at $MERKMAL_DIR" >&2
    exit 1
fi

cd "$REPO_DIR"

echo "Building C/WASM cognator..."
emcc \
    -std=c99 -O3 \
    -D_POSIX_C_SOURCE=200809L \
    -Iinclude -Isrc -I"$MERKMAL_DIR/include" -I"$MERKMAL_DIR/src" \
    web/cognator_wasm.c \
    src/context.c src/data.c src/detect.c src/eval.c src/align.c src/graph.c \
    src/oov.c src/options.c src/posterior.c src/segment.c src/wordlist.c \
    "$MERKMAL_DIR/src/feature_set.c" \
    "$MERKMAL_DIR/src/geometry.c" \
    "$MERKMAL_DIR/src/registry.c" \
    "$MERKMAL_DIR/src/status.c" \
    "$MERKMAL_DIR/src/string_list.c" \
    "$MERKMAL_DIR/src/system.c" \
    "$MERKMAL_DIR/src/unicode.c" \
    "$MERKMAL_DIR/src/generated/builtin_data.c" \
    -sMODULARIZE=1 \
    -sEXPORT_NAME=createCognatorModule \
    -sENVIRONMENT=web,worker,node \
    -sALLOW_MEMORY_GROWTH=1 \
    -sEXPORTED_FUNCTIONS='["_cognator_detect_json","_cognator_free","_cognator_system"]' \
    -sEXPORTED_RUNTIME_METHODS='["ccall","UTF8ToString"]' \
    -o "$SCRIPT_DIR/cognator.js"

echo "WASM build complete:"
ls -lh "$SCRIPT_DIR/cognator.js" "$SCRIPT_DIR/cognator.wasm"
