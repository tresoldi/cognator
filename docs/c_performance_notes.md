# C performance and memory notes

Milestone 11 tightened the first C detect implementation without changing its
serial execution model.

Changes made:

- `cg_detect` now grows token sequences and temporary form-pair lists
  geometrically instead of reallocating on every append.
- Detection result rows are preallocated once and written at their original
  record index, removing the previous post-hoc O(n^2) row reorder pass.
- Concepts are grouped once before clustering. Per-concept pair extraction uses
  a temporary global-index-to-local-index map instead of scanning the concept's
  index list for every distance pair.
- `cognator detect` has explicit `--max-forms` and `--max-lects` guards for
  ad-hoc large inputs. `cognator bench` keeps its per-family guards.
- Benchmark entry points also have a same-concept cross-lect pair guard:
  `--max-pairs` defaults to `3000000`, can be set to `0` to disable it, and is
  emitted as `estimated_pairs` in result TSVs.

Native parallelism is intentionally still deferred. The current C path remains
serial so that posterior training, bootstrap, and result ordering stay
deterministic while the API is stabilizing. Parallelism should be added only
with a seed-derivation and deterministic-folding design that can be tested
independently of scheduler order.
