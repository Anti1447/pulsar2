# Round robin, sim 1v1 (kickoff suite)

N = 200 games per ordered pair, seed 20261105, tickSkip 8, actionDelay 7, game = one episode (goal, 10 s without a touch, or 90 s; the last two are draws). A is blue, B is orange. Percentages carry a 95% half-width (normal approximation). Speed in uu/s and boost 0-100 are per-car averages, touches are steps in which the car touched the ball.

| A (blue) | B (orange) | A wins % | B wins % | draws % | first touch A / B / both % | mean game s | touches A / B | speed A / B | boost A / B |
|---|---|---|---|---|---|---|---|---|---|
| Nexto | Nexto | 100.0 ±0.0 | 0.0 ±0.0 | 0.0 ±0.0 | 0 / 0 / 100 | 32.5 | 40.4 / 38.6 | 1273 / 1334 | 42 / 35 |
| Nexto | GalactusV2 | 5.5 ±3.2 | 94.5 ±3.2 | 0.0 ±0.0 | 0 / 58 / 42 | 16.0 | 3.5 / 21.4 | 1418 / 1622 | 34 / 44 |
| Nexto | ChronosV2 | 5.0 ±3.0 | 94.5 ±3.2 | 0.5 ±1.0 | 81 / 0 / 19 | 14.6 | 4.5 / 11.9 | 1469 / 1546 | 32 / 41 |
| Nexto | Random | 100.0 ±0.0 | 0.0 ±0.0 | 0.0 ±0.0 | 100 / 0 / 0 | 5.7 | 2.7 / 0.1 | 1588 / 720 | 18 / 8 |
| GalactusV2 | Nexto | 92.5 ±3.7 | 7.5 ±3.7 | 0.0 ±0.0 | 50 / 0 / 50 | 13.9 | 17.1 / 3.3 | 1565 / 1392 | 44 / 32 |
| GalactusV2 | GalactusV2 | 49.0 ±6.9 | 47.0 ±6.9 | 4.0 ±2.7 | 0 / 0 / 100 | 31.5 | 24.0 / 22.9 | 1480 / 1480 | 40 / 39 |
| GalactusV2 | ChronosV2 | 50.0 ±6.9 | 49.5 ±6.9 | 0.5 ±1.0 | 100 / 0 / 0 | 19.7 | 16.8 / 6.8 | 1572 / 1557 | 37 / 36 |
| GalactusV2 | Random | 100.0 ±0.0 | 0.0 ±0.0 | 0.0 ±0.0 | 100 / 0 / 0 | 6.7 | 5.2 / 0.0 | 1621 / 682 | 22 / 6 |
| ChronosV2 | Nexto | 92.0 ±3.8 | 8.0 ±3.8 | 0.0 ±0.0 | 0 / 78 / 22 | 15.7 | 10.9 / 5.7 | 1563 / 1480 | 38 / 32 |
| ChronosV2 | GalactusV2 | 50.0 ±6.9 | 49.5 ±6.9 | 0.5 ±1.0 | 0 / 100 / 0 | 19.9 | 7.2 / 16.3 | 1580 / 1580 | 38 / 36 |
| ChronosV2 | ChronosV2 | 49.5 ±6.9 | 49.5 ±6.9 | 1.0 ±1.4 | 3 / 3 / 94 | 26.6 | 15.6 / 14.0 | 1439 / 1421 | 39 / 38 |
| ChronosV2 | Random | 100.0 ±0.0 | 0.0 ±0.0 | 0.0 ±0.0 | 100 / 0 / 0 | 4.6 | 1.4 / 0.0 | 1801 / 698 | 14 / 8 |
| Random | Nexto | 0.0 ±0.0 | 100.0 ±0.0 | 0.0 ±0.0 | 0 / 100 / 0 | 5.5 | 0.0 / 2.5 | 741 / 1594 | 8 / 19 |
| Random | GalactusV2 | 0.0 ±0.0 | 100.0 ±0.0 | 0.0 ±0.0 | 0 / 100 / 0 | 6.7 | 0.0 / 5.3 | 677 / 1633 | 6 / 22 |
| Random | ChronosV2 | 0.0 ±0.0 | 100.0 ±0.0 | 0.0 ±0.0 | 0 / 100 / 0 | 4.7 | 0.0 / 1.5 | 726 / 1781 | 9 / 14 |
| Random | Random | 0.0 ±0.0 | 0.0 ±0.0 | 100.0 ±0.0 | 2 / 4 / 0 | 10.3 | 0.0 / 0.1 | 625 / 637 | 5 / 4 |

## Per bot against the others (mirror games excluded)

| Bot | games | wins % | losses % | draws % | wins % as blue | wins % as orange |
|---|---|---|---|---|---|---|
| Nexto | 1200 | 37.7 ±2.7 | 62.3 ±2.7 | 0.1 ±0.2 | 36.8 ±3.9 | 38.5 ±3.9 |
| GalactusV2 | 1200 | 81.1 ±2.2 | 18.8 ±2.2 | 0.2 ±0.2 | 80.8 ±3.1 | 81.3 ±3.1 |
| ChronosV2 | 1200 | 81.0 ±2.2 | 18.8 ±2.2 | 0.2 ±0.3 | 80.7 ±3.2 | 81.3 ±3.1 |
| Random | 1200 | 0.0 ±0.0 | 100.0 ±0.0 | 0.0 ±0.0 | 0.0 ±0.0 | 0.0 ±0.0 |

## Side bias (mirror games: identical players, so blue and orange should win equally often)

| Bot | blue wins % | orange wins % | draws % |
|---|---|---|---|
| Nexto | 100.0 ±0.0 | 0.0 ±0.0 | 0.0 ±0.0 |
| GalactusV2 | 49.0 ±6.9 | 47.0 ±6.9 | 4.0 ±2.7 |
| ChronosV2 | 49.5 ±6.9 | 49.5 ±6.9 | 1.0 ±1.4 |
| Random | 0.0 ±0.0 | 0.0 ±0.0 | 100.0 ±0.0 |
