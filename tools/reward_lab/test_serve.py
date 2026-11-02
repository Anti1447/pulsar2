#!/usr/bin/env python3
"""Tests for tools/reward_lab/serve.py. Needs build-lab/reward_lab_host (tools/reward_lab/build.sh). Run: python3 tools/reward_lab/test_serve.py"""
import http.client
import json
import os
import subprocess
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SERVE = os.environ.get("LAB_SERVE_PATH", os.path.join(HERE, "serve.py"))


class ServeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.proc = subprocess.Popen([sys.executable, SERVE, "--port", "0"], stdout=subprocess.PIPE, text=True)
        line = cls.proc.stdout.readline()
        cls.port = int(line.split("127.0.0.1:")[1].split("/")[0])

    @classmethod
    def tearDownClass(cls):
        cls.proc.terminate()
        cls.proc.wait(timeout=10)

    def req(self, method, path, body=None, headers=None, host=None, raw=None):
        c = http.client.HTTPConnection("127.0.0.1", self.port, timeout=120)
        h = {"Host": host or "127.0.0.1:%d" % self.port}
        h.update(headers or {})
        data = raw if raw is not None else (json.dumps(body) if body is not None else None)
        c.request(method, path, body=data, headers=h)
        r = c.getresponse()
        text = r.read().decode("utf-8")
        c.close()
        return r.status, text

    def api(self, cmd, **kw):
        status, text = self.req("POST", "/api", body=cmd, headers={"X-Lab-Client": "1", "Content-Type": "application/json"}, **kw)
        return status, json.loads(text)

    def acts(self, n_cars, throttle=1.0, boost=1.0):
        return [[throttle, 0, 0, 0, 0, 0, boost, 0]] + [[0] * 8 for _ in range(n_cars - 1)]

    def test_01_page_is_live_and_lists_scenarios(self):
        status, html = self.req("GET", "/")
        self.assertEqual(status, 200)
        self.assertIn("window.LAB_LIVE = true;", html)
        self.assertNotIn("<!--LAB_", html)
        self.assertNotIn("<!--INLINE", html)
        status, text = self.req("GET", "/api/scenarios")
        names = json.loads(text)["scenarios"]
        self.assertEqual(status, 200)
        self.assertIn("passing_setup_straight_hit", names)
        self.assertEqual(len(names), 10)

    def test_02_load_step_seek_roundtrip(self):
        status, r = self.api({"cmd": "load_named", "name": "passing_setup_straight_hit", "steps": 60})
        self.assertEqual(status, 200)
        self.assertTrue(r["ok"])
        self.assertEqual(r["run"]["scenario"]["name"], "passing_setup_straight_hit")
        self.assertEqual(len(r["run"]["frames"]), 1)
        n = len(r["run"]["scenario"]["cars"])
        _, d = self.api({"cmd": "step", "actions": self.acts(n), "repeat": 5})
        self.assertTrue(d["ok"])
        self.assertEqual((d["keep"], len(d["frames"])), (1, 5))
        _, d2 = self.api({"cmd": "seek", "frame": 2})
        self.assertEqual((d2["keep"], len(d2["frames"])), (3, 0))
        _, st = self.api({"cmd": "state"})
        self.assertEqual(len(st["run"]["frames"]), 3)
        # the page served now carries the CURRENT run, so a reload does not lose the session
        _, html = self.req("GET", "/")
        self.assertIn("passing_setup_straight_hit", html)
        _, rec = self.api({"cmd": "macro_record"})
        self.assertTrue(rec["ok"])
        self.assertEqual(rec["macro"]["ticks"], 2 * r["run"]["scenario"]["tick_skip"])

    def test_03_only_a_page_on_this_server_may_post(self):
        status, r = self.req("POST", "/api", body={"cmd": "state"})                       # no X-Lab-Client header
        self.assertEqual(status, 403)
        status, r = self.req("POST", "/api", body={"cmd": "state"}, headers={"X-Lab-Client": "1"}, host="evil.example:80")
        self.assertEqual(status, 403)
        status, r = self.req("GET", "/", host="evil.example:80")
        self.assertEqual(status, 403)
        status, r = self.req("GET", "/api/scenarios", host="localhost.evil.example:%d" % self.port)
        self.assertEqual(status, 403)
        # "localhost" with the right port is this machine and is fine
        status, _ = self.req("GET", "/api/scenarios", host="localhost:%d" % self.port)
        self.assertEqual(status, 200)

    def test_04_the_browser_cannot_name_a_file_or_use_server_only_commands(self):
        for cmd in ({"cmd": "page", "template": "/etc/passwd"}, {"cmd": "nonsense"}, {"nocmd": 1}, [1, 2]):
            status, r = self.api(cmd)
            self.assertEqual(status, 400, cmd)
            self.assertFalse(r["ok"])
        for name in ("../../../../etc/passwd", "passing_setup_straight_hit.scenario.json", "", None, 5, "no_such_scenario", "core/passing_setup_straight_hit"):
            status, r = self.api({"cmd": "load_named", "name": name})
            self.assertEqual(status, 400, name)
        status, r = self.api({"cmd": "load_named", "name": "passing_setup_straight_hit", "steps": 0})   # the host's own validation still applies
        self.assertEqual(status, 200)
        self.assertFalse(r["ok"])

    def test_05_malformed_requests(self):
        h = {"X-Lab-Client": "1"}
        status, text = self.req("POST", "/api", raw="{not json", headers=h)
        self.assertEqual(status, 400)
        self.assertIn("not JSON", text)          # refused as bad JSON, not merely as a disallowed command
        status, _ = self.req("POST", "/api", raw="", headers=h)
        self.assertEqual(status, 400)
        # a body that CLAIMS to be over the limit is refused before it is read (the server never buffers it)
        status, _ = self.req("POST", "/api", raw="x", headers=dict(h, **{"Content-Length": str(4 * 1024 * 1024 + 1)}))
        self.assertEqual(status, 413)
        status, _ = self.req("POST", "/other", body={}, headers=h)
        self.assertEqual(status, 404)
        status, _ = self.req("GET", "/nope")
        self.assertEqual(status, 404)

    def test_05b_listens_on_loopback_only(self):
        try:
            out = subprocess.run(["ss", "-ltnH"], capture_output=True, text=True, timeout=10).stdout
        except (OSError, subprocess.SubprocessError):
            self.skipTest("ss not available")
        locals_ = [l.split()[3] for l in out.splitlines() if l.split() and l.split()[3].endswith(":%d" % self.port)]
        self.assertTrue(locals_, "server not found in the listening sockets")
        for addr in locals_:
            self.assertEqual(addr, "127.0.0.1:%d" % self.port)

    def test_06_host_crash_is_recovered_and_reported(self):
        self.api({"cmd": "load_named", "name": "passing_setup_straight_hit"})
        subprocess.run(["pkill", "-P", str(self.proc.pid), "-f", "reward_lab_host"])
        status, r = self.api({"cmd": "state"})
        self.assertEqual(status, 200)
        self.assertFalse(r["ok"])
        self.assertIn("nothing loaded", r["error"])        # a fresh host, not a hang or a 500
        status, r = self.api({"cmd": "load_named", "name": "passing_setup_straight_hit"})
        self.assertTrue(r["ok"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
