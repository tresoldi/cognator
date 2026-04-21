# Next Experiment Plan

This plan defines the next research iteration after establishing fixed family threshold `0.80` as the current baseline to beat on `Dev-Core`.

Primary target:
- beat `t0.80` on mean per-dataset B-cubed F1

Secondary targets:
- improve win count against LingPy
- keep runtime materially below LingPy
- produce a defensible cluster-membership confidence value

## Working Hypothesis

The main weakness in the current architecture is the hard threshold-only graph construction rule.

Current behavior compresses rich pairwise evidence into:
- keep edge
- drop edge

before the clustering algorithm can use any notion of support or uncertainty.

The next experiment should therefore preserve richer edge evidence and use it to compute cluster-membership confidence.

## Proposed Model Change

Add a second graph stage:

1. Pair evidence stage
- compute pair distances as today
- retain additional edge features instead of only the final distance

2. Graph support stage
- keep a permissive candidate graph
- reweight or prune edges by structural support

3. Clustering stage
- cluster on weighted edges

4. Membership confidence stage
- for each form, estimate confidence that it belongs to its assigned cognate class

## Minimal First Version

The first version should be intentionally simple and measurable.

### New Edge Features

For every candidate pair within a concept:
- raw distance
- normalized similarity `1 - distance`
- rank of the neighbor from each endpoint
- mutual-top-k indicator
- bootstrap edge persistence if bootstrap is enabled

These features are all unsupervised and explainable.

### Candidate Graph Rule

Use a high-recall admission rule:
- include edges whose distance passes a permissive family threshold
- or include edges in the top `k` nearest neighbors for either endpoint

Then add support weighting:
- boost edges that are mutual-top-k
- boost edges supported by triangles
- downweight isolated one-off edges

This should reduce the current false split problem without opening the door to arbitrary long-range merges.

### Membership Confidence

For a form assigned to cluster `C`, define confidence from:
- mean support weight to other members of `C`
- share of strong in-cluster neighbors versus strong out-of-cluster alternatives
- bootstrap stability of the form's cluster assignment when available

This is more paper-friendly than opaque threshold tuning because it can be described as:
- how strongly the form is connected to its own class
- how weakly it is connected to competing classes
- how stable that assignment is under perturbation

## Code Refactor Scope

The next iteration should not bury this logic deeper into `internal/pipeline`.

Add new internal layers:
- `internal/evidence`
  stores per-pair evidence rows and feature extraction
- `internal/graph`
  candidate graph building and support-weight computation
- `internal/confidence`
  membership-confidence summaries from weighted clusters

Keep existing packages for now:
- `internal/score`
- `internal/cluster`

This gives a clean ablation surface without rewriting the full system first.

## Evaluation Protocol

Every candidate must be evaluated on the same `Dev-Core` export used for the current findings.

Acceptance bar:
- mean F1 must beat fixed `0.80`

Preferred promotion bar:
- mean F1 beats `0.80`
- win count improves over `4/5/0`
- confidence values qualitatively match obvious easy and hard cases

## Suggested Experiment Order

1. Freeze current `t0.80` outputs as the baseline artifact.
2. Add an offline evidence export format so clustering policies can be swept without rerunning scorer training.
3. Implement mutual-top-k candidate graphs.
4. Add triangle-support weighting.
5. Add membership-confidence summaries.
6. Benchmark against `t0.80` and LingPy.

## Questions

These are the two design questions that still matter before implementation expands:

1. For the paper target, do you care mainly about mean F1, or do you want me to optimize more aggressively for win count even if mean F1 gains are smaller?
2. For membership confidence, do you want a bounded `0..1` score that reads as a probability-like value, or a raw support index that is easier to justify but less directly interpretable?

## April 2026 Execution Plan

This is the concrete next step after commit `b6a4162`.

