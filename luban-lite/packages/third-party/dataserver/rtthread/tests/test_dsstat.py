#!/usr/bin/env python3
"""
dsstat endpoint (rtthread/dsstat.c) and the dashboard's board network panel.

The host build serves /proc/net/dev counters in place of the board's netmon
hooks, so pulling a file over 127.0.0.1 must show up on "lo" exactly as a pull
over WiFi shows up on "w1" on the board.
"""
import importlib
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
RTT = os.path.dirname(HERE)
PKG = os.path.dirname(RTT)
SERVER = os.path.join(RTT, "host", "dataserver_c")
sys.path.insert(0, RTT)
import remote_client  # noqa: E402

try:
    import flask  # noqa: F401
    HAVE_FLASK = True
except ImportError:
    HAVE_FLASK = False


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def read_stats(port):
    with socket.create_connection(("127.0.0.1", port), timeout=3) as s:
        data = b""
        while True:
            chunk = s.recv(4096)
            if not chunk:
                break
            data += chunk
    assert data.endswith(b"\n"), data[-40:]
    assert data.count(b"\n") == 1
    return json.loads(data)


class HostServer:
    def __init__(self, root):
        subprocess.check_call(["make", "-s", "-C", os.path.join(RTT, "host"), "dataserver_c"])
        self.port, self.sport = free_port(), free_port()
        self.proc = subprocess.Popen([SERVER, "-p", str(self.port), "-s", "2", "-q", "4",
                                      "-b", "4096", "-r", root, "-S", str(self.sport)],
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        assert self.proc.stdout.readline().startswith("READY")
        for _ in range(50):                      # the stats thread starts after READY
            try:
                read_stats(self.sport)
                return
            except OSError:
                time.sleep(0.05)
        raise RuntimeError("stats port did not open")

    def stop(self):
        self.proc.terminate()
        self.proc.wait(5)
        self.proc.stdout.close()
        self.proc.stderr.close()


class DsstatEndpointTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.mkdtemp(prefix="dsstat_")
        cls.root = os.path.join(cls.work, "root")
        os.makedirs(os.path.join(cls.root, "clip"))
        cls.size = 300_000
        with open(os.path.join(cls.root, "clip", "event.bin"), "wb") as f:
            f.write(os.urandom(cls.size))
        cls.srv = HostServer(cls.root)

    @classmethod
    def tearDownClass(cls):
        cls.srv.stop()
        shutil.rmtree(cls.work, ignore_errors=True)

    def test_schema(self):
        j = read_stats(self.srv.sport)
        self.assertEqual(j["v"], 1)
        self.assertIsInstance(j["uptime_ms"], int)
        self.assertEqual(j["ip_forwarded"], -1)          # host: no provider
        names = [i["name"] for i in j["ifs"]]
        self.assertIn("lo", names)
        for i in j["ifs"]:
            for k in ("rx_bytes", "tx_bytes", "rx_pkts", "tx_pkts"):
                self.assertTrue(0 <= i[k] < 2 ** 32, (i["name"], k, i[k]))
            self.assertRegex(i["ip"], r"^\d+\.\d+\.\d+\.\d+$")
        self.assertEqual(j["dataserver"]["running"], 1)

    def test_uptime_monotonic(self):
        a = read_stats(self.srv.sport)["uptime_ms"]
        time.sleep(0.12)
        b = read_stats(self.srv.sport)["uptime_ms"]
        self.assertGreaterEqual(b - a, 100)

    def test_counters_follow_a_pull(self):
        def lo(j):
            return next(i for i in j["ifs"] if i["name"] == "lo")
        before = read_stats(self.srv.sport)
        out = os.path.join(self.work, "out")
        files = remote_client.fetch("127.0.0.1", self.srv.port, "clip", out, verbose=False)
        self.assertEqual(len(files), 1)
        after = read_stats(self.srv.sport)
        ds0, ds1 = before["dataserver"], after["dataserver"]
        self.assertEqual(ds1["files_sent"] - ds0["files_sent"], 1)
        self.assertEqual(ds1["requests_done"] - ds0["requests_done"], 1)
        self.assertGreaterEqual(ds1["bytes_sent"] - ds0["bytes_sent"], self.size)
        grew = (lo(after)["rx_bytes"] - lo(before)["rx_bytes"]) % 2 ** 32
        self.assertGreaterEqual(grew, self.size)         # payload + headers + acks

    def test_many_quick_connections(self):
        for _ in range(40):
            read_stats(self.srv.sport)


class BoardUpdateTest(unittest.TestCase):
    """client_web.board_update: rates from the board's uptime, uint32 wrap, reboot."""

    @classmethod
    def setUpClass(cls):
        if not HAVE_FLASK:
            raise unittest.SkipTest("flask not installed")
        sys.path.insert(0, os.path.join(PKG, "dashboard"))
        cls.web = importlib.import_module("client_web")

    def sample(self, t, rx, tx, fw=-1):
        return {"v": 1, "uptime_ms": t, "ip_forwarded": fw, "dataserver": None,
                "ifs": [{"name": "w1", "ip": "192.168.169.1", "up": 1,
                         "rx_bytes": rx, "tx_bytes": tx, "rx_pkts": 0, "tx_pkts": 0}]}

    def state(self):
        return {"history": {}}

    def test_rate_and_wrap(self):
        st = self.state()
        prev = self.web.board_update(st, None, self.sample(1000, 2 ** 32 - 1000, 10, fw=2 ** 32 - 5))
        self.assertIsNone(st["ifs"][0]["rx_rate"])
        self.web.board_update(st, prev, self.sample(3000, 3000, 2010, fw=15))
        self.assertEqual(st["ifs"][0]["rx_rate"], 2000)  # 4000 bytes over 2 s, across the wrap
        self.assertEqual(st["ifs"][0]["tx_rate"], 1000)
        self.assertEqual(st["fw_rate"], 10.0)            # 20 packets over 2 s, across the wrap
        self.assertEqual(st["history"]["w1"], [[3.0, 2000, 1000]])

    def test_reboot_resets(self):
        st = self.state()
        prev = self.web.board_update(st, None, self.sample(5000, 100, 100))
        prev = self.web.board_update(st, prev, self.sample(6000, 200, 200))
        self.assertEqual(len(st["history"]["w1"]), 1)
        self.web.board_update(st, prev, self.sample(500, 5, 5))      # uptime went back
        self.assertEqual(st["history"], {})
        self.assertIsNone(st["ifs"][0]["rx_rate"])

    def test_no_forward_counter(self):
        st = self.state()
        prev = self.web.board_update(st, None, self.sample(1000, 0, 0))
        self.web.board_update(st, prev, self.sample(2000, 0, 0))
        self.assertIsNone(st["ip_forwarded"])
        self.assertIsNone(st["fw_rate"])


@unittest.skipUnless(HAVE_FLASK, "flask not installed")
class BoardApiTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.mkdtemp(prefix="dsapi_")
        os.makedirs(os.path.join(cls.work, "root"))
        cls.srv = HostServer(os.path.join(cls.work, "root"))
        sys.path.insert(0, os.path.join(PKG, "dashboard"))
        cls.web = importlib.import_module("client_web")
        cls.web.BOARD_POLL_S = 0.2
        cls.client = cls.web.app.test_client()

    @classmethod
    def tearDownClass(cls):
        cls.srv.stop()
        shutil.rmtree(cls.work, ignore_errors=True)

    def wait(self, port, pred, timeout=5.0):
        end = time.time() + timeout
        while time.time() < end:
            b = self.client.get(f"/api/board?board=127.0.0.1&port={port}").get_json()
            if pred(b):
                return b
            time.sleep(0.1)
        self.fail(f"timeout, last state {b}")

    def test_live_rates(self):
        b = self.wait(self.srv.sport, lambda b: b["ok"] and b["history"].get("lo"))
        self.assertEqual(b["target"], f"127.0.0.1:{self.srv.sport}")
        lo = next(i for i in b["ifs"] if i["name"] == "lo")
        self.assertIsInstance(lo["rx_rate"], int)
        self.assertEqual(b["dataserver"]["running"], 1)

    def test_unreachable_board(self):
        dead = free_port()
        b = self.wait(dead, lambda b: b["error"])
        self.assertFalse(b["ok"])
        self.assertIn(str(dead), b["error"])
        self.assertEqual(b["ifs"], [])

    def test_bad_port(self):
        r = self.client.get("/api/board?board=127.0.0.1&port=abc")
        self.assertEqual(r.status_code, 400)


if __name__ == "__main__":
    unittest.main(verbosity=2)
