# GCO reset mix: diagnosis and proposals (2026-11-16)

Status: **P1 APPLIED 2026-11-17 (user-approved), in `MakeEnv`, shared by main and `--gco`; built into `build/`, live at the
next trainer start.** P2 and P3 also applied 2026-11-17 (user-requested; setter code, see section 4). Branch `gco-goalcritic`. The critic-LR test that had to finish first
ended NEUTRAL and was reverted (`docs/GCO_CRITIC_LR_TEST.md`), so P1 is the only lever in flight.

## 0. Summary

1. **No setter is dead.** In 8,064 self-play episodes, 14 ended in the 20 s no-touch timeout (0.2%). Every setter, in
   every mode, ends in a goal or runs long. The `-0.5` timeout penalty almost never fires at this skill level.
2. **The setters are nearly interchangeable after the first seconds.** Episodes last about 30 s (1v1), 55 s (2v2) and
   65 s (3v3); only 14-46% have a goal within 20 s (air-carry excepted). The reset decides the first few seconds, then
   it is ordinary play. That is why the states visited in training match self-play matches almost bin for bin
   (section 3), and why each setter's share of rows and of return variance equals its reset weight (section 1.2).
   **The mix is a weak lever on signal per step.** Critic EV is low in every cell (0.19-0.45) and tracks the mode, not
   the setter: a reset change will not move `GCO/Critic Explained Variance`.
3. **The one real coverage gap is the kickoff.** The kickoff phase is 1.1% / 0.4% / 0.35% of training rows (1v1 / 2v2 /
   3v3) against 9.2% / 3.2% / 2.3% of match time, 7-9x under. The bot's kickoff is measurably weak: 0 first touches in
   96 gauntlet games against each of the four external bots, and 79 goals for against 694 conceded within 5 s of a
   kickoff in training serves.
4. **Three setter defects** were found by the new test (section 2). The largest: in AirPlay's flip-reset mode the
   second car is lifted onto the ball, so 37% of those resets start with a car inside the ball.

Recommended first change (section 5): Kickoff 0.10 -> 0.20, AirPlay 0.30 -> 0.20. One line each, revert is the old
two weights.

## 1. Per-setter diagnostics

Method: `tools/reset_diag` (build target `ResetDiag`) plays checkpoint `checkpoints_gco_6.1/58050057492` against itself on
CPU with sampled actions, in the GCO env (tickSkip 8, actionDelay 7, 278-float obs, `NoTouchCondition(20, truncate=false)`,
goal terminal). Each arena has one fixed setter cell and team size; 336 episodes per cell and mode. An episode still
running at 120 s is cut and counted as "cap". V is `V_real` scaled by the return std (0.554); EV is against the realised
discounted return (gamma 0.9969), capped episodes excluded.

### 1.1 Outcomes

