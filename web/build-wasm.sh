#!/bin/bash
#
# Build cognator WASM for browser use from the C implementation. Requires
# Emscripten (`emcc`); merkmal comes from the pinned copy in third_party/, the
# same sources CMake and setup.py compile.
#
# STACK_SIZE is not a tuning knob here. Emscripten's default stack is 64 KB and
# the detector's frames do not fit in it, but nothing said so: the overflow ran
# off the end of the stack into whatever followed, and what it corrupted was
# the small char buffers that snprintf writes cluster ids and row counts into.
# The page rendered, the numbers were right, and the JSON came back with
# `"rows":` followed by nothing and cluster ids of `:NIGHT:` with the module
# index missing -- a malformed document that JSON.parse rejects, from a build
# that reported success. STACK_OVERFLOW_CHECK is on so that the next time this
# happens it aborts and says so.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
MERKMAL_DIR="${COGNATOR_MERKMAL_SOURCE_DIR:-$REPO_DIR/third_party/merkmal}"

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
    -sSTACK_SIZE=8MB \
    -sSTACK_OVERFLOW_CHECK=1 \
    -sEXPORTED_FUNCTIONS='["_cognator_detect_json","_cognator_free","_cognator_system"]' \
    -sEXPORTED_RUNTIME_METHODS='["ccall","UTF8ToString"]' \
    -o "$SCRIPT_DIR/cognator.js"

echo "WASM build complete:"
ls -lh "$SCRIPT_DIR/cognator.js" "$SCRIPT_DIR/cognator.wasm"

# The artifacts are committed, so the stamp that says which sources produced
# them has to move in the same commit. scripts/check.sh and the Pages deploy
# both refuse a stamp that has fallen behind.
"$REPO_DIR/scripts/wasm-provenance.sh" --write
