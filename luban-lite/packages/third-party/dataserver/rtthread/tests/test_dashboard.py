#!/usr/bin/env python3
"""
Dashboard backend test: the adapted client_web.py pulls a directory from the
host build of the board server and exposes progress through /api/status.
Skipped when Flask is not installed.
"""
import importlib
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

try:
    import flask  # noqa: F401
    HAVE_FLASK = True
except ImportError:
    HAVE_FLASK = False


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


@unittest.skipUnless(HAVE_FLASK, "flask not installed")
class DashboardTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not os.path.exists(SERVER):
            subprocess.check_call(["make", "-C", os.path.join(RTT, "host"), "dataserver_c"])
        cls.work = tempfile.mkdtemp(prefix="dash_")
        cls.root = os.path.join(cls.work, "root")
        cls.files = {"log/a.bin": 9000, "log/sub/<b>&x.bin": 5, "log/empty.bin": 0}
        for rel, n in cls.files.items():
            p = os.path.join(cls.root, *rel.split("/"))
            os.makedirs(os.path.dirname(p), exist_ok=True)
            with open(p, "wb") as f:
                f.write(os.urandom(n))
        cls.port = free_port()
        cls.proc = subprocess.Popen([SERVER, "-p", str(cls.port), "-s", "2", "-q", "4",
                                     "-b", "1024", "-r", cls.root],
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        assert cls.proc.stdout.readline().startswith("READY")
        os.environ["DASHBOARD_OUT"] = os.path.join(cls.work, "out")
        sys.path.insert(0, os.path.join(PKG, "dashboard"))
        cls.web = importlib.import_module("client_web")
        cls.client = cls.web.app.test_client()

    @classmethod
    def tearDownClass(cls):
        cls.proc.terminate()
        cls.proc.wait(timeout=10)
        cls.proc.stdout.close()
        cls.proc.stderr.close()
        shutil.rmtree(cls.work, ignore_errors=True)

    def wait_done(self, timeout=20):
        deadline = time.time() + timeout
        while time.time() < deadline:
            d = self.client.get("/api/status").get_json()
            if d["status"] in ("SUCCESS", "FAILED", "ERROR"):
                return d
            time.sleep(0.05)
        self.fail("transfer did not finish")

    def pull(self, directory):
        self.client.post("/api/reset")
        r = self.client.post("/api/pull", json={"board": "127.0.0.1", "port": self.port,
                                                "directory": directory})
        self.assertEqual(r.status_code, 200, r.get_data(as_text=True))
        return self.wait_done()

    def test_index_served_offline(self):
        html = self.client.get("/").get_data(as_text=True)
        self.assertIn("D133 dataServer", html)
        self.assertNotIn("fonts.googleapis", html)

    def test_pull_reports_progress_and_hashes(self):
        import hashlib
        d = self.pull("log")
        self.assertEqual(d["status"], "SUCCESS")
        self.assertEqual(d["block_size"], 1024)
        self.assertEqual(d["bytes_total"], sum(self.files.values()))
        self.assertEqual(sorted(f["path"] for f in d["files"]), sorted(self.files))
        for f in d["files"]:
            self.assertTrue(f["done"])
            src = os.path.join(self.root, *f["path"].split("/"))
            with open(src, "rb") as fh:
                self.assertEqual(f["sha256"], hashlib.sha256(fh.read()).hexdigest(), f["path"])
            dst = os.path.join(os.environ["DASHBOARD_OUT"], *f["path"].split("/"))
            self.assertTrue(os.path.exists(dst))
        self.assertIsNotNone(d["transfer_time"])
        self.assertTrue(d["speed_history"])

    def test_missing_directory_fails_cleanly(self):
        d = self.pull("nope")
        self.assertEqual(d["status"], "FAILED")
        self.assertEqual(d["files"], [])

    def test_unreachable_board_is_error(self):
        self.client.post("/api/reset")
        self.client.post("/api/pull", json={"board": "127.0.0.1", "port": free_port(),
                                            "directory": "log"})
        d = self.wait_done()
        self.assertEqual(d["status"], "ERROR")
        self.assertTrue(any("ERROR" in line for line in d["logs"]))

    def test_directory_required(self):
        r = self.client.post("/api/pull", json={"board": "127.0.0.1", "port": self.port})
        self.assertEqual(r.status_code, 400)


if __name__ == "__main__":
    unittest.main(verbosity=2)
