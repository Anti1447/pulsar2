# Round robin, sim 1v1 (kickoff suite)

N = 200 games per ordered pair, seed 20261105, tickSkip 8, actionDelay 7, game = one episode (goal, 10 s without a touch, or 90 s; the last two are draws). A is blue, B is orange. Percentages carry a 95% half-width (normal approximation). Speed in uu/s and boost 0-100 are per-car averages, touches are steps in which the car touched the ball.

| A (blue) | B (orange) | A wins % | B wins % | draws % | first touch A / B / both % | mean game s | touches A / B | speed A / B | boost A / B |
|---|---|---|---|---|---|---|---|---|---|
| Nexto | Nexto | 42.5 ±6.9 | 57.5 ±6.9 | 0.0 ±0.0 | 0 / 0 / 100 | 36.8 | 28.0 / 26.4 | 1230 / 1266 | 32 / 27 |
| Nexto | GalactusV2 | 7.5 ±3.7 | 92.5 ±3.7 | 0.0 ±0.0 | 0 / 88 / 12 | 13.3 | 2.6 / 14.8 | 1348 / 1551 | 25 / 43 |
| Nexto | ChronosV2 | 5.5 ±3.2 | 94.5 ±3.2 | 0.0 ±0.0 | 48 / 16 / 36 | 16.2 | 4.9 / 12.7 | 1391 / 1536 | 31 / 43 |
| Nexto | Random | 100.0 ±0.0 | 0.0 ±0.0 | 0.0 ±0.0 | 100 / 0 / 0 | 6.3 | 3.2 / 0.1 | 1553 / 718 | 19 / 6 |
| GalactusV2 | Nexto | 95.0 ±3.0 | 5.0 ±3.0 | 0.0 ±0.0 | 85 / 0 / 15 | 12.9 | 15.2 / 2.4 | 1563 / 1357 | 42 / 25 |
| GalactusV2 | GalactusV2 | 46.5 ±6.9 | 47.5 ±6.9 | 6.0 ±3.3 | 0 / 0 / 100 | 34.4 | 25.9 / 25.6 | 1503 / 1502 | 39 / 38 |
| GalactusV2 | ChronosV2 | 55.0 ±6.9 | 45.0 ±6.9 | 0.0 ±0.0 | 100 / 0 / 0 | 20.4 | 19.4 / 7.6 | 1591 / 1548 | 39 / 35 |
| GalactusV2 | Random | 100.0 ±0.0 | 0.0 ±0.0 | 0.0 ±0.0 | 100 / 0 / 0 | 6.7 | 5.4 / 0.0 | 1619 / 682 | 22 / 6 |
| ChronosV2 | Nexto | 96.0 ±2.7 | 4.0 ±2.7 | 0.0 ±0.0 | 14 / 44 / 42 | 15.6 | 11.8 / 4.5 | 1552 / 1399 | 42 / 29 |
| ChronosV2 | GalactusV2 | 44.5 ±6.9 | 55.5 ±6.9 | 0.0 ±0.0 | 0 / 100 / 0 | 18.5 | 6.9 / 17.2 | 1540 / 1562 | 35 / 38 |
| ChronosV2 | ChronosV2 | 42.0 ±6.8 | 55.5 ±6.9 | 2.5 ±2.2 | 0 / 0 / 100 | 28.9 | 16.1 / 16.1 | 1467 / 1466 | 38 / 39 |
| ChronosV2 | Random | 100.0 ±0.0 | 0.0 ±0.0 | 0.0 ±0.0 | 100 / 0 / 0 | 4.6 | 1.3 / 0.0 | 1786 / 719 | 13 / 8 |
| Random | Nexto | 0.0 ±0.0 | 100.0 ±0.0 | 0.0 ±0.0 | 0 / 100 / 0 | 6.3 | 0.1 / 3.1 | 719 / 1584 | 7 / 19 |
| Random | GalactusV2 | 0.0 ±0.0 | 100.0 ±0.0 | 0.0 ±0.0 | 0 / 100 / 0 | 6.8 | 0.0 / 5.7 | 682 / 1638 | 7 / 22 |
| Random | ChronosV2 | 0.0 ±0.0 | 100.0 ±0.0 | 0.0 ±0.0 | 0 / 100 / 0 | 4.8 | 0.0 / 1.4 | 703 / 1777 | 8 / 14 |
| Random | Random | 0.0 ±0.0 | 0.0 ±0.0 | 100.0 ±0.0 | 2 / 2 / 0 | 10.4 | 0.0 / 0.0 | 637 / 649 | 5 / 4 |

## Per bot against the others (mirror games excluded)

| Bot | games | wins % | losses % | draws % | wins % as blue | wins % as orange |
|---|---|---|---|---|---|---|
| Nexto | 1200 | 37.0 ±2.7 | 63.0 ±2.7 | 0.0 ±0.0 | 37.7 ±3.9 | 36.3 ±3.8 |
| GalactusV2 | 1200 | 83.0 ±2.1 | 17.0 ±2.1 | 0.0 ±0.0 | 83.3 ±3.0 | 82.7 ±3.0 |
| ChronosV2 | 1200 | 80.0 ±2.3 | 20.0 ±2.3 | 0.0 ±0.0 | 80.2 ±3.2 | 79.8 ±3.2 |
| Random | 1200 | 0.0 ±0.0 | 100.0 ±0.0 | 0.0 ±0.0 | 0.0 ±0.0 | 0.0 ±0.0 |

## Side bias (mirror games: identical players, so blue and orange should win equally often)

| Bot | blue wins % | orange wins % | draws % |
|---|---|---|---|
| Nexto | 42.5 ±6.9 | 57.5 ±6.9 | 0.0 ±0.0 |
| GalactusV2 | 46.5 ±6.9 | 47.5 ±6.9 | 6.0 ±3.3 |
| ChronosV2 | 42.0 ±6.8 | 55.5 ±6.9 | 2.5 ±2.2 |
| Random | 0.0 ±0.0 | 0.0 ±0.0 | 100.0 ±0.0 |
