# Reward audit overview: the 14 active rewards, before and after

Written 2026-11-03. Checklist item 2 (audit) and the fixes that came out of it (item 3) for the 14 rewards active in `BuildRewards()` in `src/ExampleMain.cpp`. This is the short summary per reward. The full numbers, hand calculations and mutation results are in `docs/REWARD_CHANGE_LOG.md` (one entry per audit and per fix); live weights are in `docs/ENABLED_INVENTORY.md`; the why of the stack is `docs/REWARD_SHAPING.md`.

How each reward was audited: (1) read the code and derive expected values by hand; (2) a compiled test per claim (`tests/test_reward_audit_*.cpp`, fabricated states); (3) an engine pack of scenarios run through the RocketSim engine (`tools/reward_lab/packs/<name>/`); (4) mutation checks: break the code on purpose and confirm a test or scenario fails. Nothing here counts as verified until you sign the packs (last section).

## Scoreboard

| Reward | Weight | Wrap | Audit result | Changed? | Tests / pack scenarios |
|---|---|---|---|---|---|
| Goal | 150 | none | no issue | no | pack 6 |
| AerialTouch | 120 | ZeroSum 0.3 | no defect, 2 notes | no | 14 / 5 |
| BallToGoal | 75 | none (already zero-sum) | no defect, 3 findings | no | 13 / 5 |
| AirIntercept | 75 | ZeroSum 0.3 | no defect, 3 findings | no | 12 / 3 |
| CarEnergy | 75 | ZeroSum 0.3 | no defect, magnitude question open | no | 12 / 4 |
| WallJumpToBall | 60 -> 30 | ZeroSum 0.3 | **defect: could never fire** | **fixed**, weight halved | 15 / 4 |
| ConsecutiveAirTouch | 40 | ZeroSum 0.3 | **5 findings + floor-ball chain** | **reworked (3 fixes)** | 21 / 4 |
| Demo | 37.5 | ZeroSum 0.3 | logic fine, 1 naming issue | name only | 7 / 5 |
| OpposedSave | 25 | ZeroSum 0.3 | **engine shot too narrow**, 3 more findings | threshold fixed, rest open | 11 / 2 |
| TouchAccel | 10 | ZeroSum 0.3 | **5 findings** | **reworked (3 fixes)** | 17 / 7 |
| GuardedPickupBoost | 6 | ZeroSum 0.3 | no defect, 3 notes | no | 16 / 8 |
| BallProximity | 4 | ZeroSum 0.3 | no defect, then **2v2/3v3 gap** | **new team potential** | 22 / 7 |
| TeamPressure | 0.15 | ZeroSum 0.3 | **1 finding + hard cliffs** | **smoothed** | 16 / 9 |
| TimeCost | 0.01 | none (must stay unwrapped) | no defect, 4 notes | no | 6 / 3 |

Six rewards got a logic change (WallJumpToBall, ConsecutiveAirTouch, OpposedSave via the engine threshold, TouchAccel, BallProximity, TeamPressure), Demo got a rename, and seven were confirmed correct as written (Goal, AerialTouch, BallToGoal, AirIntercept, CarEnergy, GuardedPickup, TimeCost). KickoffRace was the 15th active reward and is now commented out (appendix).

State at the last commit (`36e5907`, pushed to `fork`): GigaLearnTests 453/453, Lab suite 92/92, all 18 packs pass, all 18 UNSIGNED.

---

## 1. Goal (150)

**Before.** +1 to the scoring team, -1 to the conceding team; the heaviest reward and the only fully shared signal.
**Issues.** None. Paid once, team-wide, right sign, nothing for a miss.
**After.** Unchanged. Pack: 2v2 score, the same at weight 150, own goal reversed, wide of the post, over the bar, rolls short (all misses pay 0). Mutations (scale, inverted team test, dropped goal gate) all caught. Not covered: farming after a goal (the Lab ends the episode on a goal).

