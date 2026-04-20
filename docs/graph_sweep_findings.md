# Graph Sweep Findings

This note summarizes the first full `Dev-Core` graph-policy sweep after adding support-aware graph construction.

Primary artifact:
- [benchmark/runs/20260419T211954Z-a-dev_core/graph_sweep/summary.tsv](/home/tiagot/nas-dev/new_chl/cognator/benchmark/runs/20260419T211954Z-a-dev_core/graph_sweep/summary.tsv)

Baseline to beat:
- fixed threshold `0.80`

## Result

The support-aware graph architecture is a substantial improvement over the threshold-only graph.

Threshold-only baseline:
- mean F1 `0.7941`
- mean delta vs LingPy `+0.0084`
- win/loss/tie `4/5/0`

Best support graph from the first fixed-`0.80` sweep:
- label: `graph-support-k3-m0.2-t0.3`
- mean F1 `0.8234`
- mean delta vs LingPy `+0.0378`
- win/loss/tie `5/4/0`

This is a large jump:
- `+0.0294` mean F1 over fixed-threshold baseline

## What Improved

The gain comes from recovering recall on the datasets that were previously dominated by false splits.

Selected dataset deltas against fixed `0.80` threshold:
- `utoaztecan`: `0.4987 -> 0.7559` (`+0.2572`)
- `zhangrgyalrong`: `0.8213 -> 0.9424` (`+0.1211`)
- `chacontukanoan`: `0.9490 -> 0.9788` (`+0.0298`)
- `carvalhopurus`: `0.9846 -> 0.9920` (`+0.0074`)
- `gaotb`: `0.6611 -> 0.6671` (`+0.0060`)

Error decomposition confirms that support graphs reduce false splits sharply on several of these families.

Examples:
- `chacontukanoan` false split global: `0.1029 -> 0.0355`
- `utoaztecan` false split global: `0.8074 -> 0.5167`
- `zhangrgyalrong` false split global: `0.3722 -> 0.1270`
- `carvalhopurus` false split global: `0.0426 -> 0.0135`

## What Got Worse

The architecture still pays for the recall gains with higher false merges on merge-sensitive datasets.

Main regressions against fixed `0.80` threshold:
- `lundgrenomagoa`: `0.8931 -> 0.8203`
- `dunnaslian`: `0.8184 -> 0.7675`
- `peirosaustroasiatic`: `0.6061 -> 0.5893`
- `hattorijaponic`: `0.9142 -> 0.8977`

Error decomposition shows the dominant issue in these regressions is increased false merge rate.

Examples:
- `dunnaslian` false merge global: `0.2869 -> 0.3729`
- `lundgrenomagoa` false merge global: `0.5263 -> 0.6806`
- `peirosaustroasiatic` false merge global: `0.4370 -> 0.4804`
- `gaotb` false merge global: `0.3777 -> 0.4106`

## Interpretation

This changes the research direction.

Before the sweep, the main question was whether richer graph construction would help at all.

After the sweep, that question is answered:
- yes, support-aware graph construction is the right direction

The next question is narrower:
- how do we keep the recall gains while constraining merges on merge-sensitive families?

That suggests the next architecture step should not be another threshold search. It should be:
- stricter candidate admission for low-support long-range edges
- or a merge-penalizing cluster refinement stage
- or both

## Confidence Output Status

The new pipeline now emits:
- `confidence`
- `bootstrap`
- `bootstrap_low`
- `bootstrap_high`

These outputs are operational, but the semantics are not final yet.

Observed from a targeted bootstrap check:
- clean families such as `carvalhopurus` mostly show near-1 stability with narrow intervals
- problematic families such as `dunnaslian` show wider intervals on average

However, some rows still show unintuitive combinations of:
- high membership confidence
- low bootstrap stability

This is not necessarily a bug, because the two quantities measure different things:
- membership confidence is graph-local support inside the chosen clustering
- bootstrap stability is assignment persistence under edge perturbation

But before presenting this in a paper, the relationship between those quantities needs to be tightened and explained better.

## Recommended Next Step

Promote `graph-support-k3-m0.2-t0.3` to the new graph architecture baseline.

Then test one targeted refinement aimed specifically at merge control:
- keep the same support graph
- penalize edges that are not mutual-top-k when they bridge already-dense neighborhoods

