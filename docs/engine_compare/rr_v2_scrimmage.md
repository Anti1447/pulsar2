# Round robin, sim 1v1 (scrimmage suite)

N = 200 games per ordered pair, seed 20261105, tickSkip 8, actionDelay 7, game = one episode (goal, 10 s without a touch, or 90 s; the last two are draws). A is blue, B is orange. Percentages carry a 95% half-width (normal approximation). Speed in uu/s and boost 0-100 are per-car averages, touches are steps in which the car touched the ball.

| A (blue) | B (orange) | A wins % | B wins % | draws % | first touch A / B / both % | mean game s | touches A / B | speed A / B | boost A / B |
|---|---|---|---|---|---|---|---|---|---|
| Nexto | Nexto | 50.0 ±6.9 | 44.5 ±6.9 | 5.5 ±3.2 | 41 / 40 / 19 | 33.5 | 30.6 / 33.7 | 1363 / 1373 | 48 / 47 |
| Nexto | GalactusV2 | 10.0 ±4.2 | 90.0 ±4.2 | 0.0 ±0.0 | 38 / 52 / 9 | 13.6 | 2.9 / 21.5 | 1429 / 1549 | 44 / 50 |
| Nexto | ChronosV2 | 8.0 ±3.8 | 92.0 ±3.8 | 0.0 ±0.0 | 43 / 44 / 14 | 12.9 | 4.0 / 10.9 | 1443 / 1535 | 45 / 44 |
| Nexto | Random | 99.5 ±1.0 | 0.0 ±0.0 | 0.5 ±1.0 | 92 / 7 / 0 | 6.7 | 5.8 / 0.1 | 1520 / 786 | 36 / 16 |
| GalactusV2 | Nexto | 93.0 ±3.5 | 7.0 ±3.5 | 0.0 ±0.0 | 54 / 38 / 7 | 12.9 | 21.4 / 2.8 | 1530 / 1413 | 50 / 47 |
| GalactusV2 | GalactusV2 | 52.0 ±6.9 | 43.5 ±6.9 | 4.5 ±2.9 | 40 / 37 / 22 | 29.3 | 19.4 / 21.9 | 1576 / 1570 | 48 / 47 |
| GalactusV2 | ChronosV2 | 62.5 ±6.7 | 37.5 ±6.7 | 0.0 ±0.0 | 50 / 44 / 4 | 18.4 | 19.9 / 6.4 | 1579 / 1579 | 50 / 43 |
| GalactusV2 | Random | 99.5 ±1.0 | 0.5 ±1.0 | 0.0 ±0.0 | 96 / 2 / 0 | 9.2 | 17.9 / 0.0 | 1478 / 687 | 42 / 12 |
| ChronosV2 | Nexto | 89.5 ±4.2 | 10.5 ±4.2 | 0.0 ±0.0 | 50 / 37 / 12 | 13.2 | 10.2 / 4.0 | 1538 / 1431 | 44 / 44 |
| ChronosV2 | GalactusV2 | 39.0 ±6.8 | 61.0 ±6.8 | 0.0 ±0.0 | 50 / 44 / 6 | 16.5 | 5.0 / 17.2 | 1578 / 1567 | 42 / 47 |
| ChronosV2 | ChronosV2 | 47.0 ±6.9 | 51.5 ±6.9 | 1.5 ±1.7 | 44 / 38 / 18 | 23.4 | 12.1 / 12.3 | 1546 / 1542 | 44 / 43 |
| ChronosV2 | Random | 99.0 ±1.4 | 1.0 ±1.4 | 0.0 ±0.0 | 96 / 2 / 0 | 6.0 | 4.1 / 0.1 | 1582 / 797 | 35 / 17 |
| Random | Nexto | 0.0 ±0.0 | 100.0 ±0.0 | 0.0 ±0.0 | 5 / 94 / 0 | 7.1 | 0.1 / 5.0 | 766 / 1517 | 17 / 39 |
| Random | GalactusV2 | 0.0 ±0.0 | 100.0 ±0.0 | 0.0 ±0.0 | 4 / 94 / 0 | 9.1 | 0.1 / 18.4 | 688 / 1451 | 13 / 45 |
| Random | ChronosV2 | 0.0 ±0.0 | 100.0 ±0.0 | 0.0 ±0.0 | 2 / 98 / 0 | 6.0 | 0.1 / 3.9 | 772 / 1618 | 17 / 35 |
| Random | Random | 1.5 ±1.7 | 1.0 ±1.4 | 97.5 ±2.2 | 10 / 11 / 0 | 10.4 | 0.1 / 0.1 | 697 / 720 | 13 / 12 |

## Per bot against the others (mirror games excluded)

| Bot | games | wins % | losses % | draws % | wins % as blue | wins % as orange |
|---|---|---|---|---|---|---|
| Nexto | 1200 | 39.2 ±2.8 | 60.8 ±2.8 | 0.1 ±0.2 | 39.2 ±3.9 | 39.2 ±3.9 |
| GalactusV2 | 1200 | 84.3 ±2.1 | 15.7 ±2.1 | 0.0 ±0.0 | 85.0 ±2.9 | 83.7 ±3.0 |
| ChronosV2 | 1200 | 76.2 ±2.4 | 23.8 ±2.4 | 0.0 ±0.0 | 75.8 ±3.4 | 76.5 ±3.4 |
| Random | 1200 | 0.2 ±0.3 | 99.7 ±0.3 | 0.1 ±0.2 | 0.0 ±0.0 | 0.5 ±0.6 |

## Side bias (mirror games: identical players, so blue and orange should win equally often)

| Bot | blue wins % | orange wins % | draws % |
|---|---|---|---|
| Nexto | 50.0 ±6.9 | 44.5 ±6.9 | 5.5 ±3.2 |
| GalactusV2 | 52.0 ±6.9 | 43.5 ±6.9 | 4.5 ±2.9 |
| ChronosV2 | 47.0 ±6.9 | 51.5 ±6.9 | 1.5 ±1.7 |
| Random | 1.5 ±1.7 | 1.0 ±1.4 | 97.5 ±2.2 |