| setter | mode | blue / orange / timeout / cap % | goal within 20 s % | mean s | first touch s | mean abs V(s0) | higher-V(s0) team scores % | critic EV |
|---|---|---|---|---|---|---|---|---|
| BallNearCar | 1v1 | 45 / 54 / 0.3 / 1 | 46 | 28 | 1.1 | 0.19 | 65 | 0.37 |
| BallNearCar | 2v2 | 44 / 43 / 0.3 / 12 | 20 | 56 | 0.9 | 0.10 | 54 | 0.23 |
| BallNearCar | 3v3 | 36 / 39 / 0.0 / 24 | 19 | 66 | 0.9 | 0.07 | 57 | 0.21 |
| AirDrill_solo | 1v1 | 49 / 49 / 0.0 / 1 | 46 | 30 | 1.0 | 0.12 | 54 | 0.33 |
| AirDrill_solo | 2v2 | 45 / 40 / 0.0 / 15 | 18 | 58 | 1.0 | 0.08 | 50 | 0.21 |
| AirDrill_solo | 3v3 | 37 / 36 / 0.0 / 27 | 16 | 69 | 1.0 | 0.07 | 54 | 0.20 |
| AirDrill_contest | 1v1 | 47 / 49 / 1.2 / 2 | 41 | 33 | 0.9 | 0.14 | 52 | 0.34 |
| AirDrill_contest | 2v2 | 45 / 39 / 0.0 / 15 | 27 | 56 | 0.7 | 0.08 | 54 | 0.24 |
| AirDrill_contest | 3v3 | 38 / 35 / 0.0 / 28 | 16 | 70 | 0.6 | 0.07 | 60 | 0.23 |
| AirPlay_wall | 1v1 | 52 / 47 / 0.0 / 1 | 38 | 32 | 3.3 | 0.11 | 53 | 0.35 |
| AirPlay_wall | 2v2 | 45 / 38 / 0.0 / 17 | 23 | 57 | 1.5 | 0.11 | 53 | 0.22 |
| AirPlay_wall | 3v3 | 42 / 34 / 0.0 / 24 | 15 | 68 | 1.1 | 0.09 | 50 | 0.19 |
| AirPlay_flip | 1v1 | 47 / 50 / 0.3 / 2 | 40 | 33 | 0.4 | 0.17 | 55 | 0.32 |
| AirPlay_flip | 2v2 | 38 / 45 / 0.0 / 18 | 22 | 58 | 0.4 | 0.07 | 54 | 0.23 |
| AirPlay_flip | 3v3 | 40 / 36 / 0.0 / 24 | 20 | 64 | 0.4 | 0.05 | 49 | 0.21 |
| AirPlay_carry | 1v1 | 60 / 40 / 0.0 / 0 | 70 | 17 | 0.5 | 0.46 | 76 | 0.45 |
| AirPlay_carry | 2v2 | 53 / 35 / 0.0 / 12 | 41 | 45 | 0.5 | 0.39 | 68 | 0.26 |
| AirPlay_carry | 3v3 | 48 / 32 / 0.0 / 20 | 39 | 51 | 0.6 | 0.29 | 70 | 0.31 |
| Kickoff | 1v1 | 51 / 47 / 1.2 / 1 | 30 | 37 | 3.2 | 0.06 | 50 | 0.30 |
| Kickoff | 2v2 | 44 / 43 / 0.0 / 14 | 20 | 58 | 2.4 | 0.06 | 54 | 0.21 |
| Kickoff | 3v3 | 38 / 33 / 0.0 / 29 | 14 | 72 | 2.3 | 0.04 | 50 | 0.23 |
| Random | 1v1 | 45 / 53 / 0.9 / 1 | 44 | 30 | 5.5 | 0.24 | 64 | 0.36 |
| Random | 2v2 | 41 / 45 / 0.0 / 14 | 31 | 53 | 4.0 | 0.20 | 61 | 0.26 |
| Random | 3v3 | 38 / 38 / 0.0 / 24 | 23 | 63 | 3.5 | 0.18 | 53 | 0.25 |

Percentages carry about +-5 points (95%). "blue / orange" is who scored.

Reading:
- **Timeouts:** 14 of 8,064. No setter produces dead signal.
- **Near-deterministic states:** only AirPlay carry stands out. Mean |V(s0)| is 0.29-0.46 against 0.04-0.24 elsewhere,
  and 70% of its 1v1 episodes have a goal within 20 s. It is the most decided cell, not a fully decided one: the
  higher-valued team scores 68-76% of the time.
- **Kickoff is the least informative start for the critic:** mean |V(s0)| 0.04-0.06 and the higher-valued team scores
  50-54% of the time. Expected for a symmetric start; it means kickoff rows carry signal only through what follows.
- **Critic EV per setter:** 0.30-0.45 in 1v1, 0.19-0.31 in 2v2 and 3v3. Pooled over the mix: 0.35 / 0.23 / 0.23. The
  trainer's panel reads 0.30 over all modes. EV is a horizon and mode effect.

### 1.2 Share of rows and of return variance (live weights)

