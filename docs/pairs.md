# Pairwise export: q(pairs) and partial cognacy

`cognator detect --pairs-out` writes one weighted row per same-concept,
cross-lect pair of **units**: whole words, or (with `--pairs-unit morphemes`)
the morphemes between `+` boundaries. It is the `q(pairs)` the framework's
interchange describes (`role: "q"`): proposal mass for a correspondence model
to reweight, not cognacy and not evidence. Nothing in it is a cognate
judgement, and the weights must not be thresholded into one and passed on as
if they were.

Clustering is not affected by any of this. With or without these options, the
sidecar's existing columns are byte-for-byte what `detect` writes without them.

## What is compared

- **Units.** In word mode, each row is one unit. In morpheme mode, each row is
  split at its `+` tokens into spans `[0, b1), [b1, b2), ... [bk, n)`; empty
  spans (a leading, trailing or doubled `+`) are dropped, which is the rule
  regulae's loaders apply to `breaks`, so morpheme *k* here is morpheme *k*
  there. A row without `+` is one unit, the whole word, so a corpus without
  boundaries gives exactly the word-mode weights in either mode.
- **Pairs.** Every unit of row *i* against every unit of row *j*, for rows of
  the same concept and different lects. Morphemes of one word share its lect
  and are never paired; nothing is compared across concepts.
- **Distance.** The pipeline's own: the per-lect-pair posterior scorer (or
  the merkmal prior where a lect pair has too few shared concepts), the
  alignment mode in force, and the combined global/local/overlap distance.
  The scorers are trained on whole words, as for clustering, and are not
  retrained on morphemes. The `distance` column is bit-for-bit what clustering
  used for word pairs; the tests recompute it through the public API.
- **Components.** `global`, `local` and `overlap` are each `1 - 2s/(aa+bb)`
  for that alignment's score *s*, clamped to [0, 1]. The combined distance is
  the clamped mean of the *unclamped* three, so it is not always the mean of
  the reported ones.

## The weight: an empirical null

```
weight = 1 - (number of null pairs with distance <= this pair's distance) / (null size)
```

The **null** for a pair is a sample of cross-**concept** pairs of units from
the **same lect pair** whose lengths fall in the **same length bins** as this
pair's two units, the bin of each side matched separately. Bins (segments per
unit, shared by words and morphemes):

| bin | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 9 | 11 | 14 |
|---|---|---|---|---|---|---|---|---|---|---|
| lengths | 1 | 2 | 3 | 4 | 5 | 6 | 7-8 | 9-10 | 11-13 | 14+ |

Exact up to six segments, where affixes live. Short units match by chance far
more often than long ones, so the same distance earns a short affix a much
lower weight than a long root. In the test fixture, the shared suffix `n a`
and the shared root `m o r e v` both sit at distance 0; the suffix gets weight
0.0 (every cross-concept `n a ~ n a` is just as close) and the root 1.0.

**Sampling.** Each (lect pair, bin pair) stratum is drawn once. With at most
`--null-samples` (default 256) candidate pairs, all are used; otherwise that
many are drawn uniformly with replacement, rejecting same-concept draws (at
most eight attempts per sample), from a SplitMix64 generator seeded with
`--null-seed` (default 1; 0 also means 1) mixed with the stratum's lect names
and bins. A stratum's sample therefore depends only on its own key and the
seed, not on what else is in the run, and repeated runs are identical.

**Widening.** A stratum with fewer than `--null-min` (default 20) pairs is
too small to read a rate from, and the pair falls back, in order:

| `null_scope` | null used |
|---|---|
| `stratum` | the matched stratum |
| `lect_pair` | the same lect pair at any length, when that has at least `--null-min` |
| `sparse` | whichever of the two is larger, when neither reaches `--null-min` |
| `none` | there is no cross-concept pair at all; the weight is 0 |

Weight 0 under `none` is deliberate: with no background, nothing is claimed.
Every row carries `null_n` and `null_scope`, so a consumer can discard the
weaker ones.

## What the weight is not

It is a **background-match rate** -- one minus a p-value-like quantity for
"this pair is closer than chance pairings of the same shape are" -- and **not
a calibrated posterior probability of cognacy**. In particular:

- It is not calibrated against any gold standard. A weight of 0.9 does not
  mean nine in ten such pairs are cognate.
