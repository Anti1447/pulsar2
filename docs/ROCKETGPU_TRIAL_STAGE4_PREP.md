# RocketGPU trial: ready for stage 4 (2026-11-25)

Plan: [ROCKETGPU_TRIAL_PLAN.md](ROCKETGPU_TRIAL_PLAN.md). Earlier: stages [1](ROCKETGPU_TRIAL_STAGE1.md), [2](ROCKETGPU_TRIAL_STAGE2.md), [3](ROCKETGPU_TRIAL_STAGE3.md).
**The learning run has not been started.**

## Learner, matched to Pulsar3

| Part | How it works |
|---|---|
| Rewards | divided by the running spread of discounted returns, clipped to 50; advantages are not normalized |
| Policy loss | only the half of samples with the largest advantage size; entropy over all, divided by ln 90, weight 0.035 |
| Optimizer | Pulsar3's Muon (and its Adam for vector parameters), learning rate 0.00015, gradient limit 0.5 per network, BF16 |
| Steps | 3 large optimizer steps per 524,288 samples, each on 229,376 (Pulsar3: about 2 steps of about 225,000 per iteration) |
| Learning per sample | 1.31 passes (Pulsar3 measured 1.15 to 1.26) |

RocketChad3's own learner is bypassed (a plugin hook added to its `train.py`).

**Check against Pulsar3's own code:** two optimizer steps from the same weights on the same batch give weight updates that differ by about 0.001% of the update
(all four networks); reported losses, entropy and KL are identical to six decimals; reward scaling and advantage calculation agree to 0.00002.

**Still different:** the trial computes its return spread over a sliding 192-decision window (Pulsar3 uses each iteration's slice of about that length), and
uses 32-decision rollouts. No external or old-version opponents, no research networks (as planned).

## Speed with the matched learner

**120,809 player steps/s** (median over 47 updates), 2.3 times Pulsar3's 52,780. Gate B2 still passes.

## Evaluation and pass marks

Each checkpoint (every 30 minutes) is exported and played by a frozen copy of Pulsar3's binary against the five bots, 480 games each (about 50 s, while training
continues). Scoring older frozen copies of Pulsar3 with the same evaluator:

| Pulsar3 at | Training hours | Offline: all five / Dice | In-run, nearby: all five / Dice |
|---|---|---|---|
| 0.68B | 6.1 | 1.2% / 0.6% | 1.0% / 1.2% |
| 1.05B | 8.8 | 4.0% / 3.1% | 2.7% / 2.6% |
| 1.70B | 13.3 | 5.1% / 6.9% | 3.5% / 3.5% |
| 2.70B | 21.9 | 14.1% / 11.2% | 13.1% / 11.2% |
| 3.35B | 28.2 | 15.8% / 16.2% | 16.4% / 13.0% |

The approved marks (Dice 7% and all five 9.8%) correspond to roughly Pulsar3 at about 2.0B to 2.7B on the offline evaluator, that is 15 to 22 hours of Pulsar3
training. They are left unchanged. The offline scores of the young copies run somewhat above the in-run ones, so the offline evaluator is not stricter.

## The run

- `bench/stage4.sh` stops Pulsar3, trains, scores, applies the plan's stop rules, and restarts Pulsar3 on any exit.
- A crashed trainer resumes from its last checkpoint (at most 30 minutes lost); three quick crashes end the run.
- It ends on pass, on 13 hours without a pass, on the 8-hour early stop, on non-finite values or entropy outside 0.40 to 1.20, or at 15 hours of wall time.
- Tested in short form: start, scoring during training, a forced crash and resume, stop, and Pulsar3's restart.
- Stop it by hand: `touch ~/pulsar_rg/runs/stage4/STOP`.

```bash
mkdir -p /home/nhafe/pulsar_rg/runs/stage4 && systemd-run --user --unit=rg-stage4 --collect -p StandardOutput=append:/home/nhafe/pulsar_rg/runs/stage4/runner.log -p StandardError=append:/home/nhafe/pulsar_rg/runs/stage4/runner.log /home/nhafe/pulsar_rg/bench/stage4.sh /home/nhafe/pulsar_rg/runs/stage4
```