| setter | reset weight | 1v1 rows / sum G^2 % | 2v2 rows / sum G^2 % | 3v3 rows / sum G^2 % |
|---|---|---|---|---|
| BallNearCar | 0.200 | 19 / 19 | 20 / 20 | 20 / 20 |
| AirDrill solo | 0.100 | 10 / 10 | 11 / 10 | 11 / 10 |
| AirDrill contested | 0.100 | 11 / 11 | 10 / 10 | 11 / 10 |
| AirPlay wall | 0.090 | 10 / 10 | 9 / 10 | 9 / 9 |
| AirPlay flip-reset | 0.105 | 12 / 11 | 11 / 11 | 10 / 11 |
| AirPlay carry | 0.105 | 6 / 7 | 9 / 9 | 8 / 9 |
| Kickoff | 0.100 | 12 / 11 | 11 / 10 | 11 / 11 |
| Random | 0.200 | 20 / 20 | 19 / 19 | 19 / 20 |

No setter is over- or under-weighted in the batch relative to its reset weight, except carry (short episodes).

## 2. State validity (`tests/test_reset_mix_validity.cpp`)

4,000 resets per setter cell and mode. Kickoff and Random have no hard violations. The test passes on the setters as
they are, with these defects pinned as upper bounds (tighten to 0 when fixed):

| defect | where | rate | effect |
|---|---|---|---|
| Car beyond a 45-degree corner wall | supports of BallNearCar, AirDrill, AirPlay; AirPlay wall-mode cars | 0.3-1.8% of those resets (0 in BallNearCar 1v1) | Low. The car can drive back in (probe: back at midfield within 3 s). The placement checks the side and back walls only, not `abs(x)+abs(y)`. |
| Second air-player lifted onto the ball | AirPlay flip-reset (10.5% of resets) | 37% start with a car inside the ball; orange starts closer in 95% | Medium. The ball gets a ~300 uu/s push in the first 8 ticks. Orange takes the first touch in 741 of 1,008 episodes against 251 for blue. The drill is "flip-reset ready" for one car only. |
| Same lift, carry mode | AirPlay carry (10.5%) | blue starts closer in 67%; blue scores 60% (+-3.3) of goals | Medium. The two cars are placed on the same spot and the second is moved 350 uu up. |
| Setup car is always blue | AirPlay wall (9%) | blue first touch 948 of 1,008; blue scores 54% (+-3.3) | By design. Harmless in mirror self-play (the obs is team-canonical). |

RandomState has rare overlaps (0.03-0.4% of resets), expected from independent placement. Team canonicalization and
team-size handling are otherwise correct: BallNearCar, AirDrill, Kickoff and Random are colour-fair within 5 sigma, and
2v2/3v3 put exactly one contester per team with supports goal-side.

## 3. Coverage against 5-minute matches

Match side: 24 arenas per mode, 300 s each, kickoff resets, goal-only terminals, same checkpoint (6 hours of game).
Goals come every 35 s (1v1), 74 s (2v2) and 99 s (3v3). 20 s no-touch stalls: 1 in 6 hours.

Across ball third, ball height, wall proximity, car boost, car speed, air/ground, car position and car-ball distance,
the states visited in training are within 0.8-1.3x of match frequency in all three modes. Full tables:
`python3 tools/reset_diag/analyze.py <outDir>`. The exceptions:

| region | training % (1v1 / 2v2 / 3v3) | match % | ratio |
|---|---|---|---|
| Ball nearly still (< 300 uu/s): the kickoff wait and dead balls | 5.8 / 2.0 / 1.3 | 14.7 / 5.4 / 3.6 | 0.36-0.40 (**under**) |
| Kickoff phase (reset to first touch) | 1.1 / 0.43 / 0.35 | 9.2 / 3.2 / 2.3 | 0.12-0.15 (**under**) |
| Car in the air above 300 uu, 1v1 | 6.2 | 3.2 | 1.9 (**over**) |
| Ball above 1200 uu, 1v1 | 5.8 | 4.1 | 1.4 (over) |
| Ball in midfield third, 1v1 | 31.0 | 42.5 | 0.73 |

