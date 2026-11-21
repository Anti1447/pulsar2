# GCO offline head-to-head evaluation of the post-wall fork arms (pre-registration, 2026-11-20)

## 1. Why
The in-run gauntlet (96 games per bot every 50 iterations, wins near 1%, mostly draws) cannot rank arms that differ by about 1 point (500M-window SD about 0.45). Policy-vs-policy games
are mostly decisive, so a few thousand goals give a tight, direct ranking. This measures what the arms' reward actually is (goal share against a peer), not performance against bots.

## 2. What is measured
For a pair (A, B): A plays B on 128 arenas, 900 s of sim time per side, once as BLUE and once as ORANGE (sides cancel). Kickoff after every goal (FuzzedKickoffState, goal-only terminal),
argmax actions (GGL_H2H_STOCH=1 samples instead; run as a robustness read for any decisive pair). Statistic: goal share s = goalsFor / (goalsFor + goalsAgainst) and the Elo gap
400*log10(s/(1-s)). Binomial SE = sqrt(s(1-s)/n); goals within an arena are correlated, so the **effective SE is taken as 1.5x binomial**.
The tool is the trainer binary itself (`GGL_H2H_OPP=<dir>[,<dir>...]`, `Learner::Start`, `RunHeadToHead` in `PolicyVersionManager.cpp`), so the obs builder, action table, tick rate and
action delay are the training ones exactly (the Python harness in research/tools still builds 230-wide obs and cannot load these 278-wide nets). It never trains, saves, or opens a W&B run;
it runs as `build/h2h_eval` (a copy of the binary) so the chain scripts' trainer-alive check does not see it, niced to 19.

## 3. Policies and pairs
Start = run 1's checkpoint at 4,995,887,104 (the fork point), ctrl, kick, opp, kickopp, ctrl2 (each at +500M). A = the process's own folder (latest checkpoint), B = a checkpoint dir.
All 15 pairs, upper triangle (sides are swapped inside each pair).

## 4. Pre-registered reading
- **Sanity:** every arm vs Start should be above 50% (500M more training); a pair where it is not is reported, not discarded.
- **Noise floor:** ctrl vs ctrl2 (two seeds of the identical config). Any arm-vs-ctrl gap must exceed BOTH 3x the effective SE AND the |ctrl2 vs ctrl| gap to count as a real effect.
- **Ranking:** order the arms by mean goal share over their pairs against the other arms (leave-one-out against control as the reference); the arm to continue is the one that clears the noise
  floor against control AND is not beaten by another arm beyond 3x SE. If no arm clears it, the arms are indistinguishable and the continuation is the control.
- This ranking is read together with section 5b of GCO_POSTWALL_FORKS.md (gauntlet). A disagreement between the two is reported as such; head-to-head is the primary read for "which policy is better",
  the gauntlet for "does it transfer to the held-out bots".

## 5. Record
(filled by tools/gco_health/h2h_matrix.py output, `build/h2h_results.jsonl`, summarized below)
