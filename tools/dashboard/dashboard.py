#!/usr/bin/env python3
"""Pulsar2 live training dashboard.

Run it with tools/dashboard/run_dashboard.sh (or RUN_DASHBOARD.bat on Windows), then open http://localhost:8766

Data: the server tails the trainer's report blocks in run_logs/train-*.log, so a new iteration shows up in about a
second (the W&B API only refreshes about once a minute). All logs are merged in order; when a run is restarted from
an older checkpoint, the rows that were rolled back are dropped, so the charts always show the surviving history.
The latest W&B run is followed too: its id, link and online/offline state come from build/wandb/latest-run, and when
you are logged in (wandb login) the server also asks the W&B API how far the cloud copy has got.

Controls: start training, stop (saves a checkpoint after the current iteration, same as pressing Q), kill, rebuild,
package the newest or strongest-rated checkpoint as an RLBot bot. Listens on 127.0.0.1 only; anything that changes
state must carry an X-Dashboard header, which a page on another site cannot send.
"""
import argparse
import bisect
import json
import math
import os
import re
import statistics
import subprocess
import threading
import time
import webbrowser
from collections import deque
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
BUILD = REPO / "build"
LOGS = REPO / "run_logs"
CKPT_NAME = "checkpoints_5.3"
RUN_SH = REPO / "tools" / "run_trainer.sh"
WANDB_PROJECT = "gigalearncpp"

args = None

# --------------------------------------------------------------------------------------------- log parsing

NUMBER = r"[-+]?(?:\d[\d,]*(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?|[-+]?nan|[-+]?inf"
LINE = re.compile(r"^\s*(?:-\s+)?([A-Za-z][^:\n]{0,80}?):\s*(" + NUMBER + r")\s*$", re.I)
RULE = re.compile(r"^={20,}\s*$")


def to_float(s):
    return float(s.replace(",", ""))