## 2. AerialTouch (120)

**Before.** Pays a touch by height AND time in the air, once per 13 steps (cooldown), never from the ground or a floor-level ball.
**Issues.** No defect. (1) A long high flight pays a second small touch (0.05) that the hand calculation did not model. (2) The 12-step cooldown (0.8 s) is in steps, not seconds, so a tickSkip change means re-deriving it.
**After.** Unchanged. Formula checked: full credit 1.0, short flight 0.2, `heightFrac` is a MIN not a product, 150 uu floor exact, cooldown exact. Engine: a hit 0.27 s into a flight pays 0.152; a 1.87 s flight on a high ball pays exactly 1.0; ground and floor touches pay 0.

## 3. BallToGoalPotential (75)

**Before.** A true potential on ball position toward the goal (the proven core of the 1132-Elo run).
**Issues.** No defect. (1) The potential at the END state is not zeroed, so a goal step keeps about +9 weighted on top of Goal 150, and a timeout keeps the potential of wherever the ball sits. (2) The discount leak (gamma-1)*Phi is -0.047 to -0.074 weighted per step with the ball deep in the attacking half; harmless only because gamma equals the learner's GAE gamma (wiring verified). (3) One end-to-end carry is worth 74.6 weighted, half a Goal.
**After.** Unchanged. Antisymmetric over 300 random moves (zero-sum with no wrapper), telescopes, a loop never pays positive. Do not wrap it in ZeroSum (that doubles it).

## 4. AirInterceptPotential (75)

**Before.** Potential on the climb toward a high ball: pays the approach, nothing for a grounded car.
**Issues.** No defect. (1) The potential reads the BALL, so when a touch knocks the ball away the car gives back part of its climb credit (AerialTouch pays the touch, AirIntercept refunds part of the approach). (2) A resting grounded car is not exactly 0 (a few 1e-6). (3) An engine quirk found while writing the pack (see "Engine facts").
**After.** Unchanged. A 0.53 s ballistic climb pays +18.2 weighted (hand +18.25); a grounded car pays under 1e-5; a hop and landing nets about 0.

## 5. CarEnergyPotential (75)

**Before.** Potential on kinetic energy, height and boost; raised 15 -> 75 earlier without being measured.
**Issues.** No defect; the open point is magnitude. A big-pad pickup from empty is worth +73 weighted (half a Goal); the potential reaches 2.5 (187 weighted, above a Goal); holding a full tank costs -3.5 weighted per second (the discount leak). The loop is closed (burning the tank pays back), so it is a magnitude question, not an exploit.
**After.** Unchanged. **Still open, needs a run:** the credit share of this term at 75 (compare with BallToGoal 75 and AerialTouch 120 in the first 100M steps of 6.0 before deciding to keep 75).

## 6. WallJumpToBall (60 -> 30)