Not covered by the tests: a run of many hours (memory growth, GPU lockups, the PC sleeping or rebooting). A reboot ends the run and Pulsar3 does not restart by itself.

---

## First run (2026-11-25, 11:55 to 13:45): stopped, the bot farmed wavedashes

Self-play only, Pulsar3's current reward list. At 0.66B steps: all win rates 0%, learner touch rate 0.032% (an untrained bot: 0.031%; Pulsar3 at that age: 0.71%).
Wavedash (weight 10) was paid on 10% of decisions and gave +1.03 reward per decision; RolvWavedash +0.13; everything else together about -0.13. Both are
per-event bonuses that take nothing from the opponent. Pulsar3 added the weight-10 term only at about 1.7B steps; its live rate is about 0.03% of decisions.
Not tested before the run: whether the current reward list can be farmed by a bot starting from nothing. Stopped by the user.

## Changes for the second run (user-directed)

| Change | Detail |
|---|---|
| Opponents | 30% of arenas against Pulsar2, GalactusV2, ChronosV2 and Nexto (7.5% each); 70% self-play with the current policy; no old versions. Dice is evaluation only. Fixed per arena (Pulsar3 picks one per iteration); the learner is blue in half of each bot's arenas and only its side is trained on |
| Rewards | Wavedash off; BallProximity 4 -> 6. Every other term as Pulsar3, including RolvWavedash at 5 (user-directed). RolvWavedash is a per-event bonus that only adds reward, and in the first run it also paid (+0.13 per decision), so it is watched in this run |
| Size | 12,288 arenas (was 8,192); 4 optimizer steps of 196,608 rows per update, 1.18 learning passes per sample (Pulsar3 1.15 to 1.26) |

