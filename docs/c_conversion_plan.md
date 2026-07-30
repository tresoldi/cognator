# Cognator C Conversion Plan

This document plans the conversion of `cognator` from a Go implementation to a
C core library, a small C CLI, and a Python wrapper. The neighboring `merkmal`
repository is the model: C99 core, opaque public handles, explicit status
codes, CMake build, native C tests, Python `abi3` wrapper, and a WebAssembly
path with no required runtime filesystem access for built-in assets.

The C core and C CLI are now implemented. The Go implementation is frozen as
archival reference code while the Python wrapper is still future work.

## Target Outcome

- Drop the Go implementation after the C and Python surfaces cover the selected
  behavior and pass quality/regression checks.
- Provide a reusable C core for cognate detection.
- Keep the C core modular enough to add future detection methods without
  rewiring file IO, clustering, diagnostics, or wrappers.
- Provide a small C CLI named `cognator` with at least `detect`, `eval`,
  `version`, and `help`.
- Provide a Python package that can run detection from files and from in-memory
  records.
- Use `merkmal` as a C library, not by copying its internals.
- Keep WebAssembly viable from the beginning: explicit ownership, no global
  mutable state, predictable memory use, and a narrow ABI-friendly facade.

## Current Go Surface To Preserve

The frozen Go reference workflow was:

```sh
go run ./cmd/cognator detect --in forms.csv --out sidecar.tsv
go run ./cmd/cognator eval --pred sidecar.tsv --truth forms.csv
```

The active C workflow is:

```sh
./build/cognator detect --in forms.csv --out sidecar.tsv
./build/cognator eval --pred sidecar.tsv --truth forms.csv
./build/cognator sweep --in forms.csv
./build/cognator bench --in forms.csv --out-tsv benchmark.tsv
./build/cognator diag --in forms.csv
./build/cognator oovscan --in forms.csv
```

Input columns:

```text
ID  Language_ID  Concept  Segments  [Family]  [Cognacy]
```

Supported aliases include `Concepticon_Gloss`, `Parameter_ID`, `Dataset`,
`av_id`, `concept_id`, `concept_label`, and synthesized IDs from
`av_id::source_form_id` when `ID` is absent.

Detection output columns:

```text
ID
Cognator_ID
Cognator_Confidence
Cognator_Assigned_Support
Cognator_Alternative_Support
Cognator_Support_Margin
Cognator_Cluster_Size
Cognator_Candidate_Neighbors
Cognator_Bootstrap
Cognator_Bootstrap_Low
Cognator_Bootstrap_High
```

The C rewrite should initially preserve these columns and their formatting so
existing benchmark and diagnostic scripts keep working.

## Important Existing Behavior

The current v1 pipeline is not the final framework model, but it is the baseline
to preserve during the rewrite:

- Per-family processing.
- Per-concept clustering by default.
- Optional global clustering exists in Go, but the first C release should defer
  it. The supported target is per-concept clustering; global mode is
  experimental, precision-risky, and memory-heavy.
- Space-separated grapheme input with `+` as morpheme boundary.
- CLDF-style standalone tone tokens re-attached to the preceding nucleus before
  scoring.
- OOV graphemes recorded for a report, not fatal during detection.
- Class reduction is enabled by default for the CLI.
- Combined alignment is the default: global NW + local SW + overlap.
- The first C default scorer should be the posterior/Dirichlet scorer, not the
  legacy learned log-odds scorer. The rewrite target is a good practical
  cognate detector, preferably competitive with or stronger than LingPy, not
  byte-for-byte preservation of the Go baseline.
- Prosody-aware prior scorer is the production default.
- Ordered lect-pair correspondence learning streams worker-local scorers instead
  of retaining all O(L^2) scorer tables.
- Graph construction supports threshold mode and support mode with top-k,
  mutual/adaptive expansion, triangle boost, contrastive penalty, and bridge-cut
  refinement.
- Otsu thresholding is clamped by named defaults.
- Confidence uses assigned-vs-alternative support.
- Benchmark guards exist because large families can create large transient edge
  allocations.

## Dependency Boundary With Merkmal

Cognator should depend only on the public `merkmal` C API:

- `mk_registry_new_builtin`, `mk_registry_get_system`, `mk_registry_free`
- `mk_system_is_segment`
- `mk_system_grapheme_features`
- `mk_system_segment_distance`
- `mk_normalize_grapheme`
- `mk_segment_ipa`, `mk_merge_tone_digits`, `mk_segment_ipa_merged` where useful
- `mk_string_list_*`, `mk_feature_set_*`, `mk_free_string`

