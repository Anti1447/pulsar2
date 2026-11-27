# WavedashGainReward (productive wavedash), 2026-11-26. enabled in Pulsar3 2026-11-26 evening

User intent: wavedashes are worth farming when they move the bot productively. "Speed gained in any direction, slight bonus (25%)
if towards the ball."

Class `RewardMath` per event, bare, one-sided (`Rewards/WavedashGainReward.h`). Paid on a dodge's landing step:
`clip((|v_land| - speed expected from the best energy of the previous 1 s) / 500, 0, 1) x (1 + 0.25 x aim at ball)`.
Energy is speed and height together, so falling pays nothing; the 1 s window makes brake-then-dash pay nothing. No cooldown.

## Scenarios (executed, `tests/test_wavedash_gain.cpp`, 7 passed)

| Case | Result |
|---|---|
| +400 uu/s dodge on the floor | about 0.8 on the landing step only |
| Large gain; at the speed limit; losing speed | 1; 0; 0 |
| Brake 0.2 s then dash back, ten times | 0 |
| Same after more than 1 s slow | paid |
| Three chained gaining dashes | each paid |
| Speed from falling; same speed higher up a wall | 0; paid |
| Aim: away / sideways / at the ball | base; base; base x 1.25 |
| Speed gain without a dodge; first state of an episode | 0 |

## On real play (v3 CPU, tick 4, 768,000 car decisions each)

| Policy | Old reward: pays per decision | New: pays per decision | New: mean value | New: value per decision |
|---|---|---|---|---|
| Wall-dash farming bot (`stage4`, 659M) | 10.1% (value 0.101) | 2.5% | 0.14 | 0.0034 (about 1/30 of the old) |
| Pulsar3, latest save | 0.016% | 0.0048% | 0.50 | 0.000024 |

## Farming probes

- Brake-dash cycle: closed by the window (tested). A cycle slower than 1 s can still be paid, at most one payment per such cycle.
- Wall-dash loop: still paid where each dash adds speed over the last second; at weight 20 the farming bot's play would earn about
  0.07 per decision, a goal's worth (150) every 70 s or so at 30 decisions per second.
- Not tested: a policy trained against this reward. Boost spent during a dodge counts as gain.
- Bare term: income against every opponent.

## Enabled as proposed (user-approved)

Replace `WavedashReward` (12) with this at **weight 20** in Pulsar3; `RolvWavedashReward` (5) unchanged.
Watch `Rewards/N4RLGC18WavedashGainRewardE`, its `Reward Share %`, `Player/Speed`, `Player/Ball Touch Ratio`. Healthy: share under
5% with speed steady or rising. Suspicious: share over 15%, or touch ratio falling while this share rises. Revert: restore the
`WavedashReward` line, rebuild, restart.
