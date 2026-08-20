# Dev-Core Findings And Refactor Direction

This note summarizes what the current benchmark work says about `cognator` on the `Dev-Core` Track A benchmark and what to do next if the goal is to consistently beat LingPy `LexStat + Infomap`.

Primary evidence lives under these run artifacts, which the benchmark
pipeline regenerates and which are not committed:
- `benchmark/runs/20260419T211954Z-a-dev_core/comparison.tsv`
- `benchmark/runs/20260419T211954Z-a-dev_core/threshold_sweep/summary.tsv`
- `benchmark/runs/20260419T211954Z-a-dev_core/concept_shrink_sweep/summary.tsv`
- `benchmark/runs/20260419T211954Z-a-dev_core/cognator_error_summary.tsv`
- `benchmark/runs/20260419T211954Z-a-dev_core/lingpy_error_summary.tsv`

## Bottom Line

Current default `cognator` does not beat LingPy on `Dev-Core`.

Measured result:
- current default mean F1: `0.7552`
- LingPy mean F1: `0.7857`
- head-to-head: `3` wins, `6` losses

The dominant failure mode is not spurious merging. It is over-splitting:
- very high precision on several datasets
- materially lower recall than LingPy
- error decomposition shows false splits are the main source of the gap

This means the next architecture step should optimize recall without letting merge-heavy families collapse.

## What We Learned

### 1. Thresholding is a real lever

A fixed family-level threshold improves results substantially over the current Otsu-based default.

Observed on `Dev-Core`:
- `auto`: mean F1 `0.7552`
- `t0.75`: mean F1 `0.7886`
- `t0.80`: mean F1 `0.7941`

At `0.80`, `cognator` edges above LingPy on mean F1:
- mean delta vs LingPy: `+0.0084`

But it still loses more datasets than it wins:
- `4` wins, `5` losses

Interpretation:
- the scoring signal is better than the current default clustering decision rule lets through
- family-level threshold selection matters more than concept-local threshold adaptation

### 2. Concept-local auto thresholding is not the answer

The experimental `concept-shrink` mode did not help in auto-threshold mode.

Observed:
- `concept-shrink-s0.2-auto`: mean F1 `0.7428`
- `concept-shrink-s0.5-auto`: mean F1 `0.7496`
- `concept-shrink-s0.8-auto`: mean F1 `0.7519`

All of these are worse than family-level `auto` at `0.7552`.

Interpretation:
- per-concept histograms are too noisy or too small to support stable local threshold estimation
- local threshold variance amplifies fragmentation rather than curing it
- if per-concept adaptation exists in the final system, it should be driven by a stronger prior or a richer graph objective, not raw per-concept Otsu decisions

### 3. The problem is mostly graph connectivity, not only pair scoring

Moving threshold upward from `auto` to `0.80` sharply reduces false splits on multiple families:
- `carvalhopurus`: false split rate `0.387 -> 0.043`
- `chacontukanoan`: `0.367 -> 0.103`
- `hattorijaponic`: `0.225 -> 0.060`
- `zhangrgyalrong`: `0.662 -> 0.372`

But this same change increases false merges on some families:
- `dunnaslian`: false merge rate `0.104 -> 0.287`
- `gaotb`: `0.292 -> 0.378`
- `lundgrenomagoa`: `0.293 -> 0.526`
- `peirosaustroasiatic`: `0.331 -> 0.437`

Interpretation:
- one global family threshold is too coarse
- but naive concept-local thresholding is worse
- the likely missing component is a calibrated notion of edge reliability or clusterability beyond simple threshold-and-cluster

### 4. There is already a usable performance envelope

On several datasets, runtime is already dramatically better than LingPy.

Examples from the baseline run:
- `gaotb`: `2060 ms` vs LingPy `109866 ms`
- `peirosaustroasiatic`: `6366 ms` vs LingPy `398723 ms`
- `dunnaslian`: `1174 ms` vs LingPy `32194 ms`

Interpretation:
- there is room to spend more compute on better clustering or calibration
- a modestly more expensive second-stage decision process is acceptable if it buys recall and still stays well below LingPy runtime

## Recommended Architecture Direction

The next major iteration should be framed as:

`better calibrated edge evidence + richer graph decision rule`

not:

`more local threshold heuristics`

### Priority 1: Replace threshold-only graph admission with calibrated edge confidence

Current pipeline logic effectively turns continuous pair distances into a hard keep/drop decision before clustering. That is likely the main structural bottleneck.

Recommended change:
- transform pair evidence into an edge confidence score with a family-level calibration step
- make confidence explicitly reflect uncertainty, not just raw distance
- preserve this score through clustering and output it in the sidecar

Concrete options:
- fit a family-level distance-to-confidence mapping from the bootstrap edge stability signal already present in the pipeline
- combine distance rank, bootstrap survival, and mutual-nearest-neighbor style support into one edge score
- use calibrated edge confidence as graph weight instead of only as a post-hoc annotation

