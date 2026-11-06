# Round robin, sim 1v1 (scrimmage suite)

N = 200 games per ordered pair, seed 20261105, tickSkip 8, actionDelay 7, game = one episode (goal, 10 s without a touch, or 90 s; the last two are draws). A is blue, B is orange. Percentages carry a 95% half-width (normal approximation). Speed in uu/s and boost 0-100 are per-car averages, touches are steps in which the car touched the ball.

| A (blue) | B (orange) | A wins % | B wins % | draws % | first touch A / B / both % | mean game s | touches A / B | speed A / B | boost A / B |
|---|---|---|---|---|---|---|---|---|---|
| Nexto | Nexto | 44.0 ±6.9 | 28.0 ±6.2 | 28.0 ±6.2 | 41 / 42 / 16 | 51.1 | 223.3 / 209.8 | 861 / 844 | 57 / 53 |
| Nexto | GalactusV2 | 10.5 ±4.2 | 76.0 ±5.9 | 13.5 ±4.7 | 37 / 51 / 10 | 41.2 | 39.6 / 288.6 | 1022 / 927 | 49 / 57 |
| Nexto | ChronosV2 | 17.5 ±5.3 | 79.5 ±5.6 | 3.0 ±2.4 | 52 / 40 / 6 | 30.5 | 56.8 / 138.6 | 1049 / 1031 | 49 / 47 |
| Nexto | Random | 100.0 ±0.0 | 0.0 ±0.0 | 0.0 ±0.0 | 96 / 4 / 0 | 9.2 | 50.1 / 0.2 | 1230 / 741 | 40 / 13 |
| GalactusV2 | Nexto | 74.0 ±6.1 | 10.0 ±4.2 | 16.0 ±5.1 | 52 / 41 / 6 | 45.0 | 317.4 / 42.3 | 919 / 1023 | 57 / 50 |
| GalactusV2 | GalactusV2 | 33.5 ±6.5 | 28.5 ±6.3 | 38.0 ±6.7 | 45 / 36 / 18 | 58.3 | 174.6 / 170.6 | 1048 / 1056 | 45 / 43 |
| GalactusV2 | ChronosV2 | 30.5 ±6.4 | 48.5 ±6.9 | 21.0 ±5.6 | 42 / 46 / 10 | 44.3 | 213.9 / 74.1 | 997 / 1112 | 50 / 39 |
| GalactusV2 | Random | 99.0 ±1.4 | 0.0 ±0.0 | 1.0 ±1.4 | 96 / 4 / 0 | 24.8 | 215.0 / 0.2 | 916 / 627 | 51 / 7 |
| ChronosV2 | Nexto | 86.0 ±4.8 | 10.0 ±4.2 | 4.0 ±2.7 | 48 / 43 / 8 | 28.8 | 128.2 / 53.1 | 1039 / 1067 | 47 / 45 |
| ChronosV2 | GalactusV2 | 44.0 ±6.9 | 39.0 ±6.8 | 17.0 ±5.2 | 37 / 53 / 10 | 44.2 | 68.1 / 210.9 | 1117 / 1000 | 41 / 49 |
| ChronosV2 | ChronosV2 | 43.0 ±6.9 | 45.0 ±6.9 | 12.0 ±4.5 | 40 / 38 / 22 | 40.0 | 108.6 / 123.4 | 1063 / 1047 | 41 / 42 |
| ChronosV2 | Random | 99.0 ±1.4 | 1.0 ±1.4 | 0.0 ±0.0 | 96 / 2 / 0 | 7.7 | 26.7 / 0.2 | 1341 / 772 | 38 / 16 |
| Random | Nexto | 1.0 ±1.4 | 98.5 ±1.7 | 0.5 ±1.0 | 7 / 90 / 2 | 9.1 | 0.4 / 50.3 | 727 / 1201 | 12 / 39 |
| Random | GalactusV2 | 1.0 ±1.4 | 96.5 ±2.5 | 2.5 ±2.2 | 2 / 98 / 0 | 25.7 | 0.2 / 218.4 | 621 / 923 | 6 / 50 |
| Random | ChronosV2 | 0.0 ±0.0 | 100.0 ±0.0 | 0.0 ±0.0 | 4 / 95 / 0 | 7.8 | 0.3 / 28.0 | 739 / 1337 | 15 / 39 |
| Random | Random | 0.5 ±1.0 | 2.5 ±2.2 | 97.0 ±2.4 | 8 / 8 / 0 | 10.4 | 0.3 / 0.3 | 715 / 686 | 13 / 11 |

## Per bot against the others (mirror games excluded)

| Bot | games | wins % | losses % | draws % | wins % as blue | wins % as orange |
|---|---|---|---|---|---|---|
| Nexto | 1200 | 41.1 ±2.8 | 52.8 ±2.8 | 6.2 ±1.4 | 42.7 ±4.0 | 39.5 ±3.9 |
| GalactusV2 | 1200 | 69.2 ±2.6 | 19.0 ±2.2 | 11.8 ±1.8 | 67.8 ±3.7 | 70.5 ±3.6 |
| ChronosV2 | 1200 | 76.2 ±2.4 | 16.3 ±2.1 | 7.5 ±1.5 | 76.3 ±3.4 | 76.0 ±3.4 |
| Random | 1200 | 0.5 ±0.4 | 98.8 ±0.6 | 0.7 ±0.5 | 0.7 ±0.7 | 0.3 ±0.5 |

## Side bias (mirror games: identical players, so blue and orange should win equally often)

| Bot | blue wins % | orange wins % | draws % |
|---|---|---|---|
| Nexto | 44.0 ±6.9 | 28.0 ±6.2 | 28.0 ±6.2 |
| GalactusV2 | 33.5 ±6.5 | 28.5 ±6.3 | 38.0 ±6.7 |
| ChronosV2 | 43.0 ±6.9 | 45.0 ±6.9 | 12.0 ±4.5 |
| Random | 0.5 ±1.0 | 2.5 ±2.2 | 97.0 ±2.4 |