Frozen baseline to beat:
- graph mode `support`
- expansion mode `adaptive`
- `graph-top-k=3`
- `graph-adaptive-margin=0.1`
- `graph-adaptive-either-max=0.15`
- `graph-adaptive-ambiguity=0.5`
- `graph-contrastive-penalty=0.2`
- `graph-contrastive-floor=0.05`
- risk weights `rank=0.5 gap=0.5 triangle=0.0 unilateral=0.0`
- refine mode `bridge-cut`
- `refine-confidence-threshold=0.55`
- `refine-bridge-ratio=0.7`

Reference outcomes to keep in view:
- `Dev-Full`: still clearly above LingPy on mean F1
- `Holdout`: still below LingPy, but improved over the earlier support-only policy
- current best calibrated holdout result is still not enough for the "consistently beat LingPy" target

Primary promotion rule:
- a candidate only advances if it improves mean per-dataset F1 on `Dev-Core` against the frozen baseline

Secondary promotion rules:
- must not materially degrade runtime on `Dev-Core`
- should not collapse confidence calibration on obvious easy cases
- the promoted candidate must be rechecked on `Dev-Full` before any `Holdout` run

## Experiment Order

Run the next two feature families in this order:

1. concept-level density and dispersion features
2. alternative-cluster support features from a first-pass clustering
3. combined model only if each family helps independently or the interaction is theoretically defensible

This order is deliberate:
- density and dispersion features are cheaper, simpler, and less circular
- alternative-cluster features are stronger but depend on a first-pass partition and are therefore easier to overfit or rationalize post hoc

## Family 1: Density And Dispersion

Goal:
- detect concept graphs where permissive expansion is safe versus concept graphs where the local neighborhood structure is too diffuse

Hypothesis:
- some bad holdout concepts are not failing because the current edge features are weak in isolation
- they are failing because the whole concept graph is sparse, flat, or ambiguity-heavy, and the edge policy should react to that context

Candidate feature groups:
- node-level local density
  - mean similarity of top-`k` neighbors
  - top-1 minus top-2 similarity gap
  - ratio of in-threshold neighbors to expanded-only neighbors
- concept-level dispersion
  - share of active nodes with ambiguous top-1 support
  - variance of top-1 similarity across nodes
  - variance of top-1 minus top-2 gap across nodes
  - share of candidate edges that are mutual versus unilateral
- graph-shape summaries
  - mean candidate degree
  - degree variance
  - triangle density among admitted candidates
  - connected-component count before Infomap

Planned ablations:
- `D1`: only node-level ambiguity and gap features
- `D2`: only concept-level dispersion features
- `D3`: node plus concept features together
- `D4`: replace current adaptive fallback rules with a learned heuristic over the new density/dispersion summaries
- `D5`: keep the current adaptive policy but use density/dispersion only as an extra edge-risk term

Success criteria for Family 1:
- one of `D1` to `D5` improves `Dev-Core`
- the best `D*` candidate preserves or improves the two previously problematic holdout families during spot checks

## Family 2: Alternative-Cluster Support

Goal:
- use a first-pass clustering to estimate whether an edge or node assignment is unstable because a competing cluster is nearly as well supported

Hypothesis:
- the current edge-risk model is still too local
- many remaining errors come from cases where a form has several coherent competitors, which only becomes obvious after an initial partition exists

First-pass design:
- run the current frozen baseline to get a provisional partition
- compute support of each node to its assigned cluster
- compute strongest support to each alternative cluster
- derive a margin and probability-like confidence from assigned versus alternative support

Candidate feature groups:
- node-to-cluster support
  - mean weight to assigned cluster
  - max weight to assigned cluster
  - total weight to assigned cluster
- alternative-cluster pressure
  - strongest alternative cluster total weight
  - strongest alternative cluster mean weight
  - assigned-versus-best-alternative margin
  - ratio of assigned support to assigned plus alternative support
- edge-level cluster context
  - whether both endpoints prefer the same alternative cluster
  - whether an edge bridges two internally coherent provisional clusters
  - whether an edge is only supported through one low-confidence node