Why this is the right next step:
- it directly targets the user requirement for a confidence value per form or edge
- it gives a paper-friendly explanation for why one pair is admitted and another is not
- it should reduce false splits without relying on brittle concept-local thresholds

### Priority 2: Introduce a two-stage graph construction rule

Current behavior is too close to:
- score all pairs
- threshold edges
- run Infomap

That is simple, but it leaves too much responsibility to one scalar threshold.

Recommended graph architecture:
- Stage A: build a permissive candidate graph using a high-recall gate
- Stage B: prune or reweight edges using structural support

Good support signals for Stage B:
- triangle support within a concept graph
- agreement across multiple lect-pair evidence paths
- bootstrap edge persistence
- local edge rank within each node's neighborhood

This should be easier to explain in a paper than a large number of local thresholds:
- "an edge survives if it is both phonetically plausible and structurally supported"

### Priority 3: Make threshold selection family-calibrated, not concept-estimated

The current evidence supports family-level calibration as the stable regime.

Recommended rule:
- keep threshold estimation at family scope
- estimate one or a few family parameters from the aggregate distance or confidence distribution
- allow concept-level behavior only through edge support terms, not direct local threshold estimation

This preserves the "same-family, per-concept" requirement while avoiding unstable concept-local statistics.

### Priority 4: Separate scoring from clustering in the codebase

Right now `internal/pipeline` owns too much of the modeling logic in one pass.

Refactor target:
- `internal/score`: pairwise evidence generation
- `internal/calibrate`: distance or evidence to confidence mapping
- `internal/graph`: candidate graph building, pruning, and edge weighting
- `internal/cluster`: community detection or connected-component policy over weighted graphs
- `internal/confidence`: cluster or form confidence summaries

Benefits:
- easier ablation studies
- cleaner paper narrative
- easier swap of clustering backends or edge policies without retraining everything

## Performance Recommendations

Performance is not the primary bottleneck today, but there are some clear engineering improvements worth making before larger datasets.

### 1. Cache pairwise alignments or distances within a run

The current pipeline computes lect-pair scorers and then distances in worker-local loops. If the next iteration adds a second-stage graph rule or calibration passes, repeated distance work will become expensive.

Recommended:
- store per-concept pair distances once
- optionally store alignment-derived features needed for confidence calibration
- treat these as the canonical evidence layer for all clustering experiments in a run

### 2. Make benchmark-time caches explicit

Benchmarking is now rich enough that recomputing everything for every threshold sweep is wasteful.

Recommended:
- serialize family/concept distance tables
- sweep clustering policies over frozen evidence
- only rerun alignment and scorer training when scoring parameters change

This will make research iteration much faster than rerunning the full Go pipeline for every clustering idea.

### 3. Reduce lock contention in family detection if graph features expand

`detectFamily` currently folds worker-local edges under a shared mutex. This is fine now, but it will become more expensive if each pair emits additional features.

Recommended:
- accumulate per-worker slices and merge once at the end
- or shard the merge structure by concept

This is not urgent, but it is the first place to clean up if profiling shows contention.

## Proposed Next Experiment Set

These should be done before broader refactors:

1. Build an offline evidence table for `Dev-Core`.
2. Sweep graph-admission policies over that frozen evidence.
3. Compare at least these policies:
- fixed threshold baseline
- mutual top-k edge gating
- threshold plus triangle support
- threshold plus bootstrap persistence
- weighted graph clustering using calibrated confidence
4. Promote only policies that beat the fixed `0.80` threshold baseline on mean F1 and win count.

Rationale:
- fixed `0.80` is now the benchmark to beat inside this repo, not the current `auto` mode
- if a new architecture cannot beat `0.80`, it is not yet competitive enough to justify complexity

## Resolved Decisions

These decisions are now fixed for the next phase:

- internal baseline to beat: fixed family threshold `0.80`
- primary reported confidence target: cluster-membership confidence
- benchmark data source for iteration: the existing exported `CoreCog` archive, not a rebuilt local `../arcaverborum` tree

Membership confidence should still be derived from lower-level edge evidence. The implementation should therefore model edge reliability explicitly and then summarize it at the form-to-cluster or form-within-cluster level.

## Questions To Resolve Before The Next Refactor

These are the remaining design decisions that would most affect the next implementation:

1. Is it acceptable if the published system uses a fixed family-level calibration rule learned from the family itself at inference time, as long as it remains unsupervised?
2. For the paper target, is "beats LingPy on mean per-dataset F1 while staying much faster" sufficient, or do you want the stricter criterion of winning on a majority of datasets too?

My recommendation:
- allow unsupervised family-level calibration
- treat majority win count as a secondary but important target after mean F1
