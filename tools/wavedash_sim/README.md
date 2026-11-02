# Wavedash sim (2D, pitch only)

`wavedash_sim.html` is self-contained: open it in a browser. `wavedash_core.js` is the same simulator core (the HTML embeds it) and
`check_presets.js` runs the presets headlessly (`node check_presets.js`).

Calibrated against RocketSim v3 (constants and the facts they come from: `docs/WAVEDASH_FORMAL.md` section 2, pinned by
`tests/test_wavedash_formal.cpp`). The contact model (two wheels, four hitbox corners, impulses, a toy suspension memory) is NOT RocketSim's:
it reproduces the 1-2 tick wavedash window, the cancel ~9 ticks after the press, +500 uu/s retained, the nose-down no-cancel, the flip hop
and the availability rules, but flips pressed 3-8 ticks before first contact end in a nose/body strike in the toy while the engine stays airborne.
To change the physics edit wavedash_core.js (and check_presets.js), then run python3 build.py: it inlines the core into wavedash_sim.template.html and rewrites wavedash_sim.html.

## Tick skip
The page has a tick-skip control (8 = 15 Hz training config, 4, 2, 1) that sets the decision spacing: pitch input and the flip are held for the whole decision and the flip happens on a decision boundary. Three small views run the same scenario at skip 8, 4 and 1 with the flip on the last boundary at or before first contact; a table shows how many grid phases give a wavedash at each skip.

## engine_probes/
The RocketSim v3 measurements behind the constants (each prints its results; build like the tests, linking libRLGymCPP.a, libRocketSimV3.a and RocketSimV3/target/release/librocketsim_ffi.a, run from the repo root): wd_cal.cpp (availability, flip impulse vs speed, rotation, pitch control), wd_vz2.cpp (dodge vertical damping), wd_phase.cpp (how far boost can shift the first-contact tick, per decision spacing).
