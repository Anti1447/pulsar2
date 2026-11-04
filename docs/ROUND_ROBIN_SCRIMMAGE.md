# Round robin, sim 1v1 (scrimmage suite)

N = 200 games per ordered pair, seed 20261105, tickSkip 8, actionDelay 7, game = one episode (goal, 10 s without a touch, or 90 s; the last two are draws). A is blue, B is orange. Percentages carry a 95% half-width (normal approximation). Speed in uu/s and boost 0-100 are per-car averages, touches are steps in which the car touched the ball.

| A (blue) | B (orange) | A wins % | B wins % | draws % | first touch A / B / both % | mean game s | touches A / B | speed A / B | boost A / B |
|---|---|---|---|---|---|---|---|---|---|
| Nexto | Nexto | 32.5 ±6.5 | 33.0 ±6.5 | 34.5 ±6.6 | 38 / 39 / 22 | 54.7 | 233.1 / 231.7 | 859 / 844 | 60 / 58 |
| Nexto | GalactusV2 | 11.5 ±4.4 | 75.0 ±6.0 | 13.5 ±4.7 | 40 / 52 / 7 | 39.4 | 36.6 / 276.8 | 1053 / 930 | 52 / 57 |
| Nexto | ChronosV2 | 14.0 ±4.8 | 82.0 ±5.3 | 4.0 ±2.7 | 42 / 52 / 6 | 28.6 | 50.6 / 124.3 | 1042 / 1026 | 44 / 47 |
| Nexto | Random | 98.0 ±1.9 | 1.5 ±1.7 | 0.5 ±1.0 | 94 / 3 / 0 | 9.2 | 50.9 / 0.3 | 1206 / 721 | 39 / 13 |
| GalactusV2 | Nexto | 74.0 ±6.1 | 10.5 ±4.2 | 15.5 ±5.0 | 54 / 40 / 4 | 41.1 | 279.1 / 43.4 | 929 / 1037 | 54 / 50 |
| GalactusV2 | GalactusV2 | 29.0 ±6.3 | 31.0 ±6.4 | 40.0 ±6.8 | 35 / 40 / 26 | 58.7 | 171.0 / 177.7 | 1037 / 1042 | 43 / 44 |
| GalactusV2 | ChronosV2 | 41.0 ±6.8 | 43.0 ±6.9 | 16.0 ±5.1 | 44 / 48 / 8 | 42.7 | 209.9 / 69.5 | 973 / 1096 | 49 / 39 |
| GalactusV2 | Random | 99.0 ±1.4 | 0.5 ±1.0 | 0.5 ±1.0 | 96 / 2 / 0 | 22.5 | 200.3 / 0.2 | 904 / 619 | 51 / 7 |
| ChronosV2 | Nexto | 80.0 ±5.5 | 15.0 ±4.9 | 5.0 ±3.0 | 56 / 36 / 8 | 31.4 | 150.0 / 57.7 | 1008 / 1041 | 46 / 47 |
| ChronosV2 | GalactusV2 | 40.5 ±6.8 | 41.5 ±6.8 | 18.0 ±5.3 | 38 / 52 / 8 | 45.6 | 72.5 / 232.8 | 1092 / 974 | 41 / 52 |
| ChronosV2 | ChronosV2 | 41.0 ±6.8 | 49.0 ±6.9 | 10.0 ±4.2 | 35 / 44 / 20 | 39.2 | 110.5 / 108.4 | 1067 / 1077 | 42 / 43 |
| ChronosV2 | Random | 100.0 ±0.0 | 0.0 ±0.0 | 0.0 ±0.0 | 97 / 2 / 0 | 7.7 | 28.0 / 0.1 | 1317 / 733 | 35 / 14 |
| Random | Nexto | 0.5 ±1.0 | 99.5 ±1.0 | 0.0 ±0.0 | 6 / 94 / 0 | 9.6 | 0.4 / 55.5 | 701 / 1190 | 12 / 42 |
| Random | GalactusV2 | 1.5 ±1.7 | 97.5 ±2.2 | 1.0 ±1.4 | 4 / 94 / 0 | 23.6 | 0.2 / 210.8 | 618 / 895 | 7 / 51 |
| Random | ChronosV2 | 0.0 ±0.0 | 100.0 ±0.0 | 0.0 ±0.0 | 2 / 96 / 2 | 7.1 | 0.3 / 22.5 | 763 / 1336 | 16 / 35 |
| Random | Random | 1.5 ±1.7 | 1.5 ±1.7 | 97.0 ±2.4 | 10 / 8 / 0 | 10.4 | 0.3 / 0.2 | 692 / 712 | 11 / 11 |

## Per bot against the others (mirror games excluded)

| Bot | games | wins % | losses % | draws % | wins % as blue | wins % as orange |
|---|---|---|---|---|---|---|
| Nexto | 1200 | 41.4 ±2.8 | 52.2 ±2.8 | 6.4 ±1.4 | 41.2 ±3.9 | 41.7 ±3.9 |
| GalactusV2 | 1200 | 71.3 ±2.6 | 17.9 ±2.2 | 10.8 ±1.8 | 71.3 ±3.6 | 71.3 ±3.6 |
| ChronosV2 | 1200 | 74.2 ±2.5 | 18.6 ±2.2 | 7.2 ±1.5 | 73.5 ±3.5 | 75.0 ±3.5 |
| Random | 1200 | 0.7 ±0.5 | 99.0 ±0.6 | 0.3 ±0.3 | 0.7 ±0.7 | 0.7 ±0.7 |

## Side bias (mirror games: identical players, so blue and orange should win equally often)

| Bot | blue wins % | orange wins % | draws % |
|---|---|---|---|
| Nexto | 32.5 ±6.5 | 33.0 ±6.5 | 34.5 ±6.6 |
| GalactusV2 | 29.0 ±6.3 | 31.0 ±6.4 | 40.0 ±6.8 |
| ChronosV2 | 41.0 ±6.8 | 49.0 ±6.9 | 10.0 ±4.2 |
| Random | 1.5 ±1.7 | 1.5 ±1.7 | 97.0 ±2.4 |
