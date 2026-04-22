# Cognator: framework-native cognate detection with calibrated confidence

**Tiago Tresoldi**
Uppsala University

---

## Abstract

[NEEDS INPUT — deferred until numbers are frozen. Skeleton: we present
cognator, a cognate detection system built around a merkmal-based
phonological prior, asymmetric learned correspondences, and
support-aware graph clustering; on the arcaverborum CoreCog benchmark
it matches or exceeds LingPy's LexStat+Infomap mean F while running
~50× faster; we introduce a stratified isotonic calibration of the
system's confidence score that produces per-prediction risk bands
with documented error rates on a held-out split.]

---

## 1. Introduction

Cognate detection — the task of clustering phonologically related
forms across languages by their descent from a common ancestor — is
a foundational step in quantitative historical linguistics. Its
outputs feed phylogenetic inference, lexical-dating studies,
reconstruction pipelines, and dialectometric analyses. The dominant
open-source system in the field is LingPy's `LexStat+Infomap`
(List 2014, List et al. 2018), which combines a learned phonetic
alignment scorer with community detection on a threshold-pruned
similarity graph.

Two observations motivate the work reported here.

First, existing tools report aggregate accuracy figures but do not
tell a downstream user which individual predictions to trust. A form
aligned to a cluster may be an obvious inherited cognate, an
ambiguous short-form case, or a spurious merge under a weak threshold;
nothing in a typical sidecar distinguishes them. Practitioners
handle this by spot-checking or by re-running with multiple thresholds
and comparing, which is expensive and non-scalable.

Second, the design choices inherited from LingPy predate several
framework-level developments in the field: explicit phonological
feature bundles (Rama & Kolachina 2012, List et al. 2023); asymmetric
descriptions of sound change (Bowern 2012, Kümmel 2008); and the
availability of large, curated lexical aggregations such as
arcaverborum (Tresoldi, in prep.).

This paper describes **cognator**, an open-source cognate detection
system designed around three principles:

1. **Framework-native scoring.** Phonetic distances come from a
   pinned `merkmal` bundle (Tresoldi, in prep.); tone is treated as
   a property of the syllable nucleus; prosody enters the alignment
   score through a role-based penalty rather than as a separate
   post-processing pass.
2. **Asymmetric by construction.** Each ordered lect-pair trains its
   own correspondence scorer, and the Bayesian variant (§3.6) makes
   asymmetry a first-class modeling choice via per-direction
   Dirichlet priors.
3. **Calibrated per-prediction confidence.** The system emits a
   support-ratio confidence plus two structural signals
   (`cluster_size`, `candidate_neighbors`). A companion calibration
   tool fits isotonic regression against a form-local correctness
   label, producing per-prediction risk bands that transfer cleanly
   from development to held-out data.

On the arcaverborum CoreCog benchmark, cognator's promoted
configuration [NEEDS INPUT — final numbers after new arcaverborum
baseline]. Runtimes are 29–54× faster than LingPy depending on
split; on a 14 GB laptop the full 18-family Dev-Full sweep completes
in [NEEDS INPUT — seconds] versus LingPy's 1+ hour.

The remaining contribution is the calibration. A Dev-Core fit under a
three-axis stratification (`cluster_size` × `assigned_support`
quartile) yields risk bands that cover 51% of Held-out predictions at
under 7% empirical error. We are not aware of any prior cognate-
detection work that reports per-prediction calibrated confidence
with empirical-error risk bands validated on held-out data; we
position this as a methodological contribution rather than only an
engineering feature.

The paper is organized as follows. §2 surveys prior work on cognate
detection and on confidence calibration. §3 describes the cognator
pipeline end to end. §4 presents the calibration methodology. §5
reports head-to-head evaluation against LingPy across three
arcaverborum splits. §6 discusses where the system works, where it
does not, and the implications for downstream tooling. The codebase,
frozen benchmark configuration, and a browser-based WebAssembly demo
are released at [NEEDS INPUT — URL/DOI].

---

## 2. Background

### 2.1 Cognate detection

