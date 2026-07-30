# cognator

`cognator` is an unsupervised cognate detection system for tokenized wordlists.
The main supported mode is **per-concept clustering within the same language
family**. Cross-concept/global clustering exists as an experimental path, but it
is not the core target.

The active implementation is the C library, C CLI, and Python wrapper; the old
Go implementation has been removed.

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
- No Go, CMake, LingPy, or separate merkmal checkout is required for the Python package

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
- The neighboring `merkmal` C rework checkout or an installed `merkmal` C package
- Python 3 for scripts
- LingPy only if you want to reproduce LingPy baselines

Build and test:

```sh
cmake -S . -B build -DCOGNATOR_BUILD_TESTS=ON -DCOGNATOR_BUILD_CLI=ON
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

The public C API ownership and ABI rules are documented in `docs/c_api.md`.

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

Arcaverborum comparison scripts now use the C/Python benchmark runner and no
longer require Go.

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
  --allow-archival-go \
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
  --arcaverborum-root ../arcaverborum \
  --allow-archival-go
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
| `python/vendor/merkmal` or linked `merkmal::merkmal` | distance/prosody/class features |
| `src/align.c` | alignment distances |
| `src/posterior.c` | correspondence and posterior scoring |
| `src/graph.c` | distance-to-graph construction and clustering |
| `src/detect.c` | orchestration, refinement, confidence |
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
