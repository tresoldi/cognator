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
