"""Reward Lab stream receiver: the read-only side of the render-mode stream tap (docs/REWARD_VIEWER_SPEC.md section 10, P4-3).

The tap (Rewards/RewardStreamTap.h) sends one JSON datagram per message to 127.0.0.1:PORT. This module receives them, validates them, keeps a ring of the most
recent ones, optionally records every accepted datagram to a JSONL file, and can instead REPLAY such a file. From the ring it builds a `lab_run` for the viewer.
Standard library only. Nothing here sends anything anywhere except HTTP replies to the page.

What it promises:
  * every message is validated before it is stored (shape the viewer relies on); a bad datagram is counted, with a sample reason, and never stored;
  * a lost datagram is VISIBLE: a jump in `seq` is recorded as a gap, never filled in; a falling `seq` is recorded as a restart of the sender;
  * the run shown is the latest contiguous stretch of one episode (it stops at a gap or at the ring's edge); the first frame of a stretch that does not start
    at the episode's reset is shown as a state-only frame 0 (its own reward trace is not shown, because the viewer's frame 0 has none);
  * the ring never grows past `ring` messages.
"""
import collections
import json
import math
import os
import socket
import threading

SCHEMA = 1
KINDS = ("lab_stream_hello", "lab_stream_frame", "lab_stream_error")
MAX_DATAGRAM = 65535
MAX_EPISODES_LISTED = 200


def _is_int(v):
    return isinstance(v, int) and not isinstance(v, bool)


def _is_num(v):
    # finite only: Python's json accepts NaN/Infinity, a browser's JSON.parse does not, and the page is fed with json.dumps of what is stored here
    return isinstance(v, (int, float)) and not isinstance(v, bool) and math.isfinite(v)


def _vec3(v):
    return isinstance(v, list) and len(v) == 3 and all(_is_num(x) for x in v)


def _numlist(v, n=None):
    return isinstance(v, list) and all(_is_num(x) for x in v) and (n is None or len(v) == n)


def validate_message(m):
    """None if `m` is a well-formed stream message, else a short reason. Checks the shape the viewer relies on, not the physics."""
    if not isinstance(m, dict):
        return "not an object"
    if m.get("kind") not in KINDS:
        return "unknown kind"
    if m.get("schema") != SCHEMA or isinstance(m.get("schema"), bool):
        return "unsupported schema"
    if not _is_int(m.get("seq")) or m["seq"] < 0:
        return "seq must be a non-negative integer"
    if not _is_int(m.get("episode")) or m["episode"] < 0:
        return "episode must be a non-negative integer"
    kind = m["kind"]
    if kind == "lab_stream_error":
        if not isinstance(m.get("reason"), str) or not _is_int(m.get("bytes")):
            return "error needs reason and bytes"
        return None
    if kind == "lab_stream_hello":
        if not _is_int(m.get("tick_skip")) or not 1 <= m["tick_skip"] <= 120:
            return "tick_skip must be 1..120"
        cars, rewards = m.get("cars"), m.get("rewards")
        if not isinstance(cars, list) or not all(isinstance(c, dict) and _is_int(c.get("car")) and c.get("team") in (0, 1) for c in cars):
            return "cars must be a list of {car, team}"
        if not isinstance(rewards, list) or not all(isinstance(r, dict) and isinstance(r.get("name"), str) and isinstance(r.get("wandb_key"), str)
                                                    and _is_num(r.get("weight")) and isinstance(r.get("zero_sum"), bool) for r in rewards):
            return "rewards must be a list of {name, wandb_key, weight, zero_sum}"
        return None
    return _validate_frame(m.get("frame"))