Automated cognate detection has converged, over the past two decades,
on a relatively stable pipeline: compute phonetic similarities among
forms sharing a concept, prune or weight the resulting graph, and
cluster. The dominant open-source implementation of this pattern is
LingPy's LexStat (List 2012), which learns a sound-correspondence
scorer per language pair before computing Needleman–Wunsch alignments
and applying a family-wide threshold (List et al. 2018). LexStat's
output feeds one of several clustering routines, most commonly Infomap
community detection (Rosvall & Bergstrom 2008). Coarser alternatives
exist for settings where learned correspondences are unreliable: the
SCA sound classes (List 2012) and their predecessors the Dolgopolsky
sound classes (Dolgopolsky 1964) collapse the phonetic space into
equivalence buckets before string comparison, trading resolution for
robustness on sparse or typologically unfamiliar data.

**Neural and hybrid approaches.** A strand of work investigates
whether machine-learned scorers can displace LexStat. Jäger, List &
Sofroniev (2017) apply SVM classifiers to phonetic alignment columns
to predict cognacy, finding gains on specific families but
comparable aggregate performance on the Indo-European benchmark.
Rama (2016) and subsequent work explored convolutional and
recurrent neural architectures. Rama, List, Wahle & Jäger (2018)
systematically compare SVM, LexStat, and several alternatives across
families and show that the choice of detector has only modest
downstream impact on phylogenetic inference — a finding that
reframes what "better" cognate detection should mean: if downstream
consumers are largely insensitive to the detector's aggregate F, the
useful distinction shifts from "which system wins F" to "which
individual predictions can we trust." This observation motivates
the calibration contribution of §4. Transformer-based models have
been applied both to cognate detection directly and to the sibling
task of proto-form reconstruction (Akavarapu & Bhattacharya 2023;
Meloni et al. 2021; Bouchard-Côté et al. 2013); results are
promising on single well-studied families but do not straightforwardly
transfer to typologically distant or deeper-time settings where the
learned-correspondence signal is weak.

**Expanding evaluation surface.** The canonical benchmark has
historically been constrained to well-curated Indo-European and
Austronesian datasets with gold cognacy from expert-curated lists.
Recent efforts have pushed outward: Jambu (Arora et al. 2023)
provides an Indo-Aryan benchmark; CLICS² (List, Greenhill, Anderson,
Mayer, Tresoldi & Forkel 2018) and its descendants offer
cross-linguistic colexification data useful for concept-level
evaluation; Lexibank (List et al. 2022) standardizes wordlists across
roughly a hundred datasets from multiple source formats. List &
Forkel (2022) extend the clustering evaluation to borrowing
identification, reframing cognate detection as one point on a
broader spectrum of inherited-versus-contact lexical relatedness.
The arcaverborum aggregation we use here (Tresoldi, in prep.)
continues this expansion of the empirical base.

**Sibling tasks and ecosystem.** Cognate detection sits within a
family of related problems: automated sound-correspondence induction
(List 2019), proto-form reconstruction (Meloni et al. 2021;
Bouchard-Côté et al. 2013), phylogenetic inference from cognate
data (Greenhill, Heggarty & Gray 2020), and the construction of
phonological-feature bundles (Rama & Kolachina 2012) — the last of
which is the research lineage that produced merkmal (Tresoldi, in
prep.). The CLDF standard (Forkel et al. 2018) provides the
input-output conventions that let systems in this family compose;
arcaverborum, merkmal, and cognator all adopt CLDF-compatible schemas.

**cognator's lineage.** cognator sits squarely within the LingPy
lineage — same task definition, same B-cubed evaluation metric,
largely compatible input format — but diverges in the scoring and
graph-construction layers in ways articulated in §3. It does not
attempt to compete with neural approaches on a per-pair
classification basis; its contribution is framework-native scoring
(merkmal-grounded, asymmetric-by-construction, prosody-aware) and
calibrated per-prediction confidence — both orthogonal to the
neural-versus-symbolic debate. Cognator's response to the Rama et al.
(2018) finding about downstream insensitivity is to shift what the
tool emits rather than what it scores: produce confidence-labelled
predictions that downstream phylogenetic or reconstruction pipelines
can consume selectively.