class Store:
    """Every report row ever logged, as aligned columns. Rows are keyed by Total Timesteps."""

    def __init__(self):
        self.lock = threading.RLock()
        self.xs, self.its, self.arr, self.fids = [], [], [], []
        self.phase_by_fid = {}
        self._fid = 0
        self.cols = {}
        self.epoch = 0
        self.nan_keys = set()
        self.live = deque(maxlen=40)  # wall-clock arrival times of rows that were seen live
        self.file_mtime = 0.0
        self.log_path = None
        self.stop_seen = False
        self.saving = False
        self.notes = deque(maxlen=60)  # interesting log lines (saves, stops, errors)

    def next_fid(self):
        with self.lock:
            self._fid += 1
            return self._fid

    def _rollback(self, ts):
        while self.xs and self.xs[-1] >= ts:
            self.xs.pop(); self.its.pop(); self.arr.pop(); self.fids.pop()
            for col in self.cols.values():
                col.pop()
        self.epoch += 1

    def add(self, m, arrival, fid=0):
        ts = m.get("Total Timesteps")
        if ts is None or not math.isfinite(ts):
            return
        with self.lock:
            if self.xs and ts <= self.xs[-1]:
                self._rollback(ts)
            n = len(self.xs)
            it = m.get("Total Iterations")
            self.xs.append(ts)
            self.its.append(it if it is not None and math.isfinite(it) else (self.its[-1] + 1 if self.its else 1))
            self.arr.append(arrival)
            self.fids.append(fid)
            self.nan_keys = set()
            for k, v in m.items():
                if not math.isfinite(v):
                    self.nan_keys.add(k)
                    v = None
                col = self.cols.get(k)
                if col is None:
                    col = self.cols[k] = [None] * n
                col.append(v)
            for col in self.cols.values():
                if len(col) < n + 1:
                    col.append(None)
            if arrival:
                self.live.append(arrival)

    def query(self, keys, x0, x1, maxpts, axis):
        """Rows in [x0, x1], thinned to about maxpts. Each output point is one bucket of `stride` rows: its x is the
        bucket's last row and each metric's value is its last real value in the bucket, so a metric that is only
        logged every 16th iteration (Rating/*) never aliases away."""
        with self.lock:
            xs = self.xs if axis == "ts" else self.its
            lo = bisect.bisect_left(xs, x0) if x0 is not None else 0
            hi = bisect.bisect_right(xs, x1) if x1 is not None else len(xs)
            n = max(0, hi - lo)
            stride = max(1, -(-n // maxpts)) if n else 1
            starts = list(range(lo, hi, stride))
            anchors = [min(s + stride, hi) - 1 for s in starts]
            y = {}
            for k in keys:
                col = self.cols.get(k)
                if col is None:
                    y[k] = [None] * len(starts)
                elif stride == 1:
                    y[k] = [col[i] for i in anchors]
                else:
                    out = []
                    for s, a in zip(starts, anchors):
                        v = None
                        for t in range(a, s - 1, -1):
                            if col[t] is not None:
                                v = col[t]
                                break
                        out.append(v)
                    y[k] = out
            return {"ts": [self.xs[i] for i in anchors], "it": [self.its[i] for i in anchors], "y": y,
                    "n": n, "stride": stride, "epoch": self.epoch, "rows": len(self.xs)}

    def latest(self):
        with self.lock:
            n = len(self.xs)
            latest, prev10, prev60 = {}, {}, {}
            for k, col in self.cols.items():
                i = n - 1
                while i >= max(0, n - 80) and col[i] is None:
                    i -= 1
                if i >= 0 and col[i] is not None:
                    latest[k] = col[i]
                    prev10[k] = col[i - 10] if i - 10 >= 0 else None
                    prev60[k] = col[i - 60] if i - 60 >= 0 else None
            return latest, prev10, prev60

    def iter_seconds(self):
        with self.lock:
            a = list(self.live)
        if len(a) < 3:
            return None
        d = [b - c for b, c in zip(a[1:], a[:-1]) if 0 < b - c < 600]
        return statistics.median(d) if d else None


store = Store()


class Parser:
    """Turns log lines into rows. A report block starts at a line of ===== and ends at 'Total Iterations: N'."""

    def __init__(self):
        self.in_block, self.cur = False, {}
        self.fid = 0

    def reset(self):
        self.in_block, self.cur = False, {}

    def feed(self, line, live):
        line = line.rstrip("\r\n")
        if line.startswith("[20") and "] launch #" in line:  # run_trainer.sh relaunched the trainer inside this same log file
            self.fid = store.next_fid()
        if line.startswith("Team curriculum: PHASE"):
            store.phase_by_fid[self.fid] = line.split("PHASE", 1)[1].strip()[:1]
        if RULE.match(line):
            self.in_block, self.cur = True, {}
            return
        if line.startswith(("Stop signal received", "Save queued")):
            store.stop_seen = True
            store.notes.appendleft((time.time(), line))
        elif line.startswith("Saving to folder"):
            store.saving = True
            store.notes.appendleft((time.time(), line))
        elif "FATAL" in line or line.startswith("Traceback"):
            store.notes.appendleft((time.time(), line[:300]))
        if not self.in_block:
            return
        m = LINE.match(line)
        if not m:
            return
        name, val = m.group(1).strip(), to_float(m.group(2))
        self.cur[name] = val
        if name == "Total Iterations":
            store.add(self.cur, time.time() if live else None, self.fid)
            self.in_block, self.cur = False, {}


class Tailer(threading.Thread):
    def __init__(self):
        super().__init__(daemon=True)
        self.parser, self.path, self.off, self.partial = Parser(), None, 0, b""
        self.fid = 0

    @staticmethod
    def newest():
        link = LOGS / "latest.log"
        try:
            if link.exists():
                return link.resolve()
        except OSError:
            pass
        files = sorted(LOGS.glob("train-*.log"), key=lambda p: p.stat().st_mtime)
        return files[-1] if files else None

    def read_new(self, live):
        with open(self.path, "rb") as f:
            f.seek(self.off)
            data = f.read()
        self.off += len(data)
        data = self.partial + data
        lines = data.split(b"\n")
        self.partial = lines.pop()  # an unfinished last line waits for the next read
        for raw in lines:
            self.parser.feed(raw.decode("utf-8", "replace"), live)

    def run(self):
        files = sorted(LOGS.glob("train-*.log")) if LOGS.is_dir() else []
        for f in files:  # history, oldest first (a restart from an older checkpoint rolls rows back)
            self.parser.reset()
            self.parser.fid = store.next_fid()
            self.path, self.off, self.partial = f, 0, b""
            try:
                self.read_new(live=False)
            except OSError:
                pass
        store.stop_seen = store.saving = False
        while True:
            try:
                latest = self.newest()
                if latest is not None:
                    if latest != self.path:
                        self.path, self.off, self.partial = latest, 0, b""
                        self.parser.reset()
                        self.parser.fid = store.next_fid()
                        store.stop_seen = store.saving = False
                    st = latest.stat()
                    store.log_path, store.file_mtime = str(latest), st.st_mtime
                    if st.st_size < self.off:  # truncated
                        self.off, self.partial = 0, b""
                        self.parser.reset()
                    if st.st_size > self.off:
                        self.read_new(live=True)
            except Exception as e:  # keep tailing no matter what
                store.notes.appendleft((time.time(), f"dashboard tailer: {type(e).__name__}: {e}"))
            time.sleep(0.7)


# --------------------------------------------------------------------------------------------- trainer / files

_cache = {}


def cached(key, ttl, fn):
    hit = _cache.get(key)
    if hit and time.time() - hit[0] < ttl:
        return hit[1]
    val = fn()
    _cache[key] = (time.time(), val)
    return val


def run_cmd(cmd, timeout=60, cwd=BUILD, env=None):
    e = dict(os.environ)
    e["PATH"] = str(Path.home() / ".cargo" / "bin") + os.pathsep + e.get("PATH", "")
    if env:
        e.update(env)
    r = subprocess.run(cmd, cwd=str(cwd), capture_output=True, text=True, timeout=timeout, env=e)
    return r.returncode, (r.stdout + r.stderr).strip()


def processes():
    """GigaLearnBot processes: the trainer and any render viewer."""
    out = []
    try:
        ps = subprocess.run(["ps", "-eo", "pid=,etimes=,args="], capture_output=True, text=True, timeout=5).stdout
    except Exception:
        return out
    for line in ps.splitlines():
        parts = line.split(None, 2)
        if len(parts) < 3 or not parts[2].split()[0].endswith("/GigaLearnBot") and parts[2].split()[0] != "./GigaLearnBot":
            continue
        pid = int(parts[0])
        viewer = False
        try:
            viewer = b"GGL_RENDER=1" in Path(f"/proc/{pid}/environ").read_bytes().split(b"\0")
        except Exception:
            pass
        out.append({"pid": pid, "uptime": int(parts[1]), "viewer": viewer})
    return out


stop_requested_at = None


def trainer_info():
    global stop_requested_at
    procs = cached("procs", 1.5, processes)
    train = [p for p in procs if not p["viewer"]]
    running = bool(train)
    if not running:
        stop_requested_at = None
    return {
        "running": running, "pid": train[0]["pid"] if running else None,
        "uptime": train[0]["uptime"] if running else None,
        "viewers": sum(1 for p in procs if p["viewer"]),
        "stopping": bool(running and (stop_requested_at or store.stop_seen)),
        "saving": bool(running and store.saving),
        "stopRequestedAt": stop_requested_at,
    }


def read_json(path):
    try:
        return json.loads(Path(path).read_text())
    except Exception:
        return None


def checkpoints():
    d = BUILD / CKPT_NAME
    out = {"numbered": [], "best": []}
    try:
        for p in d.iterdir():
            if not p.is_dir():
                continue
            m = re.fullmatch(r"best_r(-?\d+)_(\d+)", p.name)
            if p.name.isdigit():
                out["numbered"].append({"step": int(p.name), "mtime": p.stat().st_mtime})
            elif m:
                out["best"].append({"name": p.name, "rating": int(m.group(1)), "step": int(m.group(2)), "mtime": p.stat().st_mtime})
    except OSError:
        pass
    out["numbered"].sort(key=lambda c: -c["step"])
    out["best"].sort(key=lambda c: (-c["rating"], -c["step"]))
    out["numbered"], out["best"] = out["numbered"][:6], out["best"][:6]
    return out


def releases():
    out = []
    root = REPO / "rlbot-run" / "releases"
    try:
        for p in root.iterdir():
            j = read_json(p / "release.json")
            if j:
                out.append({"version": j.get("version"), "step": j.get("checkpoint_step"), "rating": j.get("rating") or None,
                            "mode": j.get("mode"), "created": j.get("created")})
    except OSError:
        pass
    out.sort(key=lambda r: [int(x) for x in (r["version"] or "0.0.0").split(".")], reverse=True)
    return out[:8]


_wandb_api = {"t": 0, "data": None}


def wandb_info():
    """The newest local W&B run (build/wandb/latest-run), plus the cloud copy's progress if we can ask."""
    info = {"mode": None, "id": None, "name": None, "url": None, "dir": None, "loggedIn": False, "cloud": None}
    try:
        latest = (BUILD / "wandb" / "latest-run").resolve()
        info["dir"] = latest.name
        info["mode"] = "offline" if latest.name.startswith("offline-run") else "online"
        info["id"] = latest.name.rsplit("-", 1)[-1]
        for logf in sorted((latest / "logs").glob("debug*.log")):
            m = re.search(r"https://[^\s\"']+/runs/\w+", logf.read_text(errors="replace"))
            if m:
                info["url"] = m.group(0)
        meta = read_json(latest / "files" / "wandb-summary.json") or {}
        info["localStep"] = meta.get("_step")
    except Exception:
        pass
    info["loggedIn"] = bool(os.environ.get("WANDB_API_KEY"))
    try:
        import netrc  # the key is stored under whichever server you logged in to (api.wandb.ai, now api.forge.coreweave.com)
        for host, auth in netrc.netrc().hosts.items():
            if ("wandb" in host or "coreweave" in host) and auth and auth[2]:
                info["loggedIn"] = True
    except Exception:
        pass
    if info["mode"] == "online" and info["loggedIn"] and info["url"]:
        if time.time() - _wandb_api["t"] > 30:
            _wandb_api["t"] = time.time()
            threading.Thread(target=_fetch_wandb_cloud, args=(info["url"],), daemon=True).start()
        info["cloud"] = _wandb_api["data"]
    return info


def _fetch_wandb_cloud(url):
    try:
        import wandb
        path = "/".join(url.split("://", 1)[1].split("/", 1)[1].replace("/runs/", "/").split("/")[:3])
        run = wandb.Api(timeout=20).run(path)
        s = run.summary
        _wandb_api["data"] = {"state": run.state, "name": run.name, "step": s.get("_step"),
                              "timesteps": s.get("Total Timesteps"), "checked": time.time()}
    except Exception as e:
        _wandb_api["data"] = {"error": f"{type(e).__name__}: {str(e)[:160]}", "checked": time.time()}


# --------------------------------------------------------------------------------------------- actions

class ActionError(Exception):
    pass


_build = {"state": "idle", "started": None, "ended": None, "code": None, "lines": []}
_build_lock = threading.Lock()
activity = deque(maxlen=60)


def say(msg, err=False):
    activity.appendleft({"t": time.time(), "msg": msg, "err": err})


def do_start():
    if trainer_info()["running"]:
        raise ActionError("Training is already running.")
    if _build["state"] == "running":
        raise ActionError("A build is running; start training after it finishes.")
    global stop_requested_at
    stop_requested_at = None
    store.stop_seen = store.saving = False
    code, out = run_cmd([str(RUN_SH)], timeout=60)
    _cache.pop("procs", None)
    if code != 0:
        raise ActionError(out or f"run_trainer.sh exited with {code}")
    return "Training started. " + " ".join(l for l in out.splitlines() if l.startswith("log:"))


def do_stop():
    global stop_requested_at
    if not trainer_info()["running"]:
        raise ActionError("Training isn't running.")
    if stop_requested_at:
        raise ActionError("A stop is already in progress. Use Kill to end it immediately (nothing more is saved).")
    stop_requested_at = time.time()

    def work():
        code, out = run_cmd([str(RUN_SH), "--stop"], timeout=1800)
        say("Training stopped and checkpoint saved." if code == 0 else "Stop failed: " + out[-300:], code != 0)
        _cache.pop("procs", None)
    threading.Thread(target=work, daemon=True).start()
    return "Stop requested: it finishes this iteration, saves a checkpoint, then exits (the same as pressing Q)."


def do_kill():
    global stop_requested_at
    if not trainer_info()["running"]:
        raise ActionError("Training isn't running.")
    code, out = run_cmd([str(RUN_SH), "--kill"], timeout=60)
    stop_requested_at = None
    _cache.pop("procs", None)
    return "Killed. Nothing was saved; it resumes from the last checkpoint next time."


def do_build():
    with _build_lock:
        if _build["state"] == "running":
            raise ActionError("A build is already running.")
        if trainer_info()["running"] or trainer_info()["viewers"]:
            raise ActionError("Stop training (and any viewer) before rebuilding.")
        _build.update(state="running", started=time.time(), ended=None, code=None, lines=[])

    def work():
        env = dict(os.environ)
        env["PATH"] = str(Path.home() / ".cargo" / "bin") + os.pathsep + env.get("PATH", "")
        try:
            p = subprocess.Popen(["cmake", "--build", "build", "-j", str(os.cpu_count() or 4)], cwd=str(REPO), env=env,
                                 stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
            for line in p.stdout:
                with _build_lock:
                    _build["lines"].append(line.rstrip())
                    del _build["lines"][:-1500]
            code = p.wait()
        except Exception as e:
            code = -1
            _build["lines"].append(f"Couldn't run cmake: {e}")
        with _build_lock:
            _build.update(state="ok" if code == 0 else "failed", ended=time.time(), code=code)
        say("Build finished." if code == 0 else f"Build failed (exit {code}).", code != 0)
    threading.Thread(target=work, daemon=True).start()
    return "Build started."


def do_package(best, install):
    cmd = [str(REPO / "tools" / "package_bot.sh")]
    if best:
        cmd.append("--best")
    if not install:
        cmd.append("--no-install")
    code, out = run_cmd(cmd, timeout=300, cwd=REPO)
    if code == 3:
        raise ActionError("Nothing new to release.\n" + out)
    if code != 0:
        raise ActionError(out[-600:])
    return "\n".join(out.splitlines()[-6:])


def build_status():
    with _build_lock:
        b = {k: _build[k] for k in ("state", "started", "ended", "code")}
        lines = list(_build["lines"])
    b["errors"] = [l for l in lines if re.search(r"\berror\b|undefined reference", l, re.I)][:10]
    b["tail"] = lines[-8:]
    return b


# --------------------------------------------------------------------------------------------- state

_pb = {"t": 0, "c": None}


def phase_b_constants():
    """Threshold, streak length and marker name, read from src/ExampleMain.cpp so they never drift from the trainer."""
    if _pb["c"] and time.time() - _pb["t"] < 30:
        return _pb["c"]
    thr, need, marker = 1200.0, 3, "PHASE_B_ENGAGED"
    try:
        src = (REPO / "src" / "ExampleMain.cpp").read_text(errors="replace")
        thr = float(re.search(r"PHASE_B_RATING_TRIGGER\s*=\s*([\d.]+)", src).group(1))
        need = int(re.search(r"PHASE_B_TRIGGER_STREAK\s*=\s*(\d+)", src).group(1))
        marker = re.search(r'PHASE_B_MARKER\s*=\s*"([^"]+)"', src).group(1)
    except Exception:
        pass
    _pb.update(t=time.time(), c=(thr, need, marker))
    return _pb["c"]


def phase_b_info():
    """The trainer's PHASE A -> B trigger: Rating/1v1 >= threshold on `need` consecutive evals. The trainer keeps that
    counter in memory only and does not log it, so it is rebuilt here from the rating evals of the newest training
    process (log file). `streakAll` ignores restarts, to show what a restart threw away."""
    thr, need, marker = phase_b_constants()
    mpath = BUILD / CKPT_NAME / marker
    engaged, text, ts = mpath.exists(), None, None
    if engaged:
        try:
            text = mpath.read_text().strip()
        except Exception:
            text = ""
        m = re.search(r"engaged at ts (\d+)", text or "")
        ts = int(m.group(1)) if m else None
    with store.lock:
        R = store.cols.get("Rating/1v1")
        n = len(store.xs)
        fid_last = store.fids[-1] if n else None
        evals, streak, streak_all, cnt, cnt_all = [], 0, 0, True, True
        if R:
            i = n - 1
            while i >= 0 and len(evals) < 12:
                v = R[i]
                if v is not None:
                    ok = v >= thr
                    evals.append({"iter": store.its[i], "steps": store.xs[i], "rating": v, "ok": ok})
                    if cnt_all:
                        if ok:
                            streak_all += 1
                        else:
                            cnt_all = False
                    if cnt:
                        if ok and store.fids[i] == fid_last:
                            streak += 1
                        else:
                            cnt = False
                i -= 1
        last_iter = store.its[-1] if n else None
        phase = store.phase_by_fid.get(fid_last)
    evals.reverse()
    since = (last_iter - evals[-1]["iter"]) if evals and last_iter is not None else None
    return {"threshold": thr, "need": need, "streak": min(streak, need), "streakAll": min(streak_all, need), "evals": evals,
            "sinceLastEval": since, "engaged": engaged, "markerText": text, "engagedTs": ts, "phase": phase}


def state():
    latest, p10, p60 = store.latest()
    with store.lock:
        n = len(store.xs)
        last = {"x": store.xs[-1] if n else None, "iter": store.its[-1] if n else None}
        arrival = store.arr[-1] if n and store.arr[-1] else None
    tr = trainer_info()
    now = time.time()
    # Freshness: when the newest row was seen live, else when the log last changed
    age = now - (arrival or store.file_mtime) if (arrival or store.file_mtime) else None
    itsec = store.iter_seconds()
    alerts = []
    if tr["running"] and age is not None and age > max(120, (itsec or 10) * 8) and not tr["saving"]:
        alerts.append({"level": "critical", "msg": f"No new iteration for {int(age)} s. The trainer may be stuck. On WSL this is usually the CUDA allocator hang (needs PYTORCH_CUDA_ALLOC_CONF=expandable_segments:False)."})
    if store.nan_keys:
        alerts.append({"level": "critical", "msg": "NaN/Inf in the newest row: " + ", ".join(sorted(store.nan_keys)[:6])})
    if tr["stopping"]:
        alerts.append({"level": "warning", "msg": "Stopping: it finishes the current iteration, saves a checkpoint, then exits."})
    return {
        "now": now, "rows": n, "epoch": store.epoch, "last": last, "age": age, "iterSeconds": itsec,
        "trainer": tr, "latest": latest, "prev10": p10, "prev60": p60,
        "keys": sorted(store.cols), "alerts": alerts,
        "log": store.log_path, "notes": [{"t": t, "msg": m} for t, m in list(store.notes)[:8]],
        "checkpoints": cached("ckpt", 5, checkpoints), "releases": cached("rel", 5, releases),
        "wandb": cached("wandb", 3, wandb_info), "build": build_status(), "activity": list(activity)[:25],
        "phaseB": phase_b_info(),
    }


# --------------------------------------------------------------------------------------------- HTTP

class Handler(BaseHTTPRequestHandler):
    server_version = "Pulsar2Dashboard/1.0"

    def log_message(self, fmt, *a):
        pass

    def host_ok(self):
        return (self.headers.get("Host") or "").split(":")[0] in ("127.0.0.1", "localhost")

    def send(self, code, body, ctype="application/json"):
        data = body if isinstance(body, bytes) else json.dumps(body, allow_nan=False).encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        if not self.host_ok():
            return self.send(403, {"error": "forbidden"})
        u = urlparse(self.path)
        q = parse_qs(u.query)
        try:
            if u.path in ("/", "/index.html"):
                return self.send(200, (HERE / "index.html").read_bytes(), "text/html; charset=utf-8")
            if u.path == "/api/state":
                return self.send(200, state())
            if u.path == "/api/series":
                keys = q.get("k", [])[:60]
                f = lambda name: float(q[name][0]) if q.get(name) and q[name][0] not in ("", "null") else None
                maxpts = max(50, min(int((q.get("max") or ["1500"])[0]), 100000))
                axis = "it" if (q.get("axis") or ["ts"])[0] == "it" else "ts"
                return self.send(200, store.query(keys, f("x0"), f("x1"), maxpts, axis))
            return self.send(404, {"error": "not found"})
        except Exception as e:
            return self.send(500, {"error": f"{type(e).__name__}: {e}"})

    def do_POST(self):
        if not self.host_ok() or self.headers.get("X-Dashboard") != "1":
            return self.send(403, {"error": "forbidden"})
        try:
            body = json.loads(self.rfile.read(int(self.headers.get("Content-Length") or 0)) or b"{}")
        except Exception:
            return self.send(400, {"error": "bad JSON"})
        path = urlparse(self.path).path
        try:
            if path == "/api/start":
                msg = do_start()
            elif path == "/api/stop":
                msg = do_stop()
            elif path == "/api/kill":
                msg = do_kill()
            elif path == "/api/build":
                msg = do_build()
            elif path == "/api/package":
                msg = do_package(bool(body.get("best")), body.get("install", True))
            else:
                return self.send(404, {"error": "not found"})
            say(msg)
            return self.send(200, {"ok": True, "message": msg})
        except ActionError as e:
            say(str(e), True)
            return self.send(409, {"ok": False, "message": str(e)})
        except Exception as e:
            say(f"{type(e).__name__}: {e}", True)
            return self.send(500, {"ok": False, "message": f"{type(e).__name__}: {e}"})


def main():
    global args
    ap = argparse.ArgumentParser(description="Pulsar2 live training dashboard")
    ap.add_argument("--port", type=int, default=8766)
    ap.add_argument("--open", action="store_true", help="open a browser tab")
    args = ap.parse_args()
    t = Tailer()
    t.start()
    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    url = f"http://localhost:{args.port}"
    print(f"Pulsar2 dashboard: {url}   (Ctrl+C stops the dashboard; training keeps running)", flush=True)
    print(f"  logs: {LOGS}", flush=True)
    if args.open:
        threading.Timer(0.8, lambda: webbrowser.open(url)).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()