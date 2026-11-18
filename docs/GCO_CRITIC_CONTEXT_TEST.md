# GCO critic-context test (pre-registration, 2026-11-18)

Status: **RUN on the live trainer (sections 12-14). Verdict: H-0 supported, no gain from the opponent source or the clocks.** Branch `gco-goalcritic`. Doctrine: measurement before machinery;
one lever; an automatic guard; a revert path. Follows `GCO_CRITIC_LR_TEST.md` (neutral), `GCO_LAMBDA_TEST.md` (reverted),
`GCO_TEAM_CRITIC_TEST.md` (the horizon-composition finding, section 12).

## 1. The problem (measured, last ~2.2B steps of `metrics.jsonl`, medians)

The bin names `<=2s / <=5s / <=10s` are DISJOINT (0-2 s, 2-5 s, 5-10 s to the episode end, `GCO/EV TTE`). Bins are defined
by the REALISED time to the end, which the critic cannot see.

| EV by time-to-end | 1v1 | 2v2 | 3v3 |
|---|---|---|---|
| 0-2 s | 0.938 | 0.883 | 0.860 |
| 2-5 s | 0.674 | 0.515 | 0.450 |
| 5-10 s ("middle") | 0.191 | 0.099 | 0.121 |
| > 10 s ("far") | -0.217 | -0.192 | -0.176 |
| row fraction > 10 s | 0.59 | 0.71 | 0.76 |

