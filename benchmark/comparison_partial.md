# Cognator vs. lingpy: head-to-head on arcaverborum CoreCog

Both systems scored under their natural defaults (cognator: framework-native prior+learned pipeline, Otsu-selected per-family threshold, 3 EM iterations; lingpy: LexStat scorer with runs=1000 permutation, SCA-classes, fixed threshold=0.55, Infomap).

Datasets where both succeeded: **17**.

| Metric | Cognator | Lingpy | Δ (cognator − lingpy) |
|---|---|---|---|
| Mean F | **0.7908** | 0.8524 | -0.0615 |
| Mean Precision | 0.8913 | 0.8859 | +0.0054 |
| Mean Recall | 0.7211 | 0.8338 | -0.1127 |
| Mean time | 734ms | 35.7s | 48.6x faster |

Cognator wins on **1**, lingpy wins on **16**, ties on **0**.

## Per-dataset breakdown (sorted by Δ; positive = cognator better)

| Dataset | Lects | Forms | Cognator F | Lingpy F | **Δ F** | Cognator P/R | Lingpy P/R | Time cog → lpy |
|---|---|---|---|---|---|---|---|---|
| crossandean | 50 | 7518 | 0.917 | 0.911 | **+0.007** = | 0.894/0.942 | 0.848/0.983 | 2.0s → 1m36s |
| dunnaslian | 32 | 4560 | 0.858 | 0.885 | **-0.027** ↓ | 0.929/0.798 | 0.861/0.910 | 603ms → 33.9s |
| bodtkhobwa | 8 | 4720 | 0.830 | 0.862 | **-0.031** ↓ | 0.861/0.801 | 0.790/0.948 | 1.4s → 7.5s |
| gaotb | 51 | 5085 | 0.595 | 0.631 | **-0.036** ↓ | 0.732/0.501 | 0.896/0.487 | 911ms → 1m39s |
| felekesemitic | 21 | 3120 | 0.863 | 0.907 | **-0.044** ↓ | 0.939/0.799 | 0.904/0.909 | 736ms → 22.0s |
| dravlex | 20 | 2127 | 0.788 | 0.834 | **-0.046** ↓ | 0.871/0.719 | 0.885/0.788 | 233ms → 17.2s |
| constenlachibchan | 25 | 2631 | 0.716 | 0.764 | **-0.048** ↓ | 0.743/0.691 | 0.847/0.696 | 278ms → 22.2s |
| deepadungpalaung | 16 | 1584 | 0.846 | 0.900 | **-0.054** ↓ | 0.919/0.783 | 0.856/0.949 | 95ms → 9.3s |
| chaconcolumbian | 69 | 9030 | 0.698 | 0.752 | **-0.055** ↓ | 0.835/0.599 | 0.849/0.675 | 4.5s → 3m20s |
| chaconbaniwa | 14 | 2354 | 0.834 | 0.894 | **-0.061** ↓ | 0.963/0.735 | 0.898/0.891 | 341ms → 14.9s |
| davletshinaztecan | 9 | 854 | 0.861 | 0.922 | **-0.061** ↓ | 0.963/0.779 | 0.902/0.943 | 40ms → 5.3s |
| chaconnorthwestarawakan | 26 | 2390 | 0.690 | 0.757 | **-0.066** ↓ | 0.902/0.559 | 0.825/0.699 | 414ms → 32.1s |
| blustaustronesian | 20 | 4358 | 0.756 | 0.829 | **-0.073** ↓ | 0.686/0.842 | 0.854/0.806 | 703ms → 23.0s |
| hattorijaponic | 10 | 1986 | 0.840 | 0.916 | **-0.076** ↓ | 0.982/0.734 | 0.915/0.918 | 101ms → 7.2s |
| chaconarawakan | 8 | 711 | 0.702 | 0.778 | **-0.077** ↓ | 0.965/0.551 | 0.941/0.663 | 46ms → 4.3s |
| carvalhopurus | 4 | 731 | 0.856 | 0.973 | **-0.117** ↓ | 0.968/0.767 | 0.991/0.955 | 85ms → 3.0s |
| chacontukanoan | 16 | 1542 | 0.794 | 0.975 | **-0.181** ↓ | 0.999/0.659 | 0.998/0.954 | 105ms → 7.7s |

