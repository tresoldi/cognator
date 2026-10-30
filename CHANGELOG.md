# Changelog

## Unreleased

### Weighted pairs, and partial cognacy from `+` boundaries

cognator computed a distance for every same-concept pair and kept none of
them: the only output was the partition. A correspondence model that wants
soft cognacy -- regulae's soft loader, the framework's `q(pairs)` -- had
nothing to read, and forms written with `+` boundaries were compared as whole
words with the boundaries thrown away.

`detect --pairs-out PATH` writes one row per same-concept cross-lect pair:
`form_a form_b weight distance global local overlap null_n null_scope`, or the
`q_pairs` JSON document (`role: "q"`, producer `cognator@<version>+merkmal.<commit>`,
diagnostics) when PATH ends in `.json` or with `--pairs-format json`. With
`--pairs-unit morphemes`, forms are split at `+` (a form without `+` is one
morpheme, so a corpus without boundaries gives the word weights) and every
cross-lect morpheme pair within a concept is scored with the scorers trained
on whole words; rows gain `morph_a morph_b`, JSON ids become `ID#k`. Morphemes
of one word are never paired. `--morpheme-ids` adds a trailing
`Cognator_Morpheme_IDs` sidecar column from running the existing graph policy
and Infomap on the morpheme nodes -- a convenience partition, not evidence.

The weight is an empirical null rather than a transform of the distance:
`1 -` the share of cross-concept pairs from the same lect pair, in the same
length bins, whose distance is no larger. Short units match by chance more
often, so a shared two-segment affix gets 0 where an identical five-segment
root at the same distance gets 1 -- the heightened chance rate short exponents
need. The sample is bounded (`--null-samples`, 256 per stratum) and seeded
(`--null-seed`); small strata widen to the whole lect pair, and every row says
which null it was read on. It is a background-match rate, not a calibrated
probability of cognacy, and `docs/pairs.md` says so along with the rest of
its limits: the 1.0 distance clamp compresses it, and on the small fixtures
unrelated forms sharing two good correspondences can reach 0.9.

The exported distance is the clustering distance bit for bit (one alignment
routine now returns the components too, and the prior-scorer case is checked
against the public API), and segment scores are memoized for the export so
the null costs a fraction of a run rather than ten of them. Without the new
options, sidecars are byte-identical to before on every fixture.

C: `cg_detect_with_pairs`, `cg_pairs_spec`, `cg_pair_row`,
`cg_pairs_summary`, `cg_result_pair_at`, `cg_result_pairs_copy` /
`cg_pair_rows_free`, `cg_result_pairs_tsv` / `_json`, `cg_write_pairs`,
`cg_write_sidecar_morphemes`, `cg_result_morpheme_ids_at`. New symbols only,
so `CG_ABI_VERSION` stays 1. Python: `detect(..., pairs=True | "words" |
"morphemes", morpheme_ids=..., null_samples=..., null_seed=...)`,
`CognateResult.pairs`, `.pairs_document`, `.write_pairs`, and
`cognator.detect_pairs`; the `cognator` console script takes the same flags.

The committed WebAssembly engine was not rebuilt: its build script now lists
`src/pairs.c`, and `web/BUILD_INFO` is stale until someone with Emscripten
runs `web/build-wasm.sh`. The page does not use the export.

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

### Results say which merkmal produced them

The pin is only half an answer while nothing in the output names it. A run
against a different merkmal is not comparable to any number here, and until now
a sidecar, a log line and a page all looked identical whichever one had been
compiled in.

`cg_merkmal_version()` and `cg_merkmal_commit()` are compiled in from
`third_party/merkmal/PINNED` and reported by `cognator version`, by the
`detect` summary beside the resolved threshold, by `cognator.merkmal()` in
Python, and by the browser page in its footer and in every result document.

`src/context.c` refuses to compile without them rather than accept a default,
because a wrong provenance is worse than no build and an empty one is
indistinguishable from a correct one at the point it is read. The check that
the reported value is the *right* one has to come from outside the library —
`cli_provenance` compares the binary against `PINNED`, since a test written
against the C API would assert the compiled-in string against itself and pass
on any value at all.

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

### The page's advanced controls now do something

Twenty-seven of the thirty-six controls in the configuration panel were parsed
by nothing. `apply_options` in the WebAssembly shim read eleven keys and
dropped the rest, so a visitor could set the graph policy, the scorer, the
refinement mode or any Dirichlet parameter, press Run, and get byte-identical
output. Both halves were individually correct — the page sent what it said it
sent, the engine read what it said it read — and nothing compared the two.

Every field with a control is read now, and a value the engine does not
recognise comes back as an error naming the option instead of falling silently
back to a default.

The preset was matched by looking for its name anywhere in the request, so a
wordlist containing a form or concept spelled `support-adaptive-bridge`
silently reconfigured the run. It is matched as the value of its own key.

`wasm_options` fails if `KNOB_IDS` in app.js grows a control `apply_options`
does not name, and the shim's own suite now asserts that two runs differing
only in a formerly-ignored option produce different results — the check that
would have caught this to begin with.

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
