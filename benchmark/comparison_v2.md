# Cognator vs. lingpy: head-to-head on arcaverborum CoreCog

Both systems scored under their natural defaults (cognator: framework-native prior+learned pipeline, Otsu-selected per-family threshold, 3 EM iterations; lingpy: LexStat scorer with runs=1000 permutation, SCA-classes, fixed threshold=0.55, Infomap).

Datasets where both succeeded: **48**.

| Metric | Cognator | Lingpy | Δ (cognator − lingpy) |
|---|---|---|---|
| Mean F | **0.8117** | 0.8615 | -0.0498 |
| Mean Precision | 0.8477 | 0.8980 | -0.0503 |
| Mean Recall | 0.7966 | 0.8383 | -0.0418 |
| Mean time | 3.4s | 34.8s | 10.3x faster |

Cognator wins on **6**, lingpy wins on **42**, ties on **0**.

## Per-dataset breakdown (sorted by Δ; positive = cognator better)

| Dataset | Lects | Forms | Cognator F | Lingpy F | **Δ F** | Cognator P/R | Lingpy P/R | Time cog → lpy |
|---|---|---|---|---|---|---|---|---|
| gaotb | 51 | 5085 | 0.649 | 0.631 | **+0.019** = | 0.663/0.636 | 0.896/0.487 | 1.2s → 1m39s |
| crossandean | 50 | 7518 | 0.929 | 0.911 | **+0.018** = | 0.889/0.972 | 0.848/0.983 | 2.6s → 1m36s |
| chaconnorthwestarawakan | 26 | 2390 | 0.766 | 0.757 | **+0.010** = | 0.873/0.683 | 0.825/0.699 | 584ms → 32.1s |
| wichmannmixezoquean | 10 | 1106 | 0.910 | 0.900 | **+0.009** = | 0.939/0.883 | 0.886/0.915 | 62ms → 6.0s |
| syrjaenenuralic | 7 | 1401 | 0.897 | 0.892 | **+0.005** = | 0.904/0.891 | 0.911/0.874 | 82ms → 5.3s |
| mcelhanonhuon | 14 | 1960 | 0.880 | 0.880 | **+0.001** = | 0.852/0.911 | 0.833/0.932 | 164ms → 9.5s |
| dunnaslian | 32 | 4560 | 0.881 | 0.885 | **-0.004** = | 0.910/0.855 | 0.861/0.910 | 812ms → 33.9s |
| chaconarawakan | 8 | 711 | 0.773 | 0.778 | **-0.005** = | 0.910/0.671 | 0.941/0.663 | 37ms → 4.3s |
| walworthpolynesian | 31 | 7518 | 0.885 | 0.891 | **-0.006** = | 0.901/0.869 | 0.877/0.905 | 1.8s → 52.8s |
| davletshinaztecan | 9 | 854 | 0.912 | 0.922 | **-0.010** = | 0.954/0.874 | 0.902/0.943 | 52ms → 5.3s |
| nagarajakhasian | 6 | 1214 | 0.881 | 0.892 | **-0.011** = | 0.927/0.839 | 0.920/0.866 | 37ms → 3.7s |
| savelyevturkic | 32 | 8360 | 0.908 | 0.921 | **-0.013** = | 0.946/0.874 | 0.904/0.940 | 1.6s → 49.4s |
| dravlex | 20 | 2127 | 0.817 | 0.834 | **-0.017** = | 0.801/0.834 | 0.885/0.788 | 308ms → 17.2s |
| starostintujia | 5 | 513 | 0.898 | 0.916 | **-0.018** = | 0.978/0.831 | 0.987/0.855 | 11ms → 3.5s |
| sidwellbahnaric | 24 | 4546 | 0.866 | 0.888 | **-0.022** ↓ | 0.879/0.853 | 0.847/0.934 | 519ms → 22.9s |
| simsrma | 11 | 1055 | 0.715 | 0.738 | **-0.023** ↓ | 0.976/0.565 | 0.999/0.585 | 44ms → 5.5s |
| chaconbaniwa | 14 | 2354 | 0.871 | 0.894 | **-0.023** ↓ | 0.960/0.797 | 0.898/0.891 | 348ms → 14.9s |
| yanglalo | 8 | 7798 | 0.725 | 0.750 | **-0.024** ↓ | 0.877/0.619 | 0.962/0.614 | 18.0s → 14.7s |
| hattorijaponic | 10 | 1986 | 0.891 | 0.916 | **-0.025** ↓ | 0.969/0.825 | 0.915/0.918 | 129ms → 7.2s |
| zhangrgyalrong | 7 | 603 | 0.722 | 0.748 | **-0.026** ↓ | 0.947/0.583 | 0.982/0.604 | 56ms → 3.7s |
| lieberherrkhobwa | 22 | 2144 | 0.795 | 0.822 | **-0.026** ↓ | 0.937/0.691 | 0.901/0.754 | 229ms → 15.7s |
| felekesemitic | 21 | 3120 | 0.880 | 0.907 | **-0.027** ↓ | 0.937/0.830 | 0.904/0.909 | 997ms → 22.0s |
| bodtkhobwa | 8 | 4720 | 0.830 | 0.862 | **-0.031** ↓ | 0.796/0.868 | 0.790/0.948 | 2.2s → 7.5s |
| pharaocoracholaztecan | 9 | 799 | 0.786 | 0.818 | **-0.033** ↓ | 0.892/0.702 | 0.878/0.767 | 32ms → 4.4s |
| oskolskayatungusic | 21 | 6095 | 0.851 | 0.884 | **-0.034** ↓ | 0.949/0.771 | 0.926/0.846 | 1.1s → 28.4s |
| ratcliffearabic | 14 | 1340 | 0.936 | 0.971 | **-0.034** ↓ | 1.000/0.881 | 1.000/0.943 | 105ms → 7.4s |
| houchinese | 15 | 2789 | 0.754 | 0.789 | **-0.035** ↓ | 0.698/0.820 | 0.739/0.847 | 508ms → 21.7s |
| carvalhopurus | 4 | 731 | 0.927 | 0.973 | **-0.046** ↓ | 0.959/0.897 | 0.991/0.955 | 100ms → 3.0s |
| sidwellvietic | 33 | 3217 | 0.821 | 0.868 | **-0.047** ↓ | 0.859/0.787 | 0.853/0.884 | 408ms → 35.5s |
| chacontukanoan | 16 | 1542 | 0.925 | 0.975 | **-0.050** ↓ | 0.990/0.868 | 0.998/0.954 | 155ms → 7.7s |
| zhivlovobugrian | 21 | 2055 | 0.869 | 0.920 | **-0.051** ↓ | 0.980/0.780 | 0.939/0.901 | 191ms → 15.2s |
| deepadungpalaung | 16 | 1584 | 0.846 | 0.900 | **-0.055** ↓ | 0.944/0.766 | 0.856/0.949 | 111ms → 9.3s |
| saenkoromance | 43 | 4853 | 0.860 | 0.921 | **-0.060** ↓ | 0.951/0.786 | 0.919/0.923 | 1.5s → 1m07s |
| peirosaustroasiatic | 109 | 10706 | 0.634 | 0.696 | **-0.062** ↓ | 0.580/0.701 | 0.685/0.707 | 7.3s → 6m23s |
| mannburmish | 7 | 2076 | 0.824 | 0.892 | **-0.068** ↓ | 0.962/0.721 | 0.949/0.842 | 60ms → 5.5s |
| liusinitic | 19 | 4302 | 0.822 | 0.893 | **-0.072** ↓ | 0.924/0.740 | 0.908/0.880 | 400ms → 22.1s |
| utoaztecan | 46 | 5813 | 0.668 | 0.741 | **-0.073** ↓ | 0.676/0.660 | 0.810/0.682 | 2.1s → 1m25s |
| robinsonap | 13 | 4841 | 0.775 | 0.851 | **-0.076** ↓ | 0.677/0.905 | 0.855/0.847 | 501ms → 11.9s |
| leekoreanic | 15 | 2365 | 0.839 | 0.926 | **-0.087** ↓ | 0.985/0.731 | 0.955/0.898 | 179ms → 10.4s |
| chaconcolumbian | 69 | 9030 | 0.664 | 0.752 | **-0.089** ↓ | 0.623/0.710 | 0.849/0.675 | 5.5s → 3m20s |
| kahd | 18 | 5935 | 0.818 | 0.918 | **-0.100** ↓ | 0.728/0.932 | 0.912/0.923 | 21.3s → 18.1s |
| sagartst | 50 | 12179 | 0.629 | 0.737 | **-0.108** ↓ | 0.540/0.753 | 0.859/0.646 | 3.5s → 2m10s |
| kesslersignificance | 8 | 1600 | 0.793 | 0.913 | **-0.120** ↓ | 0.686/0.939 | 0.920/0.906 | 90ms → 4.7s |
| constenlachibchan | 25 | 2631 | 0.642 | 0.764 | **-0.122** ↓ | 0.550/0.770 | 0.847/0.696 | 362ms → 22.2s |
| blustaustronesian | 20 | 4358 | 0.707 | 0.829 | **-0.122** ↓ | 0.586/0.892 | 0.854/0.806 | 666ms → 23.0s |
| lionnetyotonahua | 15 | 2870 | 0.784 | 0.959 | **-0.176** ↓ | 0.889/0.701 | 0.991/0.929 | 4.1s → 8.6s |
| luangthongkumkaren | 11 | 3233 | 0.781 | 0.958 | **-0.177** ↓ | 0.760/0.803 | 0.997/0.922 | 40.1s → 7.9s |
| lundgrenomagoa | 3 | 3289 | 0.723 | 0.967 | **-0.243** ↓ | 0.577/0.969 | 0.940/0.994 | 40.8s → 5.2s |

