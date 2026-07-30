# Benchmark And Research Plan

This document turns the current research direction into an implementation plan for this repository.

Scope:
- core task: per-concept cognate detection within a dataset/family
- benchmark target: LingPy `LexStat + Infomap`
- evaluation source: datasets incorporated in `../arcaverborum`
- optimization target: mean per-dataset B-cubed F1, with win count as a secondary criterion

Non-goals for the core path:
- global clustering across concepts
- cross-family clustering
- supervised tuning

## Ground Truth

The benchmark corpus must come from the `CoreCog` datasets declared in:
- [../arcaverborum/datasets.csv](../arcaverborum/datasets.csv)

The input semantics must follow:
- [../arcaverborum/docs/MERGER_SPECIFICATION.md](../arcaverborum/docs/MERGER_SPECIFICATION.md)

This matters because `arcaverborum` merged forms may contain:
- semicolon-separated `Cognacy`
- repeated `ID` values caused by multiple alignments or multiple cognate judgments
- partial-cognacy annotations via `Morpheme_Index` and `Segment_Slice`

The benchmark must not treat those cases implicitly. It needs an explicit evaluation export.

## Benchmark Tracks

Define two benchmark tracks.

### Track A: Simple Full-Form Cognacy

This is the main paper benchmark and the default optimization target.

Include rows only when all are true:
- `Segments` is non-empty
- `Cognacy` is non-empty
- no partial-cognacy annotation is present
- the row can be mapped to one unambiguous gold cognate label

Recommended policy:
- exclude rows with `Morpheme_Index` or `Segment_Slice`
- exclude rows whose `Cognacy` contains `;`
- keep one row per source row, but assign a stable `Eval_ID` so repeated raw `ID`s are distinguishable

Rationale:
- this yields a defensible full-form benchmark
- it prevents evaluation contamination from partial cognacy and multi-membership rows

### Track B: Partial/Complex Cognacy

This is exploratory and reported separately.

Include rows with one or more of:
- semicolon-separated `Cognacy`
- non-empty `Morpheme_Index`
- non-empty `Segment_Slice`
- duplicated raw `ID` values caused by multiple judgments

This track is not used for early method selection. It is a stress test and future work surface.

## Primary Metrics

Primary optimization metric:
- mean per-dataset B-cubed F1 on Track A

Secondary metrics:
- head-to-head win/loss/tie count against LingPy
- mean precision
- mean recall
- form-weighted F1
- runtime
- confidence calibration quality

Decision rule for candidate changes:
- accept only if the candidate improves mean per-dataset F1 on `Dev-Full`
- prefer candidates that also improve win count
- reject changes that improve recall by creating large precision collapses on already-strong datasets

## Dataset Splits

Use all `CoreCog=TRUE` datasets, but partition them into:
- `Dev-Core`
- `Dev-Full`
- `Holdout`

### Dev-Core

Purpose:
- fast iteration
- early ranking of hypotheses

Target size:
- 12 to 15 datasets

Selection rules:
- balance small, medium, and large datasets
- balance low, medium, and high lect counts
- include at least one dataset with high OOV
- include at least one dataset with known partial-cognacy artifacts
- include both easy and difficult datasets

### Dev-Full

Purpose:
- verify that a promising idea generalizes

Definition:
- all CoreCog datasets except those assigned to `Holdout`

### Holdout

Purpose:
- final evaluation of accepted changes

Target size:
- 10 to 12 datasets

Rules:
- freeze this list before tuning begins
- do not use for heuristic selection
- keep stratified diversity by dataset size, lect count, and difficulty

## Repo Changes

The current repo already has:
- `scripts/run_cognator_eval_table.py`
- `scripts/compare_benchmark_results.py`
- `scripts/run_lingpy_baseline.py`
- `scripts/run_benchmark_pipeline.py`
- `scripts/generate_arcaverborum_report.py`

The Go benchmark tools were replaced by the C/Python pipeline.

### New Files

Add these scripts and data files:

1. `scripts/export_arcaverborum_eval.py`
- reads `../arcaverborum` output tables or a supplied `forms.csv`
- filters to `CoreCog` datasets
- emits Track A and Track B evaluation tables
- emits dataset split files