Checks: the ported bots choose the same action as Pulsar3's code in all 4 x 192,000 recorded player-decisions on the GPU, compiled (CPU: all but one Galactus
decision). Pulsar3's 4.2B weights against the ported bots in the trial engine win 7.9 / 10.2 / 18.3 / 41.5%
(Pulsar2, Galactus, Chronos, Nexto; Pulsar3's evaluator: 8.7 / 11.3 / 16.0 / 36.9%). Speed with bots: **about 125,000 learner steps/s, 2.4 times Pulsar3**
(at 8,192 arenas it was 1.95 times, below the gate, so the size was raised). Peak trainer GPU memory 9.6 GB; training, evaluation and resume tested.
The second run started 2026-11-25 afternoon in `~/pulsar_rg/runs/stage4b`.

## Arena-count speed sweep (2026-11-25, 14:57 to 15:41, user request)

Each size resumed from the 0.22B checkpoint of the second run: 20 warm-up and 16 measured updates, the same learning on each sample (1.18 passes, steps of
196,608 rows; sizes are multiples of 3,072), 250 W. Script: `~/pulsar_rg/bench/sweep.sh`, logs in `~/pulsar_rg/runs/sweep/`.

| Arenas | Learner steps/s | Playing steps/s | Optimizer steps per update | Update seconds | Peak GPU MiB |
|---|---|---|---|---|---|
| 6,144 | 91,915 | 142,257 | 2 | 3.64 | 7,790 |
| 9,216 | 110,773 | 193,706 | 3 | 4.53 | 8,752 |
| 12,288 | 123,499 / 122,092 (repeat at the end) | 236,099 | 4 | 5.41 | 9,552 |
| 18,432 | 132,816 | 275,063 | 6 | 7.55 | 11,352 |
| 24,576 | 142,406 | 320,707 | 8 | 9.39 | 13,256 |
| 33,792 | 151,595 | 373,470 | 11 | 12.13 | 16,567 |
| 49,152 | 160,441 | 435,139 | 16 | 16.67 | 21,372 |

Speed only: whether the bot learns as much per step with larger, less frequent updates was not measured. Memory peaks are without a concurrent evaluation.
The script's own resume of the run failed (a variable overwritten by `env.sh`; fixed); Pulsar3 was restarted by its fallback and the run was resumed by hand at
15:43. About 5 minutes of training since the previous resume were discarded.

**Size changed (user-directed): 24,576 arenas from the save at about 1 training hour** (about 16:13), 8 steps of 196,608 rows per update (1.1765 passes).
The second run therefore trained its first hour at 12,288.

**Save and pause on request (user-directed):** `~/pulsar_rg/bench/ctl.sh save|pause|resume|status`. The trainer checks between updates; a pause saves, ends the
run and lets Pulsar3 restart; resume continues from that save (matches in progress restart from fresh start states). The runner's stops now ask for a save first.
Saves less than 0.4 training hours after the last scored one are not scored, so the pass rule still uses half-hour spacing. Tested on a 3,072-arena copy:
save, continue, save-and-exit, resume at the next update. The size switch relaunches the runner so that this applies from then on.

Switched at 16:14 from `checkpoint_651.pt` (1.00 h, 0.44B): first updates 145,000 to 149,000 learner steps/s, 13.2 GB GPU memory.

**Packaged (user request): Pulsar3GPU v0.1.0** from the second run's save at 1.30 training hours (583,541,376 steps), `rlbot-run/releases_pulsar3gpu/`, installed
into RLBot v5. Export: `~/pulsar_rg/tools/export_bot.py <checkpoint.pt>`, then `tools/package_bot.sh --checkpoint <exported folder>`.

**Online charts (user-directed):** `~/pulsar_rg/tools/wandb_upload.py` (systemd unit `rg-wandb`, started by `ctl.sh resume` too) sends the run to the wandb project
`gigalearncpp` as run `pulsar3gpu-stage4b`; x axis Total Timesteps, chart names as Pulsar3 where they mean the same.

**16:38, user-directed:** ball proximity weight back from 6 to Pulsar3's 4 for the rest of the second run (saved, trainer restarted from that save).

## Research critics ported (2026-11-25 evening, user-directed)

Goal critic, V-dagger twins and geometric critic: `~/pulsar_rg/pulsar/critics.py`, on with `STAGE4_CRITICS=1` (off by default; pre-change copy in
`~/pulsar_rg_pre_critics`). Check against Pulsar3's code (`checks/parity_critics.py`, CPU, from Pulsar3 checkpoint 4,494,843,904): identical losses, weight
updates of all ten networks within 0.0014% of the update, injections within 2e-6. One deliberate difference: the geometry reservoir takes only transitions
inside an episode (Pulsar3 also stores the jump into the next row's episode).

GPU, 24,576 arenas, 4-deep heads, from the second run's save at 0.62B (critics fresh): **66,946 learner steps/s with the critics against 140,776 without**
(collection 7.4 against 4.3 s per update, learning 12.6 against 5.2 s); peak GPU memory 17.4 against 14.8 GB. All heads update, the injected optimism term is
0.04 of the advantage spread, and a save and resume carries their state (the reservoir refills, as in Pulsar3). Not optimized yet. Not used in any training run.

## Third run prepared: profile tick8 (2026-11-25, user-directed; not started)

`~/pulsar_rg/pulsar/profile.py`: tick skip 8, action delay 7 (added to the engine step), policy 2 x 1024, critic trunk and value heads 2 x 1280, shared head
2 x 1152, gamma 0.9969, goal gamma 0.9994, lambda 0.95, per-step rewards at full weight, the second run's reward list, outside bots at the learner's rate,
1v1, 196,608-row steps. Run directory files `profile`, `critics`, `wall_seconds` select it. Scoring: a separate build `pulsar2/build-p2gpu` with
`GGL_P2GPU=1` (`ConfigureP2GPU`), template `ref_checkpoint/tick8`; no pass marks. Packaging lineage Pulsar2GPU (`checkpoints_pulsar2gpu`).
Checks against Pulsar's code at this timing: rewards and derived events 100% on 768,000 rows (CPU and compiled GPU), the four bots 100% same action, critics
and learner updates within 0.0016%; engine delay: one tick of throttle in the first decision.

| Critics (24,576 arenas, fresh bot, 21 updates timed) | Learner steps/s | Update s (collect + learn) | Peak GPU MiB |
|---|---|---|---|
| none | 190,636 | 4.14 + 2.87 | 11,746 |
| goal | 158,706 | 4.67 + 3.76 | 12,622 |
| goal + V-dagger | 125,765 | 5.15 + 5.48 | 13,873 |
| goal + V-dagger + geometry | 105,315 | 5.16 + 7.54 | 13,877 |

Third run (`runs/stage4c`, all three critics, user-directed): scheduled to start 2026-11-25 19:05 (`bench/start_run.sh`), 15 h wall limit.

## Third run started; CPU comparison run armed (2026-11-25)

- Third run (`runs/stage4c`, tick8, all three critics) started 18:58. About 105,000-110,000 learner steps/s early on, against about
  50,000 for Pulsar3's logs of the same day (different network depth, tick skip, reach heads, gap sensor and old versions).
- User-directed: at 2B steps, switch automatically to a FRESH CPU-engine run for a 1:1 comparison. `GGL_P2GPU=train` with
  `--pulsar3` (`src/ExampleMain.cpp`, `ConfigureP2GPUTrain`), built in `build-p2gpu/`, folder `checkpoints_p2gpu_cpu`, wandb
  `pulsar2cpu-t8`. As the GPU run: tick skip 8 / delay 7, same gammas, 2-deep policy and value side, goal + V-dagger + geometric
  critics, same reward list (Wavedash 0), 1v1, no old versions, 30% outside bots. Chosen by the user: reach heads and gap sensor
  off; the bot share per iteration (one opponent for all arenas, the existing CPU code) rather than fixed arenas; stop at 2B.
- Known differences left: arenas 1,024 vs 24,576; opponent assignment as above; 200,000-step iterations with 2 epochs over the
  filtered rows vs 196,608-row steps at 1.18 passes; the physics engine; the geometric critic's cross-episode transitions (the CPU
  code includes them, the GPU port does not). The in-trainer skill tracker and gauntlet are off in both; both are scored by the
  same frozen evaluator, 480 games per bot, every half hour of training.
- Checked before arming: settings dump (4 serving bots, Dice held out, no old versions), a short smoke run (all nine networks
  moved between saves), resume from its save, and the evaluator on its checkpoint (8 games per bot). The smoke run and build cost
  the GPU run some speed for a few minutes (down to about 60,000 steps/s).
- `bench/handoff.sh` (unit `rg-handoff`) saves and ends the GPU run at 2B without restarting Pulsar3, scores that save, and starts
  `bench/cpu_stage.sh` (unit `rg-cpu`, run folder `runs/p2gpu_cpu`), which stops at 2B, scores, and restarts Pulsar3. Cancel with
  `systemctl --user stop rg-handoff`; it also cancels itself if the GPU run is paused or ends first. `bench/hold.sh` stops all.

## Reward charts and the 2B head-to-head (user-requested 2026-11-25)

- GPU run reward charts since update 157 (saved and resumed at 19:37, about 1.5 minutes lost): `Rewards/<C++ name>`, the same
  names and meaning as Pulsar3's (raw value before weight and wrapper), averaged over every trained car's every decision (Pulsar3
  samples one car per sampled arena); `Reward Share %/<term>`, each term's share of the summed absolute weighted reward (new, GPU
  only); `Average Step Reward`. Reward parity (compiled, 4,000 steps) passed after the change. Rewards themselves are unchanged.
