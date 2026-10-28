# Quick-start commands (WSL2 / Ubuntu 24.04)

Everyday commands for this clone. All of them run **inside Ubuntu (WSL2)**, not in Windows PowerShell, unless a block says PowerShell.
The repo lives at `~/pulsar2` (Linux home folder, not under `/mnt/c`, so builds stay fast).

## 1. Open the repo

```powershell
# from Windows PowerShell: open a shell in the repo
wsl -d Ubuntu-24.04 --cd ~/pulsar2
```

```powershell
# from Windows PowerShell: open the repo in VS Code (needs the "WSL" extension)
wsl -d Ubuntu-24.04 --cd ~/pulsar2 -- code .
```

The bottom-left of VS Code should say **WSL: Ubuntu-24.04**. Its built-in terminal (Ctrl+`) is then an Ubuntu shell in the repo.
You can also browse the files from Windows at `\\wsl.localhost\Ubuntu-24.04\home\nhafe\pulsar2`; read and copy there, but edit and build from the WSL window.

## 2. Setup that matters on WSL (W&B login, GPU allocator)

**Weights & Biases is on by default.** The first time, log in once so runs sync online:

```bash
~/.local/bin/wandb login        # it prints the link to create an API key (W&B now lives on CoreWeave: api.forge.coreweave.com)
```

- Until you log in, training still works: `metric_receiver.py` notices there is no login (any key in `~/.netrc` for a W&B or CoreWeave host counts) and logs an **offline** run (`build/wandb/offline-run-*`) instead of aborting. The training log then shows `wandb: W&B syncing is set to offline`, and the dashboard header says "W&B offline (not syncing)".
- After logging in, the next start is **online**. An explicit `WANDB_MODE=online|offline|disabled` always wins. If you set `WANDB_MODE` earlier with `systemctl --user set-environment`, undo it so auto-detection can work: `systemctl --user unset-environment WANDB_MODE`.
- Offline runs can be uploaded later with `wandb sync`, but old offline folders that reuse one run id will merge into it; check before syncing.

**GPU allocator:** `tools/run_trainer.sh` now detects WSL and turns `expandable_segments` **off** by itself (on WSL it hangs the CUDA allocator inside `cuMemAddressReserve`). Nothing to set after a reboot any more. A `PYTORCH_CUDA_ALLOC_CONF` you set yourself still wins.

Check the GPU is visible: `nvidia-smi`.

## 3. Edit

Open the repo in VS Code (section 1). Files you will touch most:

| File | What it holds |
|---|---|
| `src/ExampleMain.cpp` | The training config: rewards, network sizes, `numGames`, `tsPerItr`, `miniBatchSize`, Nexto opponent |
| `src/RLBotMain.cpp` | The RLBot client. Network constants must mirror `ExampleMain.cpp` or a trained checkpoint plays badly |
| `tools/run_trainer.sh` | Background/restart wrapper for the trainer |

Common knobs in `src/ExampleMain.cpp` (line numbers shift as you edit; use `grep -n`):

```bash
grep -n 'cfg.numGames\|int tsPerItr\|miniBatchSize =' src/ExampleMain.cpp
```

- `cfg.numGames` is the number of simulated arenas. More games means more CPU load, heat and fan noise. This clone currently uses **256**.
- `miniBatchSize` must divide `tsPerItr` (200,000). If the GPU runs out of memory, lower it.
- Do not change network layer sizes mid-run. Checkpoints and the RLBot client depend on them.

Edit only while the trainer is **stopped** (section 6). Rebuilding under a live run replaces the binary it is using.

## 4. Build

```bash
cd ~/pulsar2
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release   # first time only
cmake --build build -j"$(nproc)"
```

After the first configure, just run the second command. Output lands in `build/`:
`GigaLearnBot` (trainer and viewer), `GigaLearnRLBot` (RLBot client), `libGigaLearnCPP.so`.
Release is required; Debug is unusably slow for training.

If the CUDA compiler complains about the host compiler version, `sudo apt install g++-13`, delete `build/`, and configure again.

## 5. Run (train)

Always run from `build/` because it holds `collision_meshes/` and `checkpoints_5.3/`.

```bash
cd ~/pulsar2/build
```

**Smoke test** (shrinks the run, CPU only; proves the whole loop works, then Ctrl+C):

```bash
GGL_SMOKE=1 GGL_DEVICE=cpu WANDB_MODE=offline ./GigaLearnBot
rm -rf checkpoints_5.3     # smoke writes into the real checkpoint folder; delete it afterwards
```

**Real run, in the background** (survives closing the terminal, restarts after crashes, resumes from the newest checkpoint):

```bash
../tools/run_trainer.sh
```

**Foreground run** (the terminal owns it; Ctrl+C stops it; type `Q` to save and quit):

```bash
../tools/run_trainer.sh --foreground
```

Logs go to `run_logs/` (newest is `run_logs/latest.log`).

## 6. Watch, stop, resume

```bash
../tools/run_trainer.sh --status      # running or not
../tools/run_trainer.sh --follow      # live log, Ctrl+C stops watching (training continues)
grep -E 'Total Iterations|Overall Steps' ../run_logs/latest.log | tail -n 4
nvidia-smi                             # GPU use
ls -lt checkpoints_5.3 | head          # newest checkpoints
```

**Stopping (saves first).** All of these finish the current iteration, write a checkpoint, then exit cleanly (exit code 0, the wrapper does not restart it):

```bash
../tools/run_trainer.sh --stop          # from any terminal
```

- **Press Q** in `../tools/run_trainer.sh --follow` (the live log view). Ctrl+C there only stops *watching*; training keeps running.
- **Press Q** or **Ctrl+C** when running `--foreground`. A second Ctrl+C exits immediately without saving.
- **Stop & save** button in the dashboard (section 14).

Expect up to a minute: the iteration has to finish (and a skill-match iteration is longer). The log shows `Stop signal received: ... saving a checkpoint` and then `Saving to folder ...`.

**Immediate stop, nothing saved:** `../tools/run_trainer.sh --kill` (or Kill in the dashboard). Everything since the last checkpoint is lost.

Automatic checkpoints are still written every 25 million steps; the newest 8 are kept, plus `best_r*` folders.

**Resuming:** run `../tools/run_trainer.sh` again. It loads the newest checkpoint in `build/checkpoints_5.3`.

The background trainer's state also shows in `systemctl --user status 'gigalearn-trainer-*'`.

## 7. Render (watch the bot play)

The viewer is a single-arena copy of `GigaLearnBot` run with `GGL_RENDER=1`. It loads the newest checkpoint, reloads when the trainer writes a newer one, and runs on the **CPU**, so it does not compete with training for the GPU. It also works while the trainer is running.

**One-time setup:**

```bash
cd ~/pulsar2
git submodule update --init tools/viz/RocketSimVisWeb   # may fail: the pinned commit is not on the remote
# if it fails, check out the viewer branch directly:
git -C tools/viz/RocketSimVisWeb fetch origin
git -C tools/viz/RocketSimVisWeb checkout origin/pulsar-patches
python3 -m venv tools/viz/.venv
tools/viz/.venv/bin/pip install websockets
```

**Each time**, two terminals (or two VS Code terminal tabs):

Terminal A, the web server:

```bash
cd ~/pulsar2/tools/viz/RocketSimVisWeb
../.venv/bin/python -u server.py
```

Terminal B, the viewer:

```bash
cd ~/pulsar2/build
GGL_RENDER=1 GGL_DEVICE=cpu WANDB_MODE=offline OMP_NUM_THREADS=1 ./GigaLearnBot
```

Then open **http://localhost:9275** in a browser on Windows.

- `GGL_RENDER_TEAM_SIZE=1|2|3` picks 1v1, 2v2 or 3v3 (default 1v1).
- `GGL_RENDER_RELOAD_SECS=30` sets how often it polls for a newer checkpoint. A value of 0 or less pins the checkpoint loaded at startup.
- `OMP_NUM_THREADS=1` keeps replays reproducible for the viewer's rewind control.
- The viewer must use the same network architecture as the trainer, so rebuild before using it after a config change.
- Ports (all on localhost only): web page 9275, websocket 9274, viewer data 9273, control 9276.
- Stop both with Ctrl+C.

Tested here: the web server answers on port 9275 and the viewer loads the newest checkpoint and opens its ports. Opening the page from the Windows browser relies on WSL's `localhost` forwarding, which is on by default; if the page does not load, run `wsl hostname -I` and try that address with `:9275`.

`tools/trainerctl viz start|stop|status` wraps this as always-on systemd services, but it expects a separate `build-viz/` build tree and was not tested here.

## 8. Checkpoints

```
build/checkpoints_5.3/<total steps>/        newest numbered folders are the rolling saves
build/checkpoints_5.3/best_r<N>_<steps>/    best-rated snapshots kept by the archive
```

Each folder holds `SHARED_HEAD.lt`, `POLICY.lt` and stats files. Back one up with:

```bash
cp -r ~/pulsar2/build/checkpoints_5.3/<steps> ~/backup_<steps>
```

## 9. Reducing fan noise

- Lower `cfg.numGames` in `src/ExampleMain.cpp`, rebuild, restart (CPU load).
- Cap the GPU power from an **admin PowerShell on Windows** (resets at reboot):

```powershell
nvidia-smi -pl 250      # cap at 250 W
nvidia-smi -pl 350      # restore the RTX 3090 default
```

- Limit WSL CPU threads: add `processors=12` under `[wsl2]` in `C:\Users\nhafe\.wslconfig`, then run `wsl --shutdown` in PowerShell.

## 10. Troubleshooting

| Symptom | Cause and fix |
|---|---|
| Trainer stops printing after "Running skill matches" or one thread sits at 100% | CUDA allocator hang on WSL. `run_trainer.sh` now avoids it automatically; if you started `./GigaLearnBot` directly, set `PYTORCH_CUDA_ALLOC_CONF=expandable_segments:False` yourself. The dashboard flags this as "stalled?" |
| `FAILED to import wandb` | wandb is missing from the Python the trainer found: `pip3 install --user --break-system-packages wandb`. A missing *login* is not an error any more; it logs offline (section 2) |
| `NextoOpponent: failed to load TorchScript model` | The Nexto model file is missing. It should be at `rlbot-run/nexto/nexto-model.pt`; the path is set in `src/ExampleMain.cpp` and the opponent is skipped if the file is absent |
| Crash about meshes or hashes at startup | `build/collision_meshes/soccar` must hold the 16-mesh set from the `rlgym-rocket-league` pip package, not the output of `collision_mesh_downloader.py` |
| GPU out of memory | Lower `miniBatchSize` in `src/ExampleMain.cpp`, rebuild; close other GPU-heavy apps |
| Build error about the CUDA host compiler | `sudo apt install g++-13`, delete `build/`, re-run the configure line |
| Visual Studio on Windows errors on `nvcc` and MSVC | It is trying a native Windows build, which this repo does not support. Ignore it and build in WSL |

## 11. Local changes in this clone

These edits are not in the upstream repo; check `git diff` before pulling or switching branches:

- `src/ExampleMain.cpp`: `cfg.numGames` lowered (now 256); Nexto opponent enabled only if its model file exists, with the path pointed at `rlbot-run/nexto/nexto-model.pt`; added `#include <filesystem>`.
- `cpp-interface/`: re-cloned at commit `8a7faf9` with `patches/cpp-interface/0001` and `0002` applied (the pinned commit is not on any remote).
- `tools/viz/RocketSimVisWeb/`: checked out at `origin/pulsar-patches`.
- `GigaLearnCPP/src/public/GigaLearnCPP/Learner.cpp`: SIGINT/SIGTERM now behave like Q (finish the iteration, save, exit 0; a second signal exits at once). Not active in render mode.
- `GigaLearnCPP/python_scripts/metric_receiver.py`: W&B online when logged in, otherwise an automatic offline run.
- `src/RLBotClient.cpp`: no longer requests RLBot's (unused) ball prediction.
- `tools/run_trainer.sh`: graceful `--stop`, `--kill`, Q in `--follow`, WSL-safe allocator default, longer systemd stop timeout.
- New: `tools/package_bot.sh`, `tools/package_best_bot.sh`, `rlbot-run/pulsar-assets/`, `tools/dashboard/`.
- Not tracked by git: `GigaLearnCPP/libtorch` (LibTorch 2.9.1 for CUDA 12.8), `build/`, `run_logs/`, `tools/viz/.venv`, `rlbot-run/nexto/nexto-model.pt`.

## 12. Package the bot and install it into RLBot v5

`tools/package_bot.sh` takes the newest checkpoint, makes a **versioned release**, and installs it into the Windows RLBot v5 app's bots folder (`%LOCALAPPDATA%\RLBot5\bots\Pulsar2\vX.Y.Z`). Run it in Ubuntu, from the repo:

```bash
cd ~/pulsar2
tools/package_bot.sh --dry-run     # preview: version, checkpoint, folders; writes nothing
tools/package_bot.sh               # release the newest checkpoint, bump PATCH
```

Versioning (semantic, `MAJOR.MINOR.PATCH`):

| Command | Result |
|---|---|
| first ever run | `v0.1.0` |
| `tools/package_bot.sh` | next patch, e.g. `v0.1.0` -> `v0.1.1` (a new checkpoint, same bot) |
| `tools/package_bot.sh --minor` | `v0.1.1` -> `v0.2.0` (meaningful improvement or new reward stage) |
| `tools/package_bot.sh --major` | `v0.2.0` -> `v1.0.0` (network or observation change; old checkpoints no longer fit) |
| `tools/package_bot.sh --version 1.2.3` | an exact version; refuses if it exists |
| `tools/package_bot.sh --checkpoint 2200132352` | a specific step instead of the newest (or give a folder path) |
| `tools/package_bot.sh --list` | show existing releases |
| `tools/package_bot.sh --no-install` | build the release in WSL but do not touch Windows |

Existing releases are never overwritten. If the newest release already holds the same checkpoint and the same `RLBotMain.cpp`/`ExampleMain.cpp`, the script stops with "nothing new to release"; add `--force` to release anyway.

What a release contains (`rlbot-run/releases/vX.Y.Z/`, ignored by git):

- `checkpoint/` holds only `SHARED_HEAD.lt`, `POLICY.lt` and `RUNNING_STATS.json` (about 20 MB; the bot does not need the optimizer files).
- `bin/` holds a frozen copy of `GigaLearnRLBot` and its libraries, so later rebuilds do not change an old release. LibTorch is shared from `GigaLearnCPP/libtorch`.
- `run.sh` is the Linux launcher; `bot.toml` and `release.json` describe the release (checkpoint step, source hash, git commit, date).

On Windows the installed folder is a thin launcher: RLBot runs `run_wsl.bat`, which starts the Linux bot in WSL and forwards the `RLBOT_*` variables. The bot appears as **"Pulsar2 vX.Y.Z"**.

Before releasing, make sure `build/GigaLearnRLBot` is built from the same network settings you trained with (`cmake --build build -j"$(nproc)"`). A mismatch in `src/RLBotMain.cpp` (especially `addResiduals`) loads without any error and the bot just plays badly.

### Playing it

1. Release and install: `tools/package_bot.sh`.
2. In the RLBot v5 app on Windows, restart or refresh it so it rescans the bots folder, then add **Pulsar2 vX.Y.Z** to a match.
3. Rocket League must be started the RLBot way (the app does this, or Steam launch option `-rlbot`). The `play.sh` script in `rlbot-run/` is for Linux Steam/Proton and does not apply on Windows.
4. WSL must be running. If the distro is stopped, RLBot's launcher starts it, which adds a few seconds.

How it was verified here: the Windows RLBot server was started, `run_wsl.bat` was run with the `RLBOT_*` variables, and the bot inside WSL loaded the checkpoint, switched `127.0.0.1` to the Windows host address and stayed connected. A full match in Rocket League has not been played yet.

### Strongest-rated checkpoint, and never releasing the same bot twice

```bash
tools/package_best_bot.sh              # same as: tools/package_bot.sh --best
tools/package_best_bot.sh --dry-run    # see which checkpoint it would pick
```

- **Which checkpoint:** the trainer archives its best-rated snapshots as `build/checkpoints_5.3/best_r<rating>_<steps>`. `--best` picks the highest rating (ties go to the later step) and skips any folder that is missing files.
- **Duplicate guard (both commands):** before writing anything, the script compares the checkpoint's *contents* (weights + stats, as a hash) and the bot code (`RLBotMain.cpp`, `RLBotClient.cpp`, `ExampleMain.cpp`) against **every** existing release, not just the latest. If the same weights with the same code already exist, it prints which version and exits with code 3 (`Nothing to release: this exact bot already exists as v0.1.2`). Running a command twice by accident cannot create a second copy, and a best-rated checkpoint that is byte-identical to a numbered one (which is common) is recognised too.
- **Same weights, new code:** allowed, with a note, because the bot behaves differently.
- `--force` overrides the guard. `--best` cannot be combined with `--checkpoint`.
- `--list` shows each release's step and rating.

### Changing how the bot looks in RLBot

The bot's `bot.toml`, `loadout.toml` and logo come from **`rlbot-run/pulsar-assets/`**: `bot.toml.template` (placeholders `@TAG@` for the version and `@STEP@` for the checkpoint), `loadout.toml`, `pulsar.jpg`. Edit those files once and every future release picks the change up. Any extra file you add there is copied into the release and into the Windows bot folder. Existing release folders are never modified.

## 13. Bot troubleshooting

Each car writes a log: `~/pulsar2/rlbot-run/releases/vX.Y.Z/bot.<pid>.log`. Its last line is `=== GigaLearnRLBot exited with code N ===`. Newest log:

```bash
ls -t ~/pulsar2/rlbot-run/releases/*/bot.*.log | head -n 1 | xargs tail -n 20
```

| Symptom | Cause and fix |
|---|---|
| Bot does not appear in the RLBot v5 app | Restart or refresh the app. Check the folder exists: `dir "%LOCALAPPDATA%\RLBot5\bots\Pulsar2"`. Re-run `tools/package_bot.sh` if not |
| Bot never joins, no new `bot.*.log` appears | The launcher did not run. Open `%LOCALAPPDATA%\RLBot5\bots\Pulsar2\vX.Y.Z` in a terminal and run `.\run_wsl.bat`; any error prints there. `wsl -l -v` must list Ubuntu-24.04 |
| Log says `Connection refused` | The bot could not reach the Windows RLBot server. The log's first line shows the address used (`server=...`); it should be the Windows host (e.g. `172.25.x.1`), not `127.0.0.1`. The server must be running before the bot starts. Windows Firewall may block WSL: allow `RLBotServer.exe` on Private networks |
| Works once, fails after a Windows reboot | WSL's host address changes between restarts. That is handled automatically by `run.sh`; if you set a fixed IP anywhere, remove it |
| Log says `failed to load` or lists a missing `.lt` file | The release is missing checkpoint files. Re-run `tools/package_bot.sh --checkpoint <step>` for a complete checkpoint |
| Bot loads but plays badly (spins, ignores the ball) | Network settings in `src/RLBotMain.cpp` differ from `ExampleMain.cpp` (most often `addResiduals`). Mirror them, rebuild, release again with `--minor`/`--patch` |
| Checkpoint load aborts with `264704 vs 262144` | That checkpoint is from the old Ladder-era architecture. Use one from your own current run |
| RLBot drops the link right after the map loads | RLBot core, the `rlbot` package and `cpp-interface` schema do not match the installed game build. Update them together; the `cpp-interface` patches are in `patches/cpp-interface/` |
| RLBot log: `outbound message queue is full`, bot log: thousands of `Partial read 115620 bytes`, bots move randomly | The bot was requesting RLBot's ball prediction (about 115 KB per update) and ignoring it, which flooded the socket (the cpp-interface patch also cut the read buffers to 4). Fixed in v0.1.1: `src/RLBotClient.cpp` now calls `manager.connect(..., false)`, and `run.sh` sets `OMP_NUM_THREADS=1` so two bots don't fight over the CPU. Use v0.1.1 or later; if the warnings return, check `load average` with `uptime` and stop the trainer while testing |
| Many `GigaLearnRLBot` processes left over after a match | `wsl -d Ubuntu-24.04 -- pkill GigaLearnRLBot` |
| Bot is slow or laggy in a match | It runs on the CPU by default. Training also loads the machine; stop the trainer while testing, or set `GGL_USE_GPU=1` in `run.sh` |
| `error: ... nothing new to release` | Expected when nothing changed since the last release; use `--force` if you really want a new version |
| `error: RLBot v5 bots folder not found` | RLBot v5 is not installed for this Windows user, or is elsewhere; build with `--no-install` and copy the folder yourself |

## 14. Live training dashboard

A browser dashboard that follows the newest training run. Start it in Ubuntu, or double-click `tools\dashboard\RUN_DASHBOARD.bat` on Windows:

```bash
~/pulsar2/tools/dashboard/run_dashboard.sh
```

Then open **http://localhost:8766** (port 8766, so it does not clash with the old v3 dashboard on 8765). Ctrl+C stops the dashboard only; training keeps running.

**Where the numbers come from.** The server reads the trainer's report blocks from `run_logs/train-*.log` as they are written, so a new iteration appears within about a second (W&B's API refreshes about once a minute). All logs are merged in order; when a run restarts from an older checkpoint, the rolled-back rows are dropped. The header also follows the **latest W&B run**: its id and link, whether it is online or offline, and, once you are logged in, how far the cloud copy has got.