def _validate_frame(f):
    if not isinstance(f, dict):
        return "frame must be an object"
    if not _is_int(f.get("i")) or f["i"] < 0:
        return "frame.i must be a non-negative integer"
    for k in ("terminal",):
        if not _is_int(f.get(k)):
            return "frame.%s must be an integer" % k
    if not isinstance(f.get("goal"), bool):
        return "frame.goal must be a boolean"
    ball = f.get("ball")
    if not isinstance(ball, dict) or not _vec3(ball.get("pos")) or not _vec3(ball.get("vel")):
        return "frame.ball needs pos and vel"
    players = f.get("players")
    if not isinstance(players, list) or not players:
        return "frame.players must be a non-empty list"
    for p in players:
        if not isinstance(p, dict) or not _is_int(p.get("car")) or p.get("team") not in (0, 1):
            return "a player needs car and team"
        for k in ("pos", "vel", "forward", "up"):
            if not _vec3(p.get(k)):
                return "player.%s must be 3 numbers" % k
        if not _is_num(p.get("boost")):
            return "player.boost must be a number"
        for k in ("on_ground", "flipping", "jumped", "double_jumped", "flipped", "demoed", "touched"):
            if not isinstance(p.get(k), bool):
                return "player.%s must be a boolean" % k
    acts = f.get("actions")
    if not isinstance(acts, list) or not all(_numlist(a, 8) for a in acts):
        return "frame.actions must be lists of 8 numbers"
    if acts and len(acts) != len(players):
        return "frame.actions must have one entry per player (or none)"
    tr = f.get("trace")
    if tr is None:
        return None
    if f["i"] == 0:
        return "the reset frame must not carry a trace"
    if not isinstance(tr, dict) or tr.get("schema") != 1 or not isinstance(tr.get("cars"), list) or not _numlist(tr.get("total")):
        return "frame.trace needs schema 1, cars and total"
    n = len(players)
    if len(tr["cars"]) != n or len(tr["total"]) != n:
        return "frame.trace must cover every player"
    if not isinstance(tr.get("rewards"), list):
        return "frame.trace.rewards must be a list"
    for r in tr["rewards"]:
        if not isinstance(r, dict) or not isinstance(r.get("name"), str) or not _is_num(r.get("weight")):
            return "a trace reward needs name and weight"
        for k in ("raw", "wrapped", "weighted"):
            if not _numlist(r.get(k), n):
                return "trace reward %s must be %d numbers" % (k, n)
        if not isinstance(r.get("players", []), list):
            return "trace reward players must be a list"
        for pt in r.get("players", []):
            if not isinstance(pt, dict) or not _is_int(pt.get("car")):
                return "a trace player needs car"
            for k, shape in (("components", dict), ("gates", dict), ("prims", dict)):
                v = pt.get(k, [])
                if not isinstance(v, list) or not all(isinstance(x, shape) for x in v):
                    return "trace player %s must be a list of objects" % k
            if not isinstance(pt.get("events", []), list) or not all(isinstance(x, str) for x in pt.get("events", [])):
                return "trace player events must be a list of strings"
            for q in pt.get("prims", []):
                if not isinstance(q.get("kind"), str) or not isinstance(q.get("role"), str) or not _vec3(q.get("a")) or not _vec3(q.get("b")):
                    return "a primitive needs kind, role, a and b"
    return None