Alternative formulation:
- keep candidate expansion at `k=3`
- require stronger support for edges whose endpoints both already have multiple strong in-cluster neighbors

This is likely to preserve the large recall gains on:
- `utoaztecan`
- `zhangrgyalrong`
- `chacontukanoan`

while reducing the regressions on:
- `dunnaslian`
- `lundgrenomagoa`
- `peirosaustroasiatic`

## Follow-Up Probe: Mutual-Only Expansion

After the sweep, a direct A/B probe compared the best support graph under:
- top-k expansion from either endpoint
- top-k expansion only when the relation is mutual

Result:
- `either` expansion: mean F1 `0.8234`
- `mutual` expansion: mean F1 `0.8162`

So mutual-only expansion improves some merge-sensitive datasets:
- `dunnaslian`: `+0.0127`
- `lundgrenomagoa`: `+0.0153`
- `hattorijaponic`: `+0.0085`

But it gives back too much of the key recall breakthrough:
- `utoaztecan`: `-0.0563`
- `zhangrgyalrong`: `-0.0469`

Conclusion:
- the next refinement should not globally replace `either` expansion with `mutual` expansion
- merge control needs to be more selective than that

## Follow-Up Probe: Selective Unilateral Penalty

A targeted refinement was tested on top of the best support graph:
- penalize non-mutual expanded edges when both endpoints already have stronger local rivals

Result:
- the effect was extremely small
- mild penalties changed mean F1 only in the fourth decimal place
- even aggressive settings did not materially improve merge-heavy families

Conclusion:
- this specific rival-penalty mechanism is too weak to be the next main lever
- keep it as an explored branch, but do not promote it

## Post-Cluster Refinement Results

Three post-cluster refinement families were implemented and tested on the tuned support baseline:

- low-confidence node reassignment
- confidence-gated bridge cutting
- selective local reclustering of low-confidence clusters

### 1. Low-Confidence Node Reassignment

This was not viable.

It improved some recall-starved datasets:
- `utoaztecan`
- `zhangrgyalrong`
- `chacontukanoan`

But it caused large regressions on merge-sensitive families:
- `dunnaslian`
- `gaotb`
- `peirosaustroasiatic`

Aggregate result:
- baseline mean F1 `0.8286`
- reassignment variants `0.8032` and `0.8008`

Conclusion:
- discard as a working direction

### 2. Confidence-Gated Bridge Cutting

This was the only competitive post-cluster refinement.

Best tested bridge-cut setting:
- `refine-mode bridge-cut`
- `refine-confidence-threshold 0.55`
- `refine-bridge-ratio 0.7`

Aggregate result:
- baseline mean F1 `0.8286`
- bridge-cut mean F1 `0.8287`

It improved several merge-heavy families:
- `dunnaslian`: `+0.0082`
- `gaotb`: `+0.0085`
- `lundgrenomagoa`: `+0.0133`
- `peirosaustroasiatic`: `+0.0097`

But it still paid recall on:
- `utoaztecan`: `-0.0349`
- `zhangrgyalrong`: `-0.0058`

Conclusion:
- keep as the best post-cluster refinement seen so far
- gains are real but modest

### 3. Selective Local Reclustering

This was too aggressive even when restricted to low-confidence clusters.

Aggregate result:
- local-recluster mean F1 `0.8271`
- precision rose sharply
- recall fell sharply

Interpretation:
- it recreates the same precision-over-recall failure mode seen in the earlier core-connectivity splitting

Conclusion:
- do not promote this branch

## Follow-Up Probe: Threshold Re-Tuning Inside Support Mode

Once support-mode graph construction is enabled, the best threshold is no longer `0.80`.

Observed support-mode threshold sweep:
- `0.65`: mean F1 `0.8285`
- `0.68`: mean F1 `0.8286`
- `0.70`: mean F1 `0.8281`
- `0.75`: mean F1 `0.8260`
- `0.80`: mean F1 `0.8234`

At `0.68`, the best support graph currently reaches:
- mean F1 `0.8286`
- mean delta vs LingPy `+0.0430`
- win/loss/tie `4/5/0`

Interpretation:
- support-mode graph construction already adds substantial connectivity
- therefore the optimal threshold shifts downward relative to the old threshold-only baseline
- the next working baseline should be:
  `graph-support-k3-m0.2-t0.3` with threshold `0.68`

## Holdout Validation: Current Baseline Does Not Yet Generalize