[NEEDS INPUT — all DOIs above still need verification against ACL
Anthology / Crossref / Linguistic Typology back issues before
submission. I can run a second verification pass once you tell me
which entries you already know are correct.]

### 2.1a The `new_chl` framework

This paper sits within a broader project that the author has
described as a "framework-native refoundation" of computational
historical linguistics tooling: a deliberate effort to build
successor tools whose design choices reflect the way contemporary
historical linguists describe sound change, rather than the design
compromises inherited from earlier-generation systems. The
framework's other artifacts are merkmal, a phonological-feature
library that supplies pinned grapheme distances (Tresoldi, in
prep.); arcaverborum, a curated multi-source lexical aggregation
(Tresoldi, in prep.); and regulae, a phrase-based sound-correspondence
learner currently under development. We use merkmal and arcaverborum
here; regulae is planned but orthogonal to this paper's scope.

### 2.2 Evaluation of cognate clustering

B-cubed precision, recall, and F1 (Bagga & Baldwin 1998; Amigó et
al. 2009) are the standard metrics for clustering-evaluation against
gold cognate sets, and we adopt them unmodified. A well-known
limitation is that B-cubed treats every form pair as equally
important within a concept, so a single misassignment in a large
cluster can dominate a concept's contribution to the aggregate.

### 2.3 Confidence and calibration

Calibration of probabilistic classifiers is well-studied outside
historical linguistics. The standard tools are reliability diagrams
against held-out empirical error rates (Guo et al. 2017); monotone
calibration via isotonic regression and pool-adjacent-violators
(Zadrozny & Elkan 2002); and temperature-scaling for neural softmax
outputs (Guo et al. 2017).

Within cognate detection, however, we are unaware of any prior work
that publishes per-prediction calibrated confidence scores backed by
empirical-error-rate risk bands against held-out data. LexStat
reports alignment distances per pair, not calibrated probabilities
of correctness; the SVM-based approach of Jäger et al. (2017) emits
per-pair probabilities but does not validate them against
reliability diagrams; recent SIGTYP shared-task systems report
aggregate F/P/R without per-prediction calibration. This gap is
what §4 addresses.

[NEEDS INPUT — confirm no known prior; please flag if you remember
any calibrated-cognate-detection work. Also DOIs for Guo 2017 and
Zadrozny-Elkan 2002 need verification before submission.]

---

## 3. System

### 3.1 Pipeline overview

cognator consumes a CLDF-shaped wordlist (arcaverborum conventions:
`ID`, `Language_ID`, `Concept`, `Segments`, and optional `Family`,
`Cognacy`) and emits a sidecar TSV in which every input form is
labelled with a cognate cluster identifier, a confidence score, and
structural diagnostics. The end-to-end pipeline is:

1. Parse tokens with a merkmal bundle; re-attach tone segments to
   their host nuclei.
2. For each family, for each ordered lect-pair with at least N
   shared concepts, train a correspondence scorer via EM-style
   alignment under a merkmal-derived prior.
3. Compute pairwise distances within each concept using asymmetric
   Needleman-Wunsch under the trained scorer.
4. Build a per-concept candidate graph under a support-aware policy
   (thresholded + top-k mutual expansion, with adaptive ambiguity
   fallbacks).
5. Cluster each concept graph with a two-level Infomap
   implementation.
6. Optionally refine clusters by cutting weak bridge edges
   incident to low-confidence nodes.
7. Emit per-form diagnostics: cluster id, confidence,
   assigned/alternative support, support margin, cluster size,
   candidate-neighbor count, and (optional) bootstrap interval.

Families are independent; the pipeline runs them in parallel, with
deterministic per-task sub-seeds so output is reproducible under
concurrency.

### 3.2 Framework-native design choices

**Merkmal prior.** The phonological-feature library merkmal
(Tresoldi, in prep.) provides pairwise grapheme distances that serve
as the system's base similarity measure. Using a pinned feature
bundle rather than a string-similarity heuristic like Levenshtein or
Dolgopolsky classes ensures reproducibility across corpora and lets
the scoring layer consume a calibrated, feature-grounded prior.

