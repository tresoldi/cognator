# Roadmap

This document is a working technical roadmap for improving `cognator` as an
unsupervised, per-concept, same-family cognate clustering system.

The research target remains: consistently beat LingPy `LexStat + Infomap` on
arcaverborum-style family benchmarks while keeping Cognator's runtime advantage
and producing interpretable confidence diagnostics.

## Current Baseline

Promoted research configuration:

```sh
--preset support-adaptive-bridge
--threshold 0
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

Implementation status:

- The Go implementation has been retired from this repository.
- The active implementation is the C library, C CLI, C/WASM facade, and Python
  wrapper.
- The Python package is the intended user-facing install path and includes a
  vendored merkmal C runtime subset for normal source installs.
- `support-adaptive-bridge` now uses family-adaptive thresholding by default,
  with resolved thresholds exposed through the Python result API and benchmark
  TSVs.
- Benchmark entry points now report `estimated_pairs` and can skip pair-heavy
  families with `--max-pairs`.

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

## Agent Handoff Roadmap

This section is the highest-priority pickup list for future agents. Each item
should land as a small, separately verified commit unless explicitly grouped.

### 1. Re-run The Wider Benchmark Suite

Run the wider Cognator-vs-LingPy benchmark after the adaptive-default,
resolved-threshold, symmetric `bridge-cut`, and pair-guard changes.

Deliverables:

- refreshed Cognator result TSVs with `threshold` resolved and
  `estimated_pairs` populated
- refreshed LingPy comparison where possible
- an updated `docs/adaptive_default_validation.md`
- a short note identifying wins, regressions, runtime shifts, and skipped
  families

Verification:

- `python3 scripts/adaptive_default_report.py`
- spot-check that `threshold` is not `0.0000` for adaptive completed datasets
- compare win/loss/tie counts against the committed baseline

### 2. Add Automatic Error Reports To Benchmark Runs

Wire `scripts/error_report.py` into the benchmark/report pipeline so each
Cognator and LingPy run produces false-merge and false-split summaries without
manual follow-up.

Deliverables:

- per-run concept error TSV
- per-run dataset error TSV
- markdown summary of largest false-merge and false-split contributors
- links from generated comparison reports to those artifacts

Verification:

- synthetic prediction fixture with known TP/FP/FN counts
- pipeline smoke that writes the error TSVs and markdown

### 3. Add Memory Telemetry

Record peak RSS or best-available process memory for C/Python benchmark runs so
`estimated_pairs`, forms, lects, runtime, and actual memory pressure can be
analyzed together.

Deliverables:

- `peak_rss_mb` or explicitly named platform-specific memory column in benchmark
  TSVs
- documentation of how the value is measured on Linux/macOS and what is
  unavailable elsewhere
- report summary correlating `estimated_pairs` and peak memory

Verification:

- unit-level parser/formatter test for telemetry fields
- benchmark smoke with non-empty memory field on supported platforms or a clear
  unavailable marker

### 4. Build And Smoke-Test C/WASM

Use Emscripten to build the C/WASM facade and verify the browser worker path,
not just source-level checks.

Deliverables:

- generated `web/cognator.js` and `web/cognator.wasm` from `web/build-wasm.sh`
  in the local build flow
- browser or Playwright smoke test exercising `web/worker.js`
- documented Emscripten version and build command

Verification:

- `web/build-wasm.sh`
- `node web/smoke_test.mjs` or browser-equivalent worker smoke
- one screenshot or log proving a non-empty detection result

### 5. Process Large Families Instead Of Only Skipping

Replace the current skip-only large-family policy with bounded per-concept
evidence processing where feasible.

Deliverables:

- concept-batch or per-concept streaming path that avoids retaining all
  pairwise evidence at once
- preserved deterministic output order
- benchmark comparison against the guarded path

Verification:

- synthetic large-concept fixture showing bounded memory behavior
- one formerly skipped family attempted with a controlled memory cap
- full C test suite and benchmark smoke

### 6. Stabilize Python Release Packaging

Move from local installability to release-quality wheels for the target Python
versions.

Deliverables:

- wheel builds for supported CPython versions
- tested install matrix in `docs/python_install.md`
- release notes describing the C extension, vendored merkmal runtime, and no-Go
  requirement

Verification:

- `python -m pip wheel . -w dist`
- install wheel into a fresh venv and run `import cognator`, CLI help, and a tiny
  detection smoke
- cibuildwheel configuration check or CI job when available

### 7. Freeze A Reproducible Benchmark Release Bundle

Create a durable benchmark bundle containing input manifests, parameters,
outputs, and narrative conclusions so future modeling work can be compared
without reconstructing old runs.

Deliverables:

- manifest of input datasets, versions, and split files
- Cognator and LingPy parameters
- result TSVs, prediction TSVs, error reports, and comparison markdown
- narrative conclusion stating what is promoted, what regressed, and what is
  still open

Verification:

- a single documented command regenerates the report from the bundle artifacts
- all linked files exist

### 8. Target The Largest Measured Regressions

Start the next modeling cycle on the worst families from the current wider
sample, especially `lundgrenomagoa`, `luangthongkumkaren`, and
`lionnetyotonahua`.

Deliverables:

- family spot-check notes with false-merge/false-split decomposition
- one narrowly scoped modeling hypothesis per family
- synthetic regression tests for any promoted rule

Verification:

- Dev-Core and Dev-Full pass before any holdout check
- no promotion based only on a single-family win

### 9. Evidence Serialization For Fast Sweeps

Add an evidence export/import layer so graph-policy and threshold experiments do
not rerun posterior training and alignment for every candidate.

Deliverables:

- versioned evidence format
- export command
- policy runner consuming frozen evidence
- compatibility check that frozen evidence reproduces normal detection for a
  small fixture

Verification:

- fixture round-trip test
- one sweep that runs from evidence and matches normal-pipeline clusters

### 10. Confidence Calibration And Review Queues

Continue confidence calibration as diagnostics first, not as automatic
assignment-changing behavior.

Deliverables:

- calibration report split by cluster size and support strata
- low-confidence review TSV
- clear language distinguishing confidence ranking from probability

Verification:

- monotonic calibration curves on dev data
- transfer check on holdout or wider validation sample

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

Implementation note: the C/Python `support-adaptive-bridge` preset now leaves
`threshold=0` so detection resolves a family-level Otsu threshold clamped to
`0.64..0.74`, while `threshold_mode=concept-shrink` remains an explicit
experiment knob rather than the default behavior.

Boundary-refinement note: `bridge-cut` now evaluates weak bridges from both
endpoints before preserving an edge, so an ambiguous node with strong competing
support cannot be pulled across a boundary only because traversal reached it
from a cleaner neighbor.

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

`src/detect.c` currently owns orchestration, evidence generation, thresholding,
graph construction, refinement, bootstrapping, and result assembly. It works,
but it is now too dense for research iteration.

Target C module layout:

| Module | Responsibility |
|---|---|
| `src/evidence.c` | immutable pair evidence tables and feature extraction |
| `src/calibrate.c` | family-level distance/support calibration |
| `src/graph.c` | candidate graph policies and edge reweighting |
| `src/refine.c` | post-cluster refinement policies |
| `src/confidence.c` | membership confidence and calibration |
| `src/detect.c` | thin orchestration only |

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

### 3. Keep The Promoted Baseline As A Named Preset

The promoted baseline is now `--preset support-adaptive-bridge`; future work
should keep benchmark scripts using that preset unless an experiment explicitly
overrides it.

Next improvements:

- write the effective preset and resolved threshold into all benchmark metadata
- add a config-file path only if repeated command lines become unmanageable
- avoid adding new default-changing flags without a promotion-ladder result

### 4. Separate Experimental Flags From Stable Flags

The CLI currently exposes many graph and refinement knobs. That is useful for
research but noisy for users.

Proposed structure:

- stable flags for normal detection
- `--preset` for known recipes
- `--experimental-*` prefix or config-only access for unstable features
- benchmark metadata that records all effective values

Cleanup targets:

- keep ordinary Python users on `preset`, `threshold`, `bootstrap`, `seed`, and
  `min_pairs`
- leave lower-level graph and refinement knobs available for benchmark scripts
  and C CLI experiments
- when a knob loses a promotion run, document the result before hiding or
  deleting it

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
- Membership-confidence code still lives mostly in `src/graph.c` and
  `src/detect.c`; split it into `src/confidence.c` only after the benchmark
  and error-report work creates a stable target for the module boundary.

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

### 5. Posterior Scoring Follow-Ups

The C implementation already has posterior scoring with a Dirichlet-prior
formulation and asymmetric lect-pair tables. The next work is not to port the
idea, but to validate and simplify its public controls.

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
  The current C implementation keeps this in `src/posterior.c`.
- Asymmetry is first-class: `α_forward(σ, τ) ≠ α_reverse(τ, σ)` because the
  prior is parameterized per direction. This is the first time the framework's
  "asymmetry is real" principle lands as a first-class modeling choice instead
  of surviving only in the ordered lect-pair bookkeeping.

Follow-up direction:

- keep the default scorer as posterior unless a benchmark rerun contradicts it
- decide whether `--lambda` still has any user-facing purpose or should become
  an ignored compatibility field
- decide whether `--min-pairs` should stay exposed or move to benchmark-only
  experiments
- document `dirichlet_concentration`, `dirichlet_sharpness`, and
  `dirichlet_scale_mode` in the C/Python API docs before release packaging

Follow-up refinements (both implemented as opt-in flags):

- **Shuffle-null integration.** *(Landed.)* `--null-mode=shuffle`
  composes with `--scorer=posterior`. The permutation-based
  conditional null fires in `PosteriorTable.LogOdds` when attached.
  On Dev-Core, shuffle was uniformly worse than marginal across every
  α₀ tested (flat ~0.76 vs marginal's 0.81+) — too noisy without
  smoothing. Left available for experimentation.
- **Per-row α₀ scaling.** *(Landed.)* `--dirichlet-scale
  {"", sparse-boost, rich-boost}` in cmd/bench and cmd/cognator.
  A Dev-Core A/B at α₀=50 found:
  - `sparse-boost` (α₀_eff = α₀ · max(1, median/n(σ))): **+0.0018 F**
    over flat. Amplifies prior for sparse rows as the roadmap
    predicted.
  - `rich-boost` (α₀_eff = α₀ · max(1, n(σ)/median)): −0.0070 F.
    Regularizes rich rows too aggressively; recall collapses.
  Small sparse-boost gain doesn't close the gap to learned
  (0.8139 vs 0.8216 target). Default stays flat; sparse-boost is
  the recommended non-default scaling mode for users who want to
  squeeze more out of the posterior scorer.

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

Current benchmark concurrency happens outside the C core through runner-level
family scheduling. The C detector itself remains serial while the API and
memory behavior stabilize, so resource limits need stronger controls before
native parallelism is reintroduced.

Improvements:

- document recommended `--parallel-families` / `--jobs` combinations
- add a memory-aware scheduler for large families
- keep deterministic seeds independent of scheduler order
- record jobs, parallel families, and memory guards in metadata

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

Use the **Agent Handoff Roadmap** near the top of this document as the active
pickup list. The older Go-era and early-C conversion tasks have either landed or
been superseded by the C/Python milestones recorded there.

Out of scope for the immediate cycle: regulae integration, a full module split
inside the C core, and an Infomap backend replacement. Revisit those only after
the wider benchmark rerun, automatic error reports, memory telemetry, and
large-family execution work produce new evidence.
