> Audit date 2026-11-01. Method: hand trace of the real header + compiled tests in `tests/test_reward_audit_movement.cpp`
> (14 pass). Fixture: scripted single-car states, deltaTime = 8/120. **Intent below was inferred from the class header
> and the v3 change log, not confirmed with Nathan - correct it and re-read the verdicts.** Not edited: the reward,
> `BuildRewards()`. Nothing here was run in the engine or in training.

# SupersonicReward

**RETIRED 2026-11-01 (Nathan): its job is already done by `CarEnergyPotentialReward`. The header stays in the tree for provenance; it is not registered anywhere.**

**Intent (inferred):** reward holding supersonic speed, with a bonus for sustaining it.
**Class:** individual, continuous, one-sided positive income, NOT wrapped in v3 either (`new SupersonicReward(), 2.0f` in PHASE_3_GOLD).
**Registered:** commented out in `BuildRewards()` at 2.0.

## Scenarios (all via `Supersonic_*` tests)

| tier | setup | traced value | verdict |
|---|---|---|---|
| easy | speed 2199 vs 2200 | 0 vs >0 (threshold is 2200, the header says "2300+") | ALIGNED with code, header stale |
| easy | first supersonic step | dur = 1/15; base 0.5*dt = 0.03333 + bonus min(dt/5,1)*2*dt = 0.00178 -> **0.03511** | ALIGNED |
| middle | held 5 s+ | (0.5 + 2.0) * dt = 0.1667/step = **2.5 per second** | ALIGNED |
| middle | one step at 2100 then back | dur resets; next step 0.03511 | ALIGNED |
| hard | demoed car still carrying velocity | still paid; the class never reads `isDemoed` | AMBIGUOUS (probably unintended) |
| hard | both cars supersonic, bare vs `ZeroSumReward` | bare: both paid (stack no longer sums to 0); wrapped: sums to 0 | MISALIGNED with the stack invariant when bare |

## Farming
- **Income stream, not a loop.** Nothing to farm cleverly: speed itself is the trigger. 30 s at supersonic = 70 raw (hand integral 15 + 55; test agrees) = **140 at weight 2.0, almost one Goal (150) per 30 s**, unwrapped.
- Scale check against the live stack (a **[6M]** figure that does not transfer): `Average Step Reward` was 0.294. At weight 2.0 this reward alone is 0.333 per supersonic step.
- **What limits it:** ground supersonic needs boost (100 per ~3 s). I did not measure how much of an episode a bot can stay supersonic (air and wall travel may keep speed cheaply) - UNVERIFIED.
- **Opponent types:** unwrapped it pays both players, so in mirror play both farm it and the margin is unchanged, but it still inflates every value target; against a slower opponent the faster side nets income.

## Verdict
Does what it says, and that is the problem: a bare positive per-second term breaks the zero-sum invariant by itself and is large next to everything else. If wanted: wrap in `ZeroSumReward`, or move to a PBRS/energy form (the live `CarEnergyPotentialReward` already prices speed/boost - check overlap first), and re-derive the weight (v3's 2.0 was tuned in a different stack). Pointer: `SupersonicReward.h`, `GetReward`.

## Pre-registration (if it is ever enabled)
Health: supersonic time fraction (the class's `GetAndResetMetrics()` is not read by anything in pulsar2 yet - wire it first); suspicious if it rises toward the boost-limited ceiling while Goals/ep or Elo do not. Behavior check in the viewer: long straight supersonic runs away from the ball. Revert if `Player/Ball Touch Ratio` falls or Rating drops over the window.
