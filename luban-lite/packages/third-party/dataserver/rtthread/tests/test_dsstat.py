#!/usr/bin/env python3
"""
dsstat endpoint (rtthread/dsstat.c). The Strata dashboard side is covered by
test_strata_d133.py.

The host build serves /proc/net/dev counters in place of the board's netmon
hooks, so pulling a file over 127.0.0.1 must show up on "lo" exactly as a pull
over WiFi shows up on "w1" on the board.
"""
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


if __name__ == "__main__":
    unittest.main(verbosity=2)