The frozen candidate validated on holdout was:
- support graph
- threshold `0.68`
- expansion mode `either`
- top-k `3`
- mutual boost `0.2`
- triangle boost `0.3`
- bridge-cut refinement with confidence threshold `0.55` and bridge ratio `0.7`

Holdout result against LingPy:
- Cognator mean F1 `0.8410`
- LingPy mean F1 `0.9018`
- delta `-0.0608`
- Cognator mean precision `0.7844`
- LingPy mean precision `0.9087`
- Cognator mean recall `0.9355`
- LingPy mean recall `0.8959`

Interpretation:
- the current support-graph baseline keeps the recall advantage
- but it does not yet beat LingPy out of sample because precision collapses on some held-out families

Main holdout regressions:
- `blustaustronesian`: `0.6169` vs LingPy `0.8353`
- `syrjaenenuralic`: `0.6926` vs LingPy `0.8857`
- `chaconbaniwa`: `0.8441` vs LingPy `0.8837`

Error decomposition confirms a broad merge problem rather than a narrow edge-case failure:
- `blustaustronesian` Cognator false-merge global `0.5711` vs LingPy `0.3410`
- `syrjaenenuralic` Cognator false-merge global `0.6489` vs LingPy `0.4237`

## Confidence Diagnostic on Holdout Failures

The confidence channel is not yet strong enough to serve as the main anti-merge guard.

On the two worst holdout families:
- impure predicted clusters often still have high mean confidence
- several badly mixed clusters in `syrjaenenuralic` saturate near `1.0` confidence

Interpretation:
- merge-heavy failures are often confidently wrong under the current membership score
- so post-hoc confidence gating alone is unlikely to close the holdout gap
- the next architecture lever should be graph construction, not just another refinement rule

## Targeted Holdout Probe: Expansion Policy Matters More Than Threshold

Using only the two worst holdout families (`blustaustronesian`, `syrjaenenuralic`), several follow-up probes were run.

Baseline on those two families:
- `either`, `k=3`, threshold `0.68`
- `blustaustronesian`: `0.6169`
- `syrjaenenuralic`: `0.6926`

Raising the threshold alone did not help:
- `either`, `k=3`, threshold `0.75`
- `blustaustronesian`: `0.600`
- `syrjaenenuralic`: `0.694`

Mutual-only expansion helped substantially:
- `mutual`, `k=3`, threshold `0.68`
- `blustaustronesian`: `0.686`
- `syrjaenenuralic`: `0.761`

The best targeted probe so far was stricter mutual expansion:
- `mutual`, `k=2`, threshold `0.68`
- `blustaustronesian`: `0.712`
- `syrjaenenuralic`: `0.820`

Interpretation:
- the holdout failure is driven more by aggressive candidate expansion than by an overly low global threshold
- mutual expansion is a real anti-merge lever
- but a global switch to strict mutual expansion is not viable on its own

## Dev-Core Countercheck: Mutual k=2 Overcorrects

The same `mutual`, `k=2`, threshold `0.68` configuration was checked on `Dev-Core`.

It improved some merge-heavy families:
- `dunnaslian`: `0.8146 -> 0.8428`
- `lundgrenomagoa`: `0.8334 -> 0.8697`

But it destroyed key recall wins that made the support graph attractive in the first place:
- `utoaztecan`: `0.7160 -> 0.5632`
- `zhangrgyalrong`: `0.9320 -> 0.7822`

Conclusion:
- no single global expansion policy currently dominates
- `either,k=3` is too merge-happy on some held-out families
- `mutual,k=2` is too conservative on recall-critical families

This sharpens the next architecture question:
- how do we choose expansion aggressiveness adaptively from the family or concept evidence, instead of fixing one global policy?

## Adaptive Expansion Policy: First Implemented Variant

An adaptive support-graph mode is now implemented.

Behavior:
- keep all sub-threshold edges as before
- keep mutual top-k expansion edges as before
- keep above-threshold unilateral expansion edges only when one endpoint is making a clear top-1 choice
- if enough nodes in the concept graph are ambiguous, adaptive mode can fall back to mutual expansion for that concept

Current exposed knobs:
- `graph-expansion-mode adaptive`
- `graph-adaptive-margin`
- `graph-adaptive-ambiguity`

The key heuristic is:
- compare each node's best and second-best neighbor support
- treat small top1-vs-top2 gaps as ambiguity
- only let a unilateral edge through when that ambiguity test says one endpoint has a clear favorite

