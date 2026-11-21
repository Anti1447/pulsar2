# GCO post-wall fork arms: one external bot as the training opponent (pre-registration, 2026-11-20)

Branch `gco-goalcritic`. Four arms, each +500M steps from run 1's final checkpoint (4,995,887,104), identical to the ctrl arm (`docs/GCO_POSTWALL_FORKS.md`) except one lever:
**30% of iterations the whole fleet's non-self team is ONE external bot** (`--gco-fork-nexto|chronos|galactus|dice`, folders `build/checkpoints_gco_fork_<arm>`). 30% matches the old-versions arm's
total non-self exposure. The other three bots keep `trainServe=false`, so they stay in the gauntlet and are a HELD-OUT yardstick; the served bot's own gauntlet column is contaminated and is not evidence.
Reset mix is the ctrl mix (no kickoff weighting): the lever is the opponent only. Combining with the kick mix comes after these results.

## Prior and why
The old-versions arm was null (head-to-head +37 vs start against ctrl +43; 50.1% vs ctrl). External bots play unlike the policy itself, so they may supply states self-play does not;
the expected effect is small and single-seed. Order: nexto, chronos, galactus, dice (about 2.3 h each).

## Pre-registered reads
- Primary: head-to-head (`GCO_H2H_EVAL.md`) of each arm against ctrl and ctrl2. A gain counts only if it exceeds 3x the effective SE and the ctrl-vs-ctrl2 gap (the noise floor).
- Secondary: gauntlet mean over the THREE held-out bots (last 250M), goals/10k, first-touch, entropy. The served bot's own column is reported but not counted.
- Falsifiers: entropy leaving 0.56 +-15%, ball-touch ratio below 0.013, a loss in head-to-head vs ctrl beyond the margin (specialisation to one bot).
- One seed per arm; one lever; revert = delete the arm folder (quarantine, never delete).

## Schedule (automatic, `tools/gco_health/`)
fresh4 (critic heads) runs to 1.0B, takes the 1B read, pauses cleanly (`chain_fresh4_pause.sh`); the four arms run back to back (`chain_arm.sh`); fresh4 resumes from its own
checkpoint to 5.0B (`chain_fresh4_resume.sh`). Status in `build/forks_status.log` and `build/fresh4_status.log`.

## Record (fill in)
| Arm | steps | H2H vs ctrl | H2H vs ctrl2 | held-out gauntlet | verdict |
|---|---|---|---|---|---|
