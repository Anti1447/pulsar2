# Round robin, sim 1v1 (scrimmage suite)

N = 200 games per ordered pair, seed 20261105, tickSkip 8, actionDelay 7, game = one episode (goal, 10 s without a touch, or 90 s; the last two are draws). A is blue, B is orange. Percentages carry a 95% half-width (normal approximation). Speed in uu/s and boost 0-100 are per-car averages, touches are steps in which the car touched the ball.

| A (blue) | B (orange) | A wins % | B wins % | draws % | first touch A / B / both % | mean game s | touches A / B | speed A / B | boost A / B |
|---|---|---|---|---|---|---|---|---|---|
| Nexto | Nexto | 55.5 ±6.9 | 38.5 ±6.7 | 6.0 ±3.3 | 36 / 40 / 22 | 30.5 | 30.9 / 32.4 | 1371 / 1398 | 50 / 49 |
| Nexto | GalactusV2 | 13.0 ±4.7 | 87.0 ±4.7 | 0.0 ±0.0 | 36 / 50 / 14 | 13.1 | 3.4 / 18.3 | 1458 / 1577 | 42 / 50 |
| Nexto | ChronosV2 | 12.0 ±4.5 | 87.5 ±4.6 | 0.5 ±1.0 | 46 / 40 / 12 | 13.7 | 4.4 / 11.8 | 1453 / 1542 | 42 / 46 |
| Nexto | Random | 99.5 ±1.0 | 0.5 ±1.0 | 0.0 ±0.0 | 96 / 4 / 0 | 6.6 | 6.3 / 0.1 | 1505 / 753 | 39 / 17 |
| GalactusV2 | Nexto | 84.5 ±5.0 | 15.5 ±5.0 | 0.0 ±0.0 | 55 / 34 / 10 | 13.8 | 20.3 / 2.9 | 1563 / 1452 | 51 / 43 |
| GalactusV2 | GalactusV2 | 46.0 ±6.9 | 48.5 ±6.9 | 5.5 ±3.2 | 38 / 39 / 23 | 27.5 | 18.6 / 19.5 | 1570 / 1557 | 46 / 46 |
| GalactusV2 | ChronosV2 | 60.5 ±6.8 | 39.5 ±6.8 | 0.0 ±0.0 | 52 / 43 / 6 | 16.9 | 18.7 / 5.6 | 1565 / 1602 | 47 / 43 |
| GalactusV2 | Random | 100.0 ±0.0 | 0.0 ±0.0 | 0.0 ±0.0 | 96 / 2 / 0 | 10.4 | 24.7 / 0.1 | 1445 / 688 | 47 / 12 |
| ChronosV2 | Nexto | 85.0 ±4.9 | 15.0 ±4.9 | 0.0 ±0.0 | 43 / 50 / 6 | 13.6 | 10.9 / 4.1 | 1534 / 1454 | 46 / 45 |
| ChronosV2 | GalactusV2 | 34.0 ±6.6 | 66.0 ±6.6 | 0.0 ±0.0 | 36 / 52 / 11 | 17.9 | 5.6 / 19.5 | 1543 / 1546 | 39 / 48 |
| ChronosV2 | ChronosV2 | 47.0 ±6.9 | 52.5 ±6.9 | 0.5 ±1.0 | 32 / 44 / 23 | 24.1 | 10.9 / 12.1 | 1552 / 1545 | 44 / 44 |
| ChronosV2 | Random | 100.0 ±0.0 | 0.0 ±0.0 | 0.0 ±0.0 | 96 / 2 / 0 | 6.2 | 4.4 / 0.1 | 1594 / 776 | 36 / 18 |
| Random | Nexto | 0.5 ±1.0 | 99.5 ±1.0 | 0.0 ±0.0 | 5 / 94 / 0 | 7.2 | 0.1 / 6.0 | 765 / 1518 | 17 / 39 |
| Random | GalactusV2 | 0.0 ±0.0 | 100.0 ±0.0 | 0.0 ±0.0 | 4 / 96 / 0 | 9.4 | 0.1 / 18.7 | 711 / 1438 | 14 / 47 |
| Random | ChronosV2 | 0.0 ±0.0 | 100.0 ±0.0 | 0.0 ±0.0 | 2 / 98 / 0 | 5.9 | 0.1 / 4.1 | 749 / 1595 | 18 / 33 |
| Random | Random | 3.5 ±2.5 | 2.0 ±1.9 | 94.5 ±3.2 | 18 / 9 / 1 | 10.5 | 0.2 / 0.2 | 687 / 698 | 12 / 14 |

## Per bot against the others (mirror games excluded)

| Bot | games | wins % | losses % | draws % | wins % as blue | wins % as orange |
|---|---|---|---|---|---|---|
| Nexto | 1200 | 42.4 ±2.8 | 57.5 ±2.8 | 0.1 ±0.2 | 41.5 ±3.9 | 43.3 ±4.0 |
| GalactusV2 | 1200 | 83.0 ±2.1 | 17.0 ±2.1 | 0.0 ±0.0 | 81.7 ±3.1 | 84.3 ±2.9 |
| ChronosV2 | 1200 | 74.3 ±2.5 | 25.6 ±2.5 | 0.1 ±0.2 | 73.0 ±3.6 | 75.7 ±3.4 |
| Random | 1200 | 0.2 ±0.2 | 99.8 ±0.2 | 0.0 ±0.0 | 0.2 ±0.3 | 0.2 ±0.3 |

## Side bias (mirror games: identical players, so blue and orange should win equally often)

| Bot | blue wins % | orange wins % | draws % |
|---|---|---|---|
| Nexto | 55.5 ±6.9 | 38.5 ±6.7 | 6.0 ±3.3 |
| GalactusV2 | 46.0 ±6.9 | 48.5 ±6.9 | 5.5 ±3.2 |
| ChronosV2 | 47.0 ±6.9 | 52.5 ±6.9 | 0.5 ±1.0 |
| Random | 3.5 ±2.5 | 2.0 ±1.9 | 94.5 ±3.2 |