- Head to head at 2B (`pulsar/h2h.py`, run by `bench/cpu_stage.sh` after the CPU run's final score, before Pulsar3 restarts):
  GPU bot against CPU bot, 1,024 games argmax (primary read) and 1,024 sampled, half per side, fuzzed kickoff, ends at a goal, 10 s
  without a touch or 60 s (draw). New binary mode `GGL_H2H_MATCH=1`; tested at 64 games. Caveats: it runs on the CPU engine, which
  only the CPU bot trained on; weak bots draw most games. Results: `runs/p2gpu_cpu/h2h.jsonl`.

## Early wins against Pulsar2 explained (2026-11-25)

- At 578M the GPU bot scored 281-195 (4 draws) against Pulsar2 and at most 2 wins against the others. The loaded model is right:
  `rlbot-run/releases/v1.0.28/checkpoint` (44,566,197,613 steps); run natively (8/7) it plays near its packaged record
  (192 games: Chronos 66-118, Galactus 36-146, Dice 76-103, Nexto 192-0).
- Pulsar2 never gets the first touch against Galactus, Chronos, Dice or Nexto (also so in its packaged stats). The GPU bot gets it
  in 476 of 480 games against Pulsar2 (0 of 480 against Galactus and Dice) and games end after about 8 s: the wins are decided from
  the kickoff. Jittered kickoffs (`GGL_H2H_FUZZ=1`, 0.1 uu) give 304-172: not a replay of identical states.
- In training (sampled actions, mixed start states) its goal share against Pulsar2 was 10% at 560-620M, against 0.5-0.8% for the
  other three. Read the Pulsar2 score as a kickoff result, not general strength. Same bot and rules in the CPU comparison run.
- User-directed after the finding: Pulsar2 is reported apart (watcher, pulse lines, `Gauntlet/Four Bots Win % (without Pulsar2)` on
  wandb; the five-bot mean is still recorded). The CPU run's scores go to wandb as `pulsar2cpu-t8-gauntlet` (unit `rg-wandb-cpu`,
  started by `bench/cpu_stage.sh`; not yet run). RolvWavedash replaced by WavedashReward at 1 (GPU run resumed at 639M, 20:47,
  `bench/restart.sh`; CPU run from step 0, rebuilt). So the GPU run trained its first 639M steps on the old pair and the CPU run
  never does: a small known difference in the comparison.
