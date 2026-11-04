# External opponents: ported vs original (checklist item 1.1)

Written 2026-11-05 by reading, nothing run. Sources read:
- Nexto: `rlbot-run/nexto/{nexto_obs.py, agent.py, bot.py}` against `NextoOpponent.{h,cpp}`.
- GalactusV2 @ 217324c: `GalactusOBS.cpp`, `AdvancedObs.cpp`, `DefaultAction.cpp`, `GameState.{h,cpp}`, `InferUnit.cpp`, `src/RLBotClient.cpp`, `src/RLBotMain.cpp`.
- Chronos @ 4f8fdd2: `AdvancedObsPadded.cpp`, `DefaultAction.{h,cpp}`, `GameState.h`, `StateUtil.cpp`, `src/RLBotClient.cpp`, `src/RLBotMain.cpp`.
- Ports: `GGLBotOpponent.{h,cpp}`, our `Gamestates/` and `CommonValues.h`.

Verdicts: **EXACT** (checked against the source), **ON PURPOSE** (differs, accepted), **VERIFY** (cannot be settled by reading; item 1.2 settles it), **ACCIDENT** (a real bug; none found).

## Facts that hold for all three
| Fact | Verdict |
|---|---|
| Boost pad table: all 34 locations, including the 3310/3308 quirks, are identical in Nexto's `nexto_obs.py`, Galactus, Chronos and ours (compared value by value). | EXACT |
| Our `Player.h` (where `HasFlipOrJump` lives) is identical to Galactus's; our `StateUtil.cpp` (`InvertPhys`, `MirrorPhysX`) is identical to Chronos's. | EXACT |
| Sim step: `actionDelay` 7 means the action chosen from an obs at tick 0 takes effect at tick 7 and holds to tick 15. Galactus and Chronos live do exactly this (`actionDelay = tickSkip - 1`, controls applied once `ticks >= 7`). | EXACT |
| 90 actions, mapped onto our table row by row at construction, hard-failing on any miss. | EXACT |
| Argmax, never sampling, for Galactus and Chronos (`deterministic = true` in both live bots). | EXACT |

## Nexto
| Item | Original | Port | Verdict |
|---|---|---|---|
| Entity rows | 24 floats: self/mate/opp/ball/boost flags, pos, vel, fw, up, angvel, boost, demo, on_ground, has_flip | same layout | EXACT |
| Normalisation | pos and vel / 2300, angvel / 5.5, boost / 100 | same | EXACT |
| Orange view | every pos, vel, fw, up, angvel (players, ball, pads) negated in x and y | same | EXACT |
| Pad rows | position, BOOST = 0.12 + 0.88 x (z > 72), DEMO = pad active flag, absolute pad order even for the mirrored orange view | same, ported on purpose | EXACT (quirk kept) |
| Relative frame | subtract self **position only**, rotate the xy pairs of pos/vel/fw/up/angvel by atan2(fw.x, fw.y) of self; `q` keeps the unrotated self row | same | EXACT |
| `q` | self row + previous **parsed** action (8 floats) | same; `prevActions` per player | EXACT |
| Mask | all zeros (no padding) | zeros | EXACT |
| Pick | argmax at beta 1 | argmax | EXACT |
| Entity order | `[self, teammates, opponents]` (the live bot re-orders the players) | arena order, self wherever it sits | ON PURPOSE only if the net is permutation-invariant over entities (attention with no positions); VERIFY in 1.2 with a shuffle test; the golden compare must put both sides in the same order |
| Scripted kickoff (`hardcoded_kickoffs`) | 168 ticks of fixed controls for the kickoff taker (the closer car; the left one on a tie) | none | ON PURPOSE (decision 2026-11-05, option a); revisit after 1.4/1.5 |
| Stochastic kickoff (`stochastic_kickoffs`, beta 0.5 = softmax sampling) | during `MatchPhase.Kickoff` | none | ON PURPOSE (same decision) |
| Celebration (beta 0) | random actions after the match ends | none | ON PURPOSE (never happens in training) |
| Latency | obs at tick 1 of the window, applied at tick 7: 6 ticks | obs at tick 0, applied at tick 7: 7 ticks | ON PURPOSE, off by 1 tick; Nexto's own training latency is unknown to us |
| `has_flip`, `on_ground`, `boost_amount` scale, rotation convention, `boost_pads` order/meaning | `rlgym_compat` `V1GameState` | our `HasFlipOrJump()`, `isOnGround`, `boost / 100`, `rotMat`, `boostPads` | VERIFY in 1.2 (the library is not installed here, so reading cannot settle it) |
| CUDA | CPU float32 | CUDA with a rewritten baked-in device constant; trainer enables TF32 | VERIFY in 1.2: the boot probe only demands 95% argmax agreement and 5e-2 relative deviation, far looser than the 1.2 gate |

