# GCO with the goal critic and V-dagger twins back on (design note, 2026-11-14)

Status: PROPOSAL / READ-ONLY AUDIT. Nothing here is implemented. Written from the code at HEAD (`v3-reward-port`)
and `docs/GCO.md`.

## 1. What the existing GCO setup is

GCO = "goal-concede-only main run" (commits `a90b789` .. `f2555ac`, 2026-11-01; branch `gco`, an ancestor of HEAD,
tag-free). `./GigaLearnBot --gco` trains a new main lineage in `checkpoints_gco` with an exploiter's recipe, but
against main's opponents (self-play, old versions, Nexto, imported exploiters) instead of a frozen target.
Canonical doc: `docs/GCO.md`. Code: `ConfigureGCO` and the `g_GCOMode` branch of the env builder in
`src/ExampleMain.cpp`; `SeedGCORun` in `src/ExploiterLauncher.cpp`; `gcoRun` in `LearnerConfig.h`; the gated policy-LR
schedule in `Learner.cpp` (~2990-3010).

| Item | GCO value |
|---|---|
| Seed | newest main checkpoint (`checkpoints_5.3/7835941626`): network weights only, fresh optimizers, no return stat |
| Reward | `GoalReward` (+1 / -1) plus `NoTouchTimeoutPenalty` -0.5 to every car (20 s no-touch is a real ending, not a truncation) |
| Resets | main's mix (was kickoff-only until 12.81B) |
| Fleet | 1/3 each 1v1 / 2v2 / 3v3 |
| Entropy | 0.4x main's |
| Policy LR | 10%, then +4.5% steps every 25M steps if the median critic EV over the window is > 0.41 |
| Skill tracker | on, 32 games; per-mode best archives |
| Opponents | same as main |
| Obs lineage | 5.3 (no ball-prediction obs). Main 6.0 cannot seed it: `g_BallPredObs = !gcoArg` |

### Which critics GCO kept: your recollection is right, with one nuance

`ConfigureGCO` sets, verbatim:

```
cfg.ppo.reachability.enabled = false;
cfg.ppo.goalCritic.enabled   = false;
cfg.ppo.vdagEnabled          = false;
cfg.ppo.geoEnabled           = false;
cfg.gapSensor.enabled        = false;
```

So GCO removed the goal critic, the V-dagger twins, the geometric critic, the V_exp gap sensor and the reachability
heads. What is left is `V_real`, the ordinary critic, at `gaeGamma = 0.9969` (about 15 s half-life). Nothing in
`ConfigureGCO` changes gamma.

Nuance: that surviving critic is not the shaped-return critic any more. Its reward is the goal-only channel (plus
the timeout penalty), so in GCO `V_real` is already a short-horizon goal value. The exploiter version of the same
slimming (`ConfigureExploiter`) is identical, and it was done for VRAM (three trainers on one 24 GB card), not
because the aux learners were judged harmful. GCO inherited the slimming.

## 2. The proposal, restated

(a) Re-enable the long-horizon goal critic (gamma 0.9994, about 77 s) in a GCO run.
(b) Make the short-horizon critic (gamma 0.9969) a goal-only critic, re-initialised, so the two critics see the
same reward and differ only in gamma.
(c) Keep the V-dagger twins on.

## 3. What the code does today that bears on this

1. **(b) is already true for the reward, not for the weights.** In GCO `V_real` trains on goal-only returns at
   0.9969. The goal critic trains on a separate goal channel (`combinedTraj.goalRews`, +1 own team scored / -1
   conceded, computed from game outcomes) at 0.9994. So with the goal critic on, you already have two critics on
   essentially the same signal at two horizons. The differences that remain are: the timeout penalty enters
   `V_real` but not the goal channel, and `V_real`'s GAE uses reward normalisation (`returnStd`) while the goal
   critic uses `returnStd=0, clipRange=0` (`Learner.cpp:3265`). "Mirror" is therefore exact only in structure,
   not in return scale.
2. **The blend.** `A = A_dense + betaEff * (A_goal - mean)`, `betaEff = beta * advStd / goalAdvStd`, beta 0.25
   (`Learner.cpp:3292`). With a goal-only dense reward both terms are goal advantages at different gammas, so the
   blend is a 75/25 mix of a 15 s and a 77 s credit horizon, std-matched. It is no longer "dense plus a long-horizon
   sparse signal".
