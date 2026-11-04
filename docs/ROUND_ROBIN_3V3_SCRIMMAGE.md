# Round robin, sim 3v3 (scrimmage suite)

N = 100 games per ordered pair, seed 20261105, tickSkip 8, actionDelay 7, game = one episode (goal, 10 s without a touch, or 90 s; the last two are draws). A is blue, B is orange. Percentages carry a 95% half-width (normal approximation). Speed in uu/s and boost 0-100 are per-car averages, touches are steps in which the car touched the ball.

| A (blue) | B (orange) | A wins % | B wins % | draws % | first touch A / B / both % | mean game s | touches A / B | speed A / B | boost A / B |
|---|---|---|---|---|---|---|---|---|---|
| Nexto | Nexto | 28.0 ±8.8 | 22.0 ±8.1 | 50.0 ±9.8 | 46 / 44 / 10 | 63.9 | 100.2 / 101.9 | 1351 / 1350 | 48 / 49 |
| Nexto | GalactusV2 | 9.0 ±5.6 | 59.0 ±9.6 | 32.0 ±9.1 | 31 / 62 / 7 | 56.4 | 52.8 / 183.7 | 1336 / 1341 | 44 / 64 |
| Nexto | ChronosV2 | 21.0 ±8.0 | 52.0 ±9.8 | 27.0 ±8.7 | 29 / 65 / 6 | 52.8 | 63.8 / 112.6 | 1355 / 1449 | 44 / 47 |
| Nexto | Random | 99.0 ±2.0 | 1.0 ±2.0 | 0.0 ±0.0 | 93 / 4 / 0 | 7.1 | 22.2 / 0.4 | 1413 / 780 | 43 / 17 |
| GalactusV2 | Nexto | 55.0 ±9.8 | 14.0 ±6.8 | 31.0 ±9.1 | 54 / 40 / 5 | 56.1 | 191.4 / 55.3 | 1340 / 1325 | 65 / 43 |
| GalactusV2 | GalactusV2 | 28.0 ±8.8 | 28.0 ±8.8 | 44.0 ±9.7 | 39 / 46 / 15 | 63.1 | 133.0 / 130.1 | 1348 / 1354 | 59 / 59 |
| GalactusV2 | ChronosV2 | 59.0 ±9.6 | 12.0 ±6.4 | 29.0 ±8.9 | 34 / 53 / 12 | 57.2 | 153.1 / 84.3 | 1351 / 1395 | 62 / 42 |
| GalactusV2 | Random | 99.0 ±2.0 | 1.0 ±2.0 | 0.0 ±0.0 | 95 / 4 / 0 | 12.9 | 86.9 / 0.2 | 1122 / 668 | 61 / 9 |
| ChronosV2 | Nexto | 44.0 ±9.7 | 29.0 ±8.9 | 27.0 ±8.7 | 65 / 30 / 5 | 57.6 | 119.6 / 77.9 | 1448 / 1369 | 47 / 44 |
| ChronosV2 | GalactusV2 | 27.0 ±8.7 | 49.0 ±9.8 | 24.0 ±8.4 | 46 / 33 / 20 | 53.4 | 81.5 / 147.9 | 1418 / 1361 | 43 / 61 |
| ChronosV2 | ChronosV2 | 37.0 ±9.5 | 36.0 ±9.4 | 27.0 ±8.7 | 32 / 42 / 25 | 51.0 | 83.6 / 84.3 | 1439 / 1446 | 43 / 42 |
| ChronosV2 | Random | 100.0 ±0.0 | 0.0 ±0.0 | 0.0 ±0.0 | 98 / 2 / 0 | 8.2 | 23.5 / 0.3 | 1450 / 749 | 47 / 15 |
| Random | Nexto | 0.0 ±0.0 | 100.0 ±0.0 | 0.0 ±0.0 | 4 / 95 / 0 | 7.6 | 0.3 / 26.8 | 744 / 1430 | 15 / 46 |
| Random | GalactusV2 | 0.0 ±0.0 | 100.0 ±0.0 | 0.0 ±0.0 | 4 / 96 / 0 | 11.9 | 0.3 / 79.6 | 670 / 1114 | 11 / 59 |
| Random | ChronosV2 | 0.0 ±0.0 | 100.0 ±0.0 | 0.0 ±0.0 | 0 / 100 / 0 | 8.4 | 0.2 / 22.2 | 759 / 1476 | 13 / 48 |
| Random | Random | 4.0 ±3.8 | 1.0 ±2.0 | 95.0 ±4.3 | 11 / 17 / 0 | 11.3 | 0.3 / 0.6 | 698 / 691 | 11 / 11 |

## Per bot against the others (mirror games excluded)

| Bot | games | wins % | losses % | draws % | wins % as blue | wins % as orange |
|---|---|---|---|---|---|---|
| Nexto | 600 | 45.3 ±4.0 | 35.2 ±3.8 | 19.5 ±3.2 | 43.0 ±5.6 | 47.7 ±5.7 |
| GalactusV2 | 600 | 70.2 ±3.7 | 10.5 ±2.5 | 19.3 ±3.2 | 71.0 ±5.1 | 69.3 ±5.2 |
| ChronosV2 | 600 | 55.8 ±4.0 | 26.3 ±3.5 | 17.8 ±3.1 | 57.0 ±5.6 | 54.7 ±5.6 |
| Random | 600 | 0.3 ±0.5 | 99.7 ±0.5 | 0.0 ±0.0 | 0.0 ±0.0 | 0.7 ±0.9 |

## Side bias (mirror games: identical players, so blue and orange should win equally often)

| Bot | blue wins % | orange wins % | draws % |
|---|---|---|---|
| Nexto | 28.0 ±8.8 | 22.0 ±8.1 | 50.0 ±9.8 |
| GalactusV2 | 28.0 ±8.8 | 28.0 ±8.8 | 44.0 ±9.7 |
| ChronosV2 | 37.0 ±9.5 | 36.0 ±9.4 | 27.0 ±8.7 |
| Random | 4.0 ±3.8 | 1.0 ±2.0 | 95.0 ±4.3 |
