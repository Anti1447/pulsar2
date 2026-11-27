# Lessons learned (SIM memory)

Started 2026-11-27. Format adapted from **Self-Improvement Memory (SIM)**, "Self-Improvement Memory Enables Continual
Recursive Self-Improvement", Yu et al., arXiv:2611.18856. In the paper SIM is a JSON memory read by an agent that edits a
harness; here it is a Markdown file about changes to this repo and its training runs. The adaptation is ours.

Each **problem** is one root-cause hypothesis plus every **attempt** made against it: what changed and why, what was predicted,
what was observed, and where it happened.

## Rules

1. **Read the whole file** before proposing a change to training, rewards, run configuration or ops.
2. **Cite a problem** (`P#`) for each proposal, or open a new one when no existing root cause explains the failure.
3. **Write the prediction before the result is known** (metric, expected range, when to look).
4. **Record every attempt that was actually applied**, including ones that failed, regressed or stayed unclear. Options that
   were only discussed do not go in.
5. **Append, never rewrite history.** Later findings go in a new attempt or a dated note. Merge two problems only if their root
   cause is the same, keeping both histories.
6. **Past entries are experience, not orders.** Retry a tested direction only with new evidence. Give a lesson less weight when
   the setting differs (other lineage, engine, tick skip or opponent mix).
7. Mark `backfilled` when the entry was written after the outcome was known. `unverified` and `confounded` are valid results.

`docs/REWARD_CHANGE_LOG.md` stays the per-change log for rewards; this file groups changes by cause.

### Template

```
## P# Short name
Root cause (hypothesis): ...
Status: open | holding | resolved | refuted

### P#.A# date, steps
- What and why:
- Predicted:
- Observed:            (fixed / missed)
- Applied: yes         Backfilled: no
- Provenance: run, engine, tick skip, evidence file
```

---

## P1 Freed models keep their GPU memory
Root cause: `Model` created an optimizer and `~Model` never deleted it; the optimizer holds the parameters.
Status: resolved for the main leak; a small rise is unexplained.

### P1.A1 2026-11-27 12:42, 9.007B
- What and why: `~Model` deletes `optim`, copying disabled. In-use memory had grown 1.66 to 6.76 GB in 11.4 h (about 49 MB per
  25M steps, matching two freed policy copies of about 24 MB); the same climb is in logs from 2026-11-20 on.
- Predicted: rise per removal cycle settles near zero after about 10 cycles.
- Observed: cycles 1-4 still rose 52-163 MB (earlier runs rose more at startup too). Cycles 5-12: 2.095 to 2.147 GB, about
  7 MB per cycle. `GPU/Peak Reserved` 18.8 GB before, about 14 GB after, with brief spikes up to 17.9 GB (cause not examined).
- Applied: yes. Backfilled: no. Test: `tests/test_model_free.cpp` failed before, passed after; 645/645; CPU smoke ran clean.
- Provenance: Pulsar3, `run_logs/train-20261127-010123.log` and `train-20261127-124239.log`.
- Lesson: chart `GPU/Allocated GB` across a long run. Growth that matches (events x object size) identifies the owner.

## P2 A bare event reward pays a motion that has no required outcome
Root cause: the wavedash detector paid every landing after an airborne flip, so a repeatable wall dash earned income without
ball play or progress.
Status: holding.

### P2.A1 RocketGPU trial `stage4`, weight 10, fresh, tick 4
- What and why: wavedash push.
- Predicted: not recorded.
- Observed: about 10% of car decisions paid; a wall-dash loop about every 0.2 s. The same policy paid 10.2% on RocketSim v3,
  so the GPU engine is not the cause. No fresh CPU run at weight 10 exists, so that CPU training would find the loop is untested.
- Applied: yes. Backfilled: yes. Provenance: `docs/reward_scenarios/WavedashReward_2026-11-26.md`.

### P2.A2 2026-11-26, 5.462B, Pulsar3 weight 10 to 12
- What and why: user wanted more wavedash use.
- Predicted: more wavedash use; watch `Wavedash/Per Episode` and the 1v1 gauntlet.
- Observed: not separable; the same restart added uneven arenas (P4).
- Applied: yes. Backfilled: no.

### P2.A3 2026-11-26, `WavedashReward` replaced by `WavedashGainReward` 20
- What and why: pay only speed gained over the best energy of the previous second, +25% toward the ball. User: wavedashes are
  worth farming when they move the bot productively.
- Predicted: reward share under 5% with speed steady or rising; suspicious over 15% or touch ratio falling.
- Observed (to 2026-11-27 15:00): share mostly 0.05-0.15%, single readings near 1.9%; car speed readings about 1,150-1,380
  against a baseline of 1,197. Touch ratio not checked. Confounded with the CarEnergy changes (P3). Offline, the farming bot's
  pay fell to about 1/30 per decision. A policy trained against the new term has not been shown unable to farm it.
- Applied: yes. Backfilled: no. Provenance: `docs/reward_scenarios/WavedashGainReward_2026-11-26.md`.

## P3 One shaping term dominates the reward mix
Root cause (hypothesis): `CarEnergyPotentialReward` takes most of |weight x reward|, diluting other terms.
Status: open (what share is best is unknown).

### P3.A1 2026-11-26, weight 75 to 25
- What and why: about 61% of the first reward-share reading.
- Predicted: other shares rise; watch speed, wavedash rate, gauntlet.
- Observed: car speed read about 4% lower; cause not established.
- Applied: yes. Backfilled: no.

