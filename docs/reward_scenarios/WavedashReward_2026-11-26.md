# WavedashReward audit: why a RocketGPU run farmed it (2026-11-26)

## Question and verdict

Question (user): a RocketGPU run with the same WavedashReward weight farmed it, the CPU engine version did not. Why?

**Verdict: the GPU engine is not the cause. The farming policy farms at the same rate on RocketSim v3 (CPU).** The reward pays a
repeatable **wall dash**, and a CPU run with that weight from a fresh start never existed.

Intent was not confirmed with the user; taken from the code: "pay the landing step after a flip began airborne" (a wavedash).

## The reward

`RewardMath: player.prev && player.isOnGround && player.prev->isFlipping && !player.prev->isOnGround` -> 1.0
(`CommonRewards.h`, `WavedashReward::Condition`). The GPU port is the same expression (`pulsar_rg/pulsar/rewards.py`).
Class: bare, one-sided, per event. No wrapper, cooldown, height, surface or speed condition. Pulsar3 weight: 12 (was 10).

## Which runs

| Run | Engine | Start | Weight | Wavedash payments per car decision |
|---|---|---|---|---|
| RocketGPU first run (`stage4`), tick 4, self-play only | GPU | fresh | 10 | about 10% at 0.66B (its own record) |
| RocketGPU `stage4c`, tick 8 | GPU | fresh, term from 0.64B | 1 | 0.01-0.02% through 2.0B |
| CPU comparison, tick 8 | v3 | fresh | 1 | 0.03-0.09% through 2.1B |
| Pulsar3, tick 4 | v3 | added at about 1.7B | 10, then 12 | 0.01-0.06% (4.0-5.7B) |

## Cross-engine test (the deciding one)

The `stage4` save at 659,030,016 steps, exported to Pulsar3's format, played on v3 (CPU), 128 1v1 arenas, tick 4, the training
reset mix, sampled actions (`tests/test_wavedash_audit.cpp`, `GGL_WAVEDASH_AUDIT=<folder>`):

| Policy on v3 CPU | Pays per decision | Flip starts per decision | Pays per flip | Where it pays |
|---|---|---|---|---|
| Farming GPU bot | **10.2%** (921,600 decisions) | 10.8% | 0.94 | 100% on wall or ramp (614,400) |
| Pulsar3 at the latest save | 0.012-0.017% | 0.91% | 0.014-0.018 | 46% floor, 53% wall/ramp, 1% ceiling |

The GPU-side figure is the run's own record; the policy was not replayed on RocketGPU for this audit.

## The move (traced)

The car drives on a side or back wall (up.z about 0, height about 850-1,150 uu) near top speed and repeats:

1. jump off the wall (1 decision);
2. wait 1-2 decisions in the air;
3. diagonal dodge (pitch and roll with jump);
4. next decision the wheels are back on the wall: flip over, jump and dodge refreshed, **paid**.

Most common period: 6 decisions (0.2 s, 67% of payments), then 7 (14%). In 99% of payments the flip was one decision old, so the
dodge ends on contact almost at once. At weight 10 one cycle pays 10, up to 50 per second: a goal's worth (150) every 3 s with the
ball untouched. The run's record (+1.03 reward per decision) matches 10% x 10.

## Why the CPU runs did not show it

- Pulsar3 got the term at about 1.7B steps, with an established policy that dodges 12 times less often and rarely lands a paying
  flip.
- The two fresh runs that never farmed (one per engine) used weight 1.
- So no fresh CPU run with weight 10 exists. That a fresh CPU run at weight 10 would find the loop is **likely but untested**:
  the engine pays it identically, but finding it depends on exploration. A difference between the engines in how easily early
  training reaches it is not ruled out; at weight 1 the GPU run showed less wavedashing than the CPU run.

## Scenario verdicts

| Case | Pays | Verdict |
|---|---|---|
| Flip in the air, wheels down next decision on the floor | yes | aligned (the intended wavedash) |
| Same on a wall or ramp, repeated every 6 decisions | yes, every time | **misaligned** as a bare bonus: income without ball play (a real mechanic, but unbounded) |
| Landing more than one decision after the flip ended | no | aligned |
| Ground flip that never leaves contact (`prev` on ground) | no | aligned |

## Farming probe

Exploitable: **yes**. Rate above. A bare term: the income stays against every opponent, and nets out only between equal copies.

## Options (not applied)

1. Floor only: also require `up.z > 0.9` and height under about 60 uu. Removes this loop; a floor wavedash loop is still open.
2. Cooldown of about 1-2 s per car: caps any loop at a fraction of today's rate.
3. Pay only for speed gained (as `RolvWavedashReward` does), nothing near top speed.
4. A small weight.

Options 1 and 2 together close the measured loop and bound the rest.

## Watch while it stays enabled (proposed)

`Reward Share %/N4RLGC14WavedashRewardE` and `Rewards/N4RLGC14WavedashRewardE`. Healthy: under about 0.1% of decisions and a
share under 2%. Suspicious: over 0.5% of decisions. Pulsar3 at 5.6B: 0.06% of decisions, share 0.2-0.5%, drifting up from 0.01%
at 4.0B.