## GalactusV2
| Item | Original | Port | Verdict |
|---|---|---|---|
| Obs layout (258) | ball 9, prev action 8, 34 pad values, nearest active big pad (3), self 29, 2 teammate slots, 3 opponent slots, 5 prediction samples x 6 | same | EXACT |
| Coefficients | pos 1/5000, vel 1/2300, angvel 1/3 | same | EXACT |
| Pad values | 1 if active, else 1 / (1 + timer) | same | EXACT |
| Pad timers | `GetBoostPadTimers` correct in Galactus | ours correct | EXACT |
| Big-pad gate | pads 3, 4, 15, 18, 29, 30, active only, 2D distance <= 1500 | same | EXACT |
| Teammate/opponent slots | padded slots are shuffled too (random order every build) | same | EXACT (this means **obs, and so the action, can differ between repeats**) |
| Mask | their `DefaultAction` (air mask starts at `numGround + 1`) | same | EXACT |
| Ball prediction, training | cached tracker refreshed every 32 ticks, read at `tick + age` | fresh RocketSim prediction every step | ON PURPOSE (matches the live deployment) |
| Ball prediction, live | RLBot's prediction, nearest slice to now + 0.25/0.5/1/1.5/2 s | ours at ticks 30/60/120/180/240 | ON PURPOSE; the engines differ (1.6 only) |
| Live flip availability | estimated from total airtime since the jump | sim's real value | live-only difference (1.6 only) |

## ChronosV2
| Item | Original | Port | Verdict |
|---|---|---|---|
| Obs layout (225) | ball 9, prev action 8, 34 pads, self 29, 2 teammate slots, 3 opponent slots; ball prediction off | same | EXACT |
| x-mirror | when the (orange-inverted) car's x > 0, pos, rotation, vel, angvel of every object are mirrored, the previous action is mirrored, pad lookups use the x-mirrored pad | same, mirror pad found by the same first-match rule | EXACT |
| Pad timers | `GetBoostPadTimers` has the typo (returns the other frame's timers) | ported verbatim | EXACT (deliberate: this is what the net trained on) |
| Slots | padded slots shuffled (random order every build) | same | EXACT |
| Action out | `ParseAction` negates steer/yaw/roll when the car is mirrored | same via `mirrorMap` | EXACT |
| Mask | same `DefaultAction` | same | EXACT |
| Live flip availability | from RLBot's `dodge_timeout` | sim's real value | live-only difference (1.6 only) |

## What this changes in the checklist
1. **1.2 golden test, Nexto:** compare in one fixed entity order and add a permutation test; the "first differing index" rule only makes sense after that.
2. **1.2 golden test, Galactus and Chronos:** the shuffle must be seeded or the comparison done per slot permutation; otherwise it is flaky by construction.
3. **1.3 "deterministic across repeats":** only true for Nexto; Galactus and Chronos can vary because of the slot shuffle (it is part of the original behaviour, not a port bug).
4. **Missing tooling:** the WSL python has no `torch` or `rlgym_compat`, so the Nexto Python reference needs an environment (`rlbot-run/nexto/requirements.txt`: `rlgym_compat` from JPK314, torch 2.4.1 cpu, numpy 1). Installing it is a download and needs your go-ahead. Galactus and Chronos have no Python reference; their reference is their C++ obs builders, which can be compiled from the pinned commits.
5. **No ACCIDENT found** in this pass. The three real open questions are the three VERIFY rows above (Nexto's `rlgym_compat` semantics, Nexto's entity-order invariance, CUDA/TF32 argmax flips).
