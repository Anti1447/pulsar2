# Round robin, sim 3v3 (scrimmage suite)

N = 100 games per ordered pair, seed 20261105, tickSkip 8, actionDelay 7, game = one episode (goal, 10 s without a touch, or 90 s; the last two are draws). A is blue, B is orange. Percentages carry a 95% half-width (normal approximation). Speed in uu/s and boost 0-100 are per-car averages, touches are steps in which the car touched the ball.

| A (blue) | B (orange) | A wins % | B wins % | draws % | first touch A / B / both % | mean game s | touches A / B | speed A / B | boost A / B |
|---|---|---|---|---|---|---|---|---|---|
| Nexto | Nexto | 34.0 ±9.3 | 20.0 ±7.8 | 46.0 ±9.8 | 42 / 39 / 19 | 63.9 | 100.3 / 93.9 | 1369 / 1346 | 48 / 48 |
| Nexto | GalactusV2 | 4.0 ±3.8 | 68.0 ±9.1 | 28.0 ±8.8 | 27 / 62 / 9 | 52.4 | 51.3 / 183.7 | 1332 / 1341 | 45 / 64 |
| Nexto | ChronosV2 | 26.0 ±8.6 | 40.0 ±9.6 | 34.0 ±9.3 | 25 / 63 / 12 | 55.3 | 73.3 / 118.0 | 1372 / 1448 | 46 / 46 |
| Nexto | Random | 100.0 ±0.0 | 0.0 ±0.0 | 0.0 ±0.0 | 93 / 6 / 0 | 8.1 | 27.7 / 0.3 | 1414 / 745 | 45 / 15 |
| GalactusV2 | Nexto | 53.0 ±9.8 | 8.0 ±5.3 | 39.0 ±9.6 | 58 / 35 / 7 | 56.4 | 191.4 / 57.8 | 1319 / 1310 | 65 / 44 |
| GalactusV2 | GalactusV2 | 28.0 ±8.8 | 29.0 ±8.9 | 43.0 ±9.7 | 44 / 34 / 22 | 60.5 | 126.4 / 132.8 | 1354 / 1345 | 59 / 60 |
| GalactusV2 | ChronosV2 | 51.0 ±9.8 | 23.0 ±8.2 | 26.0 ±8.6 | 41 / 39 / 20 | 51.9 | 135.2 / 75.1 | 1351 / 1436 | 61 / 42 |
| GalactusV2 | Random | 99.0 ±2.0 | 1.0 ±2.0 | 0.0 ±0.0 | 94 / 5 / 0 | 12.9 | 89.5 / 0.6 | 1132 / 666 | 61 / 10 |
| ChronosV2 | Nexto | 40.0 ±9.6 | 31.0 ±9.1 | 29.0 ±8.9 | 71 / 20 / 7 | 54.6 | 107.0 / 76.8 | 1429 / 1379 | 45 / 46 |
| ChronosV2 | GalactusV2 | 22.0 ±8.1 | 54.0 ±9.8 | 24.0 ±8.4 | 49 / 37 / 14 | 50.2 | 72.6 / 129.2 | 1434 / 1348 | 42 / 60 |
| ChronosV2 | ChronosV2 | 34.0 ±9.3 | 41.0 ±9.6 | 25.0 ±8.5 | 42 / 35 / 20 | 51.1 | 86.2 / 79.4 | 1431 / 1446 | 44 / 44 |
| ChronosV2 | Random | 100.0 ±0.0 | 0.0 ±0.0 | 0.0 ±0.0 | 97 / 3 / 0 | 8.2 | 23.0 / 0.4 | 1423 / 757 | 46 / 15 |
| Random | Nexto | 1.0 ±2.0 | 99.0 ±2.0 | 0.0 ±0.0 | 3 / 94 / 1 | 7.4 | 0.1 / 22.1 | 753 / 1420 | 16 / 46 |
| Random | GalactusV2 | 0.0 ±0.0 | 100.0 ±0.0 | 0.0 ±0.0 | 3 / 96 / 1 | 11.6 | 0.4 / 79.3 | 692 / 1150 | 11 / 59 |
| Random | ChronosV2 | 0.0 ±0.0 | 100.0 ±0.0 | 0.0 ±0.0 | 6 / 90 / 3 | 8.2 | 0.4 / 21.5 | 754 / 1445 | 14 / 42 |
| Random | Random | 1.0 ±2.0 | 4.0 ±3.8 | 95.0 ±4.3 | 16 / 16 / 0 | 10.9 | 0.6 / 0.6 | 685 / 696 | 11 / 12 |

## Per bot against the others (mirror games excluded)

| Bot | games | wins % | losses % | draws % | wins % as blue | wins % as orange |
|---|---|---|---|---|---|---|
| Nexto | 600 | 44.7 ±4.0 | 33.7 ±3.8 | 21.7 ±3.3 | 43.3 ±5.6 | 46.0 ±5.6 |
| GalactusV2 | 600 | 70.8 ±3.6 | 9.7 ±2.4 | 19.5 ±3.2 | 67.7 ±5.3 | 74.0 ±5.0 |
| ChronosV2 | 600 | 54.2 ±4.0 | 27.0 ±3.6 | 18.8 ±3.1 | 54.0 ±5.6 | 54.3 ±5.6 |
| Random | 600 | 0.3 ±0.5 | 99.7 ±0.5 | 0.0 ±0.0 | 0.3 ±0.7 | 0.3 ±0.7 |

## Side bias (mirror games: identical players, so blue and orange should win equally often)

| Bot | blue wins % | orange wins % | draws % |
|---|---|---|---|
| Nexto | 34.0 ±9.3 | 20.0 ±7.8 | 46.0 ±9.8 |
| GalactusV2 | 28.0 ±8.8 | 29.0 ±8.9 | 43.0 ±9.7 |
| ChronosV2 | 34.0 ±9.3 | 41.0 ±9.6 | 25.0 ±8.5 |
| Random | 1.0 ±2.0 | 4.0 ±3.8 | 95.0 ±4.3 |