### P3.A2 2026-11-26 23:57, about 6.402B, weight 25 to 50
- What and why: middle value (user).
- Predicted: watch shares, speed, gauntlet.
- Observed (to 2026-11-27): share about 45-55%; speed readings about 1,150-1,380. Confounded with P2.A3 and continued training.
- Applied: yes. Backfilled: no.

## P4 1v1 gauntlet fell after team play was added
Root cause (hypothesis, not isolated): training exposure moved away from 1v1 (Phase B, then 15% uneven arenas).
Status: open.

### P4.A1 2026-11-26, Phase B at 5.003B, uneven arenas at 5.462B
- What and why: team play, then 2v1 / 3v1 / 3v2 (user).
- Predicted: not recorded as a number.
- Observed: 1v1 four-bot win rate about 28.6% to 14%; team modes rose from about 0 to a few percent. By 9.0-9.5B the 4-check
  average was about 17-21%. A revert rule was proposed and the user declined it.
- Applied: yes. Backfilled: yes.

## P5 A run comparison that differs in more than the thing under test
Root cause: engine, speed and training results get mixed unless steps and the playing engine are matched.
Status: holding (method works; the cause of the gap is unknown).

### P5.A1 2026-11-26, CPU run against RocketGPU run
- What and why: head-to-head on both engines with mirror controls, then a matched-step sweep.
- Predicted: not recorded.
- Observed: at 2B the CPU bot won 762-57 (argmax, CPU engine) and 795-50 on RocketGPU, so the result is not engine bias. The
  GPU run led at 200-600M step pairs; the CPU run led from 700M. About 66k against 100k steps/s.
- Applied: yes. Backfilled: yes. Provenance: `docs/ROCKETGPU_TRIAL_STAGE4_PREP.md`.

## P6 Reporting from one noisy or misread value
Root cause: log values read without checking their format or spread.
Status: holding.

### P6.A1 2026-11-26/27, corrections made
- What happened: a car-speed slide was overstated from one reading. Uneven ratings were reported wrong: the log line
  `> 1v2 = X (delta)` shows the previous value, so current = X + delta.
- Observed fix: report averages beside single readings; `tools/pulse.py` for status.
- Applied: yes. Backfilled: yes.

## P7 Side jobs on the one GPU disturb the live trainer
Root cause: the trainer, tests, test-loaded bots and games share GPU memory.
Status: open.

### P7.A1 2026-11-26
- What happened: smoke tests slowed Pulsar3 to about 4k steps/s. A packaged-bot test load coincided with a `CUDA error: unknown
  error` crash; other such crashes happened without one. Cause unconfirmed.
- Applied since: build and test in `build-uneven/`, smoke on CPU (2026-11-27).
- Observed: no crash on 2026-11-27 through 15:00. Too short, and other things changed, to credit the practice.
- Backfilled: yes.

### P7.A2 2026-11-26, light mode for gaming
- What and why: `tools/light.sh` lowers compute share and minibatch size.
- Predicted: the game runs acceptably.
- Observed: missed; the user found GPU memory near 100%. A restart freed memory. P1 probably contributed (not proven).
- Applied: yes. Backfilled: yes. Lesson: find which resource is short before choosing the throttle.

### P7.A3 2026-11-27 15:25, WSL restart with light mode on
- What and why: WSL RAM stayed high in light mode (user). Restart WSL from a Windows script; `autoMemoryReclaim=dropCache` made
  explicit (already the documented default).
- Predicted: modest gain, a few GB, because the trainer needs its RAM again and the cache rebuilds.
- Observed: restart and trainer start worked unattended. WSL's Windows memory 16.2 GB before, 6.6 GB at boot, 15.5 GB after 5
  minutes; cache 9.0 to 4.5 GB. Lasting gain so far under 1 GB.
- Applied: yes. Backfilled: no.

## P8 Bot picks up less boost than wanted
Root cause (hypothesis, not measured): the pickup term is a small part of the reward mix (about 1.4% share).
Status: open.

### P8.A1 2026-11-27, about 9.619B, GuardedPickupBoost 6 to 8
- What and why: user wants more boost pickup.
- Predicted: by 10.0B, reward share about 1.8%, `Player/Boost` level or up (baseline 20.8), speed (1203) and touch ratio
  (0.0057) not clearly down. A small effect or none is plausible.
- Observed: pending.
- Applied: yes. Backfilled: no.
- Provenance: Pulsar3, tick 4, `docs/REWARD_CHANGE_LOG.md`. Starts in light mode.

---

## Older lessons (backfilled index, earlier lineages)

Summaries only; the source has the detail. From `CLAUDE.md` unless noted.

| ID | Root cause | What happened | Rule it left |
|---|---|---|---|
| H1 | Shared critic cannot price truncated and full episodes of the same observation | Two Elo collapses (`research/reports/STEERED_PRACTICE.md`) | Whoever changes episode boundaries lets the critic learn the new returns |
| H2 | PPO clipping keeps positive and drops negative gradient for pushed actions | Elo bleed from activation steering | Any activation-space push inherits this |
| H3 | New model heads left at learning rate 0 | Composition critic frozen since introduction, still injecting | Check the update-magnitude panel of anything new |
| H4 | Checkpoint assembled from healthy and borrowed parts | Policy ended worse than its restore point | Restore a full known-good checkpoint or start fresh |
| H5 | Pool rating moves with the agent | `Rating/1v1` overstated skill about 6x | Judge skill against fixed references and real match play |
| H6 | Opponent roll used a generator reseeded on each new thread | Served rate 8.9% against 15% configured | Use the persistent generator |
| H7 | Mechanism enabled without evidence (run 9uz761ua) | Best run decelerated 10x | Measurement, success criteria, guard and revert path first |
