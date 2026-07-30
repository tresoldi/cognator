# C Linguistic Validation

Milestone 13 records the C rewrite's linguistic checks beyond unit coverage.

## Local Fixtures

All commands used the C CLI with:

```sh
--preset support-adaptive-bridge --threshold 0.70 --bootstrap 2 \
--min-pairs 1
```

| Fixture | Seed | Rows | Lects | Concepts | Precision | Recall | F1 | Notes |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| `walworth_polynesian_smoke.tsv` | 17 | 16 | 4 | 4 | 1.0000 | 1.0000 | 1.0000 | Regular Polynesian-style cognates stay merged. |
| `latin_romance_triplets.tsv` | 17 | 12 | 3 | 4 | 1.0000 | 1.0000 | 1.0000 | Core Latin/Romance triplets and the fire split are preserved. |
| `latin_romance_extended.tsv` | 31 | 24 | 4 | 6 | 1.0000 | 0.8819 | 0.9373 | Richer reductions create conservative false splits, with no false merges. |

The extended Romance fixture is also covered by the `linguistic_validation`
CTest. Its per-family bench row is:

```text
romance	4	6	24	24	0	0.700000	10	1.000000	0.881944	0.937269
```

## Real Walworth CoreCog

The intended larger Walworth CoreCog source was not available in this workspace
on 2026-07-30:

```sh
/tmp/arcaverborum_corecog/arcaverborum-A-corecog-20251008/forms.csv
```

When that file is restored, rerun the C benchmark with a constrained family
selection:

```sh
./build/cognator bench \
  --in /tmp/arcaverborum_corecog/arcaverborum-A-corecog-20251008/forms.csv \
  --out-tsv benchmark/c_walworth_polynesian.tsv \
  --families walworthpolynesian \
  --preset support-adaptive-bridge \
  --threshold 0.70 \
  --bootstrap 2 \
  --min-pairs 1 \
  --seed 31
```

That run should be recorded here with the exact row count, OOV count, B-cubed
scores, and any visually obvious false splits or false merges.
