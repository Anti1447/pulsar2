#!/usr/bin/env python3
"""Reward Lab live server (docs/REWARD_VIEWER_SPEC.md, P3-2).

HTTP in front of build-lab/reward_lab_host (a JSON-lines process). Serves the viewer page in live mode and forwards the page's
commands to the host. Standard library only.

    python3 tools/reward_lab/serve.py [--pack DIR] [--port 8765] [--host-bin build-lab/reward_lab_host]
    python3 tools/reward_lab/serve.py --stream [--stream-port 9280] [--ring 4096] [--record FILE]      # also receive a render run's reward trace (/stream)
    python3 tools/reward_lab/serve.py --replay FILE [--no-live]                                        # serve a recorded stream (/stream), no trainer needed

Run it from anywhere; the host is started with the repo root as its working directory (it needs build/collision_meshes).

Safety: binds 127.0.0.1 only; POST /api needs the header "X-Lab-Client: 1" (a cross-site page cannot send it without a CORS
preflight, which this server never grants) and a Host header naming this server; the browser may only send the commands in
BROWSER_COMMANDS, never a file path (scenarios are picked by NAME from the pack folder, macros and scenarios are sent as data).

The stream side (P4-3, lab_stream.py) is read-only: it binds a UDP port on 127.x.x.x, validates every datagram, never replies, and the page can only GET
/stream and /api/stream*. Nothing the page does reaches the trainer.
"""
import argparse
import http.server
import json
import os
import subprocess
import sys
import threading
import urllib.parse

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lab_stream

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TEMPLATE = "tools/reward_lab/viewer/viewer.template.html"
BROWSER_COMMANDS = {"load_named", "load", "state", "step", "step_scripted", "seek", "reset", "macro_record", "macro_play", "capture"}
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
            started = False        # at most one fresh process per call; a host that dies right after starting is reported, not looped on
            for _ in (0, 1):
                if self.proc is None or self.proc.poll() is not None:
                    if started:
                        break
                    self._start()
                    started = True
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


def make_handler(host, pack, port, stream=None):
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

        def _stream_get(self, path, query):
            """The read-only stream routes. Returns True if the request was handled."""
            if path not in ("/stream", "/stream/", "/api/stream", "/api/stream/run", "/api/stream/info"):
                return False
            if stream is None:
                self._send(404, "stream mode is off (start with --stream, or --replay FILE)", "text/plain")
                return True
            q = urllib.parse.parse_qs(query)

            def int_arg(name, default):
                try:
                    return int(q[name][0]) if name in q else default
                except (ValueError, IndexError):
                    return default
            if path in ("/stream", "/stream/"):
                ep = int_arg("episode", None)
                run = stream.build_run(ep)
                run_text = lab_stream.json_for_script(run) if run else '{"waiting":true}'
                try:
                    page = lab_stream.build_page(os.path.join(ROOT, TEMPLATE), run_text, "<script>window.LAB_STREAM = true;</script>")
                except (OSError, ValueError) as e:
                    self._send(500, "cannot build the stream page: %s" % e, "text/plain")
                    return True
                self._send(200, page, "text/html")
            elif path == "/api/stream":
                msgs, nxt = stream.since(int_arg("after", -1), max(1, min(int_arg("limit", 500), 2000)))
                self._json(200, {"ok": True, "next": nxt, "messages": msgs, "stats": stream.stats()})
            elif path == "/api/stream/run":
                run = stream.build_run(int_arg("episode", None))
                self._json(200, {"ok": True, "run": run} if run else {"ok": False, "error": "no frames yet"})
            else:
                self._json(200, {"ok": True, "stats": stream.stats(), "episodes": stream.episodes()})
            return True

        def do_GET(self):
            if not self._host_ok():
                return self._json(403, {"ok": False, "error": "bad Host header"})
            parsed = urllib.parse.urlsplit(self.path)
            if self._stream_get(parsed.path, parsed.query):
                return
            if host is None and (self.path == "/api/scenarios" or self.path.split("?")[0] in ("/", "/index.html")):
                return self._send(503, "the live Lab is off (--no-live); the stream page is at /stream", "text/plain")
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
            if host is None:
                return self._json(503, {"ok": False, "error": "the live Lab is off (--no-live)"})
            return self._json(200, host.call(cmd))

    return Handler


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--pack", default=os.path.join(ROOT, "tools/reward_lab/packs/core"))
    ap.add_argument("--port", type=int, default=8765, help="0 picks a free port and prints it")
    ap.add_argument("--host-bin", default=os.path.join(ROOT, "build-lab/reward_lab_host"))
    ap.add_argument("--stream", action="store_true", help="receive a render run's reward trace on a loopback UDP port and serve it at /stream")
    ap.add_argument("--stream-port", type=int, default=9280)
    ap.add_argument("--ring", type=int, default=4096, help="messages kept for the page (at 15 Hz about 4.5 minutes)")
    ap.add_argument("--record", metavar="FILE", help="with --stream: also write every accepted datagram to this new JSONL file")
    ap.add_argument("--replay", metavar="FILE", help="serve a recorded stream file at /stream instead of listening")
    ap.add_argument("--no-live", action="store_true", help="do not start the live Lab host (stream or replay only)")
    args = ap.parse_args()
    if args.stream and args.replay:
        sys.exit("--stream and --replay are different sources: pick one")
    if args.record and not args.stream:
        sys.exit("--record needs --stream")
    live = not args.no_live
    if live and not os.path.exists(args.host_bin):
        sys.exit("missing %s (run tools/reward_lab/build.sh, or use --no-live)" % args.host_bin)
    host = Host(args.host_bin) if live else None
    stream, receiver = None, None
    if args.stream or args.replay:
        try:
            stream = lab_stream.StreamStore(args.ring, args.record if args.stream else None)
        except (ValueError, FileExistsError, OSError) as e:
            sys.exit("stream: %s" % e)
        if args.replay:
            try:
                ok, bad = stream.load_file(args.replay)
            except OSError as e:
                sys.exit("cannot read %s: %s" % (args.replay, e))
            stream.source = {"mode": "replay", "file": os.path.basename(args.replay)}
            print("replay: %d messages accepted, %d rejected from %s" % (ok, bad, args.replay), flush=True)
        else:
            try:
                receiver = lab_stream.UdpReceiver(stream, args.stream_port)
            except (OSError, ValueError) as e:
                sys.exit("cannot listen on UDP 127.0.0.1:%d: %s" % (args.stream_port, e))
            receiver.start()
            stream.source = {"mode": "udp", "port": receiver.port}
            print("stream: listening on UDP 127.0.0.1:%d (ring %d)%s" % (receiver.port, args.ring, ", recording to " + args.record if args.record else ""), flush=True)
    # bind first so --port 0 is known before the handler is built
    httpd = http.server.ThreadingHTTPServer(("127.0.0.1", args.port), http.server.BaseHTTPRequestHandler)
    port = httpd.server_address[1]
    httpd.RequestHandlerClass = make_handler(host, args.pack, port, stream)
    print("Reward Lab live: http://127.0.0.1:%d/%s   (Ctrl+C to stop)" % (port, "" if live else "stream"), flush=True)
    if stream is not None and live:
        print("Reward Lab stream: http://127.0.0.1:%d/stream" % port, flush=True)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        if host:
            host.close()
        if receiver:
            receiver.stop()
        if stream:
            stream.close()


if __name__ == "__main__":
    main()
