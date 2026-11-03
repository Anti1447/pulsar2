#!/usr/bin/env python3
"""Tests for tools/reward_lab/lab_stream.py and the stream routes of serve.py (P4-3).

Needs build-lab/reward_lab_stream_demo and build-lab/reward_lab_run (tools/reward_lab/build.sh): the messages under test are made by the REAL tap
(Rewards/RewardStreamTap.h), not hand-written, so the receiver is tested against what a render run would send.
Run from the repo root:  python3 tools/reward_lab/test_stream.py
"""
import copy
import http.client
import json
import os
import re
import socket
import subprocess
import sys
import tempfile
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import lab_stream  # noqa: E402

DEMO = os.path.join(ROOT, "build-lab", "reward_lab_stream_demo")
RUN = os.path.join(ROOT, "build-lab", "reward_lab_run")
SERVE = os.path.join(HERE, "serve.py")
PACK = os.path.join(HERE, "packs", "core")
TEMPLATE = os.path.join(HERE, "viewer", "viewer.template.html")


def capture_stream(scenario, episodes=1):
    """Run the real tap on a scenario and return the raw datagrams it sent (a list of bytes)."""
    rx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    rx.bind(("127.0.0.1", 0))
    rx.settimeout(0.3)
    port = rx.getsockname()[1]
    env = dict(os.environ, REWARD_LAB_STREAM="127.0.0.1:%d" % port)
    p = subprocess.Popen([DEMO, os.path.join(PACK, scenario + ".scenario.json"), "--loop", "--episodes", str(episodes), "--hz", "1000"], cwd=ROOT, env=env,
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    out = []
    while True:
        try:
            out.append(rx.recvfrom(65535)[0])
        except socket.timeout:
            if p.poll() is not None:
                break
    rx.close()
    assert p.returncode == 0, "demo failed"
    return out


def check_run(test, run):
    """The viewer's own rules (viewer_core.js validate + consistency), in Python."""
    test.assertEqual((run["schema"], run["kind"]), (1, "lab_run"))
    frames = run["frames"]
    test.assertTrue(len(frames) >= 1)
    n = len(frames[0]["players"])
    for k, f in enumerate(frames):
        test.assertEqual(f["i"], k)
        test.assertEqual(len(f["players"]), n)
        if k == 0:
            test.assertIsNone(f["trace"])
            continue
        tr = f["trace"]
        test.assertEqual(tr["schema"], 1)
        test.assertEqual(len(tr["cars"]), n)
        tot = [0.0] * n
        for r in tr["rewards"]:
            for key in ("raw", "wrapped", "weighted"):
                test.assertEqual(len(r[key]), n)
            for c in range(n):
                test.assertAlmostEqual(r["weighted"][c], r["wrapped"][c] * r["weight"], delta=1e-4 * (1 + abs(r["weighted"][c])))
                tot[c] += r["weighted"][c]
        for c in range(n):
            test.assertAlmostEqual(tr["total"][c], tot[c], delta=1e-4 * (1 + abs(tot[c])))


class RealStreamTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        for b in (DEMO, RUN):
            assert os.path.exists(b), "missing %s (tools/reward_lab/build.sh)" % b
        cls.datagrams = capture_stream("passing_setup_straight_hit")
        cls.offside = capture_stream("offside_2v2_surplus")
        cls.msgs = [json.loads(d) for d in cls.datagrams]

    def store(self, datagrams=None, ring=4096, record=None):
        s = lab_stream.StreamStore(ring, record)
        for d in (self.datagrams if datagrams is None else datagrams):
            s.ingest(d)
        return s

    # ---- what the real tap sends ----------------------------------------------------------------------------------------
    def test_01_the_real_tap_sends_a_valid_ordered_stream(self):
        self.assertEqual(self.msgs[0]["kind"], "lab_stream_hello")
        self.assertEqual(self.msgs[1]["frame"]["i"], 0)
        self.assertEqual([m["seq"] for m in self.msgs], list(range(len(self.msgs))))
        for m in self.msgs:
            self.assertIsNone(lab_stream.validate_message(m), m["kind"])
        self.assertEqual(len(self.msgs), 2 + 40)                         # hello, frame 0, then the scenario's 40 steps
        self.assertTrue(all(len(d) < 60000 for d in self.datagrams))

    def test_02_everything_is_accepted_and_the_run_is_what_the_viewer_needs(self):
        s = self.store()
        st = s.stats()
        self.assertEqual((st["received"], st["bad"], st["gaps"], st["restarts"]), (len(self.msgs), 0, [], 0))
        run = s.build_run()
        check_run(self, run)
        self.assertEqual(len(run["frames"]), 41)
        self.assertTrue(run["stream"]["starts_at_reset"])
        self.assertEqual([r["name"] for r in run["stream"]["rewards"]], ["PassingSetupReward"])
        self.assertEqual(run["scenario"]["tick_skip"], 8)
        self.assertEqual(run["frames"][5]["orig_i"], 5)
        self.assertEqual(s.episodes(), [{"episode": 1, "frames": 41, "has_hello": True, "first_i": 0, "last_i": 40}])
        # the ring's messages are never edited by building a run
        before = json.dumps([m for _, m in s.entries], sort_keys=True)
        s.build_run()
        self.assertEqual(json.dumps([m for _, m in s.entries], sort_keys=True), before)
        # a run with geometry (the offside scenario emits primitives) is also accepted whole
        s2 = self.store(self.offside)
        self.assertEqual(s2.stats()["bad"], 0)
        check_run(self, s2.build_run())
        self.assertTrue(any(pt["prims"] for f in s2.build_run()["frames"][1:] for r in f["trace"]["rewards"] for pt in r["players"]))

    # ---- bad input ---------------------------------------------------------------------------------------------------------
    def test_03_bad_datagrams_are_counted_with_a_reason_and_never_stored(self):
        s = lab_stream.StreamStore(64)
        good = copy.deepcopy(self.msgs[5])
        bad = []
        bad.append(b"not json at all")
        bad.append(b"\xff\xfe\x00")
        bad.append(b"[1,2,3]")
        bad.append(b'{"kind":"nope"}')
        m = copy.deepcopy(good); m["schema"] = 2; bad.append(json.dumps(m).encode())
        m = copy.deepcopy(good); m["seq"] = True; bad.append(json.dumps(m).encode())
        m = copy.deepcopy(good); m["seq"] = -1; bad.append(json.dumps(m).encode())
        m = copy.deepcopy(good); m["episode"] = "1"; bad.append(json.dumps(m).encode())
        m = copy.deepcopy(good); del m["frame"]["players"][0]["pos"]; bad.append(json.dumps(m).encode())
        m = copy.deepcopy(good); m["frame"]["players"] = []; bad.append(json.dumps(m).encode())
        m = copy.deepcopy(good); m["frame"]["ball"]["pos"] = [1, 2]; bad.append(json.dumps(m).encode())
        m = copy.deepcopy(good); m["frame"]["actions"] = [[1, 2, 3]]; bad.append(json.dumps(m).encode())
        m = copy.deepcopy(good); m["frame"]["actions"] = [[0] * 8]; bad.append(json.dumps(m).encode())             # one action for 4 players
        m = copy.deepcopy(good); m["frame"]["trace"]["rewards"][0]["raw"] = [1.0]; bad.append(json.dumps(m).encode())
        m = copy.deepcopy(good); m["frame"]["trace"]["total"] = [0.0]; bad.append(json.dumps(m).encode())
        m = copy.deepcopy(good); m["frame"]["trace"]["schema"] = 9; bad.append(json.dumps(m).encode())
        m = copy.deepcopy(self.msgs[1]); m["frame"]["trace"] = copy.deepcopy(good["frame"]["trace"]); bad.append(json.dumps(m).encode())   # trace on the reset frame
        m = copy.deepcopy(good); m["frame"]["players"][0]["boost"] = "full"; bad.append(json.dumps(m).encode())
        m = copy.deepcopy(good); m["frame"]["players"][0]["on_ground"] = 1; bad.append(json.dumps(m).encode())
        bad.append(json.dumps(good).replace('"boost": ', '"boost": ').encode().replace(b'"boost":', b'"boost":NaN,"x":'))           # NaN is not valid JSON for a browser
        m = copy.deepcopy(good); m["frame"]["trace"]["rewards"][0]["players"][0]["prims"] = [{"kind": "line"}]; bad.append(json.dumps(m).encode())
        m = copy.deepcopy(self.msgs[0]); m["tick_skip"] = 0; bad.append(json.dumps(m).encode())
        m = copy.deepcopy(self.msgs[0]); m["cars"] = [{"car": 1, "team": 2}]; bad.append(json.dumps(m).encode())
        m = copy.deepcopy(self.msgs[0]); m["rewards"][0]["zero_sum"] = "no"; bad.append(json.dumps(m).encode())
        for b in bad:
            self.assertFalse(s.ingest(b), b[:60])
        st = s.stats()
        self.assertEqual(st["bad"], len(bad))
        self.assertEqual((st["received"], st["kept"]), (0, 0))
        self.assertEqual(len(st["bad_samples"]), 5)
        self.assertTrue(s.ingest(json.dumps(good).encode()))             # and the unmodified message is fine
        for inf in (b'{"x": Infinity}', b'{"x": 1e999}'):
            self.assertFalse(s.ingest(inf))

    def test_04_a_lost_datagram_is_a_visible_gap_and_the_run_stops_at_it(self):
        lost = self.datagrams[:20] + self.datagrams[22:]               # seq 20 and 21 never arrive
        s = self.store(lost)
        st = s.stats()
        self.assertEqual(st["gaps"][0]["from_seq"], 20)
        self.assertEqual(st["gaps"][0]["to_seq"], 21)
        self.assertEqual(len(st["gaps"]), 1)
        run = s.build_run()
        check_run(self, run)
        self.assertEqual(len(run["frames"]), 20)                        # seq 22..41 only: the stretch after the gap
        self.assertEqual(run["stream"]["orig_first_i"], 21)             # message seq 22 = frame i 21 (hello took seq 0, frame 0 seq 1)
        self.assertFalse(run["stream"]["starts_at_reset"])
        self.assertIsNone(run["frames"][0]["trace"])                    # its own reward trace is not shown as a frame-0 value
        self.assertEqual(run["frames"][0]["actions"], [])
        self.assertIn("starts mid-episode", run["scenario"]["description"])
        self.assertEqual(s.episodes()[0]["frames"], 39)                 # the ring still holds everything it received (41 frames, 2 lost)

    def test_05_a_frame_the_sender_refused_is_a_hole_not_a_gap(self):
        msgs = copy.deepcopy(self.msgs)
        msgs[20] = {"kind": "lab_stream_error", "schema": 1, "seq": 20, "episode": 1, "reason": "frame too large for one datagram: refused, not truncated", "bytes": 70000}
        s = self.store([json.dumps(m).encode() for m in msgs])
        st = s.stats()
        self.assertEqual((st["gaps"], st["refused_by_sender"]), ([], 1))   # the error took the frame's seq slot: nothing is missing, one frame was refused
        run = s.build_run()
        check_run(self, run)
        self.assertEqual(len(run["frames"]), 21)                        # the stretch after the refused frame (seq 21..41)

    def test_06_a_restarted_sender_is_a_restart_and_a_new_episode_is_separate(self):
        two = capture_stream("passing_setup_straight_hit", episodes=2)
        s = self.store(two)
        self.assertEqual([e["episode"] for e in s.episodes()], [1, 2])
        run1, run2 = s.build_run(1), s.build_run(2)
        check_run(self, run1)
        check_run(self, run2)
        self.assertEqual(s.build_run()["stream"]["episode"], 2)           # default: the newest
        self.assertEqual(len(run1["frames"]), 41)
        self.assertEqual(len(run2["frames"]), 41)
        self.assertIsNone(s.build_run(99))
        # the same stream sent again from seq 0 (a trainer restarted): a restart, not a gap
        s2 = self.store(self.datagrams + self.datagrams)
        st = s2.stats()
        self.assertEqual((st["restarts"], st["gaps"]), (1, []))

    def test_07_the_ring_is_bounded_and_the_tail_is_still_a_valid_run(self):
        s = self.store(ring=16)
        st = s.stats()
        self.assertEqual((st["kept"], st["received"]), (16, len(self.msgs)))
        self.assertEqual(st["oldest_idx"], len(self.msgs) - 16)
        run = s.build_run()
        check_run(self, run)
        self.assertEqual(len(run["frames"]), 16)
        self.assertEqual(run["stream"]["orig_last_i"], 40)
        self.assertFalse(run["stream"]["starts_at_reset"])
        with self.assertRaises(ValueError):
            lab_stream.StreamStore(8)

    def test_08_since_pages_through_the_ring_in_order(self):
        s = self.store()
        first, nxt = s.since(-1, limit=10)
        self.assertEqual([e["idx"] for e in first], list(range(10)))
        self.assertEqual(nxt, 9)
        rest, nxt2 = s.since(nxt, limit=500)
        self.assertEqual(len(rest), len(self.msgs) - 10)
        self.assertEqual(nxt2, len(self.msgs) - 1)
        none, nxt3 = s.since(nxt2)
        self.assertEqual((none, nxt3), ([], nxt2))                      # nothing new: the cursor does not move

    def test_08b_the_gap_list_is_bounded_and_keeps_the_newest(self):
        s = lab_stream.StreamStore(4096)
        good = copy.deepcopy(self.msgs[5])
        for k in range(150):                                              # every other seq is "lost": 149 gaps
            m = copy.deepcopy(good)
            m["seq"] = 2 * k
            s.ingest(json.dumps(m).encode())
        gaps = s.stats()["gaps"]
        self.assertEqual(len(gaps), 100)
        self.assertEqual(gaps[-1]["from_seq"], 2 * 148 + 1)               # the newest is kept, the oldest are dropped

    # ---- record and replay -------------------------------------------------------------------------------------------------
    def test_09_a_recorded_stream_replays_to_the_same_run_and_refuses_to_overwrite(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "session.jsonl")
            live = self.store(record=path)
            live.close()
            replay = lab_stream.StreamStore(4096)
            ok, bad = replay.load_file(path)
            self.assertEqual((ok, bad), (len(self.msgs), 0))
            self.assertEqual(json.dumps(replay.build_run(), sort_keys=True), json.dumps(live.build_run(), sort_keys=True))
            with self.assertRaises(FileExistsError):
                lab_stream.StreamStore(64, path)
            # a rejected datagram is not recorded; a damaged line in a recording is counted, not fatal
            with open(path, "a", encoding="utf-8") as f:
                f.write("garbage line\n\n" + json.dumps({"kind": "lab_stream_hello"}) + "\n")
            r2 = lab_stream.StreamStore(4096)
            ok2, bad2 = r2.load_file(path)
            self.assertEqual((ok2, bad2), (len(self.msgs), 2))
            self.assertEqual(sum(1 for line in open(path) if line.strip()), len(self.msgs) + 2)
            path2 = os.path.join(d, "only_good.jsonl")
            g = lab_stream.StreamStore(64, path2)
            g.ingest(b"garbage")
            g.ingest(self.datagrams[0])
            g.close()
            self.assertEqual(len(open(path2).read().splitlines()), 1)

    # ---- the socket ----------------------------------------------------------------------------------------------------------
    def test_10_the_receiver_binds_loopback_only_and_survives_garbage(self):
        s = lab_stream.StreamStore(64)
        with self.assertRaises(ValueError):
            lab_stream.UdpReceiver(s, 0, host="0.0.0.0")
        with self.assertRaises(ValueError):
            lab_stream.UdpReceiver(s, 0, host="192.168.1.5")
        r = lab_stream.UdpReceiver(s, 0)
        r.start()
        tx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        for d in (b"junk", self.datagrams[0], b"", b"x" * 60000, self.datagrams[1]):
            tx.sendto(d, ("127.0.0.1", r.port))
        deadline = time.time() + 3
        while s.stats()["received"] + s.stats()["bad"] < 5 and time.time() < deadline:
            time.sleep(0.02)
        st = s.stats()
        self.assertEqual((st["received"], st["bad"]), (2, 3))
        r.stop()
        r.join(timeout=3)
        self.assertFalse(r.is_alive())

    # ---- the page --------------------------------------------------------------------------------------------------------------
    def test_11_the_page_builder_matches_the_cpp_exporter_byte_for_byte(self):
        with tempfile.TemporaryDirectory() as d:
            subprocess.run([RUN, os.path.join(PACK, "offside_goalside_car.scenario.json"), "--out", d, "--html", "--quiet"], cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
            html = open(os.path.join(d, "offside_goalside_car.lab.html"), encoding="utf-8").read()
        m = re.search(r'<script type="application/json" id="labdata">(.*?)</script>', html, re.S)
        self.assertTrue(m)
        self.assertEqual(lab_stream.build_page(TEMPLATE, m.group(1)), html)
        live = lab_stream.build_page(TEMPLATE, m.group(1), "<script>window.LAB_STREAM = true;</script>")
        self.assertIn("window.LAB_STREAM = true;", live)
        self.assertNotIn("<!--LAB_", live)
        self.assertNotIn("<!--INLINE", live)

    def test_12_run_json_in_the_page_cannot_close_the_script_element(self):
        run = {"name": "</script><script>alert(1)</script>", "x": "<!-- y -->"}
        text = lab_stream.json_for_script(run)
        self.assertNotIn("<", text)
        self.assertEqual(json.loads(text), run)
        page = lab_stream.build_page(TEMPLATE, text)
        self.assertEqual(page.count("</script>"), len(re.findall("</script>", lab_stream.build_page(TEMPLATE, "{}"))))

    def test_13_page_builder_errors_are_reported(self):
        with tempfile.TemporaryDirectory() as d:
            t = os.path.join(d, "t.html")
            open(t, "w").write("<body>no run marker <!--INLINE:a.txt--></body>")
            open(os.path.join(d, "a.txt"), "w").write("A")
            with self.assertRaises(ValueError):
                lab_stream.build_page(t, "{}")
            open(t, "w").write("<!--INLINE:missing.txt--><!--LAB_RUN_JSON-->")
            with self.assertRaises(OSError):
                lab_stream.build_page(t, "{}")
            open(t, "w").write("<!--INLINE:a.txt <!--LAB_RUN_JSON-->")
            with self.assertRaises((ValueError, OSError)):
                lab_stream.build_page(t, "{}")
            open(t, "w").write("<!--INLINE:a.txt--><!--LAB_RUN_JSON-->")
            self.assertEqual(lab_stream.build_page(t, "R"), "AR")
            open(os.path.join(d, "a.txt"), "w").write("<!--INLINE:a.txt-->")                  # an inlined file that names a marker is not expanded again
            self.assertEqual(lab_stream.build_page(t, "R"), "<!--INLINE:a.txt-->R")


class ServerTest(unittest.TestCase):
    """serve.py with the stream routes, as a subprocess (no live host needed)."""

    @classmethod
    def setUpClass(cls):
        cls.datagrams = capture_stream("passing_setup_straight_hit")
        cls.dir = tempfile.TemporaryDirectory()
        cls.rec = os.path.join(cls.dir.name, "rec.jsonl")
        with open(cls.rec, "w", encoding="utf-8") as f:
            for d in cls.datagrams:
                f.write(d.decode() + "\n")
            f.write("garbage\n")
        cls.proc = subprocess.Popen([sys.executable, SERVE, "--replay", cls.rec, "--no-live", "--port", "0"], stdout=subprocess.PIPE, text=True)
        cls.port = None
        for _ in range(10):
            line = cls.proc.stdout.readline()
            m = re.search(r"127\.0\.0\.1:(\d+)/", line)
            if m:
                cls.port = int(m.group(1))
                break
        assert cls.port, "server did not start"

    @classmethod
    def tearDownClass(cls):
        cls.proc.terminate()
        cls.proc.wait(timeout=10)
        cls.dir.cleanup()

    def get(self, path, host=None):
        c = http.client.HTTPConnection("127.0.0.1", self.port, timeout=30)
        c.request("GET", path, headers={"Host": host or "127.0.0.1:%d" % self.port})
        r = c.getresponse()
        body = r.read().decode("utf-8")
        c.close()
        return r.status, body

    def test_01_the_stream_page_embeds_the_replayed_run(self):
        status, html = self.get("/stream")
        self.assertEqual(status, 200)
        self.assertIn("window.LAB_STREAM = true;", html)
        self.assertNotIn("window.LAB_LIVE = true;", html)
        m = re.search(r'<script type="application/json" id="labdata">(.*?)</script>', html, re.S)
        run = json.loads(m.group(1))
        check_run(self, run)
        self.assertEqual(len(run["frames"]), 41)
        status, html2 = self.get("/stream?episode=99")
        self.assertEqual(status, 200)
        self.assertIn('{"waiting":true}', html2)

    def test_02_the_api_pages_through_messages_and_reports_stats(self):
        status, body = self.get("/api/stream?after=-1&limit=10")
        j = json.loads(body)
        self.assertEqual((status, len(j["messages"]), j["next"]), (200, 10, 9))
        self.assertEqual(j["stats"]["bad"], 1)                         # the garbage line in the recording
        self.assertEqual(j["stats"]["received"], 42)
        j2 = json.loads(self.get("/api/stream?after=9&limit=500")[1])
        self.assertEqual(len(j2["messages"]), 32)
        j3 = json.loads(self.get("/api/stream?after=%d" % j2["next"])[1])
        self.assertEqual(j3["messages"], [])
        j4 = json.loads(self.get("/api/stream?after=abc&limit=zzz")[1])      # junk arguments fall back to defaults, never a 500
        self.assertTrue(j4["ok"])
        self.assertLessEqual(len(j4["messages"]), 500)
        info = json.loads(self.get("/api/stream/info")[1])
        self.assertEqual(info["episodes"][0]["frames"], 41)
        r = json.loads(self.get("/api/stream/run")[1])
        self.assertTrue(r["ok"])
        check_run(self, r["run"])
        self.assertFalse(json.loads(self.get("/api/stream/run?episode=99")[1])["ok"])

    def test_03_the_stream_is_read_only_and_host_checked(self):
        self.assertEqual(self.get("/api/stream", host="evil.example:80")[0], 403)
        self.assertEqual(self.get("/stream", host="evil.example:80")[0], 403)
        c = http.client.HTTPConnection("127.0.0.1", self.port, timeout=10)
        c.request("POST", "/api/stream", body="{}", headers={"Host": "127.0.0.1:%d" % self.port, "X-Lab-Client": "1"})
        self.assertEqual(c.getresponse().status, 404)                   # no POST route exists on the stream
        c.close()
        c = http.client.HTTPConnection("127.0.0.1", self.port, timeout=10)
        c.request("POST", "/api", body=json.dumps({"cmd": "state"}), headers={"Host": "127.0.0.1:%d" % self.port, "X-Lab-Client": "1"})
        r = c.getresponse()
        self.assertEqual(r.status, 503)                                 # --no-live: the host is off
        self.assertIn("live Lab is off", r.read().decode())
        c.close()
        status, body = self.get("/")
        self.assertEqual(status, 503)
        self.assertIn("/stream", body)

    def test_04_stream_routes_say_so_when_stream_mode_is_off(self):
        p = subprocess.Popen([sys.executable, SERVE, "--no-live", "--port", "0"], stdout=subprocess.PIPE, text=True)
        try:
            m = re.search(r"http://127\.0\.0\.1:(\d+)/", p.stdout.readline())
            self.assertTrue(m)
            port = int(m.group(1))
            for path in ("/stream", "/api/stream", "/api/stream/run", "/api/stream/info"):
                c = http.client.HTTPConnection("127.0.0.1", port, timeout=10)
                c.request("GET", path, headers={"Host": "127.0.0.1:%d" % port})
                r = c.getresponse()
                body = r.read().decode()
                c.close()
                self.assertEqual(r.status, 404, path)
                self.assertIn("stream mode is off", body)
        finally:
            p.terminate()
            p.wait(timeout=10)

    def test_05_bad_command_lines_are_refused_with_a_message(self):
        def run(*args):
            r = subprocess.run([sys.executable, SERVE, "--no-live", "--port", "0", *args], capture_output=True, text=True, timeout=30)
            return r.returncode, r.stdout + r.stderr
        rc, out = run("--stream", "--replay", self.rec)
        self.assertNotEqual(rc, 0)
        self.assertIn("pick one", out)
        rc, out = run("--record", os.path.join(self.dir.name, "x.jsonl"))
        self.assertNotEqual(rc, 0)
        self.assertIn("--record needs --stream", out)
        rc, out = run("--replay", os.path.join(self.dir.name, "does_not_exist.jsonl"))
        self.assertNotEqual(rc, 0)
        self.assertIn("cannot read", out)
        rc, out = run("--stream", "--ring", "4")
        self.assertNotEqual(rc, 0)
        self.assertIn("ring must be at least", out)


class LiveStreamTest(unittest.TestCase):
    """serve.py --stream receiving from the real tap over a real socket, recording as it goes."""

    def test_end_to_end_tap_to_page_data(self):
        with tempfile.TemporaryDirectory() as d:
            rec = os.path.join(d, "live.jsonl")
            probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            probe.bind(("127.0.0.1", 0))
            sport = probe.getsockname()[1]
            probe.close()
            p = subprocess.Popen([sys.executable, SERVE, "--stream", "--stream-port", str(sport), "--record", rec, "--no-live", "--port", "0"], stdout=subprocess.PIPE, text=True)
            try:
                port = None
                for _ in range(10):
                    m = re.search(r"http://127\.0\.0\.1:(\d+)/", p.stdout.readline())
                    if m:
                        port = int(m.group(1))
                        break
                self.assertTrue(port)
                env = dict(os.environ, REWARD_LAB_STREAM="127.0.0.1:%d" % sport)
                r = subprocess.run([DEMO, os.path.join(PACK, "passing_setup_straight_hit.scenario.json"), "--hz", "1000"], cwd=ROOT, env=env, capture_output=True, timeout=120)
                self.assertEqual(r.returncode, 0, r.stderr)

                def get(path):
                    c = http.client.HTTPConnection("127.0.0.1", port, timeout=10)
                    c.request("GET", path, headers={"Host": "127.0.0.1:%d" % port})
                    x = c.getresponse()
                    b = x.read().decode()
                    c.close()
                    return json.loads(b)
                deadline = time.time() + 10
                info = get("/api/stream/info")
                while info["stats"]["received"] < 42 and time.time() < deadline:
                    time.sleep(0.05)
                    info = get("/api/stream/info")
                self.assertEqual((info["stats"]["received"], info["stats"]["bad"], info["stats"]["gaps"]), (42, 0, []))
                run = get("/api/stream/run")["run"]
                check_run(self, run)
                self.assertEqual(len(run["frames"]), 41)
                # what was recorded replays to the same run
                replay = lab_stream.StreamStore(4096)
                self.assertEqual(replay.load_file(rec), (42, 0))
                self.assertEqual(json.dumps(replay.build_run(), sort_keys=True), json.dumps(run, sort_keys=True))
            finally:
                p.terminate()
                p.wait(timeout=10)


if __name__ == "__main__":
    unittest.main(verbosity=2)
