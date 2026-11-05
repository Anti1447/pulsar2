# Round robin, sim 1v1 (scrimmage suite)

N = 200 games per ordered pair, seed 20261105, tickSkip 8, actionDelay 7, game = one episode (goal, 10 s without a touch, or 90 s; the last two are draws). A is blue, B is orange. Percentages carry a 95% half-width (normal approximation). Speed in uu/s and boost 0-100 are per-car averages, touches are steps in which the car touched the ball.

| A (blue) | B (orange) | A wins % | B wins % | draws % | first touch A / B / both % | mean game s | touches A / B | speed A / B | boost A / B |
|---|---|---|---|---|---|---|---|---|---|
| Nexto | Nexto | 40.0 ±6.8 | 33.5 ±6.5 | 26.5 ±6.1 | 40 / 40 / 20 | 50.5 | 227.7 / 184.6 | 865 / 877 | 56 / 56 |
| Nexto | GalactusV2 | 11.5 ±4.4 | 74.0 ±6.1 | 14.5 ±4.9 | 42 / 48 / 10 | 42.5 | 42.9 / 292.9 | 1054 / 934 | 53 / 56 |
| Nexto | ChronosV2 | 14.5 ±4.9 | 81.0 ±5.4 | 4.5 ±2.9 | 45 / 46 / 9 | 30.9 | 57.0 / 140.8 | 1055 / 1047 | 48 / 48 |
| Nexto | Random | 98.5 ±1.7 | 1.0 ±1.4 | 0.5 ±1.0 | 96 / 2 / 0 | 9.0 | 46.8 / 0.3 | 1227 / 746 | 39 / 13 |
| GalactusV2 | Nexto | 71.5 ±6.3 | 13.5 ±4.7 | 15.0 ±4.9 | 52 / 37 / 10 | 40.7 | 286.5 / 40.2 | 944 / 1043 | 55 / 54 |
| GalactusV2 | GalactusV2 | 27.0 ±6.2 | 38.0 ±6.7 | 35.0 ±6.6 | 38 / 42 / 18 | 55.0 | 158.7 / 165.4 | 1054 / 1056 | 43 / 44 |
| GalactusV2 | ChronosV2 | 38.0 ±6.7 | 49.0 ±6.9 | 13.0 ±4.7 | 50 / 38 / 11 | 38.8 | 183.3 / 61.2 | 990 / 1095 | 48 / 37 |
| GalactusV2 | Random | 97.0 ±2.4 | 1.5 ±1.7 | 1.5 ±1.7 | 94 / 6 / 0 | 24.3 | 218.4 / 0.2 | 886 / 622 | 49 / 6 |
| ChronosV2 | Nexto | 81.0 ±5.4 | 13.5 ±4.7 | 5.5 ±3.2 | 40 / 48 / 12 | 31.5 | 139.3 / 62.0 | 1030 / 1052 | 46 / 45 |
| ChronosV2 | GalactusV2 | 41.5 ±6.8 | 36.5 ±6.7 | 22.0 ±5.7 | 38 / 54 / 8 | 49.0 | 77.3 / 247.9 | 1097 / 978 | 41 / 51 |
| ChronosV2 | ChronosV2 | 50.0 ±6.9 | 40.0 ±6.8 | 10.0 ±4.2 | 40 / 38 / 20 | 35.0 | 94.7 / 101.8 | 1089 / 1091 | 42 / 43 |
| ChronosV2 | Random | 99.0 ±1.4 | 1.0 ±1.4 | 0.0 ±0.0 | 95 / 4 / 0 | 7.4 | 25.4 / 0.1 | 1359 / 763 | 35 / 16 |
| Random | Nexto | 0.0 ±0.0 | 100.0 ±0.0 | 0.0 ±0.0 | 8 / 92 / 0 | 9.8 | 0.3 / 57.0 | 745 / 1165 | 13 / 40 |
| Random | GalactusV2 | 1.0 ±1.4 | 99.0 ±1.4 | 0.0 ±0.0 | 4 / 95 / 0 | 23.0 | 0.3 / 194.2 | 624 / 927 | 7 / 53 |
| Random | ChronosV2 | 1.0 ±1.4 | 99.0 ±1.4 | 0.0 ±0.0 | 1 / 98 / 0 | 7.5 | 0.1 / 24.2 | 737 / 1365 | 13 / 35 |
| Random | Random | 1.0 ±1.4 | 0.0 ±0.0 | 99.0 ±1.4 | 8 / 12 / 0 | 10.8 | 0.2 / 0.5 | 695 / 690 | 12 / 12 |

## Per bot against the others (mirror games excluded)

| Bot | games | wins % | losses % | draws % | wins % as blue | wins % as orange |
|---|---|---|---|---|---|---|
| Nexto | 1200 | 41.9 ±2.8 | 51.4 ±2.8 | 6.7 ±1.4 | 41.5 ±3.9 | 42.3 ±4.0 |
| GalactusV2 | 1200 | 69.3 ±2.6 | 19.7 ±2.2 | 11.0 ±1.8 | 68.8 ±3.7 | 69.8 ±3.7 |
| ChronosV2 | 1200 | 75.1 ±2.4 | 17.4 ±2.1 | 7.5 ±1.5 | 73.8 ±3.5 | 76.3 ±3.4 |
| Random | 1200 | 0.9 ±0.5 | 98.8 ±0.6 | 0.3 ±0.3 | 0.7 ±0.7 | 1.2 ±0.9 |

## Side bias (mirror games: identical players, so blue and orange should win equally often)

| Bot | blue wins % | orange wins % | draws % |
|---|---|---|---|
| Nexto | 40.0 ±6.8 | 33.5 ±6.5 | 26.5 ±6.1 |
| GalactusV2 | 27.0 ±6.2 | 38.0 ±6.7 | 35.0 ±6.6 |
| ChronosV2 | 50.0 ±6.9 | 40.0 ±6.8 | 10.0 ±4.2 |
| Random | 1.0 ±1.4 | 0.0 ±0.0 | 99.0 ±1.4 |
