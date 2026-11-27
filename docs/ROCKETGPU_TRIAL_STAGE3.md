# RocketGPU trial, stage 3 report: speed (2026-11-25)

Plan: [ROCKETGPU_TRIAL_PLAN.md](ROCKETGPU_TRIAL_PLAN.md). Earlier stages: [1](ROCKETGPU_TRIAL_STAGE1.md), [2](ROCKETGPU_TRIAL_STAGE2.md). Code: `~/pulsar_rg`.

## Gate B2: passed

Both on this RTX 3090 at 250 W, the same morning, each with the GPU to itself.

| | Median player steps/s | Total steps / total time | Learning passes per sample | Measured over |
|---|---|---|---|---|
| Pulsar3, before the trial runs | 53,152 | 52,281 | 1.17 | 41 iterations |
| Pulsar3, after | 52,780 | 44,915 | 1.15 | 41 iterations |
| Trial | **121,657** | 121,508 | 1.255 | 36 updates, after 20 warm-up |

The gate was 2x Pulsar3's median (about 106,000). The trial reached **2.3x** by medians (2.7x by total steps over total time against the second reading), while
learning from each sample slightly more often than Pulsar3 did. Pulsar3's speed varies much more from iteration to iteration (4,600 to 89,000) than the trial's
(116,000 to 127,000).

Trial settings: 8,192 arenas of 1v1, tick skip 4, 32 decisions per update, minibatch 32,768, BF16, compiled trainer and compiled observation and reward functions.

## How it got there

| Change | Effect (player steps/s) |
|---|---|
| Start (stages 1 and 2 as written), 1 pass | about 55,000 |
| Keep ball predictions and recompute only balls that left them, as Pulsar3 does | about 102,000 |
| Learn 1.255 times per sample, reward constants built once | about 95,000 |
| Compile trainer, observation and rewards | about 120,000 before the 20 s cutoff began firing, 92,000 to 94,000 after |
| Draw start states ahead of time, recompute only the predictions needed at each point | 121,700 at steady state |

Ball prediction had been about 136 ms of each 170 ms decision. RocketGPU can carry a prediction 4 ticks further for about 1 ms, against about 50 ms to recompute
all 8,192.

Checked after the changes: cached prediction within 5 uu of a from-scratch one on 99.98% of rows 2 s ahead; observations for a subset of arenas identical to the
full ones; the compiled reward step matches Pulsar3's recording; the no-touch cutoff still lands on decision 600; observation and reward parity still pass.

Two local changes were made to RocketChad3's `train.py` (AGPL-3.0): fractional learning passes, and asking the task which arenas can time out so the extra
observation is built only for those.

Pulsar3 was paused for about 35 minutes in total, in nine windows, and restarted after each.

## What the number does not include

- The trial has no external opponents or old-version opponents (Pulsar3 spends inference on both), none of the research networks (by design), and not yet
  Pulsar3's advantage filter, return normalization or Muon. Some of these will cost speed.
- It was measured over the first few minutes of a fresh bot. Speed may change as the bot touches the ball more.
- This says nothing about learning speed per hour. That is gate C.
- RocketGPU's own tuning settings were not applied; the engine step is only about 5 ms of each decision.

## Before stage 4

Pulsar3's advantage filter, return normalization, Muon optimizer, learning rate and entropy scaling have to be matched in RocketChad3, and a script has to export
checkpoints every 30 minutes and score them with Pulsar3's five-bot evaluator. The trial's value loss (about 60, against about 0.004 for Pulsar3's normalized critic) shows the
normalization is needed. Stage 4 then needs Pulsar3 paused for up to about 12 hours.
