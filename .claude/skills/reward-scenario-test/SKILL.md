---
name: reward-scenario-test
description: Audits one reward class in Pulsar against a ladder of hand-traced scenarios (default 6, easiest through hardest-to-value) plus a mandatory farming/gaming probe set, to see whether its GetReward() logic matches what Nathan intends it to reward. Use whenever Nathan asks to "test", "scenario test", "audit" or "sanity check" a named reward, asks whether a reward "matches my vision" / "does what I think it does", asks "can this be farmed / gamed", or wants a reward checked before enabling it or after a weight/logic change - also for ported RLGymBot_v3 rewards, which are all disabled until tested. This is a code-reading trace; to get ground-truth execution of the same reward, follow it with the `reward-harness` skill and commit the scenarios as tests.
---

# Reward Scenario Test (Pulsar)

Ported from RLGymBot_v3's skill of the same name and adapted to this repo's doctrine
(`CLAUDE.md`: **measurement before machinery**; `docs/REWARD_SHAPING.md` sections 9-11).
A reward that has not passed this ladder does not get enabled. Compiling proves syntax;
W&B takes days to show a logic bug.

**This is a trace.** Read the real `GetReward()` / `PreStep()` / `Reset()` and evaluate it by hand
with real numbers. Do not write a second implementation. The compiled counterpart is the
`reward-harness` skill (`tests/`); a trace that finds anything should end as a test there.

## Step 1 - Resolve the reward and its context

1. Find the class: `grep -rn "class <Name>" GigaLearnCPP/RLGymCPP/src/RLGymCPP/Rewards/`. Read the whole
   class (constructor, `Reset`, `PreStep`, helpers it calls, file-scope constants it uses).
2. Find where it is (or would be) registered: `src/ExampleMain.cpp` `BuildRewards()`. Note the wrapper
   (`ZeroSumReward(..., spirit)`, bare, or PBRS with `gamma`) and the weight.
3. Read `docs/REWARD_CHANGE_LOG.md` for prior entries on it. For rewards ported from RLGymBot_v3 the log has
   an index of v3's history (v3 `Reward_Change_Log.md`, ~195 entries): read the relevant v3 entries, they hold
   the earlier audit findings and fixes.
4. Classify it - this decides everything in Steps 3 and 5:
   - **PBRS** (exact potential, `gamma` == `TRAIN_GAMMA`): telescopes, unfarmable by construction if never gated.
   - **Zero-sum / antisymmetric** (wrapped, or inherently antisymmetric like `GoalReward`).
   - **Individual one-sided positive** (pays whoever does it, no wrapper, not a potential) - the farmable class.
   - **Individual penalty** (never positive).
   - **One-shot event** (pays once per episode / per terminal).

## Step 2 - State the intended behavior and confirm it with Nathan

One short paragraph: what it is supposed to reward, from its header comment and the change log. Ask Nathan
to confirm or correct it **before** generating scenarios. Do not skip: the point is alignment with his intent,
and header comments in this codebase are sometimes stale.

## Step 3 - Scenario ladder (default 6)

- **Easy (1-2):** obviously pays / obviously does not pay. Catches a dead trigger.
- **Middle (2-3):** partial credit and boundaries at the reward's own constants (read the real `constexpr`s;
  test at 0.9x and 1.1x of a threshold, never "short"/"long"). If stateful (cooldown, timer, per-player map),
  include a state transition: re-touch, opponent touch, demo, `Reset()`.
- **Hard (1-2):** genuinely debatable value: marginal angle, 2v2/3v3 attribution, two gates interacting.
- **Counterfactual pairs:** the same scenario with exactly one factor changed. The reward should differ only when
  that factor matters.
- **Timing:** deltaTime is `tickSkip/120 = 8/120 s`. Any reward that detects a short sequence from state
  changes (a flip, a wavedash landing, a jump-pitch-jump) must be traced at 15 Hz, not 30 or 120.

Each scenario is a **concrete** snapshot: exact `pos`, `vel`, flags, timers (`player.pos=(0,3000,17),
ball.pos=(0,3100,93) ...`), concrete enough that someone else re-traces the same number.

## Step 4 - Trace and judge

Evaluate each gate in order, show intermediate values, state the return value. Verdict per scenario:
**ALIGNED / MISALIGNED / AMBIGUOUS** with one line of reasoning. AMBIGUOUS is legitimate.

## Step 5 - Farming and gaming probes (always, separate from N)

Why this matters here (from `docs/REWARD_SHAPING.md` and the `BuildRewards()` comments): the stack is built so
every term is zero-sum, antisymmetric or an exact PBRS, so that (a) league/rating fitness is a pure competitive
margin and (b) a per-player income stream cannot be farmed. Probe against that, in this order:

1. **Income loop (positive, repeatable):** what cheap cycle does a policy find? Compute the **rate** (reward/s
   and reward per touch), not "seems farmable". Does the trigger require a landing, a ball touch, a cooldown?
   A loop that needs 2 s to repeat at 20 per cycle is 10/s - against Goal 150.
2. **Does the wrapper actually remove it?** `ZeroSumReward` turns income into a differential `r_i - r_j`, so in
   *mirror* self-play it nets ~0. It does NOT help against a weaker opponent (past selves, Nexto, exploiters):
   the stronger side still keeps the income. State which opponents the loop works against.
3. **Penalties are not farmable for income, but have their own exploits:**
   - **Avoidance by degeneracy:** the cheapest way to never pay is to not play (net-camp, never leave the
     box). Check what behavior reaches penalty = 0 and whether it is a good policy.
   - **Early termination:** a per-step negative reward on an episode that ends at a goal pays the policy to end
     the episode. Compute worst-case accumulated penalty per episode against the goal/concede magnitude (150).
     A mutual-stall fixed point is the opposite failure (`REWARD_SHAPING.md` section 10: why `TimeCost` stays).
   - **Opposing shaping:** two terms with opposite signs on the same transition (e.g. a penalty for being upfield
     vs a reward for pressure) freeze the bot.
4. **Anti-farm defenses present?** cooldown, context gate, one-shot, PBRS. If none, that absence is itself the
   finding.
5. **Invariant check:** is it wrapped / PBRS / antisymmetric? A bare one-sided positive term (e.g. a continuous
   speed bonus) breaks the whole-stack invariant by itself - say so.

## Step 6 - Report

Write `docs/reward_scenarios/<RewardName>_<YYYY-MM-DD>.md`:
1. Confirmed intent (as Nathan corrected it).
2. Classification (Step 1.4) and registration/wrapper/weight.
3. Scenario table: tier, setup, traced value, expected, verdict, reasoning.
4. Farming section: probe, traced rate, opponent types it works against, exploitable y/n.
5. Overall verdict, and file:line pointers for anything misaligned. **Do not edit the reward or `BuildRewards()`
   as part of the audit** - a fix is a separate, explicit ask.
6. **Pre-registration block** (for a reward that will be enabled): reward-health metric and the range that
   would look healthy vs suspicious, the W&B panel to watch, what behavior to confirm in the viewer, and what
   result would make you revert (`CLAUDE.md`: a measurement convicts the problem, pre-registered criteria, an
   automatic guard, a revert path, one lever at a time).

Then turn the scenarios into a compiled test with the `reward-harness` skill. A traced-only verdict is
"unverified by execution" and the report must say so. Changes to a reward still get a line in
`docs/REWARD_CHANGE_LOG.md`; a read-only audit report does not.

In chat: overall verdict and the 1-2 most important findings first, then the file path.
