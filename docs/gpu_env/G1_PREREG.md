# G1 pre-registration - GPU physics parity vs RocketSim v3 (written 2026-11-24, before any G1 number is read)

Question. Is the vendored RocketSim-CUDA (a port of RocketSim's v2 / Bullet-3.24 lineage per its own harness) close enough to RocketSim v3 (what we train on and evaluate on) that a policy trained on GPU physics should transfer? Bit-for-bit equality is NOT required (user decision); behavioral equivalence is.

Fixed inputs. Checkpoint: a COPY of the newest `checkpoints_pulsar3` dir taken before the run (rotation window), recorded by timestep. tickSkip 4, actionDelay 0 (30 Hz decisions), argmax actions. Initial states: sampled once by the CPU reset-mix setters (BallNearCar 0.20, AirDrill 0.20, AirPlay 0.20, Kickoff 0.20, Random 0.20) and uploaded identically to both sims (paired starts). Both sims run the same code for obs, action masks, and metrics (the CPU GameState path); only the physics differs.

Part A - mechanic scenarios (diagnostic bands; one scripted control sequence, identical start, CPU v3 vs GPU, 120 Hz ticks). A scenario PASSES if every listed band holds:
 1. Ground drive (throttle 1, no boost, 2 s): position error <= 15 uu, speed error <= 15 uu/s at 1 s and 2 s.
 2. Boost drive (throttle+boost, 2 s): position <= 40 uu, speed <= 40 uu/s; boost consumed within 2%.
 3. Steering (full left/right, 2 s): final position <= 30 uu, heading <= 2 deg.
 4. Handbrake turn (1.5 s): final heading <= 5 deg, position <= 40 uu.
 5. Single jump (hold 0.2 s): apex height within 3%, flight time within 3%.
 6. Double jump and 8 dodge directions: apex within 5%, horizontal displacement after 1 s within 8%, landing orientation <= 5 deg.
 7. Wavedash (flip-cancel landing, scripted): speed gain within 15% of CPU.
 8. Wall ride and ceiling: car stays attached the same number of ticks +-10%, exit speed within 10%.
 9. Ball: floor drop, wall, ceiling, corner bounce: apex/outgoing speed within 3%, position at 2 s within 30 uu.
10. Car-ball hit (speed 800/1500/2300 x 5 contact heights): outgoing ball speed within 10%, direction <= 5 deg.
11. Bump and demo (supersonic head-on, side, rear): same demo/bump outcome on every setup; post-contact car speed within 10%.
12. Boost pad pickup and respawn: pickup tick equal +-1, respawn tick equal +-2, boost gained equal (12 / 100).
Part A is a diagnostic: a failed band is a finding that points at a physics fix, not by itself a G1 fail.

Part B - policy-level (the gate). Cells: {1v1, 2v2, 3v3} x {checkpoint vs itself, checkpoint vs oldest policy_version in the ring}. >= 20,000 episodes per cell per sim (episode ends: goal, 20 s without a touch, or 120 s). Metrics per cell:
 M1 goals per 1k steps; M2 first-toucher share at kickoff; M3 touch share of the checkpoint; M4 mean episode length (steps); M5 aerial touch ratio (touch with ball z > 300 and car off ground); M6 mean car speed; M7 boost spent per second.
PASS for a cell: for >= 6 of 7 metrics |GPU - CPU| / max(CPU, floor) <= 0.15 where floor = 0.01 (rates) or 1 uu/s (speeds), AND no metric > 0.30. G1 PASSES when all six cells pass. Uncertainty: 95% bootstrap CI over episodes is reported; a relative difference whose CI lies entirely within +-0.15 counts as inside.
On FAIL: stop, write the diagnosis (which mechanic Part A explains it), and ask the user. Do not tune the criteria after seeing data.