- Light mode (user-requested, ~/pulsar_rg: `pulsar/throttle.py`, `bench/light.sh on [share] [small] | off | status`): the trainer
  idles between decisions and microbatches so it works a set share of the GPU's time; learning unchanged (`small` also drops to
  6,144 arenas, which does change the data per update). Measured on the live run: full 96-99k steps/s, GPU 94-95%, 14.7-14.9 GB;
  share 0.25: 25k, 24%, 14.9 GB; share 0.25 small: 16k, 23%, 9.3 GB (peak whole-card memory). The half-hour score is put off while
  it is on. Not yet tried with a game running; the CPU comparison run has no light mode. Turn it on before 2B and the GPU run's
  speed figures and (with `small`) its learning no longer compare cleanly.

## 1v1 rating along the runs' own saves (user-requested 2026-11-26; team and uneven modes later)

- `~/pulsar_rg/pulsar/rating.py` (units `rg-rating`, and `rg-rating-cpu` started by `bench/cpu_stage.sh`): every ~100M steps a save
  plays the previous rated save and a fixed anchor on the CPU engine (128 arenas, 300 s per colour, sampled actions, goals count).
  Rating = previous + 400 log10(goals for / against), per-pair limit 800. Anchor = GPU run's 173M save = 1000, for both runs.
  wandb: `Rating/1v1 (anchored)`, `Rating/Anchor Goal Share %` (CPU: in `pulsar2cpu-t8-gauntlet`). First: 173M 1000, 393M 1590,
  578M 1818. Read: a chain, so noise adds up; the anchor share saturates (97-99% already at 393-578M); home-field CPU engine for the
  CPU bot; not comparable with Pulsar3's Rating/1v1. Waits during light mode. No team or 3v2 / 3v1 / 2v1 ratings: not trained.

## GPU run reached 2B; CPU comparison run started (2026-11-26)

- 02:59: GPU run saved at 2,002,097,280 steps and ended; the switch ran as scripted. Final score at 2.003B (480 games per bot):
  GalactusV2 26 wins, ChronosV2 14, Dice 3, Nexto 10 (four bots 2.8%), Pulsar2 381 (79%). Rating 2366 (anchored). Elapsed training
  time 8.0 h, which includes about 2.5 h of light mode and play on the same GPU: compare speeds at full-speed rates (about
  100,000 steps/s), not by the elapsed time. The uploader was restarted for 150 s to send the final score and ratings.
- 03:01: CPU run started (`rg-cpu`, wandb run id zhn4camd plus `pulsar2cpu-t8-gauntlet`); first minutes about 58,000 steps/s.

## CPU comparison run reached 2B; both results (2026-11-26)

- CPU run 03:01-11:26, final save 2,001,206,272 steps. Counted training time 8.41 h, about 66k steps/s (scoring and rating
  matches shared the machine). GPU run: about 100k steps/s at full speed (its 8.0 h elapsed includes light mode and gaming),
  so about 1.5x. Pulsar3 restarted 11:28:41.
