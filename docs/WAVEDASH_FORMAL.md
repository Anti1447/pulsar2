# Wavedash - formal general form

Status: 2026-11-02. Provenance marks: **[U]** stated by Nathan (the model), **[M]** measured in the real engine (RocketSim v3 through the compat
layer, `tests/test_wavedash_formal.cpp`, one geometry: front flip, 1000 uu/s along +x, car falling from z=70), **[?]** not measured.
Nothing here has been used in training.

## 1. Objects
Car frame (f, r, u) = (forward, right, up). Wheels W = {FL, FR, BL, BR}, contact bits c_i(t) (`wheelsWithContact[i]`), `isOnGround`.
Surface normal n (floor: +z; walls and ceiling: the contact normal). Dodge availability
A(t) := !hasFlipped && !hasDoubleJumped && (!hasJumped || airTimeSinceJump < 1.25 s)   while airborne [M].
**Where the flip came from does not matter, only that it is available.** Two sources, both measured: (a) a **jump** - then the 1.25 s window
applies (press at 0.83 s registered, at 1.33 s did not); (b) **leaving a surface without jumping** (fell off the ceiling or a wall, drove off a
ramp) - `hasJumped` stays false and there is **no timer**: a flip pressed 2.33 s into a fall from the ceiling still registered [M]. A car that has no
flip (already used, or the jump window expired) cannot wavedash: the press does nothing [M]. The flag resets on landing.
Flip input: a jump rising edge together with a direction input d_in = (pitch, yaw, roll) beyond the dead zone; d_h is its unit
horizontal direction in the world (front flip: the car's heading projected on the ground plane; right/back/left: rotated accordingly).
Pitch sign: +1 is nose UP, -1 is nose DOWN [M].

## 2. The flip itself [U unless marked]
At t_f with A(t_f) and a directional input the car flips: `hasFlipped` and `isFlipping` become true and stay so for up to T_f (~0.65 s) unless
cancelled. The flip applies an immediate velocity change along d_h, dv = J(v . d_h) d_h, with J > 0 when the car is below the speed cap and
J < 0 (deceleration) when d_h opposes the travel direction; J falls to 0 as the speed along d_h approaches the supersonic cap [?].
Measured flip dynamics [M] (level car, `tests/test_wavedash_formal.cpp`, the constants behind `tools/wavedash_sim`):
- **Front flip:** dv along d_h = clamp(2300 - v . d_h, 0, 500) uu/s: 500 up to 1500 uu/s, 300 at 2000, 100 at 2200, 0 at the 2300 cap. No vertical impulse.
- **Back flip:** dv = -(533.3 + 0.348 v) uu/s for v >= 0 (-533 at rest, -881 at 1000, -1229 at 2000): decelerating is stronger than accelerating.
- **Vertical damping (why early flips float):** from the 18th tick after the press until the flip ends (tick 79) the vertical speed is multiplied
  by 0.65 every tick (gravity applies afterwards), so vz converges to -5.42/0.35 = **-15.47 uu/s whatever it was** (falling at 400 or rising at 300).
  A flip made more than ~18 ticks before the ground is therefore a slow float, not a landing.
- **Flip rotation:** pitch rate 7.37 rad/s (nose down for a front flip) for 79 ticks (0.66 s), ~-270 deg in total, after a 1-tick delay and ~2-tick
  ramp; afterwards the spin decays with damping ~2.8/s.
- **Pitch control in the air:** angular acceleration 12.46 rad/s^2 per unit input, rate cap 5.6 rad/s, damping 2.8*(1-|input|) per second.

## 3. Flip cancel
A flip in progress ends when the car's wheels reach the ground and the car is back under control: `isFlipping` goes false long before T_f. [M]
Observed cancel 2-10 ticks after the press (nose-up 25 deg: 9-10 ticks; level: 2-3 ticks), all wheels down 1-7 ticks after first contact.

## 4. Geometry: leading and trailing wheels
For a flip direction d_h let L(d_h) be the wheels with the largest projection on d_h (the two front wheels for a front flip, the two right wheels
for a right flip, one wheel for a diagonal) and T(d_h) the opposite set. Define the signed lead elevation
theta_d := asin( ((p_L - p_T) . n) / |p_L - p_T| ),  with p_L, p_T the centroids of L and T in the world frame.
theta_d > 0 means the leading edge is raised (nose up for a front flip). Let phi be the elevation of the perpendicular axis (roll for a front
flip); the setup requires |phi| <= eps so the two trailing wheels touch together.

## 5. Definition
A **wavedash in direction d_h** is an episode with times t_f < t_1 <= t_2 such that:

- **W1 - flip started airborne.** All c_i(t) = 0 for t <= t_f, A(t_f) holds (from any source, section 1), and the flip input is given at t_f.
- **W2 - imminent contact.** t_1 - t_f <= tau, where t_1 is the first tick with some c_i = 1. Measured tau ~ 1-2 ticks [M] (see section 7).
- **W3 - trailing wheels first.** At t_1 the contact set is a subset of T(d_h) (measured: the two rear wheels for a front flip). This follows from
  theta_d(t_1) > 0 with the cross axis level.
- **W4 - cancel.** The leading wheels follow within delta ticks: t_2 - t_1 <= delta, t_2 the first tick with >= 3 wheels in contact and
  `isFlipping` false from t_2 on. Measured delta <= 7 ticks, flip duration t_c - t_f <= ~10 ticks [M].
- **W5 - retention (success).** The speed along d_h survives the landing: (v(t_2 + Delta) - v(t_f^-)) . d_h >= rho * J_ref.
  Measured: speed after the landing = speed before the flip + J (1523 vs 1000 + 500), i.e. rho ~ 1. The "+422 / +379 over a no-flip control" figures
  were deflated: the control car keeps accelerating on the ground with throttle (+101 vs +23), so compare against the pre-flip speed, not the control [M].

**Failure classes** (the flip happens but is not a wavedash):
- **N1 nose strike / no cancel (theta_d <= 0 with a raised trailing edge, e.g. nose down).** The leading edge arrives first or together; the flip
  keeps running. Measured: nose down 25 deg, flips 1-8 ticks before contact were never cancelled and the car was still tumbling at 90 ticks [M].
  The gain at +30 ticks was still +123..+169 over the control, i.e. smaller, not a net speed loss; the loss is of control, not (in this trace)
  of horizontal speed. Nathan's "loses speed" was not reproduced as a speed loss relative to the no-flip control.
- **N2 early flip (t_1 - t_f >= 3 ticks).** The dodge lifts the car, the flip ends in the air, the car lands ~130+ ticks later; a plain flip hop. [M]
  (Level and nose-up 25 deg: no cancel and not grounded within 90 ticks for presses 3-8 ticks before contact.)
- **N3 supersonic / opposing direction.** J -> 0 near the cap, J < 0 against the direction of travel [?].

## 6. Observable form at the 15 Hz interface
The whole event (W1-W4) spans about 10 ticks, roughly one decision (tickSkip 8, actions held 8 ticks), so a reward sees it only through
end-of-step snapshots S_k and the action a_k:

- O1  S_{k-1}: airborne, A true (`!hasFlipped`), optionally theta_d > 0 from `rotMat`.
- O2  a_k: jump held with a direction input d (`player.prevAction`).
- O3  S_k or S_{k+1}: `isOnGround`, `!isFlipping`, >= 3 wheels.
- O4  retention: (v_{k+1} - v_{k-1}) . d_h  >=  threshold. **The threshold must sit above the throttle baseline**: with throttle held, plain
  landings gain ~+145 uu/s within 3 steps [M, oracle control arm], so a 100 uu/s threshold is satisfied by 90+% of no-flip landings. The
  flip-attributable gain is ~300-350 (best nose-up cell 490 vs 150 control), so a threshold near 250-300 is the separating level.
- `hasFlipped` clears on landing [M], so a snapshot after a wavedash does not show it; use the action and the speed change.

The stock rule (`prev.flipping && !prev.ground`) and `RolvWavedashReward` need `isFlipping` true in an end-of-step snapshot, which only happens
when the flip spills past the step end (nose-up approaches: Basic 14-74%, Rolv 46-82%); for a level approach they never fire (0 of 10,000 episodes). [M]

## 7. What the 2-tick window means for the control interface
Pressing 1-2 ticks before first contact is a window of 2/8 = a quarter of a decision. A policy deciding every 8 ticks can only land its press in
that window by shaping the approach (fall height, vertical speed, pitch, boost) so that first contact falls just after a decision boundary.
That is a tick-alignment problem on the approach, not a timing-of-the-button problem. The relationship tau vs theta_d and speed is [?]
(one geometry measured). This also bounds how often even a perfect scripted executor produces W1-W5 through a decision-aligned press, which is
what the repo's oracle (`research/tools/oracle_wavedash.py`, oracle v1 buggy, ts4) never established for ts8.

## 8. Quantities still to measure
| quantity | status |
|---|---|
| J(v . d_h) curve, supersonic cutoff, back-flip deceleration | measured (section 2); not measured for pitched or yawed cars |
| tau, delta as functions of theta_d, v, fall rate | [?] (one point: theta 25 / 0 deg, v 1000) |
| the 8 directions (front, back, left, right, four diagonals) and the leading/trailing wheel sets | [?] |
| theta_min and eps (how much tilt/roll is tolerable) | [?] |
| landing losses on non-flat or tilted approaches (walls, ramps) | [?] |
| a tick-level labeler for W1-W5 (replace the confounded speed label) | not written |

## 9. Hypotheses for the next experiments (pre-registered here, not yet run)
- **G1 symmetry.** W1-W5 hold in all eight directions when that direction's leading edge is raised and the cross axis is level, with
  rho in [0.7, 1.0].
- **G2 window.** tau does not grow with theta_d for theta_d in [10, 45] deg: the press window stays about 2 ticks.
- **G3 reachability at ts8.** An executor that sweeps fall height (so first contact lands at each phase of the 8-tick grid) and presses on the
  decision boundary produces W1-W5 in a fraction of episodes close to (tau + 1)/8 per phase-aligned approach; the repo's census "11.2%" is
  compared against this number, not against a label satisfied by throttle.
- **G4 reward agreement.** A rule built on O1-O4 (threshold ~ 250-300 uu/s) agrees with the tick-level labeler on >= 90% of episodes and fires on
  < 2% of no-flip controls; the stock rule and Rolv agree only on nose-up approaches.