Cognator should not read merkmal model TSV/JSON files directly in the first C
implementation. The default should use merkmal built-in systems, especially
`descriptive`.

Open decision: prosodic role lookup currently uses the old Go merkmal role API.
The current C API exposes feature sets but not a role function. The C rewrite
should derive roles from feature names if possible, and otherwise request a
small public merkmal helper such as `mk_system_grapheme_role`.

## Proposed Repository Layout

```text
include/
  cognator.h

src/
  align.c
  cluster.c
  confidence.c
  detect.c
  evidence.c
  eval.c
  options.c
  rng.c
  score_legacy.c
  score_posterior.c
  segment.c
  table.c
  wordlist.c
  internal/
    alloc.h
    array.h
    hash.h
    sort.h
    string_pool.h
    utf8.h

cmd/
  cognator/
    main.c

tests/
  c/
    test_align.c
    test_cluster.c
    test_confidence.c
    test_evidence.c
    test_eval.c
    test_segment.c
    test_wordlist.c
    test_pipeline_fixtures.c
  fixtures/
    ...

python/
  pyproject.toml
  setup.py
  cognator/
    __init__.py
    __main__.py
    cli.py
    py.typed
  src/
    cognator_module.c

web/
  ...
```

The final removal pass should delete `go.mod`, `go.sum`, `internal/*.go`,
`cmd/*.go`, and Go WASM assets once C/WASM replaces them.

## Public C API Shape

Use C99, opaque handles, explicit status codes, and caller-owned output objects.
The prefix should be `cg_` to avoid collision with `mk_`.

Initial sketch:

```c
typedef struct cg_context cg_context;
typedef struct cg_records cg_records;
typedef struct cg_result cg_result;
typedef struct cg_sidecar_rows cg_sidecar_rows;
typedef struct cg_oov_report cg_oov_report;

typedef enum cg_status {
    CG_OK = 0,
    CG_ERR_INVALID_ARGUMENT,
    CG_ERR_IO,
    CG_ERR_PARSE,
    CG_ERR_MERKMAL,
    CG_ERR_UNSUPPORTED_OPTION,
    CG_ERR_OOM
} cg_status;

typedef struct cg_options {
    int iterations;
    double lambda;
    double threshold;
    const char *threshold_mode;
    double threshold_min;
    double threshold_max;
    double threshold_shrink;
    const char *graph_mode;
    const char *graph_expansion_mode;
    int graph_top_k;
    double graph_adaptive_margin;
    double graph_adaptive_either_max;
    double graph_adaptive_ambiguity;
    double graph_mutual_boost;
    double graph_triangle_boost;
    double graph_contrastive_penalty;
    double graph_contrastive_floor;
    double graph_risk_rank_weight;
    double graph_risk_gap_weight;
    double graph_unilateral_penalty;
    double graph_rival_floor;
    const char *refine_mode;
    double refine_confidence_threshold;
    double refine_bridge_ratio;
    unsigned long long seed;
    int jobs;
    int bootstrap;
    double bootstrap_sigma;
    const char *cluster_scope;
    int min_pairs;
    const char *alignment_mode;
    int class_reduce;
    const char *null_mode;
    int null_max_runs;
    const char *scorer_mode;
    double dirichlet_concentration;
    double dirichlet_sharpness;
    const char *dirichlet_scale_mode;
    const char *merkmal_system;
} cg_options;

cg_status cg_context_new_builtin(cg_context **out);
void cg_context_free(cg_context *ctx);

void cg_options_init_defaults(cg_options *options);
cg_status cg_options_apply_preset(cg_options *options, const char *preset);

cg_status cg_read_table(const char *path, cg_records **out);
void cg_records_free(cg_records *records);

cg_status cg_detect(
    cg_context *ctx,
    const cg_records *records,
    const cg_options *options,
    cg_result **out
);
void cg_result_free(cg_result *result);

cg_status cg_write_sidecar(const char *path, const cg_result *result);
cg_status cg_write_oov_report(const char *path, const cg_result *result);

cg_status cg_eval_b_cubed(
    const char *pred_path,
    const char *truth_path,
    int per_concept,
    double *precision,
    double *recall,
    double *f1
);
```

Later API expansion can expose in-memory record builders, result iteration, and
method plugins. Do not expose internal graph or scorer structs until there is a
consumer that needs them.

## Method Modularity

Separate the stable orchestration from the detection method:

```text
records -> normalization/tokenization -> method -> graph/partition -> sidecar
```

Suggested internal method interface:

```c
typedef struct cg_method_vtable {
    const char *name;
    cg_status (*prepare_family)(...);
    cg_status (*score_concept_pairs)(...);
    void (*free_state)(...);
} cg_method_vtable;
```

