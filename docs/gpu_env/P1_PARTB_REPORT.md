# G1 Part B report (2026-11-24): policy-level parity, sampled actions

Setup per G1_PREREG.md addendum 4: both arms sample actions at temperature 1, 20,000 episodes per cell per arm, 6 cells, paired starts, Pulsar3 checkpoint ts 2175310848 vs itself and vs ring entry ts 1375461376. CPU arm = vendored RocketSim v3 (0.3.5). Pass rule: at least 6 of 7 scored metrics within +-0.15, none beyond 0.30.

Result: **PASS, all 6 cells, 7/7 scored metrics inside; worst |rel| 0.097** (1v1 vs_oldest M2, kickoff first-touch share).

Differences that are inside the band but systematic (same sign in all 6 cells):
- M7 boost spent per car-second: GPU 3.8-5.2% lower.
- M6 mean car speed: GPU 0.9-1.4% lower.
- Informational M2b (contested first touch) is higher on GPU in every cell; +39% in 3v3 vs_oldest (3.4% vs 4.7% of kickoffs).

Part B does not cancel the Part A failures (P1_PARTA_REPORT.md); it says they do not move these aggregate metrics much for this policy. A policy that uses dodges or the ceiling more could be affected more.

**G1 Part B overall: PASS**

## 1v1 self: PASS (7/7 scored inside, worst |rel| 0.042; 20000 vs 20000 episodes)

| metric | cpu | gpu | rel diff | 95% CI | |
|---|---|---|---|---|---|
| M1 goals / 1k steps | 1.187 | 1.195 | +0.007 | [-0.010, +0.024] | ok |
| M2 blue-only first-touch share (kickoffs) | 0.5084 | 0.4871 | -0.042 | [-0.083, +0.002] | ok |
| M2b contested first-touch fraction (info) | 0.05331 | 0.05381 | +0.009 | | info |
| M2c kickoff: mean steps to first touch (info) | 92.16 | 91.94 | -0.002 | | info |
| M4k kickoff episodes: mean steps (info) | 927.5 | 917.4 | -0.011 | | info |
| M3 blue touch share | 0.5023 | 0.5023 | +0.000 | [-0.005, +0.005] | ok |
| M4 mean episode steps | 833.2 | 826.1 | -0.009 | [-0.024, +0.007] | ok |
| M5 aerial touch ratio | 0.1265 | 0.1315 | +0.039 | [+0.018, +0.059] | ok |
| M6 mean car speed (uu/s) | 1283 | 1271 | -0.009 | [-0.012, -0.007] | ok |
| M7 boost spent / car-second | 5.577 | 5.367 | -0.038 | [-0.047, -0.031] | ok |

## 1v1 vs_oldest: PASS (7/7 scored inside, worst |rel| 0.097; 20000 vs 20000 episodes)

| metric | cpu | gpu | rel diff | 95% CI | |
|---|---|---|---|---|---|
| M1 goals / 1k steps | 1.365 | 1.367 | +0.002 | [-0.015, +0.019] | ok |
| M2 blue-only first-touch share (kickoffs) | 0.2188 | 0.1975 | -0.097 | [-0.173, -0.018] | ok |
| M2b contested first-touch fraction (info) | 0.04983 | 0.05129 | +0.029 | | info |
| M2c kickoff: mean steps to first touch (info) | 86.08 | 85.07 | -0.012 | | info |
| M4k kickoff episodes: mean steps (info) | 769.7 | 771.8 | +0.003 | | info |
| M3 blue touch share | 0.5434 | 0.5417 | -0.003 | [-0.009, +0.002] | ok |
| M4 mean episode steps | 728.2 | 726.9 | -0.002 | [-0.018, +0.014] | ok |
| M5 aerial touch ratio | 0.1333 | 0.1387 | +0.040 | [+0.018, +0.062] | ok |
| M6 mean car speed (uu/s) | 1314 | 1300 | -0.011 | [-0.014, -0.009] | ok |
| M7 boost spent / car-second | 5.8 | 5.528 | -0.047 | [-0.054, -0.040] | ok |

## 2v2 self: PASS (7/7 scored inside, worst |rel| 0.052; 20000 vs 20000 episodes)