2. `scripts/run_lingpy_baseline.py`
- runs LingPy `LexStat + Infomap` on the exported evaluation tables
- writes one TSV with the same dataset-level metrics schema expected by `scripts/compare_benchmark_results.py`
- writes optional per-form predictions for diagnostics

3. `scripts/run_benchmark_matrix.py`
- orchestrates repeated Cognator benchmark runs over named configurations
- saves config metadata, raw outputs, and summaries
- can run on `Dev-Core`, `Dev-Full`, or `Holdout`

4. `benchmark/configs/*.json`
- named experiment configurations
- one file per hypothesis variant

5. `benchmark/splits/dev_core.txt`
6. `benchmark/splits/holdout.txt`
- frozen split definitions

7. `benchmark/manifest.json`
- records benchmark corpus path
- records Track A / Track B export paths
- records split files
- records LingPy version and benchmark assumptions

### Optional Additional Commands

These are useful but not mandatory in the first pass:

1. `cmd/errorreport`
- computes dataset-level and concept-level error decomposition
- compares predicted clustering against gold

2. `cmd/confidenceeval`
- evaluates confidence calibration
- outputs reliability tables and summary metrics

If command growth feels too heavy, these can be implemented as Python scripts in `scripts/` first.

## Canonical Benchmark Data Schema

The exported Track A and Track B tables should have at least:

- `Eval_ID`
- `Raw_ID`
- `Dataset`
- `Language_ID`
- `Concept`
- `Segments`
- `Gold_Cognacy`
- `Track`
- `Has_Partial`
- `Has_Multi_Cognacy`

Recommended semantics:
- `Eval_ID` is unique per row and is the evaluation key
- `Raw_ID` is the original `forms.csv` ID and may repeat
- `Gold_Cognacy` is the canonical gold cluster label used in evaluation

For Track A:
- `Gold_Cognacy` must be single-valued

For Track B:
- `Gold_Cognacy` may remain single-valued if possible, but the row should also preserve complexity flags

## Required Changes To Existing Code

### 1. `internal/wordlist`

Current limitation:
- it assumes one row maps directly to one `Record`
- it has no concept of stable evaluation row IDs distinct from source IDs

Required change:
- extend or parallel the current loader with an evaluation-oriented row type
- preserve repeated raw IDs without collisions

Suggested approach:
- keep `wordlist.Record` for detection input
- add a benchmark-only struct in a benchmark package or script layer

Do not force the core detector to absorb benchmark-specific complexity.

### 2. `scripts/run_cognator_eval_table.py`

Current behavior:
- reads a flat arcaverborum-shaped table
- uses `Family` as the dataset key
- evaluates directly against `rec.ID -> rec.Cognacy`

Implemented behavior:
- accepts a pre-exported benchmark table keyed by `Eval_ID`
- optionally evaluates only datasets listed in a split file
- supports output of per-form predictions, not just dataset-level summaries

Suggested flags:
- `--eval-table`
- `--track`
- `--split-file`
- `--pred-out`
- `--run-label`

Important:
- the benchmark must evaluate against `Eval_ID`, not the raw `ID`, so repeated source IDs remain valid

### 3. `scripts/compare_benchmark_results.py`

Current behavior:
- joins Cognator and LingPy TSVs

Required changes:
- preserve dataset split metadata
- report mean F1, weighted F1, win count, and holdout-only summaries
- optionally compare multiple Cognator runs against one LingPy baseline

Suggested enhancements:
- `--label`
- `--split-file`
- `--summary-json`

### 4. `cmd/diag`

Useful additions:
- detect how many rows would be excluded from Track A
- report counts of:
  - semicolon `Cognacy`
  - duplicated raw IDs
  - rows with `Morpheme_Index`
  - rows with `Segment_Slice`

This gives immediate visibility into benchmark cleanliness by dataset.

## Error Analysis Outputs

Add one machine-readable report per run:
- `benchmark/runs/<run-id>/errors.tsv`

Columns:
- `dataset`
- `concept`
- `forms`
- `gold_clusters`
- `pred_clusters`
- `pair_tp`
- `pair_fp`
- `pair_fn`
- `false_split_rate`
- `false_merge_rate`

Purpose:
- identify whether the dominant failure mode is false splitting or false merging
- drive prioritization of thresholding vs scoring vs graph changes

Also add:
- `benchmark/runs/<run-id>/predictions.tsv`