**Asymmetric alignment.** Sound changes are directionally asymmetric
(Bowern 2012 [NEEDS INPUT — confirm citation]); cognator trains a
separate scorer for each ordered lect-pair (A → B) rather than
symmetrizing. The forward and reverse scorers are allowed to
disagree on correspondence probabilities, and the downstream
distance is a mean of forward and reverse alignments.

**Prosody-aware scoring.** The default alignment scorer wraps the
merkmal distance with a role-based penalty: aligning a consonant
against a vowel, or a glide against a tone, is penalized by a
configurable margin. This is a cheap, framework-native substitute
for the SCA sound-class reduction that LingPy uses internally.

**Support-aware graph construction.** Rather than hard-thresholding
similarity edges, the candidate graph retains top-k mutual neighbors
and can optionally apply a contrastive penalty to edges dominated
by stronger local rivals. This is the cognator's main departure
from LingPy's pipeline, and is the source of the recall advantage we
observe on several families.

### 3.3 Scoring layer

The default scorer is a maximum-a-posteriori estimate of the
per-lect-pair correspondence table under a merkmal-derived prior,
blended with a learned log-odds signal via a concentration
parameter. For each ordered lect-pair (A → B) with at least N shared
concepts (default N = 5), we collect aligned form pairs, accumulate
per-grapheme-pair alignment-column counts via EM, and wrap the
resulting correspondence table in a log-odds scorer whose output is
bounded to [−1, 1] via a tanh squash for compatibility with the NW
aligner's similarity range. A lambda parameter mixes the raw prior
similarity with the learned log-odds; we document this parameter's
empirical default and its interaction with the shuffle-based null
model in §3.6 below.

### 3.6 Bayesian scoring variant

[NEEDS INPUT — this section will describe the Dirichlet-prior
posterior scorer in full. Short version for now:]

cognator ships an alternative posterior scorer, accessible via
`--scorer=posterior`, that replaces the lambda-blended learned scorer
with a full Bayesian treatment. For each ordered lect-pair we place
a per-source Dirichlet prior α(σ, τ) = α₀ · softmax_τ(−k · d(σ, τ))
over target graphemes, where d is the merkmal distance and α₀
controls prior strength. The posterior mean P(τ | σ) =
(α(σ, τ) + n(σ, τ)) / (α₀ + n(σ, ·)) replaces the lambda blend,
and asymmetry is first-class: each direction trains against its
own prior independently. A Dev-Core sweep identifies α₀ = 50 as the
empirical optimum; a per-row scaling mode (`sparse-boost`) adds
~0.002 F at the cost of one additional constant.

The Bayesian variant does not beat the default learned scorer on
Dev-Core (−0.008 F at the best configuration). We document it as a
principled alternative rather than the promoted default: it deletes
the Lambda and MinPairs constants from the Spec, makes asymmetry
architecturally explicit rather than an emergent property, and
exposes a sampleable posterior useful for future bootstrap work. The
honest finding is that a principled scoring layer is not by itself
the bottleneck — further quality gains appear to need changes
upstream of scoring (better evidence, e.g. phrase-based
correspondences via regulae) or downstream (cluster-level signals in
refinement).

### 3.4 Clustering layer

We use a from-scratch Go implementation of two-level Infomap (Rosvall
& Bergstrom 2008) on the per-concept candidate graph. The
implementation is deterministic under our per-task sub-seeding
scheme. Bridge-cut refinement is applied post-clustering to low-
confidence nodes — we remove weak in-cluster edges incident to
nodes whose ConfidenceMean is below a threshold, then recompute
connected components.

### 3.5 Configuration and reproducibility

All empirical constants live in `internal/pipeline/defaults.go` with
per-constant WHY comments documenting the choice. A
`--preset=support-adaptive-bridge` flag applies the promoted research
configuration in one token, and `--meta-out` captures the effective
(post-default, post-preset) Spec as a JSON artifact next to the
benchmark output. This is intended to make every published number
reproducible by re-running `go run ./cmd/bench --preset=...` against
the same arcaverborum snapshot.

---

## 4. Calibrated confidence

### 4.1 The problem

