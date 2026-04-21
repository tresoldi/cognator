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
That is useful for research but noisy for users, and several of the knobs
point at modes that already lost their promotion tests.

Proposed structure:

- stable flags for normal detection
- `--preset` for known recipes
- `--experimental-*` prefix or config-only access for unstable features
- benchmark metadata that records all effective values

Concrete deletion targets (modes that ran the promotion ladder and lost):

- `--refine-mode` values beyond `none` and `bridge-cut`: the `reassign`,
  `alt-reassign`, `bridge-alt-reassign`, `alt-reassign-bridge`, `alt-bridge-cut`,
  `local-recluster`, and `core-cc` variants all regressed on Dev-Full. Keep the
  git history, remove the code (~300 LOC of `refinePartition*` in
  `internal/pipeline/pipeline.go`).
- `--graph-risk-triangle-weight`, `--graph-risk-unilateral-weight`,
  `--graph-risk-density-weight`, `--graph-risk-dispersion-weight`: all four
  default to 0 and never earned promotion. The rank/gap pair suffices.
- `--refine-alt-cluster-threshold`, `--refine-cluster-confidence-threshold`,
  `--refine-local-core-weight`: gate parameters for refinement modes being
  removed above.

The corresponding `Spec` fields and `Default*` constants go with them. KISS
and YAGNI: three similar lines beat a premature abstraction, and dead
experimental knobs beat neither.

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

*Status: landed. Three-axis stratification (cluster_size × support
quartile) yields a populated low-risk band with <5% error rate on
Holdout for a third of all rows.*

Sidecar emits `ClusterSize` and `CandidateNeighbors` alongside the
support-ratio `Confidence`. `scripts/calibrate_confidence.py` takes
`--stratify-by {"", cluster_size, cluster_size_support}` and
`--error-label {pairwise, form-local}`. Recommended recipe:
`--stratify-by cluster_size_support --error-label form-local`.

