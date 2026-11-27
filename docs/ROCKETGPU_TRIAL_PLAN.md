# RocketGPU + RocketChad3 trial: plan for approval

Written 2026-11-25. Nothing in this plan has been started.

## The question

Does a bot trained on RocketGPU with RocketChad3 get stronger **per hour on this RTX 3090** clearly faster than Pulsar3 does?
Steps per second is only a gate on the way there; it is not the answer.

## What is known now

| | Measured | Caveat |
|---|---|---|
| Pulsar3 speed | about 50k player steps/s with BF16 | first live reading, noisy; re-measured at gate B |
| RocketChad3, policy + value sized like Pulsar3 (19.1M) | 220k, about 180k at Pulsar3's learning per sample | 3v3, none of our observation, rewards or start states |
| RocketChad3, Pulsar3's full compute (38.8M) | 108k, about 90k at equal learning per sample | same; extra value networks were copies |
| RocketGPU accuracy | 100% on its author's recording suite (our simulator: 86% of segments) | the suite is the author's own |

The full-compute figure is an upper bound of about **1.8x**. With our research networks included, the stack would probably miss the 2x speed gate below.

## Decisions for you

1. **Research networks in the trial?**
   - **(A, recommended) Leave out** the goal critic, composition critic and reachability heads. This asks whether the new stack is worth moving to at all, at the least cost.
     The goal critic and composition critic change what the policy is rewarded for, and reachability shapes the shared trunk, so the trial bot is **not** Pulsar3 on a faster engine.
   - **(B) Include them.** A cleaner comparison, but much more porting, and the speed upper bound is already below the gate.
2. **GPU time.** Stage 4 needs Pulsar3 paused for up to about 12 hours. Earlier stages need only short pauses.
3. **License.** RocketChad3 is AGPL-3.0. The plan keeps it in a separate directory (`~/pulsar_rg`), not copied into `pulsar2`. If you may distribute the bot or code, check what that license requires first.

## Scope

- 1v1 only (Pulsar3 is still in its 1v1 phase). Tick skip 4, action delay 0.
- Same as Pulsar3: 278-value observation, 90 actions with action masking, policy / shared / critic layer layout, reward stack and weights, reset mix, 20 s no-touch rule, gamma, GAE lambda, Muon, BF16, entropy strength (converted to RocketChad3's scale).
- Opponents in the first run: self-play and its own old versions. **No external bots**; porting them is left for after a pass.
- Pulsar3's code, checkpoints and live run are not modified. Any new tool on the `pulsar2` side is read-only (state dumps, evaluation).

## Stages and pass marks

Each gate is pass or stop. A stop ends with a short report, not with a looser mark.

### Stage 1: observation, actions, export (about 1 day of work, rough estimate)
- Pin both repositories to fixed commits.
- Write the 278-value observation on the GPU. The 48 ball-prediction values use RocketGPU's `ball_path` (already callable from Python). It needs arenas with no cars, so a second, car-free copy of the ball states is stepped; its cost is unmeasured and counts in gate B2.
- Add action masking to sampling and to the learning loss (a change inside RocketChad3, not only in its example files).
- Export trial weights into Pulsar3's checkpoint format, so Pulsar3's own evaluator can judge them.

**Gate A**
- Same recorded states in both builders: the 230 state values agree within 0.00001; ball-prediction differences are reported in game units (engines differ, so no exact match is expected).
- Masks are identical on every recorded row.
- Pulsar3's weights loaded into the trial network give the same action probabilities (within 0.0001), and a round-trip export plays in the evaluator at Pulsar3's own level.

### Stage 2: rewards, start states, endings (about 2 to 3 days, rough estimate)
- Port the Pulsar3 reward terms, including the ones that keep memory between steps and the ones driven by touch, demo and boost events.
- Port the reset mix and the no-touch truncation.

**Gate B1 (match):** on recorded trajectories, each state-based term agrees within 0.001 on at least 99.9% of rows; each event-based term fires on the same events at least 99% of the time. Every mismatch is explained in the report.

### Stage 3: speed (a few hours; Pulsar3 paused under 1 hour)
Everything from stages 1 and 2 switched on, same network sizes, learning per sample matched to Pulsar3, 250 W, best known RocketGPU settings.

**Gate B2 (speed):** at least **2x** Pulsar3's median player steps/s, both measured on the same day over at least 30 iterations.

### Stage 4: learning (up to 12 hours of GPU time)
One run from a fresh start. Every 30 minutes, export and play the five-bot evaluation with Pulsar3's evaluator (96 games per bot).

Pulsar3's reference, from its own logs (cold start, full precision, hours of training time):

| Timesteps | Hours | Win rate vs Dice | Win rate, all five bots |
|---|---|---|---|
| 1.0B | 8.4 | 2% | 2.2% |
| 2.0B | 15.4 | 7% | 9.8% |
| 3.5B | 29.2 | 16% | 18.1% |

- **Dice is the primary measure:** Pulsar3 never trains against it, while it does train against several of the others. A trial bot without external opponents would be at a disadvantage on those.
- BF16 would have shortened Pulsar3's hours by perhaps 15 to 20% (estimate), so the marks allow for that.

**Gate C (average of the last three evaluations, so about 290 games against Dice)**
- **Pass:** Dice at least 7% **and** all five at least 9.8% within **10 hours**.
- **Fail:** not reached within **13 hours**.
- **Between:** reported as inconclusive; you decide.
- **Early stop:** all-bot win rate below 2% at 8 hours.
- **Also stop on:** non-finite values, a crash loop, or entropy outside 0.40 to 1.20.

## What a pass would and would not show
- It would show this stack, with this reduced setup, learns faster per hour on this card in 1v1.
- It would not isolate the engine from the learner, cover team play, or say anything about the research networks (under choice A).
- One run, one seed. Evaluation happens in Pulsar3's simulator, which may count against a bot trained on slightly different physics. A real-game check would come next.

## After a pass (separate plan, separate approval)
External opponents, team modes, the research networks, bot packaging, and only then whether to stop Pulsar3.

## Undoing it
Delete `~/pulsar_rg` and any read-only tools added to `pulsar2`. Pulsar3 is unaffected throughout; after any pause it restarts with `cd build && ../tools/run_trainer.sh --detach -- ./GigaLearnBot --pulsar3`.