[NEEDS INPUT — brief, then the numerical story. Key point: node-level
confidence signals (support ratio) do not calibrate well against
concept-level pairwise error involvement because the scales don't
match. First attempt was degenerate. See roadmap docs/roadmap.md §2.]

### 4.2 Form-local correctness label

Pairwise error involvement — the standard way to binarize
clustering correctness at the form level — flags every member of a
concept whenever any same-concept pair is a false merge or false
split. On CoreCog concepts this means a single mis-assignment in a
large concept propagates to many rows' labels, and a flat confidence
metric cannot discriminate within the flagged set.

We instead define a *form-local* correctness label:

[NEEDS INPUT — do you want the formula here or in an appendix? Below
is the inline version.]

For each form *f* with gold class *G(f)* and predicted cluster
*P(f)* within a concept, let

```
precision(f) = |P(f) ∩ G(f)| / max(1, |P(f) \ {f}|)
recall(f)    = |P(f) ∩ G(f)| / max(1, |G(f) \ {f}|)
```

where set operations exclude *f* itself. *f* is labelled correct iff
`precision(f) ≥ 0.5 AND recall(f) ≥ 0.5`. True-singleton gold
classes with no peers in either set are vacuously correct.

This label is node-local by construction: a form's correctness
depends on its own cluster context, not on distant members of the
concept. It matches the scale of node-level confidence metrics like
the support ratio we emit.

### 4.3 Stratified isotonic calibration

Form-local correctness, by itself, does not make the support-ratio
confidence directly calibratable. Two specific failure modes remain
after the label switch:

**Singleton collapse.** Every predicted-singleton form has an
alternative-support total of zero by construction (its cluster has
no other members), so the support ratio defaults to 1.0. But a
singleton can be correct (an isolated form in a true-singleton gold
class) or incorrect (a form that should have merged with gold-peers
elsewhere in the concept). Both cases receive confidence = 1.0. A
calibration fit over the raw scalar cannot distinguish them.

**Within-multi bimodality.** Among multi-member predictions, many
also saturate confidence at 1.0 — nodes with zero alternative support
but multiple in-cluster partners. Within this saturated bin, empirical
accuracy ranges widely depending on how much mass the node commits to
its cluster: a node contributing 0.4 total support to a 5-member
cluster is structurally different from one contributing 30.0. Flat
isotonic binning on confidence cannot resolve this spread.

We address both with stratification along two structural axes that
the system emits alongside `confidence`:

- **`cluster_size`** (integer): number of forms in the predicted
  cluster, including the form itself.
- **`assigned_support`** (float): total edge weight connecting the
  form to its cluster-mates.

The `cluster_size` axis resolves singleton collapse by fitting
singletons and multi-member predictions separately. The
`assigned_support` axis addresses within-multi bimodality by further
stratifying multi-member predictions into quartiles of support mass
observed on the fit set. Quartile cut points are computed once and
stored in the calibration artifact so they can be applied at
inference to new predictions.

Within each stratum, we bin rows into equal-count confidence bins
(default 20 bins), compute empirical accuracy per bin, and fit a
monotone calibration via pool-adjacent-violators isotonic regression
(Zadrozny & Elkan 2002). The resulting piecewise-constant map is
stored as a set of `(confidence_bin_center, calibrated_value)` pairs
per stratum; inference interpolates linearly between pairs.

**Risk bands.** A single empirical threshold pair partitions
calibrated values into three bands:

- **low** risk: calibrated ≥ 0.90
- **medium** risk: 0.60 ≤ calibrated < 0.90
- **high** risk: calibrated < 0.60

Thresholds are stored in the calibration artifact so the bands travel
with the fit. The values reflect a pragmatic trade-off we validate
empirically in §4.4: the 0.90 threshold targets "publication-ready"
reliability (empirical error rate well under 10%), the 0.60 threshold
separates "mostly reliable" predictions from those warranting manual
review. Users can override the bands at apply time.

**Transfer methodology.** We fit calibrations on a held-out
development split (Dev-Core), apply them unchanged to Dev-Full and
to a fully held-out Holdout split, and measure two transfer metrics
per stratum:

- empirical accuracy by calibrated decile (reliability diagram),
- empirical error rate per risk band.

