# KickoffRaceReward vs KickoffOutcomeReward

Written 2026-11-03 (updated the same day for the three KickoffOutcome fixes) from the two audits (`REWARD_CHANGE_LOG.md`, 2026-11-02 and 2026-11-03), `tests/test_reward_audit_kickoffrace.cpp`, `tests/test_reward_audit_kickoffoutcome.cpp`, `tests/test_kickoff_permutations.cpp`, `tests/test_kickoff_outcome_permutations.cpp` and the packs `tools/reward_lab/packs/kickoff_race` and `kickoff_outcome`. Numbers are from the real engine over all 85 kickoff layouts (5 + 20 + 60 ordered spawn-spot choices for 1v1 / 2v2 / 3v3), or from the pack scenarios.

## Status in the stack
- **KickoffRace**: commented out of `BuildRewards()` on 2026-11-03 (user-directed, until the bot hits the ball and scores); weight was 60, `ZeroSumReward(…, 0.3)`.
- **KickoffOutcome**: ported from RLGymBot_v3, commented out in `BuildRewards()` (v3 weight 12), no registered Lab/W&B use yet. Its own header says it overlaps KickoffRace.

## Side by side

| | KickoffRace | KickoffOutcome |
|---|---|---|
| Question it answers | Who got to the ball first, and how fast? | How did the kickoff turn out 2 s later? |
| Paid when | At the first touch (steps 31 to 40 on a real kickoff) | 30 steps (2.0 s) after the first touch, or on a goal, or on the terminal step |
| Value | `1 - k/45` for a touch on step k (0.31 diag, 0.11 back) | Weighted sum of field, possession, momentum, first-touch, coverage, clipped to [-1, 1]; goal in window = +/-1 |
| Arms on | Ball at rest in the centre box (\|x\|,\|y\| < 200, speed < 50) | \|ball\| < 300 (3D) and speed < 200 (looser: 250 uu off-centre rolling at 150 arms it) |
| Who is paid | Every player who touches on the first-touch step (after the 2026-11-02 fix) | Every player, takers (closest at spawn, ties all count) 100%, support 50% |
| Zero-sum | By the `ZeroSumReward` wrapper (own - opponent) | Built in for field / possess / momentum / first-touch (1v1 exactly antisymmetric); **not** for coverage (2v2/3v3 net +0.2) or standoff (both teams lose) |
| Contested gate | Yes: pays only if an opponent is within 1500 uu or closing at >500 uu/s | Yes, the same rule (added 2026-11-03); an uncontested first touch closes the kickoff with no payout |
| Delay kickoff (orange idles) | Pays 0 in all 85 layouts | Pays 0 in all 85 layouts (it paid the rusher +0.78 before the gate) |
| Standoff (nobody touches) | Pays nothing | -0.6 to everyone at 3.0 s (support -0.3), kickoff closed |
| Dead-even touch | Both paid the same (fixed), ZeroSum nets 0 | firstTouch term 0 and possession 0 on a step both teams touch (fixed 2026-11-03; it followed the highest player index before: orange better in 75 of 85); the later scramble is decided by the latest touch step |
| Goal inside the window | n/a | +/-1 override, practically unreachable: the best hit from the centre is still at y = 3254 after 2 s |
| Calibration edges | Window 45 steps: a no-boost rush touches at step 45 and pays 0 | Standoff 3.5 s (step 53): a no-boost rush touches at 45 or 51 (fine) or 59 (standoff, 1 of 85 layouts); it was 3.0 s with 15 of 85 |
| Credit horizon for PPO | 0 (paid at the touch) | 2 s after the touch |
| Metrics | none | static atomics (armed, resolved, standoffs, goals, per-mode) |

## What each can and cannot teach
- **Race** teaches "get there fast and win the 50/50". It is tiny (0.1-0.3 raw, 7-19 weighted) and cancels to exactly 0 for any dead-even race, so it only rewards a strict win by at least a step. It cannot be farmed by rushing into a delay.
- **Outcome** teaches "do something good with the first touch" and "do not wait". The standoff tax (-7.2 per car at weight 12, not zero-sum) is the biggest signal either reward gives and the only one that pushes a random policy toward touching at all. It also rewards rushing a delay kickoff and scores a symmetric 50/50 by last-touch order, so on its own it adds noise on ties.
- After the fixes they agree on a delay kickoff (both pay 0) and on the early team in 79 of 85 head-start races. They still differ in WHEN and HOW MUCH: Race pays at the touch (0.1-0.3 raw), Outcome 2 s later (up to +/-1 raw), plus the standoff tax.

## If both are used
- Both now share the contested gate. Outcome's possession term was fixed (latest touch step, 0 on a double).
- Plan (user, 2026-11-03): add Outcome AND Race only after the bot hits the ball and scores, to avoid an early strong negative signal (Outcome's flat standoff -0.6 x weight; Race's ZeroSum charge to the loser). Outcome is already commented out; Race was commented out of `BuildRewards()` on 2026-11-03 for the same reason (both kickoff terms are now off).
- Re-check the weights together: a 12-weight Outcome gives up to +/-12, Race at 60 gives about 7-19; the standoff -7.2 dominates both early in training.
