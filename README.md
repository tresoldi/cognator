# cognator

`cognator` is an unsupervised cognate detection system for tokenized wordlists.
The main supported mode is **per-concept clustering within the same language
family**. Cross-concept/global clustering exists as an experimental path, but it
is not the core target.

It is a C99 library with a CLI, a Python wrapper around the same C, and a
browser build of it at [cognator.tresoldi.org](https://cognator.tresoldi.org/).
All three compile the same sources, including the pinned copy of
[merkmal](https://github.com/tresoldi/merkmal) that supplies the phonology.

## Current Status

The current research baseline is a support-aware graph policy with adaptive
top-k expansion and bridge-cut refinement. It is fast and competitive, but not
yet consistently stronger than LingPy `LexStat + Infomap`.

Current benchmark snapshot:

| Split | Datasets | Mean F1 | Mean P | Mean R | vs LingPy | Mean time |
|---|---:|---:|---:|---:|---|---:|
| Dev-Core | 9 | 0.8216 | 0.8523 | 0.8196 | not paired in latest diagnostic run | 1.0s |
| Dev-Full | 9 | 0.8287 | 0.8356 | 0.8457 | +0.0439 mean F1, 4/5 wins | 1.2s |
| Holdout | 7 | 0.8666 | 0.8282 | 0.9248 | -0.0352 mean F1, 3/4 wins | 571ms |

Interpretation:

- The system is much faster than LingPy on the evaluated splits.
- The main quality gap is not uniform: it wins some families clearly and loses
  others clearly.
- The dominant remaining issue is controlling false merges on merge-sensitive
  families while preserving the recall gains from support graph expansion.
- Confidence diagnostics are useful for review and error analysis, but fixed
  confidence thresholds are not yet stable enough for automatic decisions.

## Installation

Requirements:

- Python 3.9+
- A C99 compiler for source installs

Nothing else. merkmal is pinned under `third_party/merkmal` and compiled in, so
there is no second checkout, no CMake and no network involved in a source
install.

Install the Python package:

```sh
python -m pip install .
cognator --help
```

The packaging smoke test installs the project into a fresh virtual environment
with `pip install --no-deps --no-build-isolation .` and imports `cognator`, so
local source installs exercise the same C extension path users get.

Build a wheel:

```sh
python -m pip wheel . -w dist
```

For C library and C CLI development:

- CMake 3.20+
- A C99 compiler
- `libutf8proc` — optional, but see the note below
- Python 3 for the scripts under `scripts/`
- LingPy only to reproduce the LingPy baselines

Build and test:

```sh
cmake -S . -B build -DCOGNATOR_BUILD_TESTS=ON -DCOGNATOR_BUILD_CLI=ON
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

`scripts/check.sh` is what has to pass before a commit: the build under
`-Werror`, a second compiler, the suite, and both Unicode profiles.
`scripts/check.sh --full` adds the sanitizers and the WebAssembly build and
smoke test, and needs the Emscripten SDK.

About that option: merkmal normalizes through `libutf8proc` when it finds one
and through its own fallback otherwise, and the two do not normalize alike. It
picks silently, so a build that happens to find the library can cluster
differently from one that does not with nothing in the output saying which ran.
`-DCOGNATOR_REQUIRE_UTF8PROC=ON` turns its absence into a configure error. The
fallback is not a degraded mode — it is what `pip install` and the browser
build compile — and CI tests both.

The public C API ownership and ABI rules are documented in `docs/c_api.md`.

### merkmal is pinned, on purpose

Every threshold in this repository is a number on merkmal's segment-distance
scale, and that scale moves when merkmal's feature inventory does. The copy in
`third_party/merkmal` is therefore a specific upstream commit, named in
`third_party/merkmal/PINNED` and checked by
`scripts/merkmal-provenance.sh --check`. Building against a different merkmal
is supported — `-DCOGNATOR_MERKMAL_SOURCE_DIR=/path/to/merkmal`, or the same
name as an environment variable for `pip` — but none of the numbers below hold
under it. `docs/merkmal_pin.md` is what to do when the pin moves; it is a
recalibration, not an upgrade.

Which merkmal a build actually used is reported, not assumed:

```sh
cognator version                 # cognator 0.1.0 / merkmal 0.7.0 (3d21e0831241)
```

```python
import cognator
cognator.merkmal()               # ('0.7.0', '3d21e0831241')
```

`detect` prints it beside the resolved threshold, the browser page carries it
in its footer and in every result, and `cg_merkmal_version()` /
`cg_merkmal_commit()` return it from C. Record it beside any result you keep:
two runs of the same cognator against different merkmals are not comparable,
and nothing else in the output would tell you which one ran.

## Browser interface

[cognator.tresoldi.org](https://cognator.tresoldi.org/) runs the same C
compiled to WebAssembly — the detection happens in the browser, so nothing
is uploaded anywhere. The page is served from `web/`, which is deployed as it
stands: the engine is a committed artifact, and `web/BUILD_INFO` records the
hash of the sources it was built from so a stale one cannot be published
silently.

Rebuild it with the Emscripten SDK on your path:

```sh
./web/build-wasm.sh          # writes cognator.js, cognator.wasm, BUILD_INFO
node web/smoke_test.mjs
```

## Input Format

Input is an arcaverborum/CLDF-like CSV or TSV. The delimiter is inferred from
the file extension.

Required columns:

| Column | Aliases | Meaning |
|---|---|---|
| `ID` | none | form identifier |
| `Language_ID` | none | lect/language identifier |
| `Concept` | `Concepticon_Gloss`, `Parameter_ID` | concept label |
| `Segments` | none | space-separated graphemes |

Optional columns:

| Column | Aliases | Meaning |
|---|---|---|
| `Family` | `Dataset` | language family/dataset |
| `Cognacy` | none | gold cognate class, used only for evaluation |

`Segments` should contain space-separated graphemes. Morpheme boundaries may be
written as `+`.

## Basic Use

Run detection from the Python-installed CLI:

```sh
cognator detect \
  --in forms.csv \
  --out cognator_sidecar.tsv
```

Or from Python:

```python
import cognator

result = cognator.load("forms.csv").detect()
result.write_sidecar("cognator_sidecar.tsv")
metrics = result.evaluate(cognator.load("forms.csv"))
print(metrics.f1)
```

The output sidecar contains:

| Column | Meaning |
|---|---|
| `ID` | input form ID |
| `Cognator_ID` | predicted cognate class |
| `Cognator_Confidence` | bounded assigned-vs-alternative support score |
| `Cognator_Assigned_Support` | total support to the assigned cluster |
| `Cognator_Alternative_Support` | strongest support to a competing cluster |
| `Cognator_Support_Margin` | assigned minus alternative support |
| `Cognator_Bootstrap` | optional bootstrap stability mean |
| `Cognator_Bootstrap_Low` | optional 5% bootstrap interval endpoint |
| `Cognator_Bootstrap_High` | optional 95% bootstrap interval endpoint |

Python `CognateResult` objects also expose `resolved_threshold`, which is the
actual family-level threshold chosen by the adaptive policy.

### Weighted pairs and partial cognacy

`--pairs-out` additionally writes one weighted row per same-concept,
cross-lect pair: the `q(pairs)` a downstream correspondence model (regulae's
soft-cognacy loader reads the TSV directly) can reweight. With
`--pairs-unit morphemes` the forms are split at `+` and every cross-lect
morpheme pair within a concept is scored instead, and `--morpheme-ids` adds a
`Cognator_Morpheme_IDs` column (one id per morpheme, `-` for none) from
clustering the morphemes.

```sh
cognator detect --in forms.tsv --out sidecar.tsv \
  --pairs-out pairs.tsv --pairs-unit morphemes --morpheme-ids
cognator detect --in forms.tsv --out sidecar.tsv --pairs-out q_pairs.json
```

```
form_a    morph_a  form_b    morph_b  weight    distance  ...  null_n  null_scope
a_water   0        b_water   0        1.000000  0.000000  ...  256     stratum
a_water   1        b_water   1        0.000000  0.000000  ...  30      stratum
```

The weight is `1 -` the share of cross-concept, length-matched pairs from the
same lect pair that are at least as close, so the shared two-segment suffix
above gets 0 where the identical root gets 1. It is a background-match rate,
not a probability of cognacy; [docs/pairs.md](docs/pairs.md) has the exact
definition, its seed, and its limits. Without these options nothing in the
output changes.

Score a sidecar against gold cognacy with the C CLI:

```sh
./build/cognator eval \
  --pred cognator_sidecar.tsv \
  --truth forms.csv
```

The evaluator reports B-cubed precision, recall, and F1. By default it scores
cognacy within concept, which is the supported target.

## Recommended Research Configuration

The recommended C preset uses support-aware graph expansion, bridge-cut
refinement, and a family-adaptive threshold clamped to the lower-threshold band
that performed best in the wider LingPy comparison; pass a positive
`--threshold` only when running a fixed-threshold sweep:

```sh
./build/cognator detect \
  --in forms.csv \
  --out cognator_sidecar.tsv \
  --preset support-adaptive-bridge \
  --threshold 0 \
  --graph-mode support \
  --graph-expansion-mode adaptive \
  --graph-top-k 3 \
  --graph-adaptive-margin 0.1 \
  --graph-adaptive-either-max 0.15 \
  --graph-adaptive-ambiguity 0.5 \
  --graph-mutual-boost 0.2 \
  --graph-triangle-boost 0.3 \
  --graph-contrastive-penalty 0.20 \
  --graph-contrastive-floor 0.05 \
  --graph-risk-rank-weight 0.5 \
  --graph-risk-gap-weight 0.5 \
  --graph-risk-triangle-weight 0.0 \
  --graph-risk-unilateral-weight 0.0 \
  --refine-mode bridge-cut \
  --refine-confidence-threshold 0.55 \
  --refine-bridge-ratio 0.7
```

Bootstrap intervals are optional and more expensive:

```sh
./build/cognator detect \
  --in forms.csv \
  --out cognator_sidecar.tsv \
  --bootstrap 50 \
  --bootstrap-sigma 0.05
```

## Benchmarking

The normal benchmark path uses the C CLI for local datasets and the Python
wrapper for reproducible C/Python-vs-LingPy quality samples.

Run the C benchmark summary on a Cognator-shaped table:

```sh
./build/cognator bench \
  --in forms.csv \
  --out-tsv benchmark/results.tsv \
  --preset support-adaptive-bridge \
  --threshold 0 \
  --bootstrap 0 \
  --min-pairs 1
```

Run the reproducible C/Python-vs-LingPy Lexibank sample:

```sh
PYTHONPATH=python python scripts/run_milestone24_benchmark.py \
  --out-dir benchmark/milestone24
```

The comparison scripts drive the C CLI and the Python wrapper.

Export evaluation tables from a built arcaverborum repository:

```sh
python scripts/export_arcaverborum_eval.py \
  --arcaverborum-root ../arcaverborum \
  --collection corecog \
  --out-dir benchmark/export
```

Run the full Cognator vs LingPy pipeline:

```sh
python scripts/run_benchmark_pipeline.py \
  --forms ../arcaverborum/output/corecog/forms.csv \
  --datasets-csv ../arcaverborum/datasets.csv \
  --split-file benchmark/splits/dev_core.txt \
  --track A \
  --cognator-extra-args "--threshold 0.68 --graph-mode support --graph-expansion-mode adaptive --graph-top-k 3 --graph-contrastive-penalty 0.20 --graph-contrastive-floor 0.05 --refine-mode bridge-cut --refine-confidence-threshold 0.55 --refine-bridge-ratio 0.7"
```

Generate a detailed rerunnable arcaverborum comparison report:

```sh
python scripts/generate_arcaverborum_report.py --mode existing
```

`existing` mode reads local benchmark run directories that already contain
`comparison.tsv`; use `--run-dir LABEL=PATH` to point it at different artifacts.
To update the results from a fresh arcaverborum export and new Cognator/LingPy
runs, use:

```sh
python scripts/generate_arcaverborum_report.py \
  --mode fresh \
  --arcaverborum-root ../arcaverborum
```

## Diagnostics

Pairwise error decomposition:

```sh
python scripts/error_report.py \
  --predictions benchmark/predictions.tsv \
  --out-tsv benchmark/errors_by_concept.tsv \
  --out-dataset-tsv benchmark/errors_by_dataset.tsv \
  --out-md benchmark/error_report.md
```

Confidence diagnostic report:

```sh
python scripts/confidence_report.py \
  --predictions benchmark/predictions.tsv \
  --out-tsv benchmark/confidence_report.tsv \
  --out-curves-tsv benchmark/confidence_curves.tsv \
  --out-md benchmark/confidence_report.md
```

Low-confidence manual review queue:

```sh
python scripts/flag_low_confidence.py \
  --predictions benchmark/predictions.tsv \
  --out-tsv benchmark/flagged_low_confidence.tsv \
  --out-md benchmark/flagged_low_confidence.md
```

The review queue is diagnostic-only. On the current holdout run, the top 200
flagged rows were 81.5% error-involved, but threshold transfer was not stable
enough to use these flags as model behavior.

Confidence calibration (isotonic PAV, stdlib only):

```sh
# Fit on a Dev-Core run. Recommended: stratify by cluster size +
# assigned-support quartile, label with the form-local per-form
# correctness rule.
python scripts/calibrate_confidence.py fit \
  --predictions benchmark/runs/dev_core/cognator_predictions.tsv \
  --out-json benchmark/calibration.json \
  --out-md benchmark/calibration_fit.md \
  --stratify-by cluster_size_support \
  --error-label form-local

# Apply it to any other predictions TSV to add
# `confidence_calibrated` and `risk_band` columns.
python scripts/calibrate_confidence.py apply \
  --calibration benchmark/calibration.json \
  --predictions benchmark/runs/holdout/cognator_predictions.tsv \
  --out-tsv benchmark/holdout_calibrated.tsv \
  --out-md benchmark/holdout_transfer.md
```

On a recent Dev-Core → Holdout fit, the top support tier's "low" band
covers 33% of Holdout rows at 3% empirical error rate, and the third
tier's "medium" band covers another 18% at 6% error. Together ~50% of
rows sit in a calibrated band with <7% error — paper-ready. See
`docs/roadmap.md` Model §2 for the full risk-band transfer table.

## Architecture

The current pipeline is:

1. Read arcaverborum-shaped records.
2. Tokenize segments with the merkmal bundle and optional class reduction.
3. Train per-lect-pair correspondence scorers from shared-concept pairs.
4. Compute pairwise distances within each concept.
5. Convert distances to graph edges.
6. Cluster each concept graph with a greedy two-level Infomap implementation.
7. Optionally refine clusters.
8. Emit sidecar rows with cluster IDs and confidence diagnostics.

Important packages:

| Package | Role |
|---|---|
| `src/wordlist.c` | input/output tables |
| `src/segment.c` | grapheme parsing and class reduction |
| `third_party/merkmal` (pinned, compiled in) | distance/prosody/class features |
| `src/align.c` | alignment distances |
| `src/posterior.c` | correspondence and posterior scoring |
| `src/graph.c` | distance-to-graph construction and clustering |
| `src/detect.c` | orchestration, refinement, confidence |
| `src/pairs.c` | pairwise export, empirical null, morpheme clusters |
| `src/eval.c` | B-cubed evaluation |

For deeper engineering direction, see [docs/roadmap.md](docs/roadmap.md).

## Known Limitations

- The code defaults are not the best research settings.
- Large families can still trigger high transient memory use when pair-heavy
  concepts exceed the current guards; benchmark runs now report
  `estimated_pairs` and skip families above `--max-pairs`.
- `src/detect.c` owns too many responsibilities and should be split into
  evidence generation, calibration, graph policy, clustering, and confidence.
- Confidence is useful for ranking review cases, but it is not yet calibrated as
  a probability.
- Assignment-changing alternative-support refinements improved dev subsets but
  did not generalize, so they remain experimental.

## License

MIT, in `LICENSE`.

`third_party/merkmal` is a copy of [merkmal](https://github.com/tresoldi/merkmal)
at the commit in its `PINNED` file, under its own MIT license, and `web/vendor`
holds [d3](https://d3js.org/) v7.9.0 under the ISC license. Both keep the
upstream license file beside them.

## Citation

If cognator is useful in work you publish:

```bibtex
@software{tresoldi_cognator,
  author  = {Tresoldi, Tiago},
  title   = {cognator: framework-native cognate detection},
  year    = {2026},
  url     = {https://github.com/tresoldi/cognator}
}
```