class StreamStore:
    """The ring of accepted messages plus counters. Thread-safe."""

    def __init__(self, ring=4096, record_path=None):
        if ring < 16:
            raise ValueError("ring must be at least 16 messages")
        self.ring = ring
        self.lock = threading.Lock()
        self.entries = collections.deque()      # (idx, message dict)
        self.next_idx = 0
        self.received = 0
        self.bad = 0
        self.bad_samples = collections.deque(maxlen=5)
        self.gaps = []                          # {"at_idx", "from_seq", "to_seq"}, newest last, capped
        self.restarts = 0
        self.source = {"mode": "memory"}      # set by the server: {"mode": "udp", "port": n} or {"mode": "replay", "file": name}
        self.last_seq = None
        self.errors_seen = 0                    # lab_stream_error messages (frames the sender refused to send)
        self.record_file = None
        if record_path:
            if os.path.exists(record_path):
                raise FileExistsError("refusing to overwrite %s" % record_path)
            self.record_file = open(record_path, "w", encoding="utf-8")

    def close(self):
        with self.lock:
            if self.record_file:
                self.record_file.close()
                self.record_file = None

    def ingest(self, data):
        """Validate and store one datagram (bytes or str). Returns True if it was accepted."""
        try:
            text = data.decode("utf-8") if isinstance(data, (bytes, bytearray)) else data
            m = json.loads(text)
        except (UnicodeDecodeError, ValueError):
            return self._reject("not JSON")
        why = validate_message(m)
        if why:
            return self._reject(why)
        with self.lock:
            self.received += 1
            if self.last_seq is not None:
                if m["seq"] <= self.last_seq:
                    self.restarts += 1
                elif m["seq"] != self.last_seq + 1:
                    self.gaps.append({"at_idx": self.next_idx, "from_seq": self.last_seq + 1, "to_seq": m["seq"] - 1})
                    del self.gaps[:-100]
            self.last_seq = m["seq"]
            if m["kind"] == "lab_stream_error":
                self.errors_seen += 1
            self.entries.append((self.next_idx, m))
            self.next_idx += 1
            while len(self.entries) > self.ring:
                self.entries.popleft()
            if self.record_file:
                self.record_file.write(text.strip() + "\n")
                self.record_file.flush()
        return True

    def _reject(self, why):
        with self.lock:
            self.bad += 1
            self.bad_samples.append(why)
        return False

    def load_file(self, path):
        """Replay: ingest every line of a recorded JSONL file. Returns (accepted, rejected)."""
        ok = bad = 0
        with open(path, encoding="utf-8") as f:
            for line in f:
                if not line.strip():
                    continue
                if self.ingest(line):
                    ok += 1
                else:
                    bad += 1
        return ok, bad

    # ---- reading ---------------------------------------------------------------------------------------------------------
    def since(self, after, limit=500):
        """Messages with idx > after (after = -1 for all that are kept), oldest first, at most `limit`."""
        with self.lock:
            out = [{"idx": i, "msg": m} for i, m in self.entries if i > after][:limit]
            nxt = out[-1]["idx"] if out else max(after, self.next_idx - 1)
            return out, nxt

    def stats(self):
        with self.lock:
            return {"received": self.received, "bad": self.bad, "bad_samples": list(self.bad_samples), "gaps": list(self.gaps), "restarts": self.restarts,
                    "refused_by_sender": self.errors_seen, "ring": self.ring, "kept": len(self.entries), "oldest_idx": self.entries[0][0] if self.entries else None,
                    "newest_idx": self.next_idx - 1, "last_seq": self.last_seq, "source": self.source}

    def episodes(self):
        with self.lock:
            eps = collections.OrderedDict()
            for i, m in self.entries:
                e = eps.setdefault(m["episode"], {"episode": m["episode"], "frames": 0, "has_hello": False, "first_i": None, "last_i": None})
                if m["kind"] == "lab_stream_hello":
                    e["has_hello"] = True
                elif m["kind"] == "lab_stream_frame":
                    e["frames"] += 1
                    fi = m["frame"]["i"]
                    e["first_i"] = fi if e["first_i"] is None else min(e["first_i"], fi)
                    e["last_i"] = fi if e["last_i"] is None else max(e["last_i"], fi)
            return list(eps.values())[-MAX_EPISODES_LISTED:]

    def build_run(self, episode=None):
        """A `lab_run` (viewer schema 1) of the latest contiguous stretch of `episode` (default: the newest episode with a frame), or None.

        Frames are renumbered 0..n for the viewer (it requires frame k to have index k); the sender's own numbering is kept as `orig_i`.
        """
        with self.lock:
            items = list(self.entries)
            newest_idx = self.next_idx - 1
        if episode is None:
            for _, m in reversed(items):
                if m["kind"] == "lab_stream_frame":
                    episode = m["episode"]
                    break
        if episode is None:
            return None
        hello = None
        frames = []                              # (seq, message) in order, only this episode
        for _, m in items:
            if m["episode"] != episode:
                continue
            if m["kind"] == "lab_stream_hello":
                hello = m
            elif m["kind"] == "lab_stream_frame":
                frames.append(m)
            elif m["kind"] == "lab_stream_error":
                frames.append(m)                 # a refused frame is a hole: it breaks contiguity below
        # the maximal suffix of consecutive-seq frames
        window = []
        for m in reversed(frames):
            if m["kind"] == "lab_stream_error":
                break
            if window and m["seq"] != window[-1]["seq"] - 1:
                break
            window.append(m)
        window.reverse()
        if not window:
            return None
        out_frames = []
        for k, m in enumerate(window):
            f = json.loads(json.dumps(m["frame"]))          # a private copy: the ring's message is never edited
            f["orig_i"] = f["i"]
            f["i"] = k
            if k == 0:
                f["trace"] = None                           # the viewer's frame 0 has no reward trace
                f["actions"] = [] if f["orig_i"] != 0 else f["actions"]
            out_frames.append(f)
        first_orig = window[0]["frame"]["i"]
        tick = hello["tick_skip"] if hello else 8
        name = "stream, episode %d" % episode
        desc = ("Live reward trace from a render run (read-only). Showing frames %d..%d of this episode%s."
                % (first_orig, window[-1]["frame"]["i"], "" if first_orig == 0 else "; the window starts mid-episode (a gap, or the ring's edge), so cumulative sums count from its first frame"))
        run = {"schema": 1, "kind": "lab_run", "result": "NONE",
               "scenario": {"schema": 1, "name": name, "description": desc, "tags": ["stream"], "tick_skip": tick, "action_delay": 0, "steps": len(out_frames) - 1,
                            "cars": [], "ball": {}, "actions": [], "rewards": [], "expect": []},
               "frames": out_frames, "expectations": [],
               "stream": {"episode": episode, "orig_first_i": first_orig, "orig_last_i": window[-1]["frame"]["i"], "starts_at_reset": first_orig == 0,
                          "seq_first": window[0]["seq"], "seq_last": window[-1]["seq"], "next_idx": newest_idx, "rewards": hello["rewards"] if hello else [], "cars": hello["cars"] if hello else []}}
        return run