**Controls (train-alone only; exploiters are not implemented yet):** Start, **Stop & save** (same as pressing Q), Kill, Rebuild (disabled while training or while a viewer is open), Package latest / Package best rated (asks first; refuses duplicates).

**Charts**
- Live: choose 1 / 2 / 5 / 15 s or paused. The freshness chip turns amber then red, and a "stalled?" alert appears if no iteration arrives (the WSL allocator hang looks like this). NaN/Inf in the newest row raises an alert.
- Drag to zoom, Shift+drag to pan, Ctrl+wheel to zoom around the cursor, double-click to reset. "Link zoom" zooms every chart together. Zoomed views are re-fetched at full detail.
- Range presets (all, last 2B ... 20M, or a custom `from`/`to` such as `2B`), X axis as steps or iterations.
- Smoothing slider (wandb-style exponential average, raw line stays faint), log Y per chart or globally, "Trim outliers" for spiky metrics.
- A crosshair and tooltip that follow the mouse across **all** charts at once. Click a legend entry to hide a line.
- Search every metric by name (words match in any order); groups Overview, Learning, Skill & opponent, Speed, All metrics (grouped by prefix), and Pinned (use the star on any chart).
- **Team curriculum card + tile:** shows the Phase B trigger (Rating/1v1 at or above 1200 on 3 evals in a row), the last six evals with a ✓/✕, and whether Phase B is already engaged. A dashed "Phase B trigger 1200" line is drawn on the Rating/1v1 chart, and once Phase B fires a vertical line marks the step where it started. The trainer does not log its own streak counter, so the dashboard rebuilds it from the ratings of the newest training process.
- Per chart: **table** (the numbers, with CSV download), **expand** (large view). "Export CSV" downloads every visible chart at full resolution.
- Your choices (theme, zoom range, smoothing, pinned charts, columns) are remembered in the browser.

Charts need only the Python standard library. A Windows-side browser reaches it through WSL's `localhost` forwarding.
