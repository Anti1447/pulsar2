#!/usr/bin/env python3
"""Print ONE JSON line: the newest local W&B run's cloud summary (every panel the run has, with its latest value).

Why a separate script: dashboard.py is stdlib-only, and the `wandb` package is optional. The dashboard runs this with its own
interpreter in a background thread every ~45 s; a missing package, no login or no network just becomes {"ok": false, "error": ...}.

The trainer prints only a curated block to its log; every panel (Demo/*, FlipReset/*, Carry/*, Catch/*, Rewards/*, Gauntlet/*, ...) goes to
W&B. The run is found from build/wandb/latest-run (same run id across restarts of a lineage), the entity from the saved login (the key can be
for api.wandb.ai or api.forge.coreweave.com; the SDK reads ~/.netrc itself). Read-only.
"""
import json
import math
import os
import re
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
BUILD = Path(os.environ.get("TRAINERCTL_BUILD_DIR", REPO / "build"))
PROJECT = "gigalearncpp"

_MANGLED = re.compile(r"^(Rewards/)N4RLGC(\d+)(.+)$")


def pretty_key(k):
    """Rewards/* panels are named by the C++ type's mangled name (Rewards/N4RLGC10DemoRewardE): show Rewards/DemoReward."""
    m = _MANGLED.match(k)
    if not m:
        return k
    n = int(m.group(2))
    name = m.group(3)[:n]
    if len(name) != n:
        return k
    return m.group(1) + name + (" (VerifiedShot)" if "shotVerified" in m.group(3) else "")


def main():
    out = {"ok": False}
    try:
        name = (BUILD / "wandb" / "latest-run").resolve().name
        if not name.startswith("run-"):
            out["error"] = "offline or no W&B run yet" if name.startswith("offline-run") or name == "latest-run" else "no W&B run"
        else:
            import wandb  # optional dependency
            api = wandb.Api(timeout=30)
            path = f"{os.environ.get('WANDB_ENTITY') or api.default_entity}/{PROJECT}/{name.rsplit('-', 1)[-1]}"
            run = api.run(path)
            summ = dict(run.summary)
            panels = {pretty_key(k): v for k, v in summ.items()
                      if not k.startswith("_") and isinstance(v, (int, float)) and not isinstance(v, bool) and math.isfinite(v)}
            out.update(ok=True, state=run.state, name=run.name, url=run.url, step=summ.get("_step"),
                       timesteps=summ.get("Total Timesteps"), panels=panels, fetched=time.time())
    except ImportError:
        out["error"] = "the wandb package is not installed for this python"
    except Exception as e:
        out["error"] = f"{type(e).__name__}: {str(e)[:160]}"
    print(json.dumps(out, allow_nan=False))
    return 0


if __name__ == "__main__":
    sys.exit(main())
