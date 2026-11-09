# OffensiveDemoReward + Demo telemetry (2026-11-09)

Status: implemented, tests pass (20/20), `ExampleMain.cpp` syntax-checked; NOT built into `build/`, trainer NOT restarted (awaiting user approval).

## Intent (user-directed)
Strong bots in 2v2/3v3 demo the opponents in front of the ball (closest to the ball->opponent-goal line, more so near the ball) while a teammate takes the clear
ball. The flat `DemoReward` paid every demo 1.0. Make the best demo pay exactly the old full value and every worse demo less than that.

## Classification
Individual one-sided event reward, wrapped `ZeroSumReward(..., TEAM_SPIRIT)`, weight 50 (unchanged). The multiplier is in [0.4, 1.0], so the payout can only go DOWN versus the flat reward.

## Definition
`f = lineProx x ballProx`, floor plane (xy), victim position from the PREVIOUS step (the engine may already have moved a demoed car), ball from the previous step.
- lineProx (REV 2, user-directed: two lines): the shot lane is the triangle ball -> left post, ball -> right post, goal line (posts at x = +-892.755). Victim inside the triangle = distance 0; outside = distance to the nearer of the two ball->post segments. 1 at <= 400 uu, 0 at >= 1600, linear. (Rev 1 used one segment ball -> goal centre.)
- ballProx: distance victim -> ball: 1 at <= 1000, 0 at >= 3500, linear.
- multiplier = 0.4 + 0.6 f. Active only when a team has >= 2 players; 1v1 and "victim not identifiable" pay 1.0.
- Victim identification: the demoed opponent nearest the bumper (the engine event carries no pair).

## Scenarios (BLUE attacks +y, ball at origin; all in tests/test_demo_geometry.cpp)
| Setup | f | multiplier | verdict |
|---|---|---|---|
| victim (0,500), on the segment near the ball | 1.0 | 1.0 | ALIGNED (full old payout) |
| victim (0,-3500), far behind the ball | 0 | 0.4 | ALIGNED |
| victim (1000,0), beside the ball | 0.5124 (lane dist 985.1 -> 0.5124 x ballProx 1) | 0.707 | ALIGNED |
| victim (0,2250), inside the lane | 0.5 (1 x 0.5) | 0.7 | ALIGNED |
| victim (800,3000), 273 outside the right edge | lineProx 1 (one line would give 0.667) | - | ALIGNED: the corridor beside the lane counts |
| victim (2000,3000), 1455 outside | lineProx 0.121 | - | ALIGNED |
| ball on/beyond the goal line (degenerate triangle) | finite (falls back to the two segments) | - | ALIGNED |
| victim (0,6000), beyond the goal | 0 (ballProx 0) | 0.4 | ALIGNED |
| victim (0,-500), challenger just behind the ball | 0.9167 | 0.95 | AMBIGUOUS: a challenger is rewarded because the lane starts at the ball. Intended ("closer to the ball"), but it means "behind" is not penalised near the ball |
| 1v1, worst position | n/a | 1.0 | ALIGNED (no teammate to exploit the window) |
| victim flag missing | n/a | 1.0 | fallback, counted by `Demo/Unresolved Rate` |
Boundaries tested at 0.9x/1.1x of LINE_FULL, LINE_ZERO (points offset normal to the lane edge), BALL_FULL, BALL_ZERO. Mirror (x and team) symmetry tested.

## Farming probe
- Income can only fall (property tested: payout <= flat 1.0 over a position grid), so demo income per demo never rises and the old demo-farming rate is an upper bound.
- Gaming: the multiplier now rewards PLACEMENT, so the new risk is a bot that lingers near the ball for demos (crowding). Watch `Player/Demo Rate` and the existing spacing panels.
- Spawn camping: respawns are fixed points; camping pays full only when the ball is attacking near that spawn. Bounded by the 3 s respawn timer, measured by the same panels.
- Opponents it works against: unchanged from the flat reward (a stronger side keeps the income vs weaker or older opponents; ZeroSum removes it only in mirror play).

## Telemetry (state-only; independent of which demo reward is live)
`Demo/Offensive Factor` (+ `(team games)`), `Demo/High Factor Share` (f >= 0.5), `Demo/Multiplier`, `Demo/Victim Line Dist`, `Demo/Victim Ball Dist`,
`Demo/Unresolved Rate`, `Demo/Opp Mean Factor` (baseline: f of every opponent, sampled), `Demo/Goal After Own Demo` (per goal, was there a demo by the scoring team in the last 4 s),
`Demo/Own Demo Window Fraction` (per team-step, is that window open). Lift = Goal After Own Demo / Own Demo Window Fraction.
Caveat: because the reward ships together with the telemetry, there is NO pre-change baseline for these panels from this lineage. The first interval (before the policy adapts) is the nearest thing to one.

## Pre-registration
- Healthy: `Demo/Offensive Factor` rises above `Demo/Opp Mean Factor` (bot picks targets); `Player/Demo Rate` within 1.3x of its pre-change value; `Demo/Multiplier` mean rising toward 1; Goal After Own Demo lift > 1; `Player/Ball Touch Ratio` and Rating/Ref Oldest Share not degrading.
- Suspicious: demo rate > 1.3x with no lift; ball touch ratio falling; `Demo/Unresolved Rate` > 5% (victim pairing failing, multiplier silently 1.0).
- Revert: any of the above "suspicious" persisting over ~200M steps, or demo-chasing visible in the viewer: replace `OffensiveDemoReward` with `DemoReward` in BuildRewards() (telemetry can stay).
- Viewer check: in 3v3, does a teammate hit the cars in front of the ball carrier, or do all three crowd the ball?

## Limits
Fabricated states, no engine. Not verified: that the engine's `demoed` flag is raised on the same step for the victim (the EnvSet callback increments both together, so expected), the segment/radius constants against real play, or that this raises the behaviour.
