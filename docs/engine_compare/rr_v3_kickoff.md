# Round robin, sim 1v1 (kickoff suite)

N = 200 games per ordered pair, seed 20261105, tickSkip 8, actionDelay 7, game = one episode (goal, 10 s without a touch, or 90 s; the last two are draws). A is blue, B is orange. Percentages carry a 95% half-width (normal approximation). Speed in uu/s and boost 0-100 are per-car averages, touches are steps in which the car touched the ball.

| A (blue) | B (orange) | A wins % | B wins % | draws % | first touch A / B / both % | mean game s | touches A / B | speed A / B | boost A / B |
|---|---|---|---|---|---|---|---|---|---|
| Nexto | Nexto | 60.0 ±6.8 | 0.0 ±0.0 | 40.0 ±6.8 | 0 / 0 / 100 | 65.4 | 365.3 / 186.0 | 789 / 790 | 49 / 46 |
| Nexto | GalactusV2 | 12.5 ±4.6 | 68.5 ±6.4 | 19.0 ±5.4 | 0 / 64 / 36 | 48.6 | 48.0 / 345.9 | 1052 / 902 | 45 / 46 |
| Nexto | ChronosV2 | 14.0 ±4.8 | 82.5 ±5.3 | 3.5 ±2.5 | 59 / 22 / 20 | 26.2 | 41.9 / 127.5 | 1047 / 1008 | 35 / 37 |
| Nexto | Random | 100.0 ±0.0 | 0.0 ±0.0 | 0.0 ±0.0 | 100 / 0 / 0 | 6.9 | 20.4 / 0.1 | 1490 / 690 | 15 / 6 |
| GalactusV2 | Nexto | 74.0 ±6.1 | 7.5 ±3.7 | 18.5 ±5.4 | 60 / 0 / 40 | 50.2 | 356.8 / 50.1 | 909 / 1041 | 48 / 46 |
| GalactusV2 | GalactusV2 | 32.0 ±6.5 | 27.0 ±6.2 | 41.0 ±6.8 | 0 / 0 / 100 | 64.2 | 195.9 / 210.8 | 985 / 976 | 36 / 37 |
| GalactusV2 | ChronosV2 | 35.5 ±6.6 | 46.5 ±6.9 | 18.0 ±5.3 | 84 / 0 / 16 | 42.1 | 192.8 / 70.8 | 1023 / 1109 | 41 / 38 |
| GalactusV2 | Random | 100.0 ±0.0 | 0.0 ±0.0 | 0.0 ±0.0 | 100 / 0 / 0 | 13.1 | 104.3 / 0.0 | 1048 / 629 | 31 / 4 |
| ChronosV2 | Nexto | 86.5 ±4.7 | 8.0 ±3.8 | 5.5 ±3.2 | 23 / 58 / 19 | 28.6 | 134.0 / 46.6 | 1049 / 1081 | 40 / 37 |
| ChronosV2 | GalactusV2 | 54.5 ±6.9 | 29.0 ±6.3 | 16.5 ±5.1 | 0 / 84 / 16 | 42.9 | 72.6 / 200.7 | 1088 / 986 | 37 / 41 |
| ChronosV2 | ChronosV2 | 44.0 ±6.9 | 44.5 ±6.9 | 11.5 ±4.4 | 2 / 0 / 98 | 43.6 | 130.6 / 127.4 | 1033 / 1037 | 38 / 38 |
| ChronosV2 | Random | 100.0 ±0.0 | 0.0 ±0.0 | 0.0 ±0.0 | 100 / 0 / 0 | 5.7 | 9.1 / 0.0 | 1661 / 698 | 11 / 7 |
| Random | Nexto | 0.0 ±0.0 | 100.0 ±0.0 | 0.0 ±0.0 | 0 / 100 / 0 | 6.5 | 0.1 / 14.8 | 741 / 1545 | 7 / 12 |
| Random | GalactusV2 | 0.0 ±0.0 | 99.5 ±1.0 | 0.5 ±1.0 | 0 / 100 / 0 | 12.8 | 0.0 / 97.9 | 633 / 1069 | 5 / 30 |
| Random | ChronosV2 | 0.0 ±0.0 | 100.0 ±0.0 | 0.0 ±0.0 | 0 / 100 / 0 | 5.8 | 0.1 / 8.8 | 711 / 1655 | 7 / 10 |
| Random | Random | 0.5 ±1.0 | 0.0 ±0.0 | 99.5 ±1.0 | 4 / 4 / 0 | 10.5 | 0.1 / 0.1 | 639 / 657 | 5 / 5 |

## Per bot against the others (mirror games excluded)

| Bot | games | wins % | losses % | draws % | wins % as blue | wins % as orange |
|---|---|---|---|---|---|---|
| Nexto | 1200 | 40.3 ±2.8 | 51.9 ±2.8 | 7.8 ±1.5 | 42.2 ±4.0 | 38.5 ±3.9 |
| GalactusV2 | 1200 | 67.8 ±2.6 | 20.2 ±2.3 | 12.1 ±1.8 | 69.8 ±3.7 | 65.7 ±3.8 |
| ChronosV2 | 1200 | 78.3 ±2.3 | 14.4 ±2.0 | 7.2 ±1.5 | 80.3 ±3.2 | 76.3 ±3.4 |
| Random | 1200 | 0.0 ±0.0 | 99.9 ±0.2 | 0.1 ±0.2 | 0.0 ±0.0 | 0.0 ±0.0 |

## Side bias (mirror games: identical players, so blue and orange should win equally often)

| Bot | blue wins % | orange wins % | draws % |
|---|---|---|---|
| Nexto | 60.0 ±6.8 | 0.0 ±0.0 | 40.0 ±6.8 |
| GalactusV2 | 32.0 ±6.5 | 27.0 ±6.2 | 41.0 ±6.8 |
| ChronosV2 | 44.0 ±6.9 | 44.5 ±6.9 | 11.5 ±4.4 |
| Random | 0.5 ±1.0 | 0.0 ±0.0 | 99.5 ±1.0 |
