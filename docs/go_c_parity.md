# Go/C Parity

Milestones 12 and 14 check the C rewrite against the previous Go pipeline on
frozen linguistic fixtures.

The comparison is cluster-equivalent rather than string-equivalent: sidecar
cluster IDs may be named differently, so `scripts/compare_sidecars.py` compares
within-concept pair membership and independently reports B-cubed precision,
recall, and F1 for each sidecar.

## Fixtures

| Fixture | Rows | Purpose |
|---|---:|---|
| `tests/fixtures/linguistic/latin_romance_triplets.tsv` | 12 | Latin/Romance regular correspondences plus a fire split |
| `tests/fixtures/linguistic/walworth_polynesian_smoke.tsv` | 16 | Polynesian-style regular near-identical cognates |
| `tests/fixtures/linguistic/latin_romance_extended.tsv` | 24 | Wider Romance reductions and split/merge pressure |

## Commands

The local C branch uses the merkmal C rework at `../merkmal`, while the Go
module needs a Go-compatible merkmal checkout. Keep that checkout separate and
pass its `go/` module path to CMake:

```sh
cmake -S . -B build-go-parity \
  -DCOGNATOR_BUILD_TESTS=ON \
  -DCOGNATOR_BUILD_CLI=ON \
  -DCOGNATOR_ENABLE_GO_C_PARITY=ON \
  -DCOGNATOR_GO_MERKMAL_DIR=/tmp/cognator-merkmal-go/go
cmake --build build-go-parity -j2
ctest --test-dir build-go-parity -R go_c_parity --output-on-failure
```

The matched detect flags are:

```sh
--preset support-adaptive-bridge --threshold 0.70 --bootstrap 0 --scorer posterior
```

`--min-pairs` is intentionally omitted from parity because the old Go CLI has no
flag for it; C-only validation can still use it.

## Current Workspace Result

On 2026-07-30, the Go-compatible merkmal source was taken from
`../merkmal origin/master` at commit `e82d55899249412bc77ac72acfa8d30919d54b6e`
in an isolated worktree, while the C CLI used the checked-out C merkmal rework.

| Fixture | Seed | Pair agreement | Go F1 | C F1 | F1 delta | Decision |
|---|---:|---:|---:|---:|---:|---|
| `latin_romance_triplets.tsv` | 17 | 0.833333 | 1.000000 | 0.941176 | 0.058824 | C default sparse-prior regression: C merges Latin `igne` with Romance `fuoko`/`fwego`; `--min-pairs 1` removes it. |
| `walworth_polynesian_smoke.tsv` | 17 | 0.833333 | 0.933333 | 1.000000 | 0.066667 | Dead Go behavior: Go splits FISH into Hawaiian/Samoan vs Tongan/Maori, while C matches truth. |
| `latin_romance_extended.tsv` | 31 | 0.916667 | 0.900763 | 0.937269 | 0.036506 | Accepted C rework improvement: C keeps EIGHT merged where Go splits French `wit`; both keep precision at 1.0. |

The C-only `--min-pairs 1` check for `latin_romance_triplets.tsv` matches Go
exactly:

```text
pair_agreement	1.000000
go	precision=1.000000	recall=1.000000	f1=1.000000
c_min_pairs_1	precision=1.000000	recall=1.000000	f1=1.000000
f1_delta	0.000000
```

## Closure

There is no unexplained Go/C parity gap left on the frozen fixtures. The only C
regression is the small Romance FIRE false merge under the shared old-Go flag
surface; it is documented as a C default/preset tuning bug, not a reason to keep
the Go implementation alive. The other differences favor the C rework and are
classified as dead Go behavior or accepted model changes.
