# Milestone 24 LingPy Quality Benchmark

Run date: 2026-07-30

Goal: exercise the C core through the Python wrapper and compare it with LingPy best-of-grid settings on temporary Lexibank data.

## Data

- carvalhopurus: https://github.com/lexibank/carvalhopurus.git (731 forms, 4 lects, 199 concepts)
- chaconarawakan: https://github.com/lexibank/chaconarawakan.git (711 forms, 8 lects, 100 concepts)
- davletshinaztecan: https://github.com/lexibank/davletshinaztecan.git (854 forms, 9 lects, 100 concepts)
- mcelhanonhuon: https://github.com/lexibank/mcelhanonhuon.git (1960 forms, 14 lects, 140 concepts)
- starostintujia: https://github.com/lexibank/starostintujia.git (513 forms, 5 lects, 109 concepts)
- syrjaenenuralic: https://github.com/lexibank/syrjaenenuralic.git (1401 forms, 7 lects, 173 concepts)
- wichmannmixezoquean: https://github.com/lexibank/wichmannmixezoquean.git (1106 forms, 10 lects, 110 concepts)

## Best Results

- carvalhopurus: Cognator 0.9940 (support-adaptive-bridge, threshold=0.72, min_pairs=1) vs LingPy 0.9878 (lexstat, infomap, threshold=0.75), delta +0.0062.
- chaconarawakan: Cognator 0.8048 (support-adaptive-bridge, threshold=0.74, min_pairs=1) vs LingPy 0.8487 (lexstat, infomap, threshold=0.70), delta -0.0440.
- davletshinaztecan: Cognator 0.9158 (support-adaptive-bridge, threshold=0.64, min_pairs=1) vs LingPy 0.9314 (lexstat, upgma, threshold=0.75), delta -0.0156.
- mcelhanonhuon: Cognator 0.8049 (support-adaptive-bridge, threshold=0.64, min_pairs=1) vs LingPy 0.8930 (lexstat, upgma, threshold=0.55), delta -0.0882.
- starostintujia: Cognator 0.9238 (support-adaptive-bridge, threshold=0.68, min_pairs=1) vs LingPy 0.9401 (lexstat, upgma, threshold=0.70), delta -0.0163.
- syrjaenenuralic: Cognator 0.7651 (support-adaptive-bridge, threshold=0.64, min_pairs=1) vs LingPy 0.8960 (sca, upgma, threshold=0.45), delta -0.1308.
- wichmannmixezoquean: Cognator 0.8911 (support-adaptive-bridge, threshold=0.68, min_pairs=1) vs LingPy 0.9040 (lexstat, infomap, threshold=0.55), delta -0.0129.

## Aggregate

- Cognator wins 1/7 runnable datasets; mean best-of-grid delta is -0.0431 F1.
- This wider sample does not yet justify claiming broad quality superiority over LingPy; it identifies the next modeling work needed before Go retirement.

## Parameter Findings

- Cognator best thresholds by dataset: 0.64=3, 0.68=2, 0.72=1, 0.74=1.
- Cognator's stronger settings are concentrated at lower thresholds, so the next tuning work should prefer adaptive thresholding over raising the global cutoff.
- Cognator `min_pairs` best counts: 1=7; `min_pairs=5` did not produce a best-of-dataset quality win in this sample.
- LingPy best methods by dataset: lexstat=6, sca=1.
- LingPy best cluster methods by dataset: upgma=4, infomap=3.
- LingPy LexStat remains the main quality target; SCA only wins on `syrjaenenuralic`, where conservative UPGMA at threshold 0.45 is much stronger than Cognator's current graph setting.

## Notes

- Cognator is run through `python/cognator` so the result covers the installable wrapper path over the C implementation.
- LingPy is run with LexStat and SCA over infomap and UPGMA threshold grids; LexStat uses the configured permutation count.
- The Lexibank checkout lives under the temporary work directory and is intentionally not committed.
