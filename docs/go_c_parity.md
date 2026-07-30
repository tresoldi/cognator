# Go/C Parity

Milestone 12 checks the C rewrite against the previous Go pipeline on frozen
linguistic fixtures.

The comparison is cluster-equivalent rather than string-equivalent: sidecar
cluster IDs may be named differently, so `scripts/compare_sidecars.py` compares
within-concept pair membership and independently reports B-cubed precision,
recall, and F1 for each sidecar.

## Fixtures

| Fixture | Rows | Purpose |
|---|---:|---|
| `tests/fixtures/linguistic/latin_romance_triplets.tsv` | 12 | Latin/Romance regular correspondences plus a fire split |
| `tests/fixtures/linguistic/walworth_polynesian_smoke.tsv` | 16 | Polynesian-style regular near-identical cognates |

## Commands

The local C branch uses the merkmal C rework at `../merkmal`, while the Go
module still expects `../merkmal/go` from the Go-compatible merkmal branch.
Enable this test only in a workspace where that Go checkout is present:

```sh
cmake -S . -B build-go-parity \
  -DCOGNATOR_BUILD_TESTS=ON \
  -DCOGNATOR_BUILD_CLI=ON \
  -DCOGNATOR_ENABLE_GO_C_PARITY=ON
cmake --build build-go-parity -j2
ctest --test-dir build-go-parity -R go_c_parity --output-on-failure
```

The matched detect flags are:

```sh
--preset support-adaptive-bridge --threshold 0.70 --bootstrap 0 \
--min-pairs 1 --scorer posterior --seed 17
```

## Current Workspace Result

On 2026-07-30, the C side alone produces perfect B-cubed scores on both frozen
fixtures:

| Fixture | Rows | Precision | Recall | F1 |
|---|---:|---:|---:|---:|
| `latin_romance_triplets.tsv` | 12 | 1.0000 | 1.0000 | 1.0000 |
| `walworth_polynesian_smoke.tsv` | 16 | 1.0000 | 1.0000 | 1.0000 |

The Go side could not be run in this checkout because `go run ./cmd/cognator`
fails while resolving `github.com/tresoldi/merkmal/go => ../merkmal/go`; the
current `../merkmal` checkout is the C rework and has no `go/` module.

## Remaining Parity Gap

The milestone harness is in place, but the parity result must be refreshed in a
workspace that contains the old Go-compatible merkmal checkout. Any mismatch
should be reviewed first as pairwise cluster disagreement, then as a B-cubed
delta, because cluster ID labels are intentionally ignored.
