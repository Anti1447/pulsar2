# README FAQ

Plain-language answers about how this training setup works. Everything here was read from the source in this repo
(file and line pointers are given) and checked against your real training logs unless it says otherwise.

---

## Nexto

### What is Nexto?

Nexto is a public Rocket League bot from the RLGym community, trained with reinforcement learning, and the code
comments describe it as SSL-level (that is the author's description; I have not measured its strength here).
You have its model file at `rlbot-run/nexto/nexto-model.pt`. It is **never trained** by this project: its weights
are frozen, so it plays exactly the same way on day 1 and day 100.

### What is it doing in my training?

In a fraction of training iterations it replaces your bot's opponent. Instead of your bot playing a copy of itself,
the other team is played by Nexto. The point (per the comments in `NextoOpponent.h`) is twofold:

1. **Exposure.** Nexto plays in the air a lot. Playing against it fills the training data with high-ball situations
   that self-play almost never produces.
2. **A fixed yardstick.** Because Nexto never changes, how your bot does against it is comparable across the whole run.
   Your other rating (`Rating/1v1`) is measured against *older copies of your own bot*, which keep changing.

### How does one "Nexto iteration" work?

Step by step (`Learner.cpp` around lines 1440-1470, 1900-1920; `NextoOpponent.h/.cpp`):

1. At the start of every training iteration the trainer rolls a random number. With probability
   `serveFrac = 0.15` (set in `src/ExampleMain.cpp`) this is a **serve iteration**. Otherwise it's normal self-play.
2. On a serve iteration, **one team is picked at random (blue or orange) and Nexto plays it in every arena at once**
   (all 1,024 games at the moment, or however many `numGames` is). Your bot plays the other team.
3. Nexto is fed through an adapter: it builds Nexto's own observation from the game state, runs its model, and maps
   its 90 possible actions onto your bot's action table (the trainer refuses to start if any action can't be matched).
   Nexto starts each serve iteration "cold" (no memory of a previous action), like it does when it joins a real game.
4. Only **your bot's** experience is trained on. The rows for Nexto's cars are thrown away, so your bot learns from
   Nexto's behaviour but Nexto itself never changes.
5. While this happens, every goal is counted (next question).

### What are `Nexto/Goals For` and `Nexto/Goals Against`?

Two **running totals of goals scored in serve iterations**, across all arenas:

- **Goals For** = goals *your bot* scored against Nexto.
- **Goals Against** = goals Nexto scored against your bot.

How a goal is attributed (`Learner.cpp` ~line 1913): when the ball is in a goal, the code finds which team's goal it
is in. If that is Nexto's goal, your bot scored (For). Otherwise Nexto scored (Against).

Important properties:

- **They are cumulative.** They never reset between iterations and are saved in the checkpoint (`nexto_goals_for`,
  `nexto_goals_against`, `nexto_serve_iters` in the stats file), so they carry across stops and restarts. A
  brand-new run starts from 0.
- **They only grow during serve iterations.** In normal self-play iterations they stay flat. That is why they look
  like a staircase if you zoom in, and why you can see no change for several iterations in a row.
- **The level means little; the slope means a lot.** The code comments say the same thing. 487,597 goals for and
  471,341 against is just a running total. What matters is how fast each is growing relative to the other.
- **Counts depend on how many arenas were running.** `numGames` is currently 1,024, and every
  arena can score, so an iteration with more arenas produces more goals. Compare ratios, not raw counts, across that change.