**Before.** A latch opens when the car is on a wall; leaving the wall near the ball pays `gamma * exp(-d/1410)`. `OnWall()` required `isOnGround && worldContact.hasContact && |contactNormal.z| < 0.7071`.
**Issues.** **DEFECT.** The engine sets `hasContact` only when the car's BODY hits the world, not its wheels. A car riding a wall on its wheels never reported contact: resting on a wall for 20 steps gave contact on 0 of 20; a 40-step wall drive gave OnWall true on 1 step, never up the wall; a real wall exit 655 uu from the ball paid 0 instead of the hand value +0.627. The term could (almost) never fire, which is the real reason it measured 0.064% of credit density.
**After.** `OnWall()` is now `isOnGround && |rotMat.up.z| < 0.7071` (the car's roof direction). The same exit pays 0.6265 (+37.6 weighted); an exit away from the ball 0.0066; a floor jump and a wall slide 0; run net -0.059. Weight halved 60 -> 30 because one exit near the ball is now worth about +38. Commits `b84ea43`, `d11cfd2`, `72acef4`. **Watch in the run:** its share of weighted credit; if above 2% in the first 200M steps, lower it further; if near 0 with `Player/Wall Drive Frac` above 0, check the detector in RocketSimVis.

## 7. ConsecutiveAirTouch (40)

**Before.** A chain of air touches pays an exact potential `Phi(n) = 1 - exp(-(n-1)/2)`; landing refunds it.
**Issues (audit `af1c5b9`).** (1) A "touch" is a contact STEP, not a hit: a level push registered 8-20 consecutive steps, so a single push-through earned almost the full +38 while airborne. (2) No time limit between touches. (3) A demolition mid-chain kept the credit. (4) A terminal (goal, timeout) kept the credit. (5) Holding a chain bleeds about -1.2 per second. Also found: any airborne contact counted, so a hop into a ball resting on the floor built a chain (+25 to +34 weighted).
**After (`9a99ef6`, `e79d726`, `5db6a58`).** Each touch now adds a credit to an EFFECTIVE streak e (still an exact function of state, so the potential still telescopes): credit = ball height (0 at or below 150 uu, 1 at 1450 uu and up) x softness (1 - (relative speed / 500)^2) x car height (0 on the ground, 1 at 517 uu up) x horizontal-speed damping (1 -> 0.5 as the ball goes 2300 -> 3000 uu/s, waived near the opponent goal). The potential is also multiplied by a control-distance gate (1 within 150 uu of the ball, 0 at 1100 uu). An opponent's touch resets the chain and charges the banked potential back; two touches 100 steps apart no longer chain. The hop-poke now pays 0 (was +34.3). Engine carry peak +31.1 (was +38.7), net about -1.6. Four ideas were taken from the MultiAirTouch audit (appendix). Findings 1 (a push is still a contact-step chain), 4 (terminal keeps the credit) and the demo keep-the-credit point were not individually fixed; see the log.

## 8. Demo (37.5)

**Before.** `DemoReward` was a typedef of `PlayerDataEventReward<&PlayerEventState::demo, false>`; +1 to the demolisher on the step the engine reports a demo between different teams.
**Issues.** Logic is correct. (1) No cooldown or context: pays every demo anywhere, even one that leaves your own goal open; the engine's 3 s respawn is the only rate limit (a spawn camper would have to guess one of four random respawn spots, so a farm is possible in principle and hard in practice). (2) Measured credit share is tiny (0.048%), so it is a cheap option, not a driver. (3) **Naming:** the typedef's readable name was just `false>` and its trainer / W&B key the mangled template string.
**After (`8956b34`).** Logic unchanged; `DemoReward` is now a real class so its name and key read `DemoReward` like the other rewards (any saved W&B panel on the old key must be repointed; 6.0 is a fresh run so nothing is lost). Verified: a ram at 2300 uu/s demolishes on frame 19 and pays +1 once (+37.5 / -37.5); the victim is frozen exactly 45 frames and respawns on frame 64 at one of four corner spots; a 1900 uu/s bump pays nothing; a teammate is never demolished or paid.

## 9. OpposedSave (25)

**Before.** Pays a save only if the LAST touch before the save step was the opponent's (the guard against self-served phantom shots). The engine's event tracker raised a shot only for ball speed >= 1750 uu/s.
**Issues (audit `a660fc1`).** (1) **The engine's shot is too narrow:** an unflipped ground hit at 2300 uu/s leaves the ball at 1789 decaying to 1741 within 0.3 s, so ordinary ground shots were invisible and their saves paid nothing. (2) A save that takes two touch steps can be missed (conservative by design). (3) The source comment says `ZeroSum(_, 0)` but the wiring uses 0.3, so teammates share a save. (4) The shot prediction ignores walls and bounces.
**After (`f5645b7`).** Shot threshold lowered 1750 -> 1000 uu/s (`EVENT_SHOT_MIN_SPEED` in `EnvSet.cpp`); the 2.0 s predicted-score window and the shooter-touch requirement are unchanged. Nothing else reads `shot`, so only OpposedSave sees the difference. **Still open:** findings 2 and 3 (two-touch miss; comment vs wiring), and a slow-shot save end to end is not demonstrated in the engine pack.

## 10. TouchAccel (10)

**Before.** On a touch step: `max(0, min(1, |v_now|/M) - min(1, |v_prev|/M))`, M = 130 kph.
**Issues (audit `0f0293c`).** (1) Direction-blind: a hit into your own goal pays the same as a hit at theirs. (2) A ratchet, not a potential: speed lost between touches is paid again when regained (0 -> 1000 -> 500 -> 1500 paid 0.65 vs a final fraction 0.49). (3) It paid the change in |v|, so returning a fast ball paid ~0 (a 2300 uu/s head-on hit paid 0). (4) The cap was high: a typical 1000-1200 uu/s hit earned 28-33% of the maximum. (5) Two players touching on the same step are both paid in full.
**After (`b8c91bb`, `665ed1b`).** Cap lowered to 110 kph (3055.6 uu/s). A touch now pays the IMPULSE `min(|v_now - v_prev|/M, max(0, S_now - H))`, with H a per-player high-water mark that resets when an opponent touches the ball. A reversal 1000 -> -1000 pays 0.3273 (was 0); a dead stop 2300 -> 300 pays 0.098; the 0 -> 1000 -> 500 -> 1500 sequence pays 0.4909 = the final fraction; a repeated nudge pays 0 the second time; the total between opponent touches is bounded at 1.0. **Left on purpose:** finding 1 (direction-blind; the proposed GoalDirectedTouchAccel swap was not applied) and finding 5.

## 11. GuardedPickupBoost (6)

**Before.** `sqrt(b/100) - sqrt(b_prev/100)` when boost rose, else 0; 0 when demolished now or last step (the respawn refill is a boost jump in the engine).
**Issues.** No defect. Notes: (1) positive-part only, so a burn-and-refill lap pays +1.0 per big-pad lap (bounded by the pad rest times and cancelled between cars; CarEnergy charges the spending). (2) The 5.6x marginal ratio (empty vs 88 boost) is the intended incentive. (3) A car that loses a contested pad is charged what the winner gains (a 12-point pair swing for a big pad, 8% of a Goal).
**After.** Unchanged. One test was missing (a rise on the very step of the demolition); added, and it killed that mutant. With the guard removed the engine pack fails, which proves the respawn refill is real.

## 12. BallProximity (4)

**Before.** Team potential `Phi =` the closest ALIVE team car's `exp(-d/1410) + 0.08*max(0, 1 - d/12000)`; reward `gamma*Phi(now) - Phi(prev)`; maximum 1.08.
**Issues.** Audit (`293f51f`): no defect; 4 notes (hold tax of PBRS at gamma < 1, a hit that sends the ball away is charged at once, gamma is a constructor argument defaulting to 0.99, the 12000 uu linear range is shorter than the field diagonal). The 2v2/3v3 review then found the real gap: only the closest teammate counted, so a second car at the same distance as the first earned nothing extra, nothing pushed teammates apart, and the striker's credit per uu would have differed by mode.
**After (`78238d4`, your decision).** Spacing-aware team potential: alive cars are sorted by 3D ball distance and weighted by rank 1, 1/2, 1/4; a rear car within 500 uu of the car ahead gets the full crowding discount, fading to none at 1600 uu; the sum is normalised per mode so the maximum is 1.08 in 1v1, 2v2 and 3v3. 1v1 is bit-identical to before. Demolished-state flips still skip the step. **TODO left in the code:** tune the 500/1600 bounds, the rank weights and the striker credit from real results (striker credit per uu is 0.845 of 1v1 in 2v2 and 0.818 in 3v3).

## 13. TeamPressure (0.15)

**Before.** Raw 0 if some alive teammate was pressuring the ball (3D distance < 2500, or < 4000 and closing faster than 500 uu/s), else -1: a hard cliff.
**Issues (audit `91cf70f`).** **Finding:** a DEMOLISHED lone car was charged -1 for all 45 respawn steps (-6.75 weighted), and through ZeroSum its opponent got +6.75, an unintended +/-6.75 on top of the +/-37.5 Demo swing (+18%). Notes: a hard cliff (-0.15 per step at 2501 uu, 0 at 2499); one teammate covers the team; thresholds do not scale with mode. Overlap with BallProximity checked: same direction in the far field, different signals; the cliff is 9x the potential's gradient there.
**After (`3a6b583`).** Smoothed with a projected-distance rule: per alive teammate `d_eff = d - 2 s * min(v_toward, 1000 uu/s)`, `lapse = smoothstep((d_eff - 2000)/1000)` (0 under 2000, 1 over 3000), team lapse = the minimum over alive cars, reward = -lapse. A demolished player's own row is 0. Still a rate, not a potential. Hand values: a car starting 4500 uu out fades in (-1.09 total) instead of a cliff; a car closing from 3900 pays 0; a demolished lone car pays 0. The constants (2 s, 1000 uu/s, 2000-3000 uu) are baselines to re-tune.

## 14. TimeCost (0.01)

**Before / after.** `return -1.f` for every player on every step, unchanged.
**Issues.** None. Notes: (1) it is per decision STEP, not per second (-0.15/s at tickSkip 8), so a tickSkip change changes its rate; `-deltaTime * 15` would be rate-invariant and identical today. (2) It adds an ending bias worth +3.23 weighted, about 2% of a Goal; the 10 s NoTouch end is a truncation so it adds no incentive to end episodes. (3) It is the only term that taxes a mutual stall; it must stay unwrapped (ZeroSum would cancel it to exactly 0). (4) A team of three pays three times what a team of one does, as every per-player term does.

---

## Appendix: audited but not in the 14

- **KickoffRace (was 60, now OFF).** Audit found a window shorter than a real kickoff (now 45 steps) and a same-step tie that paid only the lower player index (now all are paid); all 85 kickoff layouts tested. Commented out until the bot hits the ball and scores. Commits `a294118`, `ab5c239`, `b0318cb`.
- **KickoffOutcome (v3 port, OFF).** Fixed: possession by latest touch step, contested gate, 3.5 s standoff (`928b425`). Stays off with KickoffRace.
- **MultiAirTouch (v3 port, OFF).** Audited (`e1ecb93`): findings, including an unbounded ledger; four of its ideas went into ConsecutiveAirTouch instead.

## Engine facts found along the way

- A ball set with exactly zero velocity in the air does NOT fall (it floats); any nonzero velocity makes it fall. Training state setters give the ball a nonzero velocity, so this only bites hand-written scenarios (noted in `tools/reward_lab/README.md`).
- `worldContact.hasContact` is body contact only, never wheel contact: do not use it to detect driving on a wall.
- Car-ball contact persists over several steps while a car pushes the ball; a "touch step" is a contact step.
- Demolished cars are frozen for 45 steps (frames 19-63 in the pack runs) and respawn with 100/3 boost at one of four corner spots.
- Big pads give +100 (10 s rest), small +12 (4 s).

## Open items

- Sign the packs (below). Re-tag the restore point `pre-6.0-20261102` after the last reward change; it is stale.
- OpposedSave: the two-touch miss and the comment/wiring mismatch.
- CarEnergy 75: measure credit share in the first 100M steps of 6.0.
- BallProximity and TeamPressure: tune the TODO constants from real results; striker credit tuning.
- The live run is 1v1, so the team changes (BallProximity, TeamPressure) take effect only when `playersPerTeam` is raised.
- Not started: the 23 commented-out v3 ports; checklist items 1, 5 and 6.

---

## How to sign off on the audits

Signing says: "I reviewed THESE scenarios, against THESE reward sources, on THIS engine behaviour, and they passed." The tool re-derives all of that on every `check`, so a pack is only **CURRENT** (verified) until something it depends on changes. Signing is your act; tooling never does it. All 18 packs are UNSIGNED today.

**Timing matters.** A pack goes STALE if any reward source it depends on changes, including a comment edit. The source hash covers every file reachable from the reward's header, and a reward in `CommonRewards.h` goes stale when ANY reward in that file changes, so one edit there stales every pack whose reward lives in it. Sign when item 3 is finished for the rewards you care about, not before. Docs, tests and other packs do not matter.

**1. Run from WSL in the repo root, with the Lab built.**

```bash
cd ~/pulsar2
tools/reward_lab/build.sh
```

**2. Confirm everything passes and read what each scenario claims.**

```bash
for p in goal aerial_touch ball_to_goal air_intercept car_energy wall_jump consecutive_air_touch demo opposed_save touch_accel guarded_pickup ball_proximity team_pressure time_cost; do
  echo "=== $p"; build-lab/reward_lab_run tools/reward_lab/packs/$p | grep -E 'SCENARIO|summed|passed|FAIL' | cut -c1-200
done
```

Each scenario's `description` and the `intent` of each expectation are in `tools/reward_lab/packs/<pack>/*.scenario.json`. What to judge per pack: (a) is the described situation what the reward should be tested on, (b) are the expected bands tight enough to catch a wrong reward (they were hand-derived; the per-reward numbers are in `REWARD_CHANGE_LOG.md`), (c) are the "not covered" items in the log acceptable to you. You can also watch scenarios in the viewer (`docs/REWARD_VIEWER_SPEC.md`, `tools/reward_lab/README.md`).

**3. Check one pack, sign it, check again.**

```bash
build-lab/reward_lab_pack check tools/reward_lab/packs/goal
build-lab/reward_lab_pack sign  tools/reward_lab/packs/goal --reviewer "Your Name"
build-lab/reward_lab_pack check tools/reward_lab/packs/goal
```

The first check exits 3 (UNSIGNED), the last exits 0 (CURRENT). `sign` refuses a failing pack, a scenario with no expectations and a missing `--reviewer`. It writes `manifest.json` in the pack folder (scenario hashes, reward source hashes, an engine probe, the results).

**4. Sign all 14, with a prompt per pack.**

```bash
for p in goal aerial_touch ball_to_goal air_intercept car_energy wall_jump consecutive_air_touch demo opposed_save touch_accel guarded_pickup ball_proximity team_pressure time_cost; do
  read -r -p "Sign $p? [y/N] " a
  [ "$a" = y ] && build-lab/reward_lab_pack sign tools/reward_lab/packs/$p --reviewer "Your Name"
done
```

**5. Verify and commit the manifests.**

```bash
for p in tools/reward_lab/packs/*/; do printf "%s: " $(basename $p); build-lab/reward_lab_pack check $p | head -1; done
git add tools/reward_lab/packs && git commit -m "Sign the audited reward packs"
```

Verdicts: `CURRENT` (exit 0) is verified; `STALE` (1) names the file that changed, so re-run the pack, re-read it and sign again; `ENGINE_MISMATCH` (1) means the RocketSim engine behaves differently (scenarios are not re-run; investigate); `INVALID` (2) is a broken pack; `UNSIGNED` (3) has no manifest.

**Packs not to sign yet:** `kickoff_race`, `kickoff_outcome` and `multi_air_touch` cover rewards that are OFF; sign them when you turn those rewards on. `core` (10 scenarios) is the original general pack; sign it if you want its sprint and idle scenarios counted. After signing, tick the checklist box "You sign off finished packs" and re-tag the restore point.