class UdpReceiver(threading.Thread):
    """Binds 127.x.x.x:PORT (loopback only) and feeds datagrams to a StreamStore. Never replies."""

    def __init__(self, store, port, host="127.0.0.1"):
        super().__init__(daemon=True)
        if not host.startswith("127."):
            raise ValueError("the stream receiver only binds loopback addresses")
        self.store = store
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 * 1024 * 1024)
        self.sock.bind((host, port))
        self.sock.settimeout(0.2)
        self.port = self.sock.getsockname()[1]
        self._stop_flag = threading.Event()

    def run(self):
        while not self._stop_flag.is_set():
            try:
                data, _ = self.sock.recvfrom(MAX_DATAGRAM)
            except socket.timeout:
                continue
            except OSError:
                break
            self.store.ingest(data)

    def stop(self):
        self._stop_flag.set()
        try:
            self.sock.close()
        except OSError:
            pass


# ---- the page ------------------------------------------------------------------------------------------------------------
def inline_includes(text, directory):
    """Replace each <!--INLINE:name--> with the file's text, once, without rescanning what was inlined (the C++ exporter's rule)."""
    open_, close = "<!--INLINE:", "-->"
    out, pos = [], 0
    while True:
        i = text.find(open_, pos)
        if i < 0:
            out.append(text[pos:])
            return "".join(out)
        e = text.find(close, i)
        if e < 0:
            raise ValueError("unterminated INLINE marker")
        name = text[i + len(open_):e]
        with open(os.path.join(directory, name), encoding="utf-8") as f:
            out.append(text[pos:i])
            out.append(f.read())
        pos = e + len(close)


def json_for_script(obj):
    """JSON safe inside <script type=application/json>: '<' only occurs inside strings, so it is written as \\u003c."""
    return json.dumps(obj, separators=(",", ":")).replace("<", "\\u003c")


def build_page(template_path, run_json_text, live_script=""):
    """The viewer page: INLINE markers resolved, then the run marker, then the live marker (the C++ exporter's order)."""
    with open(template_path, encoding="utf-8") as f:
        out = f.read()
    out = inline_includes(out, os.path.dirname(os.path.abspath(template_path)))
    marker = "<!--LAB_RUN_JSON-->"
    p = out.find(marker)
    if p < 0:
        raise ValueError("template has no <!--LAB_RUN_JSON--> marker")
    out = out[:p] + run_json_text + out[p + len(marker):]
    live = "<!--LAB_LIVE-->"
    q = out.find(live)
    if q >= 0:
        out = out[:q] + live_script + out[q + len(live):]
    return out