The over-visited air states are the AirDrill and AirPlay starts (33.9% of 1v1 reset states have the car above 300 uu).
In 2v2 and 3v3 they wash out. No region that matches visit is absent from training.

Kickoff evidence from the run itself (`RUNNING_STATS.json` at 58.05B, `GAUNTLET_LOG.jsonl`):

| opponent | training serves: we touch first % | bot first % | goals within 5 s, for / against | 1v1 gauntlet first touch, us / 96 |
|---|---|---|---|---|
| Nexto | 12.7 | 82.1 | 1 / 98 | 0 |
| GalactusV2 | 1.5 | 80.0 | 11 / 72 | 0 |
| ChronosV2 | 7.2 | 79.8 | 67 / 363 | 0 |
| Dice | 0.1 | 81.1 | 0 / 161 | 0 |

In self-play the 1v1 kickoff reaches the ball in 3.2 s (2.4 s in 2v2, 2.3 s in 3v3). Nobody in the mirror punishes a
slow kickoff, and the external bots, which do, are a small share of a start that is itself 10% of resets.

## 4. Proposals (one lever at a time; order = expected effect)

Shared baseline (58.05B, before the critic-LR restart): Rating/1v1 2497-2518, Rating/2v2 975-990, Rating/3v3 1400-1420,
Ref/Oldest Share 0.985, Ref/Share 0.77, `GCO/Critic Explained Variance` median 0.298 (IQR 0.263-0.347).

Guard, for all three: **this run has no automatic update-damage guard** (the rating latch was removed 2026-07-25; only
the boot sanity probe is automatic). The trip conditions below are read from the panels by hand, and the revert is a
config edit plus restart. Building a guard would be new machinery and is not proposed here.

### P1. Kickoff 0.10 -> 0.20, taken from AirPlay 0.30 -> 0.20 (weight change)

a. Conviction: section 3. Kickoff phase 7-9x under-represented; 0/96 gauntlet first touches against every bot; 79 goals
   for and 694 against within 5 s of a kickoff. Donor: AirPlay is the newest and largest slice, holds the most decided
   cell (carry) and the defective one (flip-reset), and 0.20 was its weight until 2026-11-12.
b. Success, read over 500M steps after the restart:
   - `<Bot>/Kickoff Bot First Touch %` (baseline 80-82 for all four): below 70 for at least two bots.
   - 1v1 gauntlet `first_touch_us` (baseline 0/96 each): at least 10/96 against one bot.
   - Offline, `ResetDiag` match phase on the newest checkpoint: 1v1 kickoff first touch below 2.9 s (baseline 3.23 s).
   - Health: Rating/1v1, 2v2, 3v3 and Ref/Oldest Share within the noise band of the baseline (+-50); EV median not
     below 0.27; gauntlet win % against each bot not below its last-five mean.
   Expected size: kickoff rows roughly double (1.1% -> 2.2% in 1v1). A null result at 500M means exposure is not the
   limiter, and the next step is the external-bot share on kickoff starts, not more weight.
c. Trip: Rating/1v1 more than 50 below baseline for 100M steps, or Ref/Oldest Share below 0.97, or the offline kickoff
   test below 8/10 touches. Aerial health: `FlipReset/Total Any` per iteration falling more than a third below its
   pre-change mean over 200M steps also trips (capture that mean before the restart).
d. Revert: `{ new AirPlayState(), 0.30f }`, `{ new KickoffState(), 0.10f }`.

### P2. AirPlay: give the second air-player its own spot (logic change inside a setter)

a. Conviction: section 2. 37% of flip-reset starts have a car inside the ball; orange first touch 741 vs 251; carry
   scores 60/40 for blue. About 21% of all resets are affected.
b. Change: place the second car on the opposite side of the ball's vertical axis (flip-reset) or beside the first at
   the same height (carry), with rejection sampling instead of the 350 uu lift. Success: the test's AirPlay bounds go
   to 0 overlap and both modes pass the colour-fairness check; in `ResetDiag`, flip-reset first touch within 45-55%
   blue and carry goals within 46-54% blue. Health as in P1.