- Every arena counts, including goals scored right after a kickoff and goals in arenas Nexto joined mid-play.
  (One code comment mentions skipping special "impossible-control" practice arenas; those are disabled in this
  run, `g_NumImpossibleArenas = 0`, so it doesn't matter here.)

### What is `Nexto/Serve Iters`?

How many serve iterations have happened so far (also cumulative). Your logs show 1,685 serve iterations out of
11,183 iterations, which is **15.1%**, matching the 0.15 setting. It's a sanity check that serving is really happening.

### What is "Nexto goal share" (the dashboard tile)?

It is **not** a metric the trainer reports. The dashboard computes it:

```
share = (Goals For gained in the last 60 iterations)
        / (Goals For gained + Goals Against gained in the last 60 iterations)
```

So it answers "of the goals scored against Nexto recently, what fraction were ours?" 50% means even, above 50%
means your bot is outscoring Nexto, below means Nexto is winning.

Details to know:

- It looks at the last 60 iterations only. At a 15% serve rate that window contains about 9 serve iterations on
  average, enough to be stable because each serve iteration produces hundreds of goals.
- If a 60-iteration window happens to contain no serve iteration, no goals were added, so the tile has nothing to
  show and is hidden or looks stale. That's normal, not a fault.
- It is a **goal share, not a win rate**. A bot with 86% of the goals is far ahead, but this does not say how
  often it wins a timed match.

### How has your bot done against Nexto?

From your logs (share of goals in each window, serve iterations only):

| Up to | Your goal share | | Up to | Your goal share |
|---|---|---|---|---|
| 0.5B steps | 5% | | 1.75B | 81% |
| 0.75B | 22% | | 2.0B | 84% |
| 1.0B | 43% | | 2.25B | 86% |
| 1.25B | 63% | | 2.5B | 85% |
| 1.5B | 72% | | 2.78B (latest) | 86% |

Reading it: the bot went from being scored on almost every time to roughly even around 1B steps, and has been
at about **85% of the goals** since about 2B. The curve has flattened, which suggests that Nexto no longer teaches
the bot much more. (That last sentence is my interpretation of the curve, not something the code states.)

### Why not just use `Rating/1v1`?

`Rating/1v1` is an Elo-style rating from matches against **older copies of your own bot**. Those copies are
continually replaced, so the rating can drift upward without the bot getting objectively better (the code
comments call this "pool inflation"). Nexto doesn't change, so its goal share can't be inflated that way.

### Where do I see these numbers?

- Dashboard: the **Nexto goal share** tile, and the **Skill & opponent** tab (charts for For and Against; use the
  search box for "Nexto"). Smooth the chart a bit, because the totals rise in steps.
- Trainer log / W&B: `Nexto/Goals For`, `Nexto/Goals Against`, `Nexto/Serve Iters`.

### Can I change it or turn it off?

Settings live in `src/ExampleMain.cpp` (search for `externalOpponent`), then rebuild with the trainer stopped:

- `serveFrac` (0.15): the chance an iteration is a serve iteration. Higher means more Nexto and a more precise
  yardstick, but proportionally less self-play. Changing it does not break checkpoints.
- `modelPath` and `enabled`: the model is loaded at startup. This clone enables Nexto only if the model file exists
  (a local edit), so a missing file means plain self-play instead of a crash.
- Smoke tests (`GGL_SMOKE=1`) serve Nexto 75% of the time so a short test exercises the adapter.
- The viewer (`GGL_RENDER=1`) never serves Nexto.

### Is this the same as playing Nexto in Rocket League?

No. This is only the **training** opponent inside the simulator. Playing your bot against Nexto in the real game
uses the RLBot match configs in `rlbot-run/` (for example `match_vs_nexto.toml`) and is separate from these counters.

### What I could not verify

- Nexto's actual skill level (the "SSL-level" claim comes from a code comment).
- Whether the flattening around 85% means more Nexto exposure has stopped helping; that is a reading of the curve.
- How the dashboard tile behaves while W&B is online; it is computed only from the local log.

---

## Phase B (team curriculum)

### Does Phase B start automatically?

Yes. The rule is in `src/ExampleMain.cpp` (`PHASE_B_RATING_TRIGGER = 1200`, `PHASE_B_TRIGGER_STREAK = 3`):
**`Rating/1v1` must be at or above 1200 on 3 consecutive skill-tracker evals.** Evals happen every 16 to 40 iterations, so
the streak counts evals, not iterations. Any eval below 1200 resets it to 0.

### What happens when it fires?

1. The trainer writes the marker file `build/checkpoints_5.3/PHASE_B_ENGAGED` (with the step and rating) and logs a "PHASE B ENGAGED" banner.
2. It saves a checkpoint and exits with code 99. `run_trainer.sh` treats that as a crash and relaunches it a few seconds later.
3. The relaunched process sees the marker and boots into Phase B on the same checkpoint (the observation size does not change):
   - Training arenas go from all 1v1 to about a third each of 1v1, 2v2 and 3v3.
   - `TEAM_SPIRIT` (how much teammates share reward) goes from 0.3 to 0.6.
   - The skill tracker also evaluates 2v2 and 3v3, so `Rating/2v2` and `Rating/3v3` start to appear.

### Things to know

- **It is one-way.** To go back, delete the marker file and restart. To force Phase B yourself, create the file and restart.
- **The streak counter lives only in the training process.** Stopping and restarting resets it to 0 (the checkpoint is not affected).
  The dashboard shows when a restart threw away a streak.
- **Phase B needs about twice the compute.** At 1,024 games, players per step go from 2,048 (all 1v1) to about 4,094, so expect lower
  steps per second and more GPU, CPU and fan load.
- **1200 is not an absolute skill level.** The comments say `Rating/1v1` is inflated because it is measured against old copies of the bot.
  The 1200 figure was tuned for an earlier run's plateau of about 1250 to 1300, with eval noise of roughly +/-30 to 50.
- **Changing the threshold** means editing `PHASE_B_RATING_TRIGGER` and rebuilding with training stopped. The dashboard reads the
  number from the source file, so it stays in sync.