| metric | cpu | gpu | rel diff | 95% CI | |
|---|---|---|---|---|---|
| M1 goals / 1k steps | 0.8938 | 0.8629 | -0.035 | [-0.053, -0.017] | ok |
| M2 blue-only first-touch share (kickoffs) | 0.5082 | 0.4987 | -0.019 | [-0.067, +0.025] | ok |
| M2b contested first-touch fraction (info) | 0.06584 | 0.06832 | +0.038 | | info |
| M2c kickoff: mean steps to first touch (info) | 90.39 | 89.49 | -0.010 | | info |
| M4k kickoff episodes: mean steps (info) | 1147 | 1169 | +0.019 | | info |
| M3 blue touch share | 0.4992 | 0.4992 | +0.000 | [-0.007, +0.006] | ok |
| M4 mean episode steps | 1082 | 1114 | +0.030 | [+0.013, +0.046] | ok |
| M5 aerial touch ratio | 0.07616 | 0.07549 | -0.009 | [-0.031, +0.013] | ok |
| M6 mean car speed (uu/s) | 1210 | 1195 | -0.013 | [-0.014, -0.011] | ok |
| M7 boost spent / car-second | 4.142 | 3.928 | -0.052 | [-0.057, -0.046] | ok |

## 2v2 vs_oldest: PASS (7/7 scored inside, worst |rel| 0.051; 20000 vs 20000 episodes)

| metric | cpu | gpu | rel diff | 95% CI | |
|---|---|---|---|---|---|
| M1 goals / 1k steps | 0.8613 | 0.8396 | -0.025 | [-0.044, -0.008] | ok |
| M2 blue-only first-touch share (kickoffs) | 0.4901 | 0.4947 | +0.009 | [-0.037, +0.049] | ok |
| M2b contested first-touch fraction (info) | 0.07968 | 0.08519 | +0.069 | | info |
| M2c kickoff: mean steps to first touch (info) | 94.04 | 93.2 | -0.009 | | info |
| M4k kickoff episodes: mean steps (info) | 1237 | 1245 | +0.007 | | info |
| M3 blue touch share | 0.553 | 0.5526 | -0.001 | [-0.007, +0.004] | ok |
| M4 mean episode steps | 1119 | 1143 | +0.021 | [+0.006, +0.038] | ok |
| M5 aerial touch ratio | 0.07153 | 0.07172 | +0.003 | [-0.021, +0.025] | ok |
| M6 mean car speed (uu/s) | 1233 | 1217 | -0.012 | [-0.014, -0.011] | ok |
| M7 boost spent / car-second | 4.126 | 3.914 | -0.051 | [-0.058, -0.047] | ok |

## 3v3 self: PASS (7/7 scored inside, worst |rel| 0.046; 20000 vs 20000 episodes)

| metric | cpu | gpu | rel diff | 95% CI | |
|---|---|---|---|---|---|
| M1 goals / 1k steps | 0.566 | 0.5596 | -0.011 | [-0.030, +0.010] | ok |
| M2 blue-only first-touch share (kickoffs) | 0.5026 | 0.5039 | +0.003 | [-0.043, +0.046] | ok |
| M2b contested first-touch fraction (info) | 0.04286 | 0.04417 | +0.031 | | info |
| M2c kickoff: mean steps to first touch (info) | 101.7 | 101.4 | -0.003 | | info |
| M4k kickoff episodes: mean steps (info) | 1608 | 1589 | -0.012 | | info |
| M3 blue touch share | 0.4992 | 0.5016 | +0.005 | [-0.001, +0.011] | ok |
| M4 mean episode steps | 1550 | 1560 | +0.006 | [-0.009, +0.019] | ok |
| M5 aerial touch ratio | 0.04024 | 0.03962 | -0.015 | [-0.040, +0.014] | ok |
| M6 mean car speed (uu/s) | 1044 | 1029 | -0.014 | [-0.016, -0.013] | ok |
| M7 boost spent / car-second | 3.052 | 2.911 | -0.046 | [-0.051, -0.041] | ok |

## 3v3 vs_oldest: PASS (7/7 scored inside, worst |rel| 0.046; 20000 vs 20000 episodes)

| metric | cpu | gpu | rel diff | 95% CI | |
|---|---|---|---|---|---|
| M1 goals / 1k steps | 0.5355 | 0.528 | -0.014 | [-0.033, +0.005] | ok |
| M2 blue-only first-touch share (kickoffs) | 0.5598 | 0.5676 | +0.014 | [-0.027, +0.053] | ok |
| M2b contested first-touch fraction (info) | 0.0338 | 0.04689 | +0.387 | | info |
| M2c kickoff: mean steps to first touch (info) | 110.2 | 109.6 | -0.005 | | info |
| M4k kickoff episodes: mean steps (info) | 1626 | 1685 | +0.036 | | info |
| M3 blue touch share | 0.5639 | 0.565 | +0.002 | [-0.003, +0.007] | ok |
| M4 mean episode steps | 1606 | 1619 | +0.008 | [-0.005, +0.023] | ok |
| M5 aerial touch ratio | 0.03701 | 0.03715 | +0.004 | [-0.020, +0.032] | ok |
| M6 mean car speed (uu/s) | 1102 | 1088 | -0.012 | [-0.014, -0.011] | ok |
| M7 boost spent / car-second | 3.123 | 2.981 | -0.046 | [-0.050, -0.042] | ok |