## Datasets not completed on one side

| Dataset | Lects | Forms | Cognator | Lingpy |
|---|---|---|---|---|
| smithborneo | 95 | 50997 | skipped: 50997 forms exceeds max-forms=15000 (memory guard) | skipped: 50997 forms exceeds max-forms=15000 |
| iecor | 121 | 18801 | skipped: 18801 forms exceeds max-forms=15000 (memory guard) | skipped: 18801 forms exceeds max-forms=15000 |
| mixtecansubgrouping | 110 | 15932 | skipped: 15932 forms exceeds max-forms=15000 (memory guard) | skipped: 15932 forms exceeds max-forms=15000 |
| tuled | 89 | 25004 | skipped: 25004 forms exceeds max-forms=15000 (memory guard) | skipped: 25004 forms exceeds max-forms=15000 |
| bdpa | 538 | 50095 | skipped: 538 lects exceeds max-lects=250 (memory guard) | skipped: 538 lects exceeds max-lects=250 |
| yamfinder | 0 | 0 | F=0.000 | skipped: no data |
| cals | 88 | 15826 | skipped: 15826 forms exceeds max-forms=15000 (memory guard) | skipped: 15826 forms exceeds max-forms=15000 |
| gravinachadic | 48 | 16663 | skipped: 16663 forms exceeds max-forms=15000 (memory guard) | skipped: 16663 forms exceeds max-forms=15000 |
| mcd | 60 | 20689 | skipped: 20689 forms exceeds max-forms=15000 (memory guard) | skipped: 17544 forms exceeds max-forms=15000 |
| bowernpny | 190 | 44876 | skipped: 44876 forms exceeds max-forms=15000 (memory guard) | skipped: 44876 forms exceeds max-forms=15000 |
