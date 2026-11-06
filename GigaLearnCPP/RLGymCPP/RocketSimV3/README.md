# RocketSimV3 — vendored Rust engine + C FFI

Vendored from the crates.io release **rocketsim 0.3.5** (2026-11-03; upstream https://github.com/ZealanL/RocketSim,
Rust port by VirxEC). `rocketsim/` is the UNMODIFIED crate source (minus Cargo.toml.orig / Cargo.lock) - keep it that way;
carry any local need in `rocketsim_ffi/` (ours) or the C++ compat layer (`compat/`), so upstream bumps stay a directory swap.

2026-11-06 bump from `fix-jump` @ 239808f (2026-07-12). Why: that snapshot lost the car-ball extra hit impulse (it was added
to the rigid body's per-tick accumulator AFTER the solve, and the accumulator is cleared at the start of the next tick), so
car hits came out ~45% weaker and the ball stuck to cars; the v2-trained bots then stalled (docs/engine_compare/FINDINGS.md).
0.3.5 fixes it; `tools/engine_probe` matches RocketSim v2 within ~0.5% on hit speed and carry. What the port changed:
- `rocketsim_ffi`: `CarExtraState` is gone and `CarState` is rebuilt from `CarCore` + `CarTuning`; the opaque "extra state" blob is now
  a copy of the whole `CarState`. New `rsf_arena_snapshot/restore/snapshot_free` (an `Arena` clone) give the viz rewind an exact restore.
  `ArenaEvent::CarLanded` has no C++ counterpart and is dropped.
- compat: `flipTime` is written as 10 s for a car that is not flipping and has flipTime 0 (0.3.x counts it up on every tick and locks
  pitch input for 0.3 s after it is 0, which v2 states would otherwise trigger).
- Toolchain stays the pinned nightly; glam is 0.34.

Why v3 (2026-07-17, user decision): measured ~3x single-thread physics speedup
(same protocol/meshes/machine: 267k → 815k TPS @ 2 cars) and an upstream
accuracy harness that compares per-tick impulses against real recorded game
data. Known accepted risks at the pinned commit: upstream's own
`case_simple_jump_land` comparison test FAILS (missing sticky-force impulse,
suspension impulse ~14% off) and boost-pad box-hitbox pickup is a TODO
(cylinder-only). Physics differs from v2 → checkpoints trained on v2 dynamics
are behaviorally stale; engine swap requires a fresh run.

Collision meshes: the STANDARD 16-mesh soccar dump (all hashes on the whitelist
both v2 and v3 embed) — sourced from the `rlgym_rocket_league` pip package
(`site-packages/rlgym/rocket_league/sim/collision_meshes/`). v3 hard-rejects
non-whitelisted meshes (v2 only warned); the repo's previous 10-mesh dump was
nonstandard and is parked at `build/collision_meshes/soccar_nonstandard_backup`.

Build: CMake drives `cargo build --release -p rocketsim_ffi` (staticlib) via
`rust-toolchain.toml` (auto-installs the pinned nightly through rustup; the
training box needs rustup once: `curl https://sh.rustup.rs | sh`).