## Datasets not completed on one side

| Dataset | Lects | Forms | Cognator | Lingpy |
|---|---|---|---|---|
| cals | 88 | 15826 | F=0.905 | skipped: 15826 forms exceeds max-forms=15000 |
| syrjaenenuralic | 7 | 1401 | F=0.897 | F=0.000 |
| mcelhanonhuon | 14 | 1960 | F=0.895 | F=0.000 |
| savelyevturkic | 32 | 8360 | F=0.884 | F=0.000 |
| kesslersignificance | 8 | 1600 | F=0.883 | F=0.000 |
| starostintujia | 5 | 513 | F=0.869 | F=0.000 |
| ratcliffearabic | 14 | 1340 | F=0.866 | F=0.000 |
| nagarajakhasian | 6 | 1214 | F=0.864 | F=0.000 |
| kahd | 18 | 5935 | F=0.850 | F=0.000 |
| wichmannmixezoquean | 10 | 1106 | F=0.850 | F=0.000 |
| walworthpolynesian | 31 | 7518 | F=0.845 | F=0.000 |
| zhivlovobugrian | 21 | 2055 | F=0.832 | F=0.000 |
| mannburmish | 7 | 2076 | F=0.831 | F=0.000 |
| leekoreanic | 15 | 2365 | F=0.826 | F=0.000 |
| sidwellbahnaric | 24 | 4546 | F=0.819 | F=0.000 |
| robinsonap | 13 | 4841 | F=0.796 | F=0.000 |
| oskolskayatungusic | 21 | 6095 | F=0.795 | F=0.000 |
| lundgrenomagoa | 3 | 3289 | F=0.790 | F=0.000 |
| sidwellvietic | 33 | 3217 | F=0.771 | F=0.000 |
| houchinese | 15 | 2789 | F=0.767 | F=0.000 |
| lieberherrkhobwa | 22 | 2144 | F=0.756 | F=0.000 |
| pharaocoracholaztecan | 9 | 799 | F=0.751 | F=0.000 |
| liusinitic | 19 | 4302 | F=0.750 | F=0.000 |
| gravinachadic | 48 | 16663 | F=0.725 | skipped: 16663 forms exceeds max-forms=15000 |
| saenkoromance | 43 | 4853 | F=0.721 | F=0.000 |
| luangthongkumkaren | 11 | 3233 | F=0.720 | F=0.000 |
| lionnetyotonahua | 15 | 2870 | F=0.697 | F=0.000 |
| iecor | 121 | 18801 | F=0.689 | F=0.000 |
| sagartst | 50 | 12179 | F=0.683 | F=0.000 |
| simsrma | 11 | 1055 | F=0.656 | F=0.000 |
| utoaztecan | 46 | 5813 | F=0.643 | F=0.000 |
| peirosaustroasiatic | 109 | 10706 | F=0.626 | F=0.000 |
| yanglalo | 8 | 7798 | F=0.603 | F=0.000 |
| zhangrgyalrong | 7 | 603 | F=0.563 | F=0.000 |
| bowernpny | 190 | 44876 | F=0.423 | skipped: 44876 forms exceeds max-forms=15000 |
| bdpa | 538 | 50095 | skipped: 538 lects exceeds max-lects=250 (memory guard) | skipped: 538 lects exceeds max-lects=250 |
| tuled | 89 | 25004 | skipped: 25004 forms exceeds max-forms=15000 (memory guard) | F=0.000 |
| smithborneo | 95 | 50997 | skipped: 50997 forms exceeds max-forms=15000 (memory guard) | F=0.000 |
| mixtecansubgrouping | 110 | 15932 | skipped: 15932 forms exceeds max-forms=15000 (memory guard) | F=0.000 |
| mcd | 60 | 20689 | skipped: 20689 forms exceeds max-forms=15000 (memory guard) | F=0.000 |