Form-local is a B-cubed form-level per-form label (precision ≥ 0.5
AND recall ≥ 0.5 against the form's gold class within its concept) —
matches the scale of node-level confidence. The support-tier axis
splits the multi stratum into four quartiles by `assigned_support`,
with cut points computed on the fit set and stored in the JSON.

Dev-Core fit → Holdout apply:

| Stratum  | Support range        | Low  | Medium | High  |
|----------|----------------------|-----:|-------:|------:|
| multi_q1 | ≤ 1.70               |    — |   74% err | 79% err |
| multi_q2 | (1.70, 4.59]         |    — |   27% err | 48% err |
| multi_q3 | (4.59, 10.31]        |    — |   **6% err** | 27% err |
| multi_q4 | > 10.31              |   **3% err** | 15% err | 10% err |
| singleton | n/a                 |    — |   17% err | 20% err |

(Rows in each band, truncated for brevity; numbers are the empirical
error rate on the band.)

Headline: `multi_q4` low band = 6883 rows (33% of Holdout) at 3%
error. `multi_q3` medium band = 3768 rows (18%) at 6% error. Together
~50% of Holdout rows sit in a calibrated band with <7% error rate —
paper-ready.

Transfer is clean across q2–q4 (calibrated value monotone with
empirical accuracy per decile). `multi_q1` is the weakest-support
tier and the signal there is still noisy — the calibrator correctly
flags those rows as high-risk.

Follow-ups:

- Posterior-sample bootstrap once the Bayesian scoring layer is the
  default scorer — credible intervals are the natural long-form
  output for the q4 "low" band that would still benefit from
  uncertainty annotation.
- *(Landed.)* Membership-confidence code lives in
  `internal/confidence`; pipeline consumes it across the package
  boundary. Output schema is stable.

Candidate output (shape is settled; multi-stratum semantics still
shifting):

- `confidence`: bounded score, monotonic with membership support
- `cluster_size`: integer; support for the singleton stratum
- `candidate_neighbors`: integer; support for the "should this
  singleton have merged?" analysis
- `confidence_calibrated`: isotonic-mapped P(correct) per stratum
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

### 5. Bayesian Scoring Layer (Asymmetric By Construction)

The current correspondence scorer has three separate empirical constants —
`Lambda` (learned/prior blend), `MinPairs` (sparse-pair fallback), and
`tanhScale` (log-odds squashing) — plus a smoothing `alpha` in `LogOdds`.
A Dirichlet-prior formulation replaces all four with one principled
concentration parameter and produces a proper log-posterior-odds score.

Model:

```
P(τ | σ) ~ Dirichlet(α(σ, ·))
α(σ, τ) = α₀ · π(σ, τ)
π(σ, τ) = softmax_τ(-k · d_merkmal(σ, τ))        # proper distribution per σ
P_posterior(τ | σ) = (α(σ, τ) + n(σ, τ)) / (α₀ + n(σ, ·))
score(σ, τ) = log₂ P_posterior(τ | σ) / P_null(τ)
```

- `α₀` is the single prior concentration. "The prior is worth α₀ observations
  per source row." Small-sample regime (`n(σ, ·) ≪ α₀`) → posterior ≈
  merkmal-similarity prior; rich-sample regime → posterior ≈ MLE. The blend is
  the math, not a tuned knob.
- `P_null` comes from the shuffle null when trained, else the target marginal.
  Both are already available in `internal/score`.
- Asymmetry is first-class: `α_forward(σ, τ) ≠ α_reverse(τ, σ)` because the
  prior is parameterized per direction. This is the first time the framework's
  "asymmetry is real" principle lands as a first-class modeling choice instead
  of surviving only in the ordered lect-pair bookkeeping.

API direction:

```go
// internal/score/posterior.go

type DirichletPrior struct {
    Bundle        *merkmal.Bundle
    Concentration float64 // α₀
    Sharpness     float64 // k (inherits from priorSharpness)
}

type PosteriorTable struct { /* counts, rowTotal, prior, nullMarginal */ }

func (t *PosteriorTable) PosteriorMean(source, target string) float64
func (t *PosteriorTable) LogOdds(source, target string) float64

func TrainPosterior(src, tgt string, pairs []FormPair,
                    prior *DirichletPrior, null *NullDistribution) *PosteriorTable
```

What vanishes from `Spec` and `defaults.go`:

- `Lambda` (absorbed into `α₀`)
- `MinPairs` (prior dominates naturally on sparse pairs)
- `tanhScale` (no squashing needed under a proper posterior-odds)

What stays:

- `priorSharpness` survives as `DirichletPrior.Sharpness` with a cleaner
  interpretation (softmax temperature over merkmal distances)
- `DefaultConcentration` is new (α₀), one constant replacing three

Migration:

- land `PosteriorTable` alongside `CorrespondenceTable`; no breaking changes
- A/B on Dev-Core
- promote if Dev-Core F ≥ current − 0.005; principle beats marginal F here
  because the goal is deleting three empirical constants, not chasing the last
  0.01 on a tuning run

Follow-up refinements if the constant α₀ is not the right knob:

- **Shuffle-null integration.** Replace the target-marginal null in
  `PosteriorTable.LogOdds` with the existing shuffle-permutation null
  when `--null-mode=shuffle`. Makes the posterior-odds ratio a proper
  Bayes factor against a random-pairing baseline rather than against
  whatever target marginals the corpus happens to have. Composes with
  the core α₀ knob.
- **Per-row α₀ scaling.** Replace the scalar α₀ with an effective
  concentration α₀(σ) that shrinks for rich-data rows — e.g.
  α₀_eff = α₀_base · max(1, median_row_count / n(σ, ·)). Rows with
  thousands of observations then behave like MLE; rows with a handful
  still lean on the prior. Adds a new empirical constant (α₀_base)
  but separates the small-data and rich-data regimes, which is
  exactly the failure mode that a flat α₀ shows on large families
  like peirosaustroasiatic. Only land this if the shuffle-null
  integration and a simple α₀ sweep both fail to close the gap on
  Dev-Core.

Future step (do not start before the core is promoted): `α + n` is a sampleable
posterior. Replacing the current Gaussian-distance-perturbation bootstrap with
a posterior-sample bootstrap would make the confidence intervals honest
Bayesian credible intervals rather than heuristic stability indices.

### 6. Regulae Integration (Longer-Term Methods)

The current scorer is grapheme-to-grapheme under Needleman-Wunsch. The
framework's stated target is phrase-based correspondence learning via
`regulae`. This is a larger change than the Bayesian scoring layer and should
not block it.

Plan sketch:

- keep cognator's concept-graph and clustering layers unchanged
- replace the per-lect-pair `PosteriorTable` with a regulae-backed model for
  the dominant lect pairs (by form count) once regulae is mature enough
- fall back to `PosteriorTable` for long-tail pairs where regulae training
  data is too thin
- expect gains on families where sound changes are phrase-conditioned rather
  than context-free (Grimm-style environments, Romance palatalization,
  Austronesian reflex sets)

Promotion criterion:

- must improve Dev-Core mean F1 on at least one clearly phrase-conditioned
  family without degrading context-free families
- regulae must be pinned to a specific release tag, not a floating import

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

Prioritized. Earlier items unblock later ones.

1. **Tidy pass.** *(Landed.)* Deleted the refinement modes and risk weights
   that lost their promotion tests (see Architecture Roadmap §4). Added a
   `--preset=support-adaptive-bridge` alias for the frozen baseline and
   `--meta-out` to capture the effective Spec per bench run.

2. **Bayesian scoring layer.** Implement `PosteriorTable` alongside the
   existing `CorrespondenceTable` (see Model Roadmap §5). A/B on Dev-Core;
   promote if F ≥ current − 0.005. The win is deleting `Lambda`, `MinPairs`,
   and `tanhScale` and making asymmetric scoring first-class — not chasing F.

3. **Evidence serialization.** Add an evidence export/import format (see
   Performance Roadmap §1). Deferred behind the Bayesian scoring layer because
   the natural cache level is alignment-column frequencies, not final
   distances — and the scoring layer decides which layer is the right cache
   boundary. A distance cache built before the Bayesian scorer lands would
   be invalidated on first posterior run.

4. **Confidence calibration.** *(Landed.)* Sidecar emits `ClusterSize`
   and `CandidateNeighbors`. Calibrator stratifies by `cluster_size`
   and supports both `pairwise` and `form-local` error labels.
   Form-local + cluster_size on Dev-Core gives monotone PAV fits in
   both strata; Holdout transfer lands 77% of multi rows in a medium
   band at 82% empirical accuracy, 89% of singletons at 83% accuracy.
   Top plateau caps below 0.90 on multi — see Model §2 follow-ups.

5. **`assigned_support` as a second multi-stratum axis.** *(Landed.)*
   `--stratify-by cluster_size_support` splits the multi stratum into
   four assigned-support quartiles with cut points stored in the JSON.
   On Dev-Core → Holdout, `multi_q4` low-risk band covers 33% of the
   corpus at 3% empirical error; `multi_q3` medium covers another 18%
   at 6% error. Model §2 now carries the full numbers.

6. **Selective merge control.** One experiment using alternative-support as a
   split-proposal signal rather than a reassignment rule — the only Family-2
   direction that hasn't been tried and that respects the "don't change
   assignments without a principled gain" conclusion.

7. **Spot-check reports** for the holdout families with the largest gaps
   (`blustaustronesian`, `syrjaenenuralic`, `lundgrenomagoa`). Useful for the
   paper's error analysis regardless of which of the above lands first.

Out of scope for this cycle: full package split (`internal/refine`,
`internal/calibrate`, `internal/confidence`), regulae integration (see Model
Roadmap §6), mid-family memory streaming, Infomap backend swap. Revisit after
items 2–4 land and the promoted baseline shifts.
