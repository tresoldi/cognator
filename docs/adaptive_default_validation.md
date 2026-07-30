# Adaptive Default Validation

This report records the current wider-sample evidence used to validate the C/Python adaptive default against LingPy.

## Evidence

- Comparison source: `benchmark/comparison.tsv`.
- Parameter-grid source: `benchmark/milestone24/parameter_summary.tsv`.
- Cognator is evaluated through the C/Python path; LingPy is the external quality target.
- The committed comparison artifact is the authoritative data for this report; rerun the generator after producing fresh benchmark TSVs.

## Aggregate

- Runnable paired datasets: `48`.
- Mean Cognator F1: `0.8117`.
- Mean LingPy F1: `0.8615`.
- Mean delta F1: `-0.0498`.
- Win/loss/tie count: `6/42/0`.
- Mean runtime speedup where both times are nonzero: `58.8x`.

## Threshold Findings

- `0.64`: mean F1 `0.8672`, best count `3`, mean rank `15.79`.
- `0.66`: mean F1 `0.8666`, best count `0`, mean rank `18.36`.
- `0.68`: mean F1 `0.8675`, best count `2`, mean rank `16.21`.
- `0.70`: mean F1 `0.8657`, best count `0`, mean rank `18.50`.
- `0.72`: mean F1 `0.8641`, best count `1`, mean rank `18.50`.
- `0.74`: mean F1 `0.8626`, best count `1`, mean rank `18.07`.

Interpretation: the best Cognator settings in the grid concentrate in the lower threshold band, which is why the adaptive preset clamps family-level Otsu thresholds to `0.64..0.74` and leaves concept-local shrinkage as an explicit experiment knob.

## Strongest Wins

| Dataset | Forms | Cognator F1 | LingPy F1 | Delta F1 |
|---|---:|---:|---:|---:|
| gaotb | 5085 | 0.6493 | 0.6307 | +0.0186 |
| crossandean | 7518 | 0.9287 | 0.9108 | +0.0179 |
| chaconnorthwestarawakan | 2390 | 0.7664 | 0.7566 | +0.0098 |
| wichmannmixezoquean | 1106 | 0.9097 | 0.9004 | +0.0093 |
| syrjaenenuralic | 1401 | 0.8974 | 0.8923 | +0.0051 |
| mcelhanonhuon | 1960 | 0.8804 | 0.8796 | +0.0008 |
| dunnaslian | 4560 | 0.8813 | 0.8852 | -0.0039 |
| chaconarawakan | 711 | 0.7728 | 0.7783 | -0.0055 |
| walworthpolynesian | 7518 | 0.8848 | 0.8906 | -0.0058 |
| davletshinaztecan | 854 | 0.9121 | 0.9220 | -0.0099 |

## Largest Regressions

| Dataset | Forms | Cognator F1 | LingPy F1 | Delta F1 |
|---|---:|---:|---:|---:|
| lundgrenomagoa | 3289 | 0.7232 | 0.9666 | -0.2434 |
| luangthongkumkaren | 3233 | 0.7812 | 0.9582 | -0.1770 |
| lionnetyotonahua | 2870 | 0.7836 | 0.9592 | -0.1756 |
| blustaustronesian | 4358 | 0.7070 | 0.8293 | -0.1223 |
| constenlachibchan | 2631 | 0.6416 | 0.7637 | -0.1221 |
| kesslersignificance | 1600 | 0.7931 | 0.9133 | -0.1202 |
| sagartst | 12179 | 0.6294 | 0.7371 | -0.1077 |
| kahd | 5935 | 0.8175 | 0.9178 | -0.1003 |
| chaconcolumbian | 9030 | 0.6637 | 0.7523 | -0.0886 |
| leekoreanic | 2365 | 0.8391 | 0.9257 | -0.0866 |

## Conclusion

The adaptive C/Python default is fast and competitive on several families, but it is not yet strong enough to claim consistent quality superiority over LingPy; the next modeling work should target the largest false-merge and false-split regressions without sacrificing the runtime advantage.