First method:

- `posterior_nw`: the current Dirichlet-posterior scorer promoted as the first
  production method, using the existing combined-alignment and graph pipeline.
  The legacy Go behavior remains a regression reference, not the target default.

Reserved future methods:

- `legacy_nw`: current learned log-odds scorer retained only if it helps
  debugging or benchmark comparison.
- `regulae_bridge`: future method that consumes regulae-style correspondence
  units instead of segment-only NW distances.
- `external_distances`: accepts caller-supplied pair distances for experiments.

## C Implementation Strategy

Use small, purpose-built containers rather than introducing a broad dependency:

- String interner/string pool for record IDs, lects, concepts, families, and
  graphemes.
- Dynamic arrays for rows, tokens, edges, distances, and adjacency lists.
- Hash tables for string keys and pair keys. Either implement a narrow internal
  open-addressed table or vendor a single-header implementation only if approved.
- Stable sorting wrappers for deterministic row order, concept order, lect order,
  and neighbor ranking.
- A deterministic PRNG with documented regression expectations. It does not need
  to match Go's PCG bit-for-bit, but the same seed must produce stable C output.
- Threading via a small worker-pool abstraction. Start with POSIX threads for
  native builds and a serial fallback for WASM.

## Build And Packaging

Mirror merkmal:

- Top-level `CMakeLists.txt`.
- `COGNATOR_BUILD_TESTS=ON` by default.
- `COGNATOR_BUILD_CLI=ON` by default.
- `COGNATOR_ENABLE_SANITIZER=""|"address"|"undefined"`.
- `find_package(merkmal CONFIG)` first.
- Development fallback: allow `-Dmerkmal_DIR=../merkmal/build/c-debug` or
  `FetchContent`/subdirectory only when explicitly configured.
- Install `include/cognator.h`, `libcognator`, CMake package config, and
  `cognator.pc`.

Python packaging should follow `../merkmal/python`:

- CPython extension using the Limited API / `abi3`.
- The Python wheel should be self-contained for users: `pip install cognator`
  should be enough on supported platforms. Prefer vendoring/building the
  required Cognator and merkmal C sources into the wheel, matching merkmal's
  current wheel pattern. Linking to a system `libmerkmal` can remain a
  development or downstream-packager option.
- Expose Python functions that are thin over C, with Python convenience wrappers
  for table-like inputs.

Suggested Python API:

```python
import cognator

result = cognator.detect_file("forms.csv", preset="support-adaptive-bridge")
result.write_sidecar("sidecar.tsv")

rows = cognator.detect(records, system="descriptive")
```

## WebAssembly Plan

Design for a later Emscripten build from the start:

- No mandatory filesystem access for default merkmal systems.
- Serial execution fallback when pthreads are unavailable.
- A C ABI function that accepts UTF-8 CSV/TSV text and JSON-ish options can be
  added as a thin WASM facade, separate from the native C API.
- Keep large temporary allocations visible and freeable from one owning result
  or arena.
- Avoid process-global mutable registries. A `cg_context` owns the merkmal
  registry pointer and derived role cache.

First WASM milestone should replace the Go `cmd/cognatorwasm` path with a C
module plus a small JS worker wrapper.

## Migration Milestones

### Milestone 0: Baseline Freeze

- Record current Go benchmark outputs for small deterministic fixtures. These
  are regression references, not exact parity requirements.
- Add golden sidecar outputs for:
  - a tiny artificial fixture covering singleton, pair, and bridge cases
  - Walworth Polynesian subset
  - Latin-Romance triplets
- Run and archive:
  - `go test ./...`
  - `go test -race ./...`
  - representative `detect` commands with current defaults and the
    `support-adaptive-bridge` preset
- Define tolerance-based comparisons: cluster-equivalent output up to relabeling
  and numeric fields within tolerances is sufficient.

### Milestone 1: C Skeleton And Merkmal Integration

- Add CMake project, `include/cognator.h`, status handling, allocation helpers,
  and C tests.
- Link to `merkmal::merkmal`.
- Implement `cg_context` with built-in merkmal registry and selected system.
- Implement grapheme known/distance lookup and role derivation.
- Add sanitizer builds.

### Milestone 2: Input, Segmentation, And Output

- Port wordlist reader/writer behavior.
- Port tokenization, tone attachment, OOV accounting, and class reduction.
- Preserve sidecar schema and formatting.
- Add tests for aliases, synthesized IDs, CLDF gaps, morpheme boundaries, and
  tone tokens.

### Milestone 3: Alignment And Scoring