- **First measurement against gold** (2026-10-30; IE-CoR expert cognacy for
  Latin, Spanish, Portuguese, Catalan, French, Italian and Romanian, 1,201
  forms taken from arcaverborum's committed web snapshot; default options).
  - 3,680 within-concept word pairs: 2,441 gold-cognate, 1,239 not.
  - **Ranking is good:** AUC 0.90. The median weight is 0.996 for cognates
    and 0.75 for non-cognates.
  - **As a probability it is poor.** Non-cognates average 0.59. 29% of them
    weigh more than 0.9 (89% of cognates do).
  - **Consequence for regulae:** kept as independent edges, these weights
    would carry about 727 false edges beside 2,317 true ones. Under "no
    relation", one minus a background rate is spread over [0, 1] by
    construction, so even a perfect null would not give a probability.
  - **Partial fix tried offline: a two-groups local false-discovery rate**
    over the same p-values, with no labels used. Weight = 1 − π₀/f(p), with
    π₀ from Storey's λ = 0.5 and f a non-increasing histogram fit.
    - False edges fall to about 555 (true 2,237).
    - It underestimates π₀ (0.24 against a true non-cognate share of 0.34).
      Same-meaning non-cognates in related languages resemble each other
      more than cross-meaning pairs do.
    - So mid-range weights remain overconfident: in [0.6, 0.8), 42% are
      cognate; in [0.8, 0.9), 52%.
  - A calibrated weight needs either a labelled calibration map (isotonic, per
    family or per depth band; `scripts/calibrate_confidence.py` does this for
    the cluster confidence) or a null drawn from same-meaning non-cognates.
    That second option is circular without labels.
- Its resolution is `1/null_n`; with the default 256 it cannot tell 0.997
  from 1.
- The distance is clamped at 1.0, and on small corpora much of the null sits
  exactly at the clamp (60-75% on the test fixtures), so any pair with a
  distance below 1 starts from that weight. Unrelated forms that share a
  couple of well-corresponding segments can score high: *topan ~ danem* gets
  0.93 on the fixture. More concepts make the null denser and this smaller,
  but the clamp itself stays.
- The null is cross-concept chance resemblance only. Loans, sound symbolism
  and nursery forms are not in it, and it does not model them.
- Morpheme weights use scorers trained on whole words, so correspondences
  that exist only inside affixes are seen through the word-level tables.
- Pairs are not independent: one lect pair's sample serves all its pairs, and
  the morphemes of a compound are scored against overlapping nulls.

regulae's soft loader keeps each edge independently with probability equal to
its weight. These weights are fit for that use only in the sense that they are
in [0, 1] and monotone in the evidence; treating them as probabilities is a
modelling choice the consumer makes, and its outer loop is where they get
reweighted (CONSTITUTION §4.2).

## Formats

### TSV

Word mode:

```
form_a  form_b  weight  distance  global  local  overlap  null_n  null_scope
```

Morpheme mode (`morph_a`, `morph_b` are 0-based morpheme indices):

```
form_a  morph_a  form_b  morph_b  weight  distance  global  local  overlap  null_n  null_scope
```

`form_a` is always the row that comes first in the input. These column names
are the ones regulae's soft-cognacy loader reads (`form_a form_b weight`, and
`morph_a morph_b` for morpheme pairs); the extra columns are ignored there.

### q_pairs JSON

By `.json` extension or `--pairs-format json`:

```json
{
  "schema_version": "0.1.0",
  "role": "q",
  "producer": "cognator@0.1.0+merkmal.3d21e0831241",
  "unit": "morpheme",
  "pairs": [
    {"form_obs_a": "a_pebble#0", "form_obs_b": "b_pebble#0", "weight": 1.000000,
     "components": {"distance": 0.075759, "global": 0.075759, "local": 0.075759,
                    "overlap": 0.075759, "null_n": 256, "null_scope": "stratum"}}
  ],
  "diagnostics": {
    "cognator_version": "0.1.0",
    "merkmal": {"version": "0.7.0", "commit": "3d21e0831241"},
    "threshold": 0.64, "morpheme_threshold": 0.64, "units": 78,
    "weight": {"definition": "...", "reading": "...", "length_bins": "...", "scorer": "..."},
    "null": {"samples_per_stratum": 256, "min_stratum": 20, "seed": 1, "strata": 22, "null_pairs": 2360},
    "calibration": {"pairs": 150, "by_null_scope": {"stratum": 146, "lect_pair": 4, "sparse": 0, "none": 0},
                    "mean_weight": 0.459967, "weight_ge_0_5": 76, "weight_ge_0_95": 47},
    "note": "..."
  }
}
```

`form_obs` ids are the input's `ID` in word mode and `ID#k` in morpheme mode,
which is how regulae names morpheme nodes. The producer pins both cognator and
the merkmal commit, since the distances are on merkmal's scale.
`schema_version` versions this document's shape, not the framework's.
`calibration` is a summary of the weights, not a calibration against gold.

## Morpheme clusters (`--morpheme-ids`)

`--morpheme-ids` (morpheme mode) also runs the existing graph policy and
Infomap per concept on the morpheme nodes, with the morpheme distances, and
adds a trailing `Cognator_Morpheme_IDs` column to the sidecar: one id per
morpheme, space-separated, `<family>:<concept>:m<module>`. A morpheme whose
module holds no morpheme of another lect is in no cross-lect set and gets
`-`. This is regulae's `morpheme_cognates` shape. The threshold is the fixed
`--threshold` when one is given, otherwise Otsu over the morpheme distances
clamped to `--threshold-min/max`; it is reported as `morpheme_threshold`.

Like `Cognator_ID`, these are a convenience partition, `q(partition)` and not
evidence. Two morphemes of one word are never linked by an edge, but the
clustering can still place them in one module through a third morpheme; that
is not prevented.

## Cost

The null roughly multiplies the alignments by `strata x null_samples /
pairs`. Segment scores are memoized for the export (the same doubles, looked
up rather than recomputed), so on the test fixture the morpheme export costs
about 0.1 s on top of a 0.3 s run. Lower `--null-samples` for large corpora;
the weights' resolution drops with it.
