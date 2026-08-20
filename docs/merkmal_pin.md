# Moving the merkmal pin

cognator builds against one merkmal, committed under `third_party/merkmal` and
named in `third_party/merkmal/PINNED`. This note is about what it costs to move
it, because the cost is not obvious and it is not a version bump.

## Why there is a pin at all

cognator turns a pairwise segment distance into cognate clusters, and every
constant that does the turning is a number on merkmal's distance scale:

| constant | value | where |
| --- | ---: | --- |
| default threshold | `0.68` | `src/options.c` |
| adaptive clamp | `0.64‥0.74` | `src/options.c` |
| `--graph-adaptive-margin` | `0.1` | `support-adaptive-bridge` preset |
| `--graph-adaptive-either-max` | `0.15` | same |
| `--refine-confidence-threshold` | `0.55` | same |

None of those is a property of the algorithm. Each was fitted, on the Dev-Core
and Dev-Full splits, to the distances a particular merkmal returns. merkmal's
descriptive system is free to change its feature inventory between releases —
that is what it is for — and when it does, the scale those constants sit on
moves under them.

## What moving it actually did, once

Between the pinned `3d21e08` (2026-07-26) and merkmal at 2026-08-20, the
descriptive system went from four or five articulatory labels per grapheme to
eight to ten, adding SPE-style features:

```
merkmal 3d21e08   g : consonant stop velar voiced
merkmal HEAD      g : consonant consonantal dorsal non-continuant obstruent stop velar voiced
```

Distances contracted accordingly:

| pair | `3d21e08` | HEAD |
| --- | ---: | ---: |
| d(e, o) | 0.6667 | 0.2556 |
| d(i, w) | 0.6786 | 0.3872 |
| d(n, g) | 0.7692 | 0.5000 |
| d(k, g) | 0.3750 | 0.1190 |

On `tests/fixtures/linguistic/latin_romance_triplets.tsv` that is the
difference between a clean run and a wrong one: Latin `i g n e` joins Italian
`f u o k o` and Spanish `f w e g o` in one cluster, which is *ignis* merged
with *focus*. B-cubed precision falls from 1.0000 to 0.8889. It merges at every
threshold from 0.50 to 0.90, so it is the graph expansion policy responding to
a compressed scale, not a threshold anyone can nudge.

`tests/c/test_linguistic_validation.cmake` freezes exact figures — recall
`0.8819`, F1 `0.9373`, a whole `bench` row — which come from the same scale and
move with it.

## The API also moves, and that part is easy

Worth separating, because it looks like the hard part and is not. Going to a
merkmal newer than `3d21e08` needs, at least:

- `mk_feature_set` → `mk_string_list`, and `mk_feature_set_{size,get,free}` →
  `mk_string_list_{size,get,free}` (`src/context.c`)
- `mk_free_string` → `mk_string_free` (`src/segment.c`)
- `mk_system_is_segment`'s third argument is `bool *`, not `int *`

Two behavioural repairs go with it, and both are worth keeping in mind because
neither is a rename:

- **Glides.** Current merkmal reports `sonorant` on `/j/`, which cognator's
  `role_from_features` reads as a liquid, so glides land in `CG_ROLE_R`. The
  feature that separates a glide from a liquid is `vocoid`: `/j/` and `/w/`
  carry it, `/l/` does not. Test with `j w l r m n p s ʔ ɲ`, which map
  identically under both merkmals once the rule is `approximant ∧ vocoid`.
- **Tone.** Current merkmal recognizes tone letters and Chao digit runs and
  returns `tonal-autosegment` / `tone-present` for them, where `3d21e08`
  returned `MK_ERR_UNKNOWN_GRAPHEME` and cognator fell back to an ASCII digit
  heuristic. `role_from_features` should read the feature, before the vowel
  test, since a tonal autosegment carries neither vowel nor consonant features
  and otherwise falls out as `CG_ROLE_UNKNOWN`.

## What moving the pin requires

Recalibration, in the order the promotion ladder in `docs/roadmap.md` sets out.
Not a threshold fitted to the two linguistic fixtures — those are twelve and
twenty-four forms, and tuning on them is how a family-specific win gets
mistaken for a general one.

1. Update `third_party/merkmal` from upstream, set `PINNED`'s `commit`, `date`
   and `version`, and run `./scripts/merkmal-provenance.sh --write`.
2. Apply the API changes above; `./scripts/check.sh` builds and runs the suite.
3. Refit the threshold and the adaptive clamp on Dev-Core. Confirm on Dev-Full.
4. Refit the graph policy constants only if the threshold alone does not
   recover the merge behaviour — the FIRE case above is a graph-policy failure,
   not a threshold one.
5. Check holdout once, after promotion, and do not tune on it.
6. Re-derive the frozen figures in `tests/c/test_linguistic_validation.cmake`
   and the tables in `README.md` and `docs/roadmap.md` from the new runs, in
   the same commit that moves the pin.

To do the experimental work without disturbing the committed build, point the
build at a checkout instead:

```sh
cmake -S . -B build/experiment -DCOGNATOR_MERKMAL_SOURCE_DIR=../merkmal
COGNATOR_MERKMAL_SOURCE_DIR=../merkmal python -m pip install -e .
```

Nothing this repository reports holds under that build. That is the point of
it.
