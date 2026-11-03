# ConsecutiveAirTouch vs MultiAirTouch (and AerialTouch)

Written 2026-11-03 from the audits in `REWARD_CHANGE_LOG.md`, `tests/test_reward_audit_consecairtouch.cpp`, `tests/test_reward_audit_multiairtouch.cpp` and the packs `tools/reward_lab/packs/consecutive_air_touch` and `multi_air_touch` (the latter runs both rewards on the same engine scenarios). Numbers are from the real engine or hand-computed.

## Status
- **ConsecutiveAirTouch**: live, weight 40, `ZeroSumReward(…, 0.3)`; ball-height credit added 2026-11-03.
- **MultiAirTouch**: RLGymBot_v3 port, commented out in `BuildRewards()` (v3 weight 50, ZeroSum 0.3). It overlaps Consecutive (and AerialTouch).
- **AerialTouch** (live, 120) is shown for reference.

## Side by side

| | ConsecutiveAirTouch | MultiAirTouch | AerialTouch |
|---|---|---|---|
| Kind | Potential (PBRS) on a per-player effective streak | Event sum: every counted touch pays | Event per strike, cooldown 12 steps |
| Bounded | Yes: total never reaches 40 (weighted) | **No**: 0.7+ per touch with no cap | Yes per strike (120), 0.8 s cooldown |
| Refund | Exact on landing (net 0 over a cycle) | **None** | None |
| What counts as a touch | Every contact step, weighted by ball height (z-150)/1300 | At most one per 0.5 s while the BALL is above 150 and the CAR above 300 | One strike with the ball above 150 |
| Lone air touch | 0 (needs a chain of credit > 1) | 0.1 (+5 at weight 50) | min(ballFrac, airFrac) x impulse |
| Chain tiers | Phi(e) = 1 - exp(-(e-1)/2) | 0.1, 0.4, then 0.7 per touch | n/a |
| Timing rules | None: two touches 100 steps apart chain | Spacing 0.5 s minimum, chain resets after 1.0 s idle, the first 0.5 s of an episode never counts | 0.8 s cooldown |
| Ends when | the CAR is supported (ground or wall), or a demo | the BALL falls to 150 or below, 1.0 s without a touch, or an opponent holds last touch | cooldown |
| Opponent touch | Ignored (the chain survives) | Clears the chain (via the engine's last-touch id) | Ignored |
| Teammate touch | Ignored | Does not clear | Ignored |
| Quality terms | none | contact angle, softness, z-lift, height, consistency, scaled by boost at the first touch (clamp(boost/50)), plus a 0.5x multiplier for ball horizontal speed over 2300 | impulse factor |
| Max per touch (weighted at its weight) | n/a, whole chain <= 40 | up to 1.49 raw = +74.5 at weight 50, every 0.5 s (about +140 per second) | 120 per strike |
| Demo | Resets, keeps the unrefunded credit | No guard | none |
| Same engine carry (18 contact steps, no boost) | +0.20 by frame 10, peak +0.80 (+32 weighted), net -0.0418 after landing | 0.1 at frame 8 and 0.4 at frame 16 = +0.5 (+25 weighted at 50), never refunded | n/a |
| Single 45-degree hit | 0 | +0.1 (+5.0 at 50) | pays by height / air time |
| Hop into a floor ball | 0 (ball-height credit) | 0 (car must be above 300) | 0 (ball floor 150) |

## How they differ in kind
- **Consecutive is a potential, Multi is a ledger.** Consecutive can only ever pay what is then refunded when the chain ends, so nothing can be farmed; Multi adds up every touch with no ceiling. 20 touches 0.533 s apart (a 10 s juggle) pay 0.1 + 0.4 + 18 x 0.7 = 13.1 raw with no boost, +655 weighted at the v3 weight of 50, more than four goals. That breaks the stack's "unfarmable by construction" doctrine, and it is the reason to keep Multi off.
- **Different definitions of "chain".** Consecutive is about uninterrupted airborne play by one car (it counts every contact step, however spaced, and ends when the car lands). Multi is about rhythm: a touch every 0.5-1.0 s, with the ball staying up and the car high, ending when the ball lands or the opponent takes it.
- **Different protections.** Multi has a car-height floor (300) and the ball floor (150), which block floor pokes; Consecutive now gets the same protection from its ball-height credit. Consecutive has a demo guard; Multi has none. Multi has an opponent-turnover rule; Consecutive keeps a chain alive through an opponent touch.
- **Quality.** Multi tries to reward HOW the ball is touched (soft, from below, lifting it) and discounts it by boost; Consecutive pays only for sustained airborne contact, which the audit showed is really "carrying" the ball.

## If both were used
- They double-count the same carry (one pays per step, the other in lumps), and both overlap AerialTouch's single-strike payment.
- Multi should not be enabled as written: it needs a cap or a potential form (for example, the same effective-streak potential with Multi's timing and quality terms as multipliers) before it can sit beside the others.
- Plan (user): the air-touch rewards other than the live ones stay off until the bot hits the ball and scores.
