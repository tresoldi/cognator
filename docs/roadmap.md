# Roadmap

This document is a working technical roadmap for improving `cognator` as an
unsupervised, per-concept, same-family cognate clustering system.

The research target remains: consistently beat LingPy `LexStat + Infomap` on
arcaverborum-style family benchmarks while keeping Cognator's runtime advantage
and producing interpretable confidence diagnostics.

## Current Baseline

Promoted research configuration:

```sh
--threshold 0.68
--graph-mode support
--graph-expansion-mode adaptive
--graph-top-k 3
--graph-adaptive-margin 0.1
--graph-adaptive-either-max 0.15
--graph-adaptive-ambiguity 0.5
--graph-mutual-boost 0.2
--graph-triangle-boost 0.3
--graph-contrastive-penalty 0.20
--graph-contrastive-floor 0.05
--graph-risk-rank-weight 0.5
--graph-risk-gap-weight 0.5
--graph-risk-triangle-weight 0.0
--graph-risk-unilateral-weight 0.0
--refine-mode bridge-cut
--refine-confidence-threshold 0.55
--refine-bridge-ratio 0.7
```

Latest available snapshot:

| Split | Datasets | Mean F1 | Mean P | Mean R | LingPy comparison |
|---|---:|---:|---:|---:|---|
| Dev-Core | 9 | 0.8216 | 0.8523 | 0.8196 | latest confidence run has no paired LingPy file |
| Dev-Full | 9 | 0.8287 | 0.8356 | 0.8457 | +0.0439 mean F1, 4/5 wins |
| Holdout | 7 | 0.8666 | 0.8282 | 0.9248 | -0.0352 mean F1, 3/4 wins |

Performance snapshot:

- Dev-Full mean Cognator time: about 1.2s per scored family.
- Dev-Full mean LingPy time: about 63s per scored family.
- Holdout mean Cognator time: about 571ms per scored family.
- Holdout mean LingPy time: about 16.4s per scored family.

The current system is therefore fast enough to spend more computation on better
evidence, calibration, or refinement if the quality gain is real.

## What The Experiments Say

### Thresholding Alone Is Insufficient

Fixed family thresholds improved over Otsu defaults, but they did not solve the
main problem. A fixed high threshold raised recall and improved mean F1, but it
also increased false merges in merge-sensitive families. Concept-local Otsu
variants were worse because per-concept histograms are too small and noisy.

Conclusion:

- keep calibration at family scope
- avoid raw per-concept threshold estimation
- let concept-specific behavior come from graph structure, not isolated local
  histogram cuts

### Support Graphs Are The Right Direction

Support-aware graph construction gave the first substantial quality jump. The
best early support graph moved Dev-Core from threshold-only `0.7941` mean F1 to
about `0.8234`, mainly by recovering false splits.

The cost was increased false merges on families such as:

- `dunnaslian`
- `lundgrenomagoa`
- `peirosaustroasiatic`
- `blustaustronesian` on holdout

Conclusion:

- preserve support graph expansion because it buys recall
- make merge control more selective
- do not globally replace adaptive/either expansion with mutual-only expansion,
  because mutual-only gives back too much recall

### Post-Cluster Reassignment Is Risky

Alternative-cluster reassignment and richer supervised-risk variants produced
dev gains but regressed on broader confirmation runs. Bridge cutting is safer:
it improves some merge-heavy families modestly, but its gains are not enough to
close the holdout gap.

Conclusion:

- keep bridge-cut as the promoted conservative refinement
- keep alternative support as confidence evidence
- do not use assignment-changing alternative-support rules as promoted behavior
  until they pass Dev-Core, Dev-Full, and holdout gates

### Confidence Is Useful But Not Calibrated

The current sidecar and prediction outputs expose:

- `confidence`
- `assigned_support`
- `alternative_support`
- `support_margin`
- optional bootstrap mean and interval

Low-confidence review queues are strongly enriched for errors. On holdout, the
top 200 flagged rows were 81.5% error-involved. However, dev-derived fixed
thresholds transfer imperfectly, so confidence is not yet a calibrated
probability or a safe hard-abstention rule.

Conclusion:

- use confidence for review, analysis, and paper diagnostics now
- calibrate confidence before presenting it as probability-like
- separate confidence estimation from clustering decisions unless a refiner
  passes promotion gates

## Architecture Roadmap

### 1. Split The Pipeline Into Explicit Stages

`internal/pipeline` currently owns orchestration, evidence generation,
thresholding, graph construction, refinement, bootstrapping, and confidence. It
works, but it is now too dense for research iteration.

Target package layout:

| Package | Responsibility |
|---|---|
| `internal/evidence` | immutable pair evidence tables and feature extraction |
| `internal/calibrate` | family-level distance/support calibration |
| `internal/graph` | candidate graph policies and edge reweighting |
| `internal/refine` | post-cluster refinement policies |
| `internal/confidence` | membership confidence and calibration |
| `internal/pipeline` | thin orchestration only |

Benefits:

- easier ablations without re-running alignment
- clearer paper narrative
- lower risk when adding experimental policies
- direct serialization point for large benchmark sweeps

### 2. Introduce A First-Class Evidence Table

The next major refactor should make pair evidence explicit and reusable.

Minimum evidence row:

- family
- concept
- left/right form IDs and indices
- left/right lects
- distance
- similarity
- endpoint ranks
- top-1/top-2 gap features
- mutual-top-k indicator
- triangle support summary
- optional bootstrap edge persistence

This table should be generated once and then reused by graph/clustering sweeps.
That avoids retraining scorers and recomputing alignments for every policy.

### 3. Make The Promoted Baseline A Named Preset

The current best configuration is a long CLI string. This is fragile.

Add a preset mechanism such as:

```sh
--preset research-20260421
```