3. **Architecture.** `goalCritic`, `critic`, `vdag1` and `vdag2` all read a shared `critic_trunk` (3x1280) through
   private 2x1280 heads (`ExampleMain.cpp:1409`). A goal critic is therefore cheap to add and already "mirrors" the
   `V_real` architecture. It also means these heads share trunk gradients, so re-initialising one head moves the
   trunk the others read.
4. **V-dagger twins** (`Learner.cpp:3055-3235`). Their TD target is rebuilt from GAE outputs in `V_real`'s units
   (`r_scaled = A_i - g*lam*(1-d)A_{i+1} - g(1-d)V_{i+1} + V_i`), and the seek injection `Phi = +H`,
   `H = relu(min(V†1,V†2) - V_real)`, is std-matched against the extrinsic advantage at `vdagSeekBeta`
   (config default 0.30; `CLAUDE.md` still says 0.15).

## 4. Risks and open questions

- **H on a sparse reward.** `H` is the optimism gap between the twins and `V_real`. With goal-only reward the value
  signal is nonzero only near goals and timeouts, so for most of the state space both heads sit near zero and `H`
  is mostly noise from the expectile bias. The injection is std-matched, so noise gets scaled up to the same size
  as the advantage. The composition critic was designed and validated against the shaped stack
  (`research/reports/COMPOSITION_CRITIC.md`); that paper does not cover a goal-only reward, and I found no
  measurement of it. This is the main reason to treat (c) as a hypothesis, not a safe default.
- **Re-initialising `V_real`.** GCO already seeds weights without optimizers or return stats, and its critic starts
  badly (EV swung 0.2 to 0.7, window medians 0.35-0.38 for 178 checks before the gate was lowered to 0.41). A fresh
  random critic head on a trained trunk makes that worse, and the LR gate (policy at 10% until EV > 0.41) would
  hold the policy at 10% for longer. It is also close to the "no hybrid checkpoints" doctrine in `CLAUDE.md`
  (healthy policy plus a freshly initialised critic against a trained trunk), which was written about recovery but
  names the same failure: the policy trains against a mismatched value baseline. GCO's own seeding is a milder
  version of this and was accepted; a random critic head is a step further. Alternative: seed `V_real` from main and
  let it relearn, as GCO does today.
- **Lineage.** GCO lives on the 5.3 obs lineage. Main is 6.0 (ball-prediction obs, GELU, 2-deep shared head);
  `checkpoints_6.0` cannot seed `checkpoints_gco`. A new GCO on 6.0 needs `g_BallPredObs` handling, GELU/LeakyReLU
  parity (an activation is not stored in a checkpoint, and a mismatch loads without error and plays wrong), and
  its own exploiters folder. This is real work, not a flag.
- **Confound.** Turning on the goal critic, the twins and (b) at once changes three things relative to the first
  GCO run. The repo's doctrine is one lever at a time with pre-registered criteria.
- **VRAM.** Fine for GCO alone (the slimming existed for three trainers on one card). `run_league.sh start --gco`
  with both exploiter slots would need the budget in `docs/EXPLOITERS.md` re-checked.
- **"Bad rocketsimv3 port".** I can't tell from the repo what that refers to. GCO ran on the 5.3 lineage, which
  is on the v3 engine; I found no note that convicts the port. If you mean a specific physics defect, point me at it
  and I'll check whether it affects GCO's measured Rating.

## 5. Suggested pre-registered plan (if you want to run it)

1. One lever first: GCO as-is plus `goalCritic.enabled = true` (gamma 0.9994, beta 0.25), `V_real` seeded as today.
   Watch `GoalCritic/Value-Outcome Corr` and `Adv-Outcome Corr` (must be positive), `GCO/Window EV Median`, and
   `Rating/1v1`, `Ref/Oldest Share` against the first GCO run's curve (1256 to about 1528 at 10% LR by 12.43B).
2. Then add the twins. Gate on `Headroom/Vdag Update Magnitude` > 0 and `Headroom/Vdag Mean` not inflating,
   with `vdagSeekBeta` set explicitly (the 0.15 vs 0.30 discrepancy must be resolved first).
3. Revert condition for each: Rating below the matched-steps baseline by more than the noise band (about 30-50
   mature) for 100M steps, or EV median below the baseline.
4. Only then consider re-initialising the short-horizon critic, as its own run.

Nothing was changed in the repo for this note.