- Port prior scorer with prosody penalty.
- Port NW, local, overlap, and combined distance.
- Port posterior Dirichlet scorer as the default method.
- Port legacy correspondence table training and learned scorer only after the
  default path is working, if it remains useful for regression comparison.
- Add regression tests against Go fixture distances and scorer outputs where
  helpful, but do not require exact full-sidecar parity.

### Milestone 4: Graph, Clustering, Confidence

- Port threshold/support evidence construction.
- Port Otsu thresholding and clamps.
- Port Infomap greedy optimizer and flat fallback if still needed.
- Port bridge-cut refinement.
- Port confidence diagnostics and bootstrap.
- Add graph and partition regression tests.

### Milestone 5: End-To-End Detect

- Implement family/concept orchestration.
- Keep first-release support scoped to per-concept clustering.
- Preserve streaming per-lect-pair scorer lifecycle to avoid memory spikes.
- Add native parallelism with deterministic result folding.
- Run fixture sidecar regression checks using cluster equivalence and numeric
  tolerances.
- Run small benchmark splits and compare B-cubed scores against Go, LingPy, and
  the current research target. Treat quality as the release gate.

### Milestone 6: CLI

- Implement `cognator detect`.
- Implement `cognator eval`.
- Preserve presets and option names where possible.
- Add CLI smoke tests.
- Keep old Go CLI available only as archival reference for parity checks.

### Milestone 7: Python Wrapper

- Add `python/` package with abi3 extension.
- Expose file-based and in-memory detection.
- Expose B-cubed evaluation.
- Add Python tests for wrapper behavior and error translation.

### Milestone 8: Go Removal

- Remove Go source and module files.
- Update README, AGENTS.md, docs, scripts, and benchmark commands.
- Replace `go run` invocations in scripts with the C CLI or Python wrapper.
- Keep historical Go benchmark artifacts under `benchmark/` if useful.

### Milestone 9: WebAssembly

- Add Emscripten CMake build.
- Add minimal JS worker facade.
- Replace Go `wasm_exec.js` dependency.
- Port browser smoke test and canvas/UI integration as needed.

## Validation Policy

Use three layers:

- Unit regression: C tests matching intended Go package behavior for wordlist,
  segment, alignment, scorer, evidence, cluster, confidence, and eval. Exact
  floating-point parity is not required.
- End-to-end regression: sidecar outputs on frozen fixtures. Compare cluster IDs
  up to relabeling and numeric fields within tolerances.
- Quality gate: benchmark B-cubed scores should be competitive with the current
  best Cognator configuration and should explicitly track LingPy comparisons.
- Linguistic assessment: every major milestone gets a short narrative check on
  known-clean fixtures, especially Walworth Polynesian and Latin-Romance.

Run native checks:

```sh
cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build/debug
ctest --test-dir build/debug --output-on-failure

cmake -S . -B build/asan -DCOGNATOR_ENABLE_SANITIZER=address
cmake --build build/asan
ctest --test-dir build/asan --output-on-failure
```

## Risks

- The Go implementation depends on `golang.org/x/text/unicode/norm` for class
  reduction. In C this should use merkmal normalization or utf8proc through
  merkmal; otherwise behavior can drift.
- Prosodic role derivation is not currently explicit in the merkmal C API.
- Infomap uses randomized node order. C determinism must be specified before
  regression work starts.
- Large-family memory spikes will not disappear automatically. The C design
  should still stream scorer state and should consider per-concept edge
  streaming before raising benchmark caps.
- Self-contained Python wheels that vendor both Cognator and merkmal need a
  clear versioning policy so the linked merkmal behavior is reproducible.
- WebAssembly threading and memory ceilings may force serial execution and
  smaller default guards than native builds.
- Rewriting everything before a baseline freeze would make regressions hard to
  localize.

## Questions To Resolve Before Coding

Resolved:

- First C default scorer: promote `posterior`.
- Exact Go parity: not required; use cluster-equivalent and tolerance-based
  regression checks.
- First C clustering scope: per-concept only; defer `global`.
- Python packaging: optimize for `pip install cognator` via self-contained
  wheels.
- Prosodic roles: decide during implementation; prefer a small public merkmal
  role API if the feature-name derivation is brittle.
- Native threading: POSIX pthreads are acceptable, with serial fallback on WASM
  and unsupported platforms.

Open:

1. Should benchmark and diagnostic Python scripts be preserved as scripts
   calling the C CLI, or moved into the Python package API?
2. What is the minimum supported Python version for the wrapper?
3. Should the first C API expose only file-based detection, or must in-memory
   record construction be available from day one?
4. Should the Go implementation be removed in one final milestone after
   regression/quality gates pass, or should Go directories be deleted earlier
   once each C module lands?