or:

```sh
--config configs/research-20260421.toml
```

The preset should expand to the frozen support/adaptive/bridge-cut baseline and
be written into benchmark metadata. This will reduce accidental benchmark drift.

### 4. Separate Experimental Flags From Stable Flags

The CLI currently exposes many experimental graph-risk and refinement knobs.
That is useful for research but noisy for users.

Proposed structure:

- stable flags for normal detection
- `--preset` for known recipes
- `--experimental-*` prefix or config-only access for unstable features
- benchmark metadata that records all effective values

## Model Roadmap

### 1. Improve Merge Control Without Losing Recall

The most important model problem is selective merge control.

Candidate ideas:

- detect bridge-like edges using local cut impact, not only edge weight
- penalize edges that connect two internally dense alternative neighborhoods
- use node-level alternative cluster pressure as a warning signal, not immediate
  reassignment
- add a cluster split proposal step that only accepts splits improving an
  unsupervised objective plus confidence separation

Promotion criterion:

- must improve Dev-Core mean F1
- must confirm on Dev-Full
- must not degrade holdout merge-heavy families when checked after promotion

### 2. Calibrate Confidence As A Paper-Ready Quantity

Current confidence is a support ratio, not a calibrated probability.

Next steps:

- build calibration curves on Dev-Core using error involvement labels
- evaluate transfer on Dev-Full and holdout
- compare support-ratio confidence, bootstrap stability, and a combined score
- report confidence as a calibrated risk band if point probabilities are not
  stable

Candidate output:

- `confidence`: bounded score, monotonic with membership support
- `risk_band`: `low`, `medium`, `high`
- `confidence_low` / `confidence_high` when bootstrap or calibration intervals
  are meaningful

### 3. Improve Pair Evidence

The graph stage is now better than the original threshold stage, so remaining
quality gains may require better evidence.

Candidate directions:

- expose alignment feature summaries instead of only final distance
- distinguish systematic correspondences from accidental short-form similarity
- add concept-level priors for known high-borrowing or high-homoplasy concepts
  only if they can be kept unsupervised or clearly heuristic
- improve handling of very short forms where distances are unstable
- revisit tone/prosody features by family, since uniform prosody penalties may
  not transfer equally

### 4. Treat Singleton And Near-Singleton Cases Explicitly

Some top low-confidence false positives are clean rows with zero assigned
support but strong alternative support. These are useful review cases, but they
also show that singleton confidence semantics need tightening.

Ideas:

- distinguish "stable singleton" from "unsupported singleton with competitors"
- add a separate singleton-risk diagnostic
- avoid mapping all no-internal-support cases into a single confidence behavior

## Performance Roadmap

### 1. Serialize Evidence For Sweeps

Benchmark iteration currently reruns scorer training and distance computation
for many graph-policy experiments. This wastes most of the runtime.

Plan:

1. add an evidence export command
2. add a graph-policy runner that consumes evidence
3. make threshold, support graph, refinement, and confidence sweeps operate over
   frozen evidence

This should make most model sweeps seconds-level instead of full pipeline runs.

### 2. Reduce Peak Memory In Large Families

The pipeline stores concept edges and distance slices after pairwise computation.
Large families can produce high transient memory due to pair-heavy evidence.

Potential improvements:

- stream concept evidence into per-concept temporary files
- merge worker-local evidence without a shared map lock
- process concepts in bounded batches rather than retaining all edges
- store distances as `float32` in evidence tables if quality is unchanged
- add per-family memory telemetry to benchmark output

### 3. Optimize Infomap Or Swap Backend If Needed

The current Infomap implementation recomputes the map equation for trial moves
and favors clarity. This is acceptable for small per-concept graphs, but it may
become a bottleneck with larger datasets or richer graph expansion.

Options:

- add incremental map-equation bookkeeping
- use connected components for high-confidence subgraphs before Infomap
- expose an igraph-backed experimental backend for large concept graphs
- benchmark whether clustering is actually a bottleneck after evidence caching

### 4. Make Parallelism More Predictable

Current concurrency happens at family, lect-pair, and concept stages. It is fast,
but benchmark reproducibility and resource limits need stronger controls.

Improvements:

- document recommended `--parallel-families` / `--jobs` combinations
- add a memory-aware scheduler for large families
- keep deterministic seeds independent of goroutine scheduling
- record effective `GOMAXPROCS`, jobs, and parallel families in metadata

## Benchmark And Evaluation Roadmap

### 1. Use A Clear Promotion Ladder

Promotion ladder:

1. unit tests for feature extraction and graph behavior
2. synthetic cases for false split / false merge failure modes
3. Dev-Core benchmark
4. Dev-Full confirmation
5. holdout check only after promotion

Do not tune directly on holdout.

### 2. Add A Compact Family Spot-Check Report

Every candidate should report:

- mean F1
- family win/loss/tie against LingPy
- precision/recall shift
- false merge and false split rates by family
- top concepts contributing to regressions
- runtime and memory deltas

The goal is to stop promoting changes that improve mean F1 by exploiting one
family while damaging several others.

### 3. Keep Track B Supported But Secondary

Track A remains the optimization target. Track B/partial cognacy is supported
and should remain in the benchmark harness, but promotion should be based on
Track A until the primary quality target is met.

## Immediate Next Steps

1. Add a named preset for the frozen research baseline.
2. Add benchmark metadata that records all effective pipeline settings.
3. Implement an evidence export/import format for one family and one split.
4. Move membership confidence code out of `internal/pipeline`.
5. Add calibration curves/risk bands for confidence outputs.
6. Design one selective merge-control experiment that uses alternative support
   for split proposals, not direct reassignment.
7. Add a spot-check report for the holdout families with the largest gaps:
   `blustaustronesian` and `syrjaenenuralic`.
