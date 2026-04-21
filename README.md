# cognator

`cognator` is an unsupervised cognate detection system for tokenized wordlists.
The main supported mode is **per-concept clustering within the same language
family**. Cross-concept/global clustering exists as an experimental path, but it
is not the core target.

The project is written in Go, with Python scripts for benchmark orchestration,
LingPy comparison, and diagnostic reports.

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

- Go compatible with `go.mod` (`go 1.25.0` in this workspace)
- Python 3 for scripts
- LingPy only if you want to reproduce LingPy baselines

Build and test:

```sh
go build ./...
go test ./...
```

During development, prefer `go run ./cmd/...` because binaries on shared or SMB
mounts may lose the executable bit.

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

Run detection:

```sh
go run ./cmd/cognator detect \
  --in forms.csv \
  --out cognator_sidecar.tsv
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

Score a sidecar against gold cognacy:

```sh
go run ./cmd/cognator eval \
  --pred cognator_sidecar.tsv \
  --truth forms.csv
```

The evaluator reports B-cubed precision, recall, and F1. By default it scores
cognacy within concept, which is the supported target.

## Recommended Research Configuration

The Go defaults are conservative and preserve the original threshold graph.
For current experiments, use the frozen support/adaptive/bridge-cut baseline:

```sh
go run ./cmd/cognator detect \
  --in forms.csv \
  --out cognator_sidecar.tsv \
  --threshold 0.68 \
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
go run ./cmd/cognator detect \
  --in forms.csv \
  --out cognator_sidecar.tsv \
  --bootstrap 50 \
  --bootstrap-sigma 0.05
```

## Benchmarking

The benchmark path used in this project is based on `arcaverborum` exports.

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

Run Cognator only on an exported eval table:

```sh
go run ./cmd/bench \
  --eval-table benchmark/export/track_a.tsv \
  --split-file benchmark/splits/dev_core.txt \
  --out-tsv benchmark/results.tsv \
  --out-md benchmark/results.md \
  --pred-out benchmark/predictions.tsv
```

Compare existing Cognator and LingPy result TSVs:

```sh
go run ./cmd/benchcompare \
  --cognator benchmark/cognator_results.tsv \
  --lingpy benchmark/lingpy_results.tsv \
  --out-tsv benchmark/comparison.tsv \
  --out-md benchmark/comparison.md
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
# Fit a calibration on a Dev-Core run. Recommended: stratify by
# cluster size and use the form-local per-form correctness label.
python scripts/calibrate_confidence.py fit \
  --predictions benchmark/runs/dev_core/cognator_predictions.tsv \
  --out-json benchmark/calibration.json \
  --out-md benchmark/calibration_fit.md \
  --stratify-by cluster_size \
  --error-label form-local

# Apply it to any other predictions TSV to add
# `confidence_calibrated` and `risk_band` columns.
python scripts/calibrate_confidence.py apply \
  --calibration benchmark/calibration.json \
  --predictions benchmark/runs/holdout/cognator_predictions.tsv \
  --out-tsv benchmark/holdout_calibrated.tsv \
  --out-md benchmark/holdout_transfer.md
```

The recommended recipe (stratify-by cluster_size + form-local label)
produces monotone PAV fits in both strata. On a recent Dev-Core →
Holdout run, ~77% of multi-member-cluster rows land in a "medium" risk
band at ~82% empirical accuracy; the remaining ~23% in "high" at 56%
error rate. The "low" band is still unpopulated because the PAV top
plateau caps below 0.90 — see `docs/roadmap.md` Model §2 for the
`assigned_support`-stratification follow-up that would unlock it.

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
| `internal/wordlist` | input/output tables |
| `internal/segment` | grapheme parsing and class reduction |
| `internal/merkmal` | bundled distance/prosody/class features |
| `internal/align` | alignment distances |
| `internal/score` | per-lect-pair correspondence learning |
| `internal/evidence` | distance-to-graph construction |
| `internal/cluster` | Otsu thresholding and Infomap clustering |
| `internal/pipeline` | orchestration, refinement, confidence |
| `internal/eval` | B-cubed evaluation |

For deeper engineering direction, see [docs/roadmap.md](docs/roadmap.md).

## Known Limitations

- The code defaults are not the best research settings.
- Large families can still trigger high transient memory use because evidence
  generation is pair-heavy.
- `internal/pipeline` owns too many responsibilities and should be split into
  evidence generation, calibration, graph policy, clustering, and confidence.
- Confidence is useful for ranking review cases, but it is not yet calibrated as
  a probability.
- Assignment-changing alternative-support refinements improved dev subsets but
  did not generalize, so they remain experimental.
