#!/usr/bin/env python3
"""Reward Lab live server (docs/REWARD_VIEWER_SPEC.md, P3-2).

HTTP in front of build-lab/reward_lab_host (a JSON-lines process). Serves the viewer page in live mode and forwards the page's
commands to the host. Standard library only.

    python3 tools/reward_lab/serve.py [--pack DIR] [--port 8765] [--host-bin build-lab/reward_lab_host]

Run it from anywhere; the host is started with the repo root as its working directory (it needs build/collision_meshes).

Safety: binds 127.0.0.1 only; POST /api needs the header "X-Lab-Client: 1" (a cross-site page cannot send it without a CORS
preflight, which this server never grants) and a Host header naming this server; the browser may only send the commands in
BROWSER_COMMANDS, never a file path (scenarios are picked by NAME from the pack folder, macros and scenarios are sent as data).
"""
import argparse
import http.server
import json
import os
import subprocess
import sys
import threading

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TEMPLATE = "tools/reward_lab/viewer/viewer.template.html"
BROWSER_COMMANDS = {"load_named", "load", "state", "step", "step_scripted", "seek", "reset", "macro_record", "macro_play"}
MAX_BODY = 4 * 1024 * 1024


class Host:
    """One reward_lab_host process, one command at a time."""

    def __init__(self, binary):
        self.binary = binary
        self.proc = None
        self.lock = threading.Lock()

    def _start(self):
        self.proc = subprocess.Popen([self.binary], cwd=ROOT, stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)

    def call(self, cmd):
        with self.lock:
            for attempt in (0, 1):
                if self.proc is None or self.proc.poll() is not None:
                    if attempt == 1:
                        break
                    self._start()
                    self.restarted = True
                try:
                    self.proc.stdin.write(json.dumps(cmd, separators=(",", ":")) + "\n")
                    self.proc.stdin.flush()
                    line = self.proc.stdout.readline()
                    if line:
                        return json.loads(line)
                except (BrokenPipeError, OSError):
                    pass
                self.proc = None
            return {"ok": False, "error": "the reward_lab_host process died and could not be restarted"}

    def close(self):
        if self.proc and self.proc.poll() is None:
            self.proc.stdin.close()
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.proc.kill()


def scenario_index(pack):
    """name -> path for every *.scenario.json under the pack (name = the file name without the suffix)."""
    out = {}
    for dirpath, _, files in os.walk(pack):
        for f in sorted(files):
            if f.endswith(".scenario.json"):
                out[f[: -len(".scenario.json")]] = os.path.join(dirpath, f)
    return dict(sorted(out.items()))


def make_handler(host, pack, port):
    allowed_hosts = {"127.0.0.1:%d" % port, "localhost:%d" % port}

    class Handler(http.server.BaseHTTPRequestHandler):
        server_version = "RewardLab/1"

        def log_message(self, fmt, *args):
            if os.environ.get("LAB_SERVE_LOG"):
                sys.stderr.write("%s - %s\n" % (self.address_string(), fmt % args))

        def _send(self, code, body, ctype="application/json"):
            data = body if isinstance(body, bytes) else body.encode("utf-8")
            self.send_response(code)
            self.send_header("Content-Type", ctype + "; charset=utf-8")
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(data)

        def _json(self, code, obj):
            self._send(code, json.dumps(obj, separators=(",", ":")))

        def _host_ok(self):
            return self.headers.get("Host", "") in allowed_hosts

        def do_GET(self):
            if not self._host_ok():
                return self._json(403, {"ok": False, "error": "bad Host header"})
            if self.path == "/api/scenarios":
                return self._json(200, {"ok": True, "scenarios": list(scenario_index(pack).keys())})
            if self.path.split("?")[0] in ("/", "/index.html"):
                st = host.call({"cmd": "state"})
                if not st.get("ok"):                       # first visit: load the first scenario of the pack
                    names = list(scenario_index(pack).items())
                    if not names:
                        return self._send(500, "no *.scenario.json under " + pack, "text/plain")
                    with open(names[0][1], encoding="utf-8") as f:
                        sc = json.load(f)
                    r = host.call({"cmd": "load", "scenario": sc})
                    if not r.get("ok"):
                        return self._send(500, "cannot load " + names[0][0] + ": " + str(r.get("error")), "text/plain")
                r = host.call({"cmd": "page", "template": TEMPLATE})
                if not r.get("ok"):
                    return self._send(500, str(r.get("error")), "text/plain")
                return self._send(200, r["html"], "text/html")
            return self._send(404, "not found", "text/plain")

        def do_POST(self):
            if self.path != "/api":
                return self._send(404, "not found", "text/plain")
            if not self._host_ok():
                return self._json(403, {"ok": False, "error": "bad Host header"})
            if self.headers.get("X-Lab-Client") != "1":
                return self._json(403, {"ok": False, "error": "missing X-Lab-Client header"})
            try:
                n = int(self.headers.get("Content-Length", "0"))
            except ValueError:
                n = -1
            if n < 0 or n > MAX_BODY:
                return self._json(413, {"ok": False, "error": "body too large or missing length"})
            try:
                cmd = json.loads(self.rfile.read(n))
            except ValueError:
                return self._json(400, {"ok": False, "error": "body is not JSON"})
            if not isinstance(cmd, dict) or cmd.get("cmd") not in BROWSER_COMMANDS:
                return self._json(400, {"ok": False, "error": "command not allowed from the browser"})
            if cmd["cmd"] == "load_named":
                index = scenario_index(pack)
                name = cmd.get("name")
                if not isinstance(name, str) or name not in index:
                    return self._json(400, {"ok": False, "error": "unknown scenario name"})
                with open(index[name], encoding="utf-8") as f:
                    fwd = {"cmd": "load", "scenario": json.load(f)}
                if "steps" in cmd:
                    fwd["steps"] = cmd["steps"]
                cmd = fwd
            return self._json(200, host.call(cmd))

    return Handler


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--pack", default=os.path.join(ROOT, "tools/reward_lab/packs/core"))
    ap.add_argument("--port", type=int, default=8765, help="0 picks a free port and prints it")
    ap.add_argument("--host-bin", default=os.path.join(ROOT, "build-lab/reward_lab_host"))
    args = ap.parse_args()
    if not os.path.exists(args.host_bin):
        sys.exit("missing %s (run tools/reward_lab/build.sh)" % args.host_bin)
    host = Host(args.host_bin)
    # bind first so --port 0 is known before the handler is built
    httpd = http.server.ThreadingHTTPServer(("127.0.0.1", args.port), http.server.BaseHTTPRequestHandler)
    port = httpd.server_address[1]
    httpd.RequestHandlerClass = make_handler(host, args.pack, port)
    print("Reward Lab live: http://127.0.0.1:%d/   (Ctrl+C to stop)" % port, flush=True)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        host.close()


if __name__ == "__main__":
    main()