## Adaptive Probe Results

Best compromise tested so far:
- `graph-expansion-mode adaptive`
- `graph-top-k 3`
- `graph-adaptive-margin 0.1`
- bridge-cut refinement unchanged

Against the old frozen baseline (`either`, `k=3`):

On the two worst holdout families:
- baseline mean F1 `0.6548`
- adaptive mean F1 `0.7215`

Per-family:
- `blustaustronesian`: `0.6169 -> 0.6845`
- `syrjaenenuralic`: `0.6926 -> 0.7585`

On `Holdout` overall against LingPy:
- old baseline mean F1 `0.8410`
- adaptive mean F1 `0.8660`
- LingPy mean F1 `0.9018`
- holdout gap vs LingPy improved from `-0.0608` to `-0.0358`

On `Dev-Full` against LingPy:
- old baseline mean F1 `0.8287`
- adaptive mean F1 `0.8227`
- LingPy mean F1 `0.7847`

Interpretation:
- the adaptive policy is a real improvement on held-out merge-heavy families
- it materially narrows the holdout gap to LingPy
- but it still gives back some of the recall-heavy advantage on `Dev-Full`, especially on:
  `utoaztecan` and `zhangrgyalrong`

So the first adaptive policy is directionally correct, but not yet sufficient to consistently beat LingPy.

## Contrastive Edge Penalty: First Result

The next architecture step tested was a merge-risk term in the edge weight itself.

Behavior:
- estimate whether an edge is locally dominated by stronger competitors
- use endpoint rank plus best-rival gap to derive a contrastive risk
- downweight high-risk edges before clustering instead of only admitting or rejecting them

New knobs:
- `graph-contrastive-penalty`
- `graph-contrastive-floor`

Best tested compromise so far:
- adaptive expansion baseline
- `graph-contrastive-penalty 0.2`
- `graph-contrastive-floor 0.05`

Results on the two worst holdout families:
- adaptive-only mean F1 `0.7215`
- adaptive + contrastive mean F1 `0.7263`

Per-family:
- `blustaustronesian`: `0.6845 -> 0.6869`
- `syrjaenenuralic`: `0.7585 -> 0.7658`

Results on `Holdout` against LingPy:
- adaptive-only mean F1 `0.8660`
- adaptive + contrastive mean F1 `0.8672`
- LingPy mean F1 `0.9018`
- holdout gap vs LingPy improved slightly further from `-0.0358` to `-0.0346`

Results on `Dev-Full` against LingPy:
- adaptive-only mean F1 `0.8227`
- adaptive + contrastive mean F1 `0.8209`
- LingPy mean F1 `0.7847`

Interpretation:
- the contrastive term is helping in the intended direction
- especially on the two hardest holdout merge-heavy families
- but the gain is still incremental rather than decisive
- the remaining benchmark gap is now narrow enough that additional edge-scoring work still looks justified

## Calibrated Edge-Risk Blend: First Tuning Pass

The contrastive penalty was then generalized into a calibrated local risk score.

Features currently exposed:
- endpoint rank risk
- best-rival gap risk
- low triangle-support risk
- non-mutual expansion risk

The penalty still uses one global strength parameter, but risk is now a weighted blend of those local features.

Observed from a small holdout-bad calibration sweep:
- rank+gap only: mean F1 `0.7250`
- rank+gap+triangle: mean F1 `0.7260`
- rank+gap+triangle+unilateral: mean F1 `0.7241`

So the best first calibrated blend was:
- rank `0.35`
- gap `0.35`
- triangle `0.30`
- unilateral `0.00`

With the same penalty strength:
- `graph-contrastive-penalty 0.2`
- `graph-contrastive-floor 0.05`

Results on full `Holdout` against LingPy:
- calibrated-risk mean F1 `0.8678`
- previous contrastive model mean F1 `0.8672`
- adaptive-only mean F1 `0.8660`
- LingPy mean F1 `0.9018`

This narrows the holdout gap further:
- adaptive-only: `-0.0358`
- first contrastive model: `-0.0346`
- calibrated blend: `-0.0340`

Interpretation:
- learned-style calibration of edge risk is again helping in the intended direction
- triangle support is a useful feature inside the risk model
- the effect is still incremental, but it is now consistently incremental across successive edge-scoring refinements