c. Trip: the health rows of P1.
d. Revert: `git revert` of the one setter commit (no weights change).
e. **Applied 2026-11-17** (`AirPlayState.h`): the two air-players take mirrored slots. Flip-reset: below the ball on opposite
   sides of its vertical axis (shared random bearing, horizontal offset 180-280 each, was one shared +-200 box). Carry: on
   opposite lateral sides, 180-260 each. The 350 uu lift is gone. Test, 4000 resets per mode: overlap 0 in both modes,
   blue-closer 0.49-0.51, colour-fairness check now on for both. The `ResetDiag` first-touch and goal-share criteria are
   NOT yet run.

### P3. Reject corner placements in the support ring (validity fix)

a. Conviction: section 2. 0.3-1.8% of BallNearCar, AirDrill and AirPlay resets put a car beyond a corner wall.
b. Change: add `abs(x) + abs(y) <= 7900` to the three placement loops (RandomState already does this). Success: the
   test's corner bounds go to 0. No behavioural criterion: the effect is too small to read in any panel.
c. Trip: none beyond the test. d. Revert: `git revert`.
e. **Applied 2026-11-17**: `InsideCorners` / `ClampToCorners` (`StateSetter.h`, bound 7900 like RandomState) in the
   rejection test and the fallback clamp of BallNearCar, AirDrill (climber and support) and AirPlay (air and support).
   Test: 0 corner escapes in every cell and mode; all `maxHard` bounds are now 0.

Not proposed: a new setter. Coverage shows no match region missing from training, and a new start would be diluted the
same way the existing ones are. Not proposed: any change to the timeout or its penalty; it fires in 0.2% of episodes.

## 5. First change (P1), applied 2026-11-17

```diff
--- a/src/ExampleMain.cpp
+++ b/src/ExampleMain.cpp
@@ MakeEnv()
 		// Of ALL resets now: wall 9%, flip-reset-ready ~10.5%, air-carry ~10.5%. Revert: 0.20 / Random 0.25.
-		{ new AirPlayState(), 0.30f },
-		{ new KickoffState(), 0.10f },
+		// 2026-11-17 (docs/GCO_RESET_MIX.md P1): AirPlayState 0.30 -> 0.20, KickoffState 0.10 -> 0.20. The kickoff phase was
+		// 1.1% / 0.4% / 0.35% of training rows (1v1 / 2v2 / 3v3) against 9.2% / 3.2% / 2.3% of match time, and the bot took
+		// 0 of 96 first touches against every external bot in the 1v1 gauntlet. Of ALL resets now: wall 6%, flip-reset-ready 7%,
+		// air-carry 7%. Revert: AirPlay 0.30, Kickoff 0.10.
+		{ new AirPlayState(), 0.20f },
+		{ new KickoffState(), 0.20f },
```

This block is shared with main (`MakeEnv` builds one mix for main and GCO). If the change should apply under `--gco`
only, the two weights need a `g_GCOMode` switch; say so and the diff changes accordingly.

Applied as above (approved without the `g_GCOMode` switch, so main changes too at its next start). `cmake --build build`
succeeded; the trainer was not restarted. The 500M-step read window in P1.b starts at the first restart on this binary;
record that step here: 58,368,356,512 (first iteration after the 2026-11-17 09:02:49 start from checkpoint 58,367,676,908; P1+P2+P3 live, criticLR 1.5e-4, boot sanity probe 3/3). Read window ends at about 58.87B.

## 6. Limits of this measurement

- Mirror self-play only. Training is about 40% against old versions and external bots, where kickoffs and first
  touches go differently (section 3's kickoff table is from the run's own counters for that reason).
- One checkpoint (58.05B), 336 episodes per cell and mode: single-cell differences under about 7 points are noise.
- The 120 s cap cut 14% of episodes (0-2% in 1v1, 12-29% in team modes), so team-mode mean lengths are underestimates.
- V was scaled by the checkpoint's return std (0.554); EV depends on that scale, the correlations (0.45-0.67) do not.
