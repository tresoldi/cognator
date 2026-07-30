# Milestone 24 LingPy Quality Benchmark

Run date: 2026-07-30

Goal: exercise the C core through the Python wrapper and compare it with LingPy best-of-grid settings on temporary Lexibank data.

## Data

- carvalhopurus: https://github.com/lexibank/carvalhopurus.git (731 forms, 4 lects, 199 concepts)

## Best Results

- carvalhopurus: Cognator 0.9940 (support-adaptive-bridge, threshold=0.72, min_pairs=1) vs LingPy 0.9878 (lexstat, infomap, threshold=0.75), delta +0.0062.

## Notes

- Cognator is run through `python/cognator` so the result covers the installable wrapper path over the C implementation.
- LingPy is run with LexStat and SCA over infomap and UPGMA threshold grids; LexStat uses the configured permutation count.
- The Lexibank checkout lives under the temporary work directory and is intentionally not committed.