Pooled EV 1v1/2v2/3v3 = 0.400 / 0.292 / 0.266 (median; gate threshold 0.41; policy LR has stayed at 2.85e-5, no gate step).
EV by opponent source is wildly unstable per source (IQRs from about -8 to +0.4 with tiny groups, last values as extreme as -5e6),
so per-source EV is NOT usable as a success metric; use per-mode EV, per-bin EV and the source-corrected `GCO/EV SrcCorr *` panels.
Advantage-to-outcome correlation at lambda 0.95 (the policy's actual learning signal): 0.54 / 0.73 / 0.47 in the
0-2 / 2-5 / 5-10 s bins, 0.012 beyond 10 s. Far-row advantages carry almost no outcome signal.

## 2. Hypothesis

H-info: the critic is missing information that determines the return and is available in the environment: (a) the opponent
(self-play, an old version, or one of four external bots: their strength differs and EV by source ranges from +0.30 to -0.7
in medians), and (b) the no-touch clock (how long since anyone touched the ball, which determines the -0.5 timeout outcome,
and how long the episode has run). Adding them as CRITIC-ONLY inputs raises EV in the middle and far bins.
H-0: the information is irrelevant or already inferable from the obs; EV does not move.

The baseline stays valid with these inputs: both depend on the state/context and never on the action, so the critic remains
a legal PPO baseline (an asymmetric actor-critic). The policy never sees them.

## 3. The lever (exactly one) and the design that avoids a restart

NOT appended to the 278-float obs (that changes `shared_head`'s input width, which the policy shares; every checkpoint shape
changes and the run restarts). Instead a NEW module `critic_ctx`:

- Inputs: `ctx` (below) and the detached `critic_trunk` output (1280). Hidden 2x256 (GELU, LayerNorm), output 1,
  **last layer zero-initialised**, so `Delta = 0` at start and `V_ctx = V_old + Delta = V_old` exactly.
- `ctx` = opponent one-hot over {self-play, old version, exploiter pool, frozen opponent, GalactusV2, ChronosV2, Dice, Nexto}
  (8) + no-touch clock `timeSinceTouch / 20 s` + episode time `min(t / 60 s, 1)` (2). The `GCO/EV Src *` panels already tag every
  row with its source (`tSrc`), so the source exists per iteration; the clock must be collected per row (see section 9).
- Loss: `MSE(V_old.detach() + Delta, targetValues)` on the SAME minibatches as the critic's own loss, its own optimizer at
  `criticLR`. Inputs are detached, so **no gradient reaches `critic_trunk`, `shared_head` or the policy**.

Three small heads are trained in parallel on the same data (they are cheap and share nothing), so one run decomposes the lever:
`Delta_src` (source one-hot only), `Delta_clk` (the two clock features only), `Delta_both`.

## 4. Phase A: measurement only (this document pre-registers Phase A; Phase B is decided after it)

In Phase A the Deltas are AUXILIARY: they are trained and reported but **not used** by collection value preds, GAE, the
truncation bootstrap, the critic's own loss or the EV the gate reads. The policy and the live critic train exactly as they
do now, so there is no policy-side treatment, no fork and no sequential-arm confound. Every row gets two paired predictions
(`V_old` and `V_old + Delta_*`) against the same MC return, in the same iteration.

New panels (every iteration unless noted; the TTE ones every 10th, like the existing ones):
`GCO/EVctx {src, clk, both}` pooled and per mode; `GCO/EVctx TTE <bin> <mode>`; `GCO/EVctx SrcCorr <mode>`;
`GCO/Ctx Gain {src,clk,both} <mode>` = EVctx - EV (same rows, so most noise cancels); `GCO/Ctx Update Magnitude <head>`.

Window: **150M steps** (about 25 minutes at the measured 5.7M steps/min), read over the last 100M.

## 5. Pre-registered predictions (paired gain, `EVctx - EV`, median over the last 100M)

| Measure | H-info true | H-0 true |
|---|---|---|
| Pooled gain, 2v2 and 3v3 (`both`) | >= +0.03 | within +-0.02 |
| 5-10 s bin gain, 2v2 and 3v3 (`both`) | >= +0.04 | within +-0.02 |
| > 10 s bin: EVctx median | >= -0.05 (from about -0.19) | stays below -0.12 |
| 1v1 pooled gain | positive, smaller than team | within +-0.02 |
| Decomposition | gain mostly in `src` OR mostly in `clk` (names the missing input) | no head gains |

A gain counts only if its bootstrap interval over iterations excludes 0 and it clears the floors above. Falsifiers: gain only
in the 0-2 s bin (the head memorises near-terminal states; the far bins are what matter); gain only in 1v1; gain that appears and
then decays across the window (overfit); `Delta` gain coming entirely from the `critic_trunk` input without the ctx (check by one
extra head with ctx zeroed).

Success metric is NOT pooled EV alone (the far rows dominate it and the policy barely learns from them): report the middle bin
and the far-bin sign first.

## 6. Guards (manual; there is no automatic update-damage guard in this run)

- **LR trap (the `vdag1/vdag2` lr=0 incident):** `SetLearningRates` must name every new module; its update-magnitude panel must
  read nonzero after the first iteration, else the result is void.
- **Zero init:** at iteration 1, `EVctx == EV` exactly (all heads).
- **Policy untouched:** the control panels (entropy, Policy Update Magnitude 0.1014, clip fraction, `Rating/1v1`, `Ref/Oldest Share`)
  must stay in their prior bands, and `GCO/Critic Explained Variance` must be unchanged by this addition.
- **Loader:** `PPOLearner::LoadFrom` calls `models.Load(folder, allowNotExist=true, loadOptim=true)` (`PPOLearner.cpp:1108`), so a
  missing `critic_ctx*` file is logged "will be reset" and training continues. Because that flag covers EVERY model, check the boot
  log shows "will be reset" for the new modules ONLY; any other model in that list means a damaged checkpoint, stop and do not hand-assemble.
- Revert on NaN/inf, a 3x persistent value-loss jump, or entropy below half its baseline. No policy gate to trip, since the policy
  does not read the Deltas.

## 7. Cost, and the one decision this needs from the user

Code (not written): new `Model`s in `MakeModels`, optimizer LRs in `SetLearningRates`, a per-row `ctx` tensor through collection,
the experience buffer and the learn-pass minibatches (`PPOLearner.cpp` about 480-660), and the panels (`Learner.cpp`).
The live trainer must be **stopped and restarted on the new binary** (a few minutes, at most one save interval, resumes from
its checkpoint; restart requires the user's approval). Extra GPU memory is small (three heads of about 0.6M parameters).

## 8. Revert

Delete the module registration (or a config flag set false), rebuild, restart. Old binaries ignore the extra `critic_ctx*`
checkpoint files (`Load` only reads the models the template asks for), so a rollback needs no surgery.

## 9. Known unknowns / not verified

- Where to compute the per-row clock: `NoTouchCondition::timeSinceTouch` is private to the terminal condition and is reset when
  anyone touches; the collection loop must read it (or recompute it from `ballTouchedStep`). Not located yet.
- Whether `tSrc` is known at COLLECTION time for the collection-side value preds (it is chosen at the barrier before collection,
  so it should be, but I only verified it in the post-hoc EV block).
- Minibatching: whether adding a column to `ExperienceBuffer` is local or touches many call sites.
- Opponent id may be weakly identifiable already from the obs (opponents' motion style); the paired gain measures what it adds.
- EV measures the level against the realised return; it does not measure how good the local differences V(s')-V(s) are, which is
  what GAE uses. Phase B (actuation) is where the advantage-to-outcome correlation by bin becomes the primary metric.

## 10. Decision rule

- **H-info supported** (floors met): Phase B, pre-registered separately: actuate (`V = V_old + Delta` in collection, GAE and the
  bootstrap), forked two-arm test, outcomes vs frozen bots, advantage-outcome correlation by bin. The critic-aliasing rule applies:
  the swap changes what the baseline means, so the critic must be given time to learn the new return structure.
- **H-0 supported:** drop A1/A2. Next candidates: event-probability heads (idea 5) evaluated offline first, the team `Delta` encoder,
  a fixed-window truncation test.
- **Mixed** (`src` or `clk` only): keep only the head that gained, say so, and shrink the Phase B lever to it.

## 11. Record (fill in)

| Item | Value |
|---|---|
| Binary / commit, restart step | |
| Boot log "will be reset" list | |
| Zero-init check (EVctx == EV at iter 1) | |
| Update magnitudes (src / clk / both) nonzero? | |
| Paired gain pooled, per mode | |
| Paired gain by TTE bin, per mode | |
| Decomposition (src vs clk) | |
| Control panels vs prior bands | |
| Verdict | |

## 12. Implementation record (2026-11-18)

Code (additive, 243 lines in 5 files, nothing else touched): `PPOLearnerConfig.h` (the `GGL_CRITIC_CTX` switch, context layout), `ExperienceBuffer.h`
(per-row `criticCtx`), `PPOLearner.h/.cpp` (heads, loss, LR, clip, magnitude panel, `InferCtxDeltas`, excluded from policy versions),
`Learner.cpp` (per-row clock collection, context tensor, paired EV panels). Default OFF: the rebuilt library is inert until a process starts with `GGL_CRITIC_CTX=1`.

**Differences from sections 3-5 (all disclosed):**
1. **Four heads, not three:** `critic_ctx_src`, `_clk`, `_both`, plus `_nul` (trunk read only, context absent), the control named in section 5's falsifiers.
   Every gain must be read AGAINST `nul`, not against 0: the heads can also absorb the gap between the collection-time prediction (one iteration
   stale, bf16) and the current fp32 trunk read, and `nul` measures exactly that. Success needs src/clk/both to beat `nul`.
2. **Panels:** `GCO/EVctx <head>` (pooled, and per mode `EVctx <head> <mode>`), `GCO/Ctx Gain <head> <All|mode>`, `GCO/Ctx Gain TTE <head> <bin> <mode>`,
   `GCO/EVctx TTE <bin> <mode>` (both head only), `GCO/Ctx Delta Abs <head>`, `GCO/Ctx Update Magnitude <head>`. **`EVctx SrcCorr` was not built:** the
   `src` head's out-of-sample gain answers the same question without the in-sample intercept bias. TTE panels follow the existing every-10th-iteration cadence.
3. **Definitions:** source = `rowTag / 4` (0 self-play, 1 old version, 2 exploiter pool, 3 frozen opponent, 4.. external bot in `externalOpps` order, capped at slot 7);
   no-touch clock = `NoTouchCondition::timeSinceTouch / 20 s` read BEFORE the step (so it describes the obs the row holds); episode clock =
   steps since the arena's last `Reset()` x tickSkip / 120 / 60 s, both clamped to 1.
4. **Evaluation order:** the heads' Delta is computed before this iteration's update on this iteration's rows, so each is judged out of sample.

**Smoke (CPU, `GGL_SMOKE=1`, throwaway directory, copy of one checkpoint; the live checkpoint folder and the GPU untouched).** Passed:
- Boot log: ONLY the four `critic_ctx_*` models list "will be reset"; no other model does (the section 6 loader guard).
- Zero-init: first evaluation logs mean |Delta| = 0 for all four heads; `Delta Abs` then grows to 0.02-0.05.
- LR trap: `Ctx Update Magnitude` about 0.178 for all four heads (nonzero). A first smoke showed the panel missing: it had been placed inside the `if (reach)` brace
  (reachability is off here); moved to the always-reported block and rerun.
- No crash over 6 recorded iterations; panels populate; gains near 0 (about +-0.005) with all four heads similar, as expected at this size.
**Not verified:** behaviour under the live trainer (pipelined collection, bf16 collection, GPU memory), the control panels (entropy, clip fraction) under the live run, and the
policy staying bit-untouched (by construction every head input is detached and the heads' loss never reaches the trunk, but only a live comparison shows it).

**Restart procedure (needs the user's approval; nothing has been restarted).** `systemd-run --user` does not forward a shell's environment, so
`GGL_CRITIC_CTX=1 tools/trainerctl restart` would NOT enable it. Use:
`systemctl --user set-environment GGL_CRITIC_CTX=1`, then stop and start with the GCO command (the wrapper's own crash-restarts inherit the variable):
`tools/run_trainer.sh --stop` (saves a checkpoint), then from `build/`: `../tools/run_trainer.sh --detach -- ./GigaLearnBot --gco`.
**Do NOT use `tools/trainerctl restart`:** it starts the bare `./GigaLearnBot` (no `--gco`), i.e. the main 6.1 lineage in `checkpoints_6.1`. That happened on
2026-11-18 (about 20 minutes, 47M steps, two numbered checkpoints and older policy versions rotated out of `checkpoints_6.1`; killed without a save and
restarted correctly). Check the boot log line `Checkpoint Save/Load Dir: "checkpoints_gco_6.1"` before anything else.
Revert: `systemctl --user unset-environment GGL_CRITIC_CTX`, then stop and start with the same GCO command; old binaries ignore the extra `CRITIC_CTX_*.lt` files.
On start, check the boot log lists only the four ctx heads as "will be reset" and that "first evaluation: mean |Delta|" reads 0.

## 13. Live start record (2026-11-18)

Started on the live GCO run at timestep 67,181,851,154 (resumed from `checkpoints_gco_6.1/67180565638`, saved by the clean stop). Boot log checks passed:
save/load dir `checkpoints_gco_6.1`; only `critic_ctx_both/clk/nul/src` listed "will be reset"; first evaluation mean |Delta| = 0 for all four heads.
Pre-restart control medians (last 40 metric rows): Policy Entropy 0.553, Policy Update Magnitude 0.101, Critic Update Magnitude 0.377, critic EV pooled 0.323
(1v1 0.426, 2v2 0.311, 3v3 0.277), about 4.6M steps/min. The 150M-step window therefore ends near timestep 67.33B.

## 14. Result (2026-11-18), window 67,181,851,154 to 67,341,195,334 steps (150M), read over the last 100M (29 metric rows; TTE panels 14)

Median paired gain (EVctx - EV on the same rows, out of sample), [95% bootstrap CI over iterations]:

| Scope | src | clk | both | nul (control) | both - nul |
|---|---|---|---|---|---|
| Pooled | -0.0000 [-0.0001,+0.0001] | -0.0001 [-0.0002,+0.0001] | -0.0001 [-0.0003,+0.0001] | -0.0001 [-0.0003,+0.0001] | +0.0000 [-0.0000,+0.0001] |
| 1v1 | -0.0001 | -0.0001 | -0.0001 [-0.0003,-0.0000] | -0.0002 | +0.0000 |
| 2v2 | -0.0001 | -0.0000 | -0.0000 [-0.0003,+0.0001] | -0.0001 | +0.0001 [-0.0001,+0.0001] |
| 3v3 | +0.0000 | -0.0001 | -0.0000 [-0.0004,+0.0002] | -0.0000 | -0.0001 [-0.0001,+0.0001] |

By time-to-end bin, the `both` head: gain between -0.0010 and +0.0012 everywhere, every interval contains 0; both-nul between -0.0002 and +0.0004. EV vs EVctx medians:
2-5 s (`<=5s`) 1v1 0.707/0.708, 2v2 0.568/0.569, 3v3 0.521/0.520; 5-10 s 0.195/0.196, 0.160/0.160, 0.185/0.186; far bin (`>10s`) -0.187/-0.190, -0.187/-0.188, -0.205/-0.205.
Update magnitudes 0.1761-0.1777 for all four heads (the heads trained). Control panels vs the pre-restart baseline: Policy Entropy 0.557 (0.553), Policy Update Magnitude 0.1013 (0.101),
Critic Update Magnitude 0.373 (0.377), critic EV 0.3255 (0.323); EV 1v1/2v2/3v3 0.415/0.304/0.294 (0.426/0.311/0.277). Policy and live critic untouched, as designed.

**Against the pre-registered floors (section 5): H-0 supported.** Pooled 2v2/3v3 gain needed >= +0.03, observed about 0.0000 with a CI of +-0.0003. The 5-10 s bin needed >= +0.04, observed
about 0.000 to +0.001. The far-bin EV with context needed >= -0.05, observed -0.19 to -0.21. No head, including `src` and `clk`, beat the `nul` control. The interval is tight (about
+-0.0005), so this is a measured zero, not an underpowered one, for THESE features with THESE heads.

**What this rules in and out.** The opponent source adds nothing beyond what the trunk already encodes (consistent with the earlier in-sample intercept-only upper bound: `EV SrcCorr All`
0.413 vs 0.40 pooled), and neither does the no-touch clock or the episode clock. The team-mode EV gap is therefore not a missing-input problem of these two kinds. Limits: one 150M window, small
heads (2x256 on the detached trunk read), one learning rate (1.5e-4); the heads see a detached 1280-wide trunk and could only add what is linearly or shallowly recoverable from it plus the
context. Per the decision rule (section 10): drop A1/A2. Next candidates: event-probability heads (idea 5), evaluated offline first; the team Delta encoder; a fixed-window truncation test.
