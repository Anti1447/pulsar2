# Wavedash reward - Mechanic Spec (proposal, nothing implemented)

Date: 2026-11-02, **revision 2** (same day). Category: MECHANIC (precision, short chain). Target: Pulsar (tickSkip 8, 15 Hz). Built on
`docs/WAVEDASH_FORMAL.md` (definition and measured engine facts) and `tools/wavedash_sim` (the constants). Provenance: **[M]** measured in RocketSim v3,
**[T]** toy sim only, **[A]** assumption, **[?]** unmeasured. Weights and potential parameters are placeholders until credit density is measured on the live run.

**Revision 3 (Nathan):** section 9 adds an automatic scaffold controller (success per attempt, provisional thresholds, manual override); the ZeroSum exception below becomes temporary.

**Revision 2 changes (Nathan):** (1) neither term is ZeroSum-wrapped: a wavedash is a mechanical skill, an opponent doing it is irrelevant, so both terms are
individual; (2) the event has a hard payout cap per episode (and a short cooldown) as the farming defense. Section 5 records what each choice costs.

## 1. What we learned that the design must respect
1. A wavedash is: a flip available (from a jump, 1.25 s window, or from leaving a surface without jumping, no timer) [M]; trailing wheels touch first with the
   leading edge raised; the flip is cancelled when the leading wheels touch, within ~10 ticks of the press; the full flip impulse survives the landing [M].
   Window: the press must come 1-2 ticks before first contact [M, one geometry].
2. The whole event spans about one decision at ts8, so a reward sees it only as: S(k-1) airborne with a flip available, action k = jump + direction,
   S(k) or S(k+1) on the wheels with the flip over and a speed change along the flip direction.
3. `isFlipping` in a snapshot is the wrong signal: with a level approach the stock rule and `RolvWavedashReward` fire in 0 of 10,000 oracle episodes at
   ts8; only after a nose-up approach do they fire. A bare speed-gain label is satisfied by throttle alone in 90%+ of no-flip landings (+145 uu/s). [M]
4. Flip-attributable gain is J = clamp(2300 - v.d, 0, 500) for a front flip; it vanishes at the supersonic cap. A back flip is -(533 + 0.35 v). [M]
5. By chance a decision grid hits a 2-tick window 2/skip of the time (25% at skip 8). A perfect planner using boost reaches 88-94% on timing alignment
   alone at skip 8 and ~100% at skip 4 (upper bound, timing only). So the reward must make the *approach* learnable, not only pay the event. [M]
6. `CarEnergyPotentialReward` (live, weight 75) already pays kinetic energy and tempo, so the speed a wavedash gains is already priced. A wavedash
   reward must therefore be a **discovery signal for the skill**, small, not a second speed bonus.

## 2. Design: two individual terms plus a curriculum
### 2a. `WavedashEvent` (sparse, the success signal) - individual, unwrapped, capped
Fires on the step the wavedash is *observed*, from end-of-step snapshots only (no `isFlipping`, no ball):

| # | condition (snapshot form) | why |
|---|---|---|
| E1 | prev step: `!isOnGround`, no wheel contact, flip available: `!hasFlipped && !hasDoubleJumped && (!hasJumped \|\| airTimeSinceJump < 1.25)` | the flip must exist; either source |
| E2 | this step's action (`prevAction`): jump held with a direction input (pitch/roll; yaw is not a flip direction) | a flip was actually requested; separates it from throttle landings |
| E3 | within this step or the next: `isOnGround` (>= 3 wheels) and `!isFlipping` | the flip was cancelled by the ground |
| E4 | `d_h` = flip direction on the ground plane (from the input and the car heading). Retention: `(v_after - v_before) . d_h >= rho * J_ref(d_h, v_before)`, rho = 0.6, `J_ref` = clamp(2300 - v.d, 0, 500) for a front flip | the speed survived; threshold well above the throttle baseline (~145) |
| E5 | `J_ref >= 100` (not within ~200 uu/s of the cap) | no pay for an unreachable gain; self-limits farming near the cap |
| E6 | **cap** (section 4): at most `N_cap` paid events per player per episode, and a per-player cooldown of 3 decisions | the farming defense |

