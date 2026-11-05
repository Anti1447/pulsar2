# Round robin, sim 1v1 (kickoff suite)

N = 200 games per ordered pair, seed 20261105, tickSkip 8, actionDelay 7, game = one episode (goal, 10 s without a touch, or 90 s; the last two are draws). A is blue, B is orange. Percentages carry a 95% half-width (normal approximation). Speed in uu/s and boost 0-100 are per-car averages, touches are steps in which the car touched the ball.

| A (blue) | B (orange) | A wins % | B wins % | draws % | first touch A / B / both % | mean game s | touches A / B | speed A / B | boost A / B |
|---|---|---|---|---|---|---|---|---|---|
| Nexto | Nexto | 64.0 ±6.7 | 0.0 ±0.0 | 36.0 ±6.7 | 0 / 0 / 100 | 64.1 | 351.9 / 179.8 | 801 / 799 | 49 / 46 |
| Nexto | GalactusV2 | 10.5 ±4.2 | 73.0 ±6.2 | 16.5 ±5.1 | 0 / 62 / 38 | 45.1 | 49.0 / 310.2 | 1025 / 912 | 43 / 46 |
| Nexto | ChronosV2 | 13.5 ±4.7 | 81.5 ±5.4 | 5.0 ±3.0 | 68 / 16 / 15 | 26.2 | 40.8 / 127.1 | 1073 / 1027 | 34 / 38 |
| Nexto | Random | 99.5 ±1.0 | 0.0 ±0.0 | 0.5 ±1.0 | 100 / 0 / 0 | 6.3 | 14.7 / 0.1 | 1564 / 712 | 12 / 6 |
| GalactusV2 | Nexto | 70.0 ±6.4 | 11.0 ±4.3 | 19.0 ±5.4 | 64 / 0 / 36 | 49.5 | 348.1 / 54.8 | 903 / 1019 | 47 / 45 |
| GalactusV2 | GalactusV2 | 27.5 ±6.2 | 33.0 ±6.5 | 39.5 ±6.8 | 0 / 0 / 99 | 62.8 | 198.0 / 210.5 | 966 / 958 | 35 / 35 |
| GalactusV2 | ChronosV2 | 27.5 ±6.2 | 55.0 ±6.9 | 17.5 ±5.3 | 79 / 0 / 21 | 40.7 | 184.5 / 68.5 | 1022 / 1116 | 42 / 36 |
| GalactusV2 | Random | 99.5 ±1.0 | 0.0 ±0.0 | 0.5 ±1.0 | 100 / 0 / 0 | 12.9 | 96.2 / 0.0 | 1072 / 620 | 32 / 4 |
| ChronosV2 | Nexto | 77.5 ±5.8 | 18.5 ±5.4 | 4.0 ±2.7 | 17 / 69 / 14 | 26.6 | 132.3 / 45.2 | 1005 / 1051 | 39 / 40 |
| ChronosV2 | GalactusV2 | 53.0 ±6.9 | 27.0 ±6.2 | 20.0 ±5.5 | 0 / 80 / 20 | 42.9 | 75.1 / 193.4 | 1112 / 1012 | 38 / 42 |
| ChronosV2 | ChronosV2 | 45.5 ±6.9 | 45.5 ±6.9 | 9.0 ±4.0 | 2 / 2 / 97 | 35.1 | 101.9 / 101.7 | 1028 / 1037 | 36 / 37 |
| ChronosV2 | Random | 100.0 ±0.0 | 0.0 ±0.0 | 0.0 ±0.0 | 100 / 0 / 0 | 5.7 | 9.0 / 0.0 | 1663 / 713 | 11 / 7 |
| Random | Nexto | 0.0 ±0.0 | 100.0 ±0.0 | 0.0 ±0.0 | 0 / 100 / 0 | 6.7 | 0.2 / 16.8 | 729 / 1538 | 6 / 13 |
| Random | GalactusV2 | 0.0 ±0.0 | 100.0 ±0.0 | 0.0 ±0.0 | 0 / 100 / 0 | 13.2 | 0.0 / 101.7 | 622 / 1053 | 5 / 30 |
| Random | ChronosV2 | 0.0 ±0.0 | 100.0 ±0.0 | 0.0 ±0.0 | 0 / 100 / 0 | 5.7 | 0.1 / 9.1 | 708 / 1661 | 7 / 11 |
| Random | Random | 1.0 ±1.4 | 0.0 ±0.0 | 99.0 ±1.4 | 4 / 2 / 0 | 10.3 | 0.2 / 0.0 | 641 / 643 | 5 / 5 |

## Per bot against the others (mirror games excluded)

| Bot | games | wins % | losses % | draws % | wins % as blue | wins % as orange |
|---|---|---|---|---|---|---|
| Nexto | 1200 | 42.2 ±2.8 | 50.3 ±2.8 | 7.5 ±1.5 | 41.2 ±3.9 | 43.2 ±4.0 |
| GalactusV2 | 1200 | 66.2 ±2.7 | 21.6 ±2.3 | 12.2 ±1.9 | 65.7 ±3.8 | 66.7 ±3.8 |
| ChronosV2 | 1200 | 77.8 ±2.4 | 14.4 ±2.0 | 7.8 ±1.5 | 76.8 ±3.4 | 78.8 ±3.3 |
| Random | 1200 | 0.0 ±0.0 | 99.8 ±0.2 | 0.2 ±0.2 | 0.0 ±0.0 | 0.0 ±0.0 |

## Side bias (mirror games: identical players, so blue and orange should win equally often)

| Bot | blue wins % | orange wins % | draws % |
|---|---|---|---|
| Nexto | 64.0 ±6.7 | 0.0 ±0.0 | 36.0 ±6.7 |
| GalactusV2 | 27.5 ±6.2 | 33.0 ±6.5 | 39.5 ±6.8 |
| ChronosV2 | 45.5 ±6.9 | 45.5 ±6.9 | 9.0 ±4.0 |
| Random | 1.0 ±1.4 | 0.0 ±0.0 | 99.0 ±1.4 |
