#!/usr/bin/env bash
#
# Everything that has to hold before a commit, in one place, failing loudly.
#
#   scripts/check.sh            build, tests, Python wrapper, pinned dependency
#   scripts/check.sh --full     the above plus sanitizers and WebAssembly
#
# Every step is checked by its exit status rather than by grepping its output
# for the word "error" -- CMake prints "Error" itself, so a filter like that
# reports a clean run on a build that never happened. `set -e` stops at the
# first failure and the failing command's own output is shown.
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_dir"

full=0
if [ "${1:-}" = "--full" ]; then
    full=1
fi

jobs="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)"

step() { printf '\n=== %s\n' "$1"; }
fail() { printf '\nFAILED: %s\n' "$1" >&2; exit 1; }

# First, because everything below is measured against this particular merkmal
# and a modified copy invalidates all of it. It costs milliseconds.
step "pinned merkmal"
./scripts/merkmal-provenance.sh --check || fail "third_party/merkmal is not the pinned snapshot"

step "native build"
# -Werror here rather than in the build's defaults: a warning is a reason to
# stop before committing, not a reason to deny some consumer on another
# compiler a build at all. REQUIRE_UTF8PROC because merkmal picks between
# utf8proc and its own fallback silently, and the two do not normalize alike --
# without this the suite tests whichever one this machine happens to have.
cmake -S . -B build/c \
    -DCOGNATOR_WERROR=ON \
    -DCOGNATOR_REQUIRE_UTF8PROC=ON >/dev/null || fail "cmake configure"
cmake --build build/c -j"$jobs" || fail "cmake build"

# A build system that silently declines to rebuild is the failure this whole
# script exists for. The CLI is checked against its own binary: cmd/ is not
# compiled into libcognator.a, so it stays newer than the archive forever and
# checking it there would fail on any commit that touches only the CLI.
step "build is current"
newest_source="$(find src include third_party -newer build/c/libcognator.a -type f 2>/dev/null | head -1 || true)"
[ -z "$newest_source" ] || fail "libcognator.a is older than $newest_source"
newest_cli_source="$(find cmd -newer build/c/cognator -type f 2>/dev/null | head -1 || true)"
[ -z "$newest_cli_source" ] || fail "the cognator binary is older than $newest_cli_source"

# A second compiler, because every assumption GCC happens to be lenient about
# is otherwise untested. The build is the cheap part and catches almost all of
# the divergence; --full also runs the suite there, which is where a difference
# that only shows at runtime would surface. CI runs both, unconditionally.
if command -v clang >/dev/null 2>&1; then
    step "second compiler"
    cmake -S . -B build/c-clang \
        -DCMAKE_C_COMPILER=clang \
        -DCOGNATOR_WERROR=ON \
        -DCOGNATOR_REQUIRE_UTF8PROC=ON >/dev/null || fail "clang configure"
    cmake --build build/c-clang -j"$jobs" || fail "clang build"
    if [ "$full" = "1" ]; then
        ctest --test-dir build/c-clang --output-on-failure -j"$jobs" || fail "clang ctest"
    fi
else
    printf '\n=== second compiler: clang not installed, skipped (CI runs it)\n'
fi

step "native tests"
ctest --test-dir build/c --output-on-failure -j"$jobs" || fail "ctest"

# merkmal's fallback normalization is not a degraded mode -- it is what a plain
# `pip install .` compiles, and what the WebAssembly build ships. Testing only
# the utf8proc profile leaves the one most users run unexercised.
step "fallback Unicode profile"
cmake -S . -B build/c-fallback \
    -DCOGNATOR_WERROR=ON \
    -DCMAKE_DISABLE_FIND_PACKAGE_PkgConfig=ON >/dev/null || fail "fallback configure"
cmake --build build/c-fallback -j"$jobs" || fail "fallback build"
ctest --test-dir build/c-fallback --output-on-failure -j"$jobs" || fail "fallback ctest"

if [ "$full" = "1" ]; then
    step "WebAssembly build"
    if [ -z "${EMSDK:-}" ]; then
        fail "--full needs the Emscripten SDK: source ~/emsdk/emsdk_env.sh"
    fi
    ./web/build-wasm.sh >/dev/null || fail "web/build-wasm.sh"
    node web/smoke_test.mjs || fail "web/smoke_test.mjs"
fi

# The committed engine is what the page deploys, so a source edit without a
# rebuild is a silently stale deploy. Checked even without --full, because the
# stamp is a hash of sources and needs no toolchain to verify.
step "WebAssembly build is current"
./scripts/wasm-provenance.sh --check || fail "web/ is older than the sources it claims"

if [ "$full" = "1" ]; then
    step "sanitizer build and tests"
    for san in address undefined; do
        cmake -S . -B "build/c-$san" \
            -DCOGNATOR_ENABLE_SANITIZER="$san" \
            -DCOGNATOR_WERROR=ON \
            -DCOGNATOR_REQUIRE_UTF8PROC=ON >/dev/null || fail "$san configure"
        cmake --build "build/c-$san" -j"$jobs" || fail "$san build"
        # The C suite only; the Python and packaging cases shell out to an
        # interpreter that is not instrumented, where a preloaded runtime buys
        # noise rather than coverage.
        ctest --test-dir "build/c-$san" --output-on-failure -j"$jobs" \
            -E 'python_api|python_install|package_consumer' || fail "$san ctest"
    done
fi

# The ctest suite already installs the package into a throwaway venv and
# imports it. What it does not do is lint or type-check, and both are cheap.
step "Python wrapper"
if python3 -m ruff --version >/dev/null 2>&1; then
    python3 -m ruff check python/cognator scripts setup.py || fail "ruff"
else
    printf '  ruff not installed, skipped (CI runs it)\n'
fi
if python3 -m mypy --version >/dev/null 2>&1; then
    python3 -m mypy python/cognator || fail "mypy"
else
    printf '  mypy not installed, skipped (CI runs it)\n'
fi

# Regenerating must not have changed anything: if it did, what is committed is
# stale. Untracked files are the author's business; modifications are not.
step "generated artifacts were already current"
dirty="$(git status --porcelain -- web/BUILD_INFO third_party/merkmal/PINNED | grep -v '^??' || true)"
if [ -n "$dirty" ]; then
    printf '%s\n' "$dirty" >&2
    fail "a provenance stamp is stale; review and commit it"
fi

printf '\nAll checks passed.\n'
