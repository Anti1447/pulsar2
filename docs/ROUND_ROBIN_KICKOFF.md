# Round robin, sim 1v1 (kickoff suite)

N = 200 games per ordered pair, seed 20261105, tickSkip 8, actionDelay 7, game = one episode (goal, 10 s without a touch, or 90 s; the last two are draws). A is blue, B is orange. Percentages carry a 95% half-width (normal approximation). Speed in uu/s and boost 0-100 are per-car averages, touches are steps in which the car touched the ball.

| A (blue) | B (orange) | A wins % | B wins % | draws % | first touch A / B / both % | mean game s | touches A / B | speed A / B | boost A / B |
|---|---|---|---|---|---|---|---|---|---|
| Nexto | Nexto | 28.0 ±6.2 | 40.0 ±6.8 | 32.0 ±6.5 | 0 / 0 / 100 | 53.9 | 206.8 / 197.2 | 851 / 794 | 52 / 49 |
| Nexto | GalactusV2 | 15.5 ±5.0 | 71.5 ±6.3 | 13.0 ±4.7 | 0 / 100 / 0 | 39.0 | 36.9 / 270.1 | 1117 / 958 | 45 / 48 |
| Nexto | ChronosV2 | 15.0 ±4.9 | 79.0 ±5.6 | 6.0 ±3.3 | 0 / 94 / 6 | 34.2 | 63.2 / 160.4 | 1022 / 996 | 39 / 40 |
| Nexto | Random | 100.0 ±0.0 | 0.0 ±0.0 | 0.0 ±0.0 | 100 / 0 / 0 | 5.9 | 12.0 / 0.1 | 1621 / 732 | 12 / 7 |
| GalactusV2 | Nexto | 77.0 ±5.8 | 10.5 ±4.2 | 12.5 ±4.6 | 100 / 0 / 0 | 41.4 | 285.3 / 38.8 | 967 / 1106 | 48 / 43 |
| GalactusV2 | GalactusV2 | 28.5 ±6.3 | 33.0 ±6.5 | 38.5 ±6.7 | 0 / 0 / 100 | 60.5 | 187.3 / 185.8 | 984 / 1004 | 36 / 34 |
| GalactusV2 | ChronosV2 | 28.0 ±6.2 | 55.0 ±6.9 | 17.0 ±5.2 | 80 / 0 / 20 | 42.1 | 194.4 / 71.7 | 1010 / 1112 | 41 / 37 |
| GalactusV2 | Random | 99.5 ±1.0 | 0.0 ±0.0 | 0.5 ±1.0 | 100 / 0 / 0 | 13.2 | 99.9 / 0.0 | 1077 / 633 | 31 / 4 |
| ChronosV2 | Nexto | 79.5 ±5.6 | 16.0 ±5.1 | 4.5 ±2.9 | 92 / 0 / 8 | 33.6 | 151.2 / 73.1 | 1010 / 1017 | 39 / 40 |
| ChronosV2 | GalactusV2 | 57.0 ±6.9 | 26.0 ±6.1 | 17.0 ±5.2 | 0 / 78 / 22 | 41.8 | 72.3 / 191.8 | 1084 / 998 | 37 / 41 |
| ChronosV2 | ChronosV2 | 43.0 ±6.9 | 43.0 ±6.9 | 14.0 ±4.8 | 1 / 2 / 97 | 40.7 | 117.3 / 122.3 | 1032 / 1025 | 36 / 36 |
| ChronosV2 | Random | 100.0 ±0.0 | 0.0 ±0.0 | 0.0 ±0.0 | 100 / 0 / 0 | 5.6 | 8.9 / 0.0 | 1669 / 705 | 10 / 7 |
| Random | Nexto | 0.0 ±0.0 | 100.0 ±0.0 | 0.0 ±0.0 | 0 / 100 / 0 | 5.7 | 0.2 / 10.7 | 739 / 1659 | 7 / 11 |
| Random | GalactusV2 | 0.0 ±0.0 | 99.5 ±1.0 | 0.5 ±1.0 | 0 / 100 / 0 | 14.6 | 0.0 / 116.1 | 624 / 1037 | 4 / 34 |
| Random | ChronosV2 | 0.0 ±0.0 | 100.0 ±0.0 | 0.0 ±0.0 | 0 / 100 / 0 | 5.6 | 0.0 / 7.9 | 706 / 1683 | 7 / 10 |
| Random | Random | 1.0 ±1.4 | 0.0 ±0.0 | 99.0 ±1.4 | 6 / 3 / 0 | 10.5 | 0.2 / 0.1 | 646 / 649 | 5 / 5 |

## Per bot against the others (mirror games excluded)

| Bot | games | wins % | losses % | draws % | wins % as blue | wins % as orange |
|---|---|---|---|---|---|---|
| Nexto | 1200 | 42.8 ±2.8 | 51.2 ±2.8 | 6.0 ±1.3 | 43.5 ±4.0 | 42.2 ±4.0 |
| GalactusV2 | 1200 | 66.9 ±2.7 | 23.0 ±2.4 | 10.1 ±1.7 | 68.2 ±3.7 | 65.7 ±3.8 |
| ChronosV2 | 1200 | 78.4 ±2.3 | 14.2 ±2.0 | 7.4 ±1.5 | 78.8 ±3.3 | 78.0 ±3.3 |
| Random | 1200 | 0.0 ±0.0 | 99.8 ±0.2 | 0.2 ±0.2 | 0.0 ±0.0 | 0.0 ±0.0 |

## Side bias (mirror games: identical players, so blue and orange should win equally often)

| Bot | blue wins % | orange wins % | draws % |
|---|---|---|---|
| Nexto | 28.0 ±6.2 | 40.0 ±6.8 | 32.0 ±6.5 |
| GalactusV2 | 28.5 ±6.3 | 33.0 ±6.5 | 38.5 ±6.7 |
| ChronosV2 | 43.0 ±6.9 | 43.0 ±6.9 | 14.0 ±4.8 |
| Random | 1.0 ±1.4 | 0.0 ±0.0 | 99.0 ±1.4 |