Well-calibrated transfer means the empirical accuracy on the transfer
set matches the calibrated estimate within the binomial confidence
interval expected from the band's row count; good *discrimination*
means the bands have substantially different error rates (low band
near 0%, high band substantially above). §4.4 reports both.

### 4.4 Results

[NEEDS INPUT — risk-band transfer table. Will populate from the new
arcaverborum baseline once it lands. Template:]

[TABLE: reliability by calibrated decile, per stratum, on Holdout;
columns = (stratum, decile, N, empirical accuracy, calibrated
estimate). One row per populated (stratum, decile) bucket.]

[TABLE: risk-band distribution on Holdout; columns = (stratum,
band, N, errors, empirical error rate, fraction of stratum).]

[NARRATIVE: headline number — X% of Holdout rows sit in a calibrated
band with under Y% error rate. Per-stratum commentary on where
calibration transfers cleanly vs where the top plateau caps below
0.90. Current CoreCog numbers (to be superseded): ~33% of Holdout
rows in multi_q4 "low" at 3% error; ~18% in multi_q3 "medium" at
6% error; ~51% cumulative under 7% error.]

[FIGURE: reliability diagram per stratum (5 panels: multi_q1-4,
singleton) with y=x reference line and per-band empirical error
rate annotations.]

[FIGURE: risk-band distribution stacked bar (one bar per stratum,
segments = low/medium/high with error-rate labels).]

---

## 5. Evaluation

[Section skeleton — will be fleshed out after the new arcaverborum
baseline lands.]

### 5.1 Data

### 5.2 Method

### 5.3 Head-to-head results

### 5.4 Per-family analysis

### 5.5 Runtime and memory

---

## 6. Discussion

[Skeleton — will be fleshed out once evaluation numbers are frozen.]

### 6.1 What works

### 6.2 What does not

### 6.3 Implications for downstream users

---

## 7. Future work

Several directions are natural extensions of the system but sit
outside the scope of this paper.

**Regulae integration.** The framework's long-term target is
phrase-based correspondence learning via the `regulae` package, which
models sound change at the level of conditioned environment rules
rather than context-free grapheme substitutions. We anticipate
regulae-based scoring to help on families where sound change is
heavily phrase-conditioned (e.g., Grimm's law environments, Romance
palatalization, Austronesian reflex sets) without hurting
context-free cases; this is an open research question we are
pursuing separately.

**Posterior-sample bootstrap.** The Bayesian scoring layer exposes
a sampleable Dirichlet posterior. Replacing the current
Gaussian-distance-perturbation bootstrap with a posterior-sample
bootstrap would make the emitted confidence intervals honest
Bayesian credible intervals rather than heuristic stability indices.

**Evidence caching.** Serializing per-family alignment-column
frequencies would turn multi-configuration sweeps from minutes into
seconds, which is the natural engineering unblock for deeper
exploration of the design space.

**Cross-family and deeper-time analysis.** cognator's current scope
is same-family clustering per concept. Cross-family analysis — or
clustering under a relaxed concept assumption — would require
either a different scoring layer (deeper-time cognates fall below the
learned-correspondence signal floor) or an explicit semantic-shift
model. Neither is in the current system; both are plausible
extensions.

---

## 8. Availability

[NEEDS INPUT — the right level of detail. Options: (a) minimal
reference to GitHub URL + DOI, (b) fuller reproducibility paragraph
covering WASM demo, frozen arcaverborum snapshot, and the precise
command lines used to produce each table/figure. My recommendation:
(b), but ask your call.]

---

## Acknowledgments

[NEEDS INPUT — anyone you want to acknowledge. Uppsala, funding
bodies, arcaverborum contributors, reviewers, etc. Leaving blank.]

---

## References

[NEEDS INPUT — to be assembled from citations above. I've left
citation anchors (Author Year) inline; once you confirm the citation
style and the exact bibliographic entries for merkmal/arcaverborum
I'll build the reference list.]

---

## Appendices

### A. Ablations

[Skeleton — if we want to report deletion ablations (graph modes,
refinement modes, etc.). Low priority; discuss.]