Columns:
- `Eval_ID`
- `dataset`
- `concept`
- `gold`
- `pred`
- `confidence`
- `bootstrap`

## Confidence Definition

The confidence score should become a first-class benchmark output.

Target definition:
- `support(i)`: how strongly form `i` is tied to its assigned cluster in the canonical graph
- `stability(i)`: how stable that assignment is under small perturbations
- `confidence(i)`: a fixed global blend of support and stability

Implementation plan:

1. Preserve current bootstrap-derived stability signal.
2. Add a support term based on:
- mean internal edge weight from `i` to nodes in its cluster
- penalty for strong edges from `i` to nodes outside its cluster

3. Emit the decomposed values:
- `support`
- `stability`
- `confidence`

4. Evaluate calibration with:
- reliability buckets
- Brier score if the target is binarized to correct/incorrect assignment
- rank correlation between confidence and assignment correctness

Paper-friendly wording:
- confidence is the combination of local membership support and assignment stability under perturbation

## Experiment Waves

Run the research program in waves.

### Wave 0: Benchmark Infrastructure

Deliverables:
- Track A / Track B exports from `arcaverborum`
- frozen split files
- LingPy `LexStat + Infomap` baseline run
- dataset-level comparison tables

Exit criteria:
- one reproducible benchmark run on `Dev-Core`

### Wave 1: Error Anatomy

Goal:
- determine whether current failures are mostly false splits or false merges

Deliverables:
- per-dataset error decomposition
- per-concept error summaries
- confidence vs correctness diagnostics

Exit criteria:
- clear ranking of the most likely quality bottlenecks

### Wave 2: Thresholding

Variants:
- family-wide Otsu
- concept-local Otsu
- concept-local Otsu with shrinkage to family prior
- family prior plus graph-density adjustment

Exit criteria:
- choose one threshold strategy for further experiments

### Wave 3: Representation And Reduction

Variants:
- no reduction
- current class reduction
- merkmal partition levels: `coarse`, `medium`, `fine`
- train-on-reduced / score-on-original hybrid

Exit criteria:
- choose one reduction strategy

### Wave 4: Alignment Weighting

Variants:
- global only
- current equal-weight combined
- weighted combined variants

Exit criteria:
- choose one global alignment configuration

### Wave 5: EM And Null Model

Variants:
- marginal vs shuffle null
- `MinPairs` sweep
- iteration sweep
- uncertainty-weighted correspondence accumulation

Exit criteria:
- choose one scorer-learning configuration

### Wave 6: Graph Construction

Variants:
- current thresholded graph
- threshold plus mutual-kNN retention
- threshold plus minimum-support retention
- softened edge retention near threshold

Exit criteria:
- choose one graph construction strategy

### Wave 7: Confidence Formalization

Goal:
- stabilize the paper definition of confidence

Deliverables:
- explicit support and stability terms
- calibration reports
- fixed blend rule used across all datasets

## Experiment Registry

Every run must write:
- config hash
- git commit
- benchmark manifest path
- split used
- track used
- per-dataset TSV
- markdown summary
- prediction TSV
- error report TSV
- optional profile outputs

Recommended run directory:
- `benchmark/runs/<timestamp>-<label>/`

Contents:
- `config.json`
- `results.tsv`
- `results.md`
- `comparison.tsv`
- `comparison.md`
- `predictions.tsv`
- `errors.tsv`
- `meta.json`

## Immediate Implementation Order

1. Implement `scripts/export_arcaverborum_eval.py`
2. Freeze `benchmark/splits/dev_core.txt` and `benchmark/splits/holdout.txt`
3. Use `scripts/run_cognator_eval_table.py` on exported evaluation tables keyed by `Eval_ID`
4. Implement `scripts/run_lingpy_baseline.py`
5. Run the baseline comparison on `Dev-Core`
6. Add error decomposition outputs
7. Start thresholding experiments

## Open Decisions To Freeze Early

These should be fixed before large runs begin:

1. exact Track A exclusion policy for semicolon-separated `Cognacy`
2. exact split membership for `Dev-Core` and `Holdout`
3. exact LingPy environment and version
4. exact file format for per-form predictions

Recommended default answers:
- Track A excludes semicolon-separated `Cognacy`
- use frozen text files for splits
- pin LingPy and all Python dependencies in a dedicated requirements file
- use TSV for predictions and error tables