Planned ablations:
- `A1`: use these features only for confidence output, not clustering
- `A2`: use them to drive reassignment and bridge-cut refinement only
- `A3`: use them as a second-pass edge-risk penalty before rerunning Infomap
- `A4`: compare a simple hand-built rule set versus a supervised calibrator on `Dev-Core`

Success criteria for Family 2:
- one of `A1` to `A4` improves `Dev-Core`
- any clustering-affecting version must beat the confidence-only version, otherwise we keep the method as confidence analysis only

## Combined Promotion Protocol

The full promotion ladder is:

1. unit tests for new feature extraction
2. fast synthetic checks for intended failure modes
3. `Dev-Core` benchmark sweep
4. `Dev-Full` confirmation on the best candidate only
5. `Holdout` check on the promoted candidate only

Promotion guardrails:
- do not run full grid searches directly on `Holdout`
- if a richer supervised calibrator wins by less than `0.005` mean F1 on `Dev-Core`, prefer the simpler heuristic
- if a candidate improves mean F1 but loses badly on family win count, keep both summaries because that tradeoff matters for the paper

## Artifact Layout

Use separate benchmark run labels for each branch:
- `density-*` for Family 1
- `altsupport-*` for Family 2
- `combo-*` only for combined follow-up runs

Each run should produce:
- per-dataset result TSV
- markdown comparison against the frozen baseline
- per-form prediction TSV with confidence columns
- compact notes describing which features were active

## Small Harness Gaps To Fill Before Coding

Before implementing the new features, the benchmark tooling should support:
- a baseline alias so all future sweeps can refer to the frozen `b6a4162` configuration by name
- feature flags or labels in run metadata for density/dispersion versus alternative-cluster experiments
- a compact family-spot-check report for the two holdout families that previously improved under adaptive expansion

These are small harness improvements, not architectural blockers.

## Experimental Outcome: Family 1 And Family 2 Clustering Variants

Status:
- implemented as experimental knobs
- not promoted to the frozen clustering baseline
- do not evaluate on holdout unless a future dev-full promotion gate is passed

Family 1 result:
- density/dispersion edge-risk features improved `Dev-Core`
- the best density-only variant did not generalize to `Dev-Full`
- conclusion: keep these features available for analysis and future calibration, but do not use them as promoted clustering criteria

Family 2 result:
- alternative-cluster support produced stronger `Dev-Core` gains than Family 1
- pure `alt-reassign` and hybrid `bridge-alt-reassign` still regressed on `Dev-Full`
- gating reassignment by target-cluster cohesion made the method safer but removed too much of the dev gain
- conclusion: alternative-cluster support is useful signal, but assignment-changing rules are currently too aggressive

Next direction:
- keep clustering fixed at the frozen support/adaptive/bridge-cut baseline
- expose alternative-cluster support as confidence and diagnostics output
- use those diagnostics to measure whether low-confidence forms concentrate in actual errors before designing any further reassignment policy

## Confidence Output: Alternative-Support Diagnostics

Implementation direction:
- keep the promoted clustering policy unchanged
- compute diagnostics after the final partition is chosen
- write diagnostics to sidecar and benchmark prediction outputs

New per-form diagnostics:
- `assigned_support`: total edge support from the form to members of its assigned cluster
- `alternative_support`: strongest total edge support from the form to any competing cluster
- `support_margin`: `assigned_support - alternative_support`
- `confidence`: bounded score derived from assigned versus alternative support

Interpretation:
- high assigned support and low alternative support means the form is a stable cluster member
- low or negative support margin means the form is a likely boundary case
- these values are diagnostic confidence evidence, not a reassignment rule

Next validation:
- run the frozen baseline with prediction output on `Dev-Core`
- join predictions to pairwise or form-level error labels
- test whether low `confidence` and low `support_margin` concentrate among false-positive and false-negative decisions
