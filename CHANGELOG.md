# Changelog

## Unreleased

### merkmal is pinned rather than found

The build looked for a merkmal beside the checkout and used whatever was there.
That is not a dependency declaration, and it mattered more here than it usually
does: cognator's thresholds — the `0.68` default, the `0.64‥0.74` adaptive
clamp, the graph margins — are numbers on merkmal's segment-distance scale, and
that scale moves when merkmal's descriptive feature inventory does.

Between the version cognator was calibrated against and merkmal at the time of
writing, that inventory went from four or five articulatory labels per grapheme
to eight to ten with SPE features added, and the distances contracted with it:

| pair | calibrated against | merkmal HEAD |
| --- | ---: | ---: |
| d(e, o) | 0.6667 | 0.2556 |
| d(i, w) | 0.6786 | 0.3872 |
| d(k, g) | 0.3750 | 0.1190 |

Enough to merge Latin *igne* with Italian *fuoco* on the Romance fixture, at
every threshold from 0.50 to 0.90. So merkmal is now a committed copy under
`third_party/merkmal` at a named commit, compiled into libcognator rather than
linked, with `scripts/merkmal-provenance.sh` failing if the tree stops matching
what `PINNED` records. `docs/merkmal_pin.md` covers moving it, which is a
recalibration and not an upgrade.

Building against another merkmal is still supported through
`COGNATOR_MERKMAL_SOURCE_DIR`, for packagers and for that recalibration work.
Nothing this repository reports holds under it.

### The WebAssembly build had a stack too small to run in

Emscripten's default stack is 64 KB and the detector's frames do not fit. The
overflow corrupted the small buffers `snprintf` writes cluster ids and row
counts into, so the engine returned `"rows":` followed by nothing and cluster
ids with the module index missing — a document `JSON.parse` refuses, from a
build that reported success and from C that is correct natively under ASan and
UBSan.

`STACK_SIZE=8MB`, and `STACK_OVERFLOW_CHECK` so the next one aborts instead.
`web/cognator_wasm.c` is compiled natively as a ctest case now, which is what
gives the browser engine's C its first sanitizer coverage; the engine and a
provenance stamp for it are committed, so `web/smoke_test.mjs` has something to
run against and a stale artifact cannot be deployed quietly.

### The page no longer fetches anything off-origin

It pulled six CodeMirror stylesheets, CodeMirror and d3 from a CDN. CodeMirror
is gone — it was decoration over a `textarea` whose fallback was already
written — and d3 is vendored at the version the page already pinned. The
stylesheet and script move out of `index.html` into `style.css` and `app.js`,
and the cache keys are the build's source hash rather than `Date.now()`, which
was defeating caching outright.

### Warnings, types, and the gates

- Warning flags were set on the library target alone, so the CLI, the C tests
  and the WebAssembly shim compiled with none. Directory-scoped now, with
  `COGNATOR_WERROR` for the gates to turn on. Two findings, both in tests.
- The package shipped `py.typed` while none of its 31 functions were annotated,
  which silently degraded every consumer's call site to `Any`. `mypy --strict`
  passes; the extension has a stub rather than an ignore.
- `scripts/check.sh` is the pre-commit gate, `.github/workflows/ci.yml` the
  second one, and `pages.yml` deploys `web/` behind the provenance and asset
  checks.
- merkmal's fallback Unicode path — what `pip install` and the browser build
  compile, and so what most users run — was tested by nothing. Both profiles
  are covered, and `COGNATOR_REQUIRE_UTF8PROC` stops the choice being silent.

### Also

- MIT `LICENSE`; the repository had none.
- `--allow-archival-go` is gone from the README, where it had survived the Go
  implementation's removal despite matching no flag in any script.