Payout: `r = clamp((v_after - v_before).d_h / 500, 0, 1) * R_event` (a full +500 pays `R_event`). Direction-general: front, back, left, right and diagonals
use the same rule with `d_h` from the input; the back flip's reference is its measured deceleration. Side and diagonal `J_ref` are **[A]** (front-like 500) until
measured. Surface-general (later): replace the floor by the contact normal and project `d_h` onto the surface; floor only in v1.

### 2b. `WavedashReadyPotential` (dense, the approach) - exact PBRS, `gamma = TRAIN_GAMMA`, unwrapped, uncapped
`Phi(s) = w * A(s) * g_tilt(theta_d) * g_time(tau)` where
- `A(s)` = flip available (E1's condition) and airborne,
- `theta_d` = elevation of the leading edge for the direction the car is travelling (rotMat); `g_tilt` = 1 for theta_d in [10, 40] deg, ramping to 0 at <= 0 deg and >= 70 deg (nose-down never cancels [M]),
- `tau` = predicted ticks to first wheel contact (ballistic from lowest wheel height and vz); `g_time(tau) = exp(-tau / 12)`, peaking at tau ~ 1-3, 0 once the flip is spent or on the ground.

Properties: dense guidance toward "flip kept, tilted, about to land"; spending the flip early drops Phi, so the policy is paid back for **keeping the flip until the
last moment**. It cannot be farmed and needs **no cap**: a potential telescopes per player (a cycle that returns to its start nets 0 at gamma = 1; holding Phi
constant pays (gamma - 1) Phi per step, a small cost), and Phi is 0 on the ground. Dropping ZeroSum does not change this: ZeroSum(PBRS) was only ever a scaling. Never
gated (no `WeightedReward::gated`).

### 2c. Curriculum (where the learning problem is actually solved)
A `WavedashApproachState` state setter: falling cars with a flip available (`hasJumped = false`), height 60-300 uu, vertical speed -300..0, nose-up 0-40 deg,
speed 400-1800, ~10-20% of episodes. Gives roughly one opportunity per episode. The repo's notes: skills below break-even need density; add it only with the
event reward live.

## 3. What not to do
- Do not port `WavedashReward`, `WavedashBasicReward`, or `RolvWavedashReward` unchanged (blind on level approaches at ts8, no jump condition or gain threshold, no rate limit, weight 20 in v3).
- Do not use a speed-gain-after-landing label without the jump condition and a ~300 uu/s threshold.
- Do not pay on the press tick or reward 1-tick timing: the bot cannot choose it at 15 Hz.
- Do not count the speed twice at a big weight (CarEnergy already does).

## 4. The payout cap
**Per episode, not per iteration.** A reward cannot see iterations: an iteration is a batch over ~1000 arenas, and a cap shared across arenas would need mutable
state across threads (a race, order-dependent, and unfair between arenas). A per-episode cap needs only per-player state in the reward instance (EnvSet makes one
instance per arena) cleared in `Reset()`, and it bounds the per-iteration income anyway:
`iteration income <= N_cap * R_event * (episodes per iteration)`.

**Design:** hard cap `N_cap` paid events per player per episode (the next one pays 0) + 3-decision cooldown. Starting numbers (placeholders): `R_event = 3`
(2% of a goal), `N_cap = 3`, so the most the event can add per player per episode is **9 units = 6% of one goal**, and the cap cannot be gamed by ending the episode (that
only restarts a zero count and forfeits everything else).
Why a hard cap and not a geometric decay: both are hidden state (the obs does not contain the count), but a hard cap leaves the first `N_cap` events exactly Markov, so
the critic is unaffected in the regime that matters; a decay changes the second event onward. This is the objection that killed the Seer decay envelope in
`REWARD_SHAPING.md` section 10 ("unobservable ... the critic must marginalize it away"), so keep the hidden part small: the cap only bites when the bot is farming.
Why not a time-window rate limit: its memory (seconds) exceeds the ~19-step GAE window, so the same objection applies with no benefit.

## 5. What dropping ZeroSum costs (recorded, not hidden)
- **Doctrine exception.** `CLAUDE.md` says every stack term is exactly zero-sum or antisymmetric. `TimeCost` is already an exception; this would be a second. The reason for the
  invariant (PSD probe fitness as a pure competitive margin) is gone with PSD (removed 2026-07-25), and what remains - a per-player income stream being farmable - is exactly
  what the cap, the cooldown, E2/E4/E5 and the PBRS structure address. Record the exception next to the entries in `BuildRewards()` ("NOT ZeroSum: individual mechanic").
- **Mirror play pays both cars.** In mirror self-play both players earn the event; the stack sum moves by at most `2 * 9` per episode in 1v1 (bounded by the cap). Not a
  farming problem, but the "stack sums to 0" statement stops being true and any telemetry that assumes it must be rechecked.
- **Credit-density currency.** `REWARD_SHAPING.md` section 3.4 measures density *post-ZeroSum* (`E|r_i - r_j|`). For these terms measure the plain per-player `E|r|`
  and compare to the others on that basis, not directly to the wrapped terms.
- **The cap is now the only farming defense for the event** (before, ZeroSum cancelled mirror income). Test 3 below is therefore a hard gate, not a nicety.
- Not changed: the reachability gate (`WeightedReward::gated`) stays off for both terms.
- **The exception is meant to be temporary:** the terms fade under the controller in section 9, after which the competitive terms carry the skill and the invariant holds again.

## 6. Pre-registered checks (before any training)
Engine tests (`tests/`), unless noted:
1. **Labeler agreement.** A tick-level labeler of W1-W5 vs `WavedashEvent` on the snapshot sequences of the ts8 oracle: agreement >= 90%, fires on < 2% of no-flip controls, across level and nose-up approaches.
2. **Direction symmetry (G1).** The event and the labeler agree in all eight directions with that direction's leading edge raised.
3. **Farm bound (gate).** Brake-and-repeat loop for a full episode: total paid <= `N_cap * R_event` exactly; the (N_cap + 1)-th qualifying event pays 0; `Reset()` clears the count; counters are per player (two cars do not share a count); the cooldown blocks a second event inside 3 decisions; the cap does not carry across episodes.
4. **Potential telescopes.** A cycle returning to its start state nets 0 at gamma = 1 (as `test_pbr_rewards.cpp`); spending the flip early lowers Phi; Phi is 0 grounded and with no flip.
5. **Counterfactuals.** E1-E6 fail one at a time (no flip, no jump input, nose down, near the cap, too small a gain, over the episode cap).

W&B (per `REWARD_SHAPING.md`): opportunities (airborne, flip available, tau <= 18), attempts (flip pressed with tau <= 4), **success per attempt**, early-flip rate
(flip with tau > 6: hop), crash rate, histogram of tau at the press, mean gain along d_h, events per player-episode, **share of episodes that hit the cap**, paid event
density. Healthy: attempts rise before successes; success per attempt rises toward the planner bound; cap-hit share < 5%. Suspicious: cap-hit share > 20% (the cap is
binding: either the bot farms or `N_cap` is too low for legitimate play), events clustered at the supersonic edge, high event rate with low gain, attempts with tau > 6 rising.
Behavior confirmation (viewer): flips held until just before touchdown, nose raised on the approach, the gain carried into play; not hop-and-flip loops on empty ground.

## 7. Starting weights (placeholders; measure credit density first)
Event: `R_event = 3`, `N_cap = 3` (max 9 per player-episode), no `TEAM_SPIRIT`, registered bare. Potential: `w` so that reaching the full ready state totals ~1-2 units. The v3
weight of 20 is not a reference. One lever at a time: event first, then the potential, then the state setter, each with a revert condition (cap-hit share, success per attempt).

## 8. Open items
Side/diagonal J_ref [A]; tilt and roll tolerance [?]; walls and ceiling [?]; supersonic and opposing-direction flips [?]; whether the 2-tick window changes with fall speed and
tilt [?]; the real bot's approach distribution (the oracle was scripted); whether `N_cap = 3` suits real play (the cap-hit share tells).

## 9. Automatic scaffold controller (rev 3, Nathan: success per attempt, provisional thresholds, manual override)
The wavedash terms are a **scaffold**: they exist to get the skill discovered, then fade so the competitive terms (`CarEnergyPotential`, `BallToGoalPotential`) carry its value
and the zero-sum doctrine is restored in steady state (section 5's exception becomes temporary). A controller does the fading, with no human in the loop unless they choose
to be. It is a **regulator**, not a one-way schedule: it holds the lowest bonus that keeps the skill.

### 9.1 Raw counters (per process, all arenas pooled; counted BEFORE any weight so the weight cannot feed back into the statistic)
- **opportunity**: a landing (first wheel contact) with the flip still available (E1's condition) - a chance to wavedash existed.
- **attempt**: a flip pressed (jump + direction) while the predicted time to first contact, from the previous snapshot, is <= 10 ticks (the last decision before contact).
- **success**: an attempt that also satisfies E3-E5 (the event rule, before the cap and cooldown).
- **episode**, **cap-hit** (an event suppressed by `N_cap`).
Exposed like the other rewards (`GetAndResetMetrics()`), read once per iteration. State setters change the number of opportunities, not the ratios below.

### 9.2 Statistic
`SPA` = pooled successes / pooled attempts, with the counts exponentially decayed over ~10 iterations (decay the sums, do not average per-iteration ratios).
`APO` = attempts / opportunities (is the bot actually trying). `CAP` = cap-hit share of episodes.

### 9.3 Update rule (once per iteration; `s` in [s_min, 1] multiplies both terms' output)
| condition | action |
|---|---|
| pooled attempts in the window < `n_min` | hold (not enough data) |
| `CAP` > `cap_freeze` | hold and flag (possible farming; do not anneal on it) |
| `APO` < `apo_min` | hold (success on a few lucky attempts is not mastery) |
| `SPA` >= `theta_hi` | `s <- max(s_min, s * (1 - d_down))` |
| `SPA` < `theta_lo` | `s <- min(1, s * (1 + d_up))` (re-arm: the skill decayed or was never there) |
| otherwise (inside the band) | hold |

### 9.4 Provisional values (NOT measured on the real bot; review at the gate in 9.7)
| parameter | value | provenance |
|---|---|---|
| `theta_hi` | 0.50 | above the 0.25 chance level at skip 8 by a wide margin; well below the 0.88-0.94 planner bound (timing only) [M for chance and bound] |
| `theta_lo` | 0.30 | just above chance (0.25), so a bot reduced to chance re-arms; band 0.30-0.50 is the hysteresis |
| `apo_min` | 0.20 | the bot uses at least one in five of its chances [A] |
| `n_min` | 500 attempts in the window | standard error 2.2% at SPA = 0.5 (+/-4.5% at 95%) [D] |
| `cap_freeze` | 0.20 | the "binding cap" threshold from section 6 |
| `d_down` | 0.14% per iteration | halves `s` in ~500 iterations (~100M steps at 200k per iteration) [A] |
| `d_up` | 0.70% per iteration | doubles `s` in ~100 iterations (~20M steps): recover 5x faster than fade [A] |
| `s_min` | 0.05 | never exactly 0 (multiplicative re-arm cannot leave 0) [A] |
Both rates are well inside the repo's "no instant weight jumps" rule: the event is at most 9 units per episode (6% of a goal) and fades over ~100M steps.

### 9.5 Applying and persisting `s`
- Both terms read `s` in `Reset()` and hold it for the whole episode (changing the potential's scale mid-episode would add a one-step (s' - s) Phi term and break telescoping).
- Controller state (`s`, decayed counts, last override) is written next to the checkpoint (sidecar JSON, written atomically with it, like the other controllers that persist through
  `RUNNING_STATS`) and loaded at boot; if absent, `s = 1`. A restart therefore does not reset the fade.
- Log to W&B each iteration: `Wavedash/Scale`, `SPA`, `SPA_raw`, `APO`, `CAP`, opportunities, attempts, successes, override mode.

### 9.6 Manual override (the human stays in control)
A small file in the run directory, re-read every iteration, no restart needed: `wavedash_scaffold.override` containing `auto` (default), `pin <value>` (force `s` to a value in
[s_min, 1]), or `off` (controller disabled, `s` left where it is). Precedence: override > controller. An active override is logged every iteration and shown in `Wavedash/OverrideMode`.
Deleting the file returns to `auto`.

### 9.7 Review gate (provisional numbers must be checked against data)
Once the window holds >= 5000 attempts, a human reviews the logged `SPA`/`APO`/`CAP` trajectory and either confirms the thresholds or edits them in the spec and the config. Until then
`theta_hi/lo` are provisional. The first red flag: `SPA` sitting at 0.25 +/- 0.05 for a long time (the bot is at chance: the scaffold is not working, not "mastered").

### 9.8 Known limits and risks
- **Self-reference.** `SPA` is computed from the reward's own rule. If the bot farms events, `SPA` looks high and the controller fades the term away: a useful brake, but `SPA` alone cannot
  prove real skill; keep the viewer confirmation (section 6).
- **Forgetting after the fade.** Handled by the re-arm branch, not prevented; the cost is a few oscillations around `theta_lo`.
- **Non-stationarity.** The fade is slow and small relative to the stack; the critic sees a slowly shrinking small term.
- **Static counters are per process.** Exploiter and main runs are separate processes, each with its own controller and sidecar.
- **Thresholds are guesses until the gate.** The chance level and the planner bound are measured; where the real bot lands between them is not.

### 9.9 Pre-registered controller tests (offline, no training; the controller is a pure function `Update(counts, state, override) -> state`)
1. Sustained `SPA` >= `theta_hi` decays `s` monotonically at `d_down` per iteration down to `s_min`, never below.
2. `SPA` < `theta_lo` grows `s` at `d_up` per iteration up to 1, never above.
3. `SPA` inside the band holds `s` exactly; a noisy `SPA` straddling `theta_hi` (smoothed) moves `s` by less than 5% over 50 iterations (no chatter).
4. Gates: attempts < `n_min`, `APO` < `apo_min`, `CAP` > `cap_freeze` each hold `s` regardless of `SPA`.
5. Override: `pin v` forces `s = v`; `off` freezes; `auto` resumes from the pinned value; precedence over the controller; unreadable or malformed file falls back to `auto` and logs.
6. Persistence round trip: state survives a write/read and a restart mid-fade.
7. Robustness: zero counts, NaN, `theta_lo >= theta_hi`, `s_min` out of range are rejected or clamped, never propagate.
8. Determinism: same inputs, same trajectory.

### 9.10 Build order (each step with its own revert)
(1) **DONE 2026-11-02:** `Rewards/ScaffoldController.h` + `tests/test_scaffold_controller.cpp` (10 tests covering 9.9; 14 deliberate mutations of the controller are each caught by a test); also provides the override-file parser and the atomic sidecar save/load; (2) the raw counters in the event reward; (3) wiring in the trainer's per-iteration callback, **default `off` via the override file until you
flip it to `auto`** so the first run records the statistic without acting on it; (4) enable `auto`.