- Head-to-head, 1,024 games each, CPU engine (the CPU bot's own training physics, so it is favoured):
  argmax GPU 57 / CPU 762 / 205 draws; sampled GPU 117 / CPU 584 / 323 draws. No GPU-engine rematch has been run.
- 2B gauntlet (480 games per bot, wins) GPU vs CPU: GalactusV2 26/0, ChronosV2 14/15, Dice 3/6, Nexto 10/27 (four bots
  2.8% vs 2.5%); Pulsar2 apart 381 (79%) vs 9 (1.9%).
- Anchored rating at 2B: GPU 2366, CPU 2623. The anchor share is above 99% for both, so late gaps are chains of
  own-save matches and the two numbers are only loosely comparable.
- One run each: no seed-to-seed spread is measured.

## GPU-engine head-to-head rematch (2026-11-26, user-requested)

`pulsar/h2h_gpu.py` (pulsar_rg): same two 2B saves and game rules on RocketGPU, 1,024 games each, kickoffs jittered 0.1 uu.
CPU weights load into the torch network bit-exactly. Output: runs/p2gpu_cpu/h2h.jsonl (engine "rocketgpu"), h2h_gpu.log.

| actions | engine | GPU wins | CPU wins | draws | GPU share of decided |
|---|---|---|---|---|---|
| argmax | CPU | 57 | 762 | 205 | 7.0% |
| argmax | RocketGPU | 50 | 795 | 179 | 5.9% |
| sampled | CPU | 117 | 584 | 323 | 16.7% |
| sampled | RocketGPU | 111 | 585 | 328 | 15.9% |

Mirror controls on RocketGPU (share of decided games for the first copy): GPU 48.9% / 45.3%, CPU 53.4% / 49.0%.
Read: the CPU bot's win is not explained by engine home advantage.
Pulsar3 kept training alongside (slowed while the matches ran).

## Matched-step head-to-head (2026-11-26, user-requested)

`pulsar/h2h_sweep.py`: each GPU save against the CPU rated save nearest in steps, RocketGPU engine, 1,024 games per mode.
Share = GPU share of decided games. Output: runs/p2gpu_cpu/h2h_steps.jsonl, h2h_sweep.log.

| GPU / CPU steps (M) | argmax GPU-CPU-draw | share | sampled GPU-CPU-draw | share |
|---|---|---|---|---|
| 209 / 200 | 44-12-968 | 78.6% | 32-24-968 | 57.1% |
| 393 / 400 | 657-14-353 | 97.9% | 548-23-453 | 96.0% |
| 579 / 600 | 453-111-460 | 80.3% | 484-81-459 | 85.7% |
| 736 / 700 | 225-488-311 | 31.6% | 273-414-337 | 39.7% |
| 892 / 900 | 260-630-134 | 29.2% | 158-652-214 | 19.5% |
| 1117 / 1100 | 131-622-271 | 17.4% | 164-546-314 | 23.1% |
| 1298 / 1300 | 118-742-164 | 13.7% | 145-594-285 | 19.6% |
| 1478 / 1500 | 91-813-120 | 10.1% | 90-750-184 | 10.7% |
| 1658 / 1700 | 87-796-141 | 9.9% | 129-693-202 | 15.7% |
| 1839 / 1800 | 34-859-131 | 3.8% | 71-714-239 | 9.0% |
| 2003 / 2001 | 59-761-204 | 7.2% | 122-569-333 | 17.7% |

## CPU comparison run resumed past 2B; Pulsar3 paused (2026-11-26 13:21, user-requested)

- Pulsar3 stopped cleanly (saved). CPU run resumed from 2,001,206,272 with the same settings (still no old versions, reach
  heads and gap sensor off, 30% outside bots), unit rg-cpu, no step limit. Scores every 0.5 training hours, ratings every
  95M+ steps.
- Full 2B checkpoint kept at pulsar_rg/runs/p2gpu_cpu/final_2001206272 (the run's own folder rotates it out).
- Stop: `touch ~/pulsar_rg/runs/p2gpu_cpu/STOP` (the trainer saves, then Pulsar3 restarts by itself).
- 13:49: CPU run stopped on request at 2,110,134,272 (copy in runs/p2gpu_cpu/final_2110134272); rating at 2.1B 2745 (+122),
  uploaded; no new gauntlet score since resuming. Pulsar3 restarted 13:49:15.
