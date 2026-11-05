#!/usr/bin/env python3
"""
Strata dashboard (dashboard/strata-host) D133 back end, server/d133.mjs:
board stats from the host build's dsstat port and dataServer pulls through
/api/d133/*. Needs Node and a build (npm ci && npm run build); skipped otherwise.
"""
import hashlib
import json
import os
import shutil
import socket
import subprocess
import tempfile
import time
import unittest
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
RTT = os.path.dirname(HERE)
PKG = os.path.dirname(RTT)
STRATA = os.path.join(PKG, "dashboard", "strata-host")
SERVER = os.path.join(RTT, "host", "dataserver_c")
HAVE_NODE = shutil.which("node") is not None
HAVE_DIST = os.path.exists(os.path.join(STRATA, "dist", "index.html"))


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def node_eval(code):
    """Run an ES module snippet with server/d133.mjs imported as d."""
    script = "import * as d from './server/d133.mjs';\n" + code
    r = subprocess.run(["node", "--input-type=module", "-e", script], cwd=STRATA,
                       capture_output=True, text=True, timeout=30)
    if r.returncode:
        raise AssertionError(r.stderr)
    return json.loads(r.stdout)


@unittest.skipUnless(HAVE_NODE, "node not installed")
class BoardUpdateTest(unittest.TestCase):
    def test_rate_wrap_and_forwarding(self):
        out = node_eval("""
const s = {history:{t:[],ifs:{}}}, mk=(t,rx,tx,fw)=>({uptime_ms:t,ip_forwarded:fw,ifs:[{name:'w1',rx_bytes:rx,tx_bytes:tx}]});
let p = d.boardUpdate(s, null, mk(1000, 2**32-1000, 10, 2**32-5));
const first = s.ifs[0].rx_rate;
p = d.boardUpdate(s, p, mk(3000, 3000, 2010, 15));
console.log(JSON.stringify({first, rx:s.ifs[0].rx_rate, tx:s.ifs[0].tx_rate, fw:s.fwRate, h:s.history}));
""")
        self.assertIsNone(out["first"])
        self.assertEqual(out["rx"], 2000)          # 4000 bytes over 2 s, across the wrap
        self.assertEqual(out["tx"], 1000)
        self.assertEqual(out["fw"], 10)            # 20 packets over 2 s, across the wrap
        series = {"rx": [2000], "tx": [1000], "rx_pps": [None], "tx_pps": [None], "rx_drop": [None], "tx_err": [None]}
        self.assertEqual(out["h"], {"t": [3000], "ifs": {"w1": series}})   # packet and drop series are null: this sample has no such fields

    def test_reboot_and_late_interface(self):
        out = node_eval("""
const s = {history:{t:[],ifs:{}}};
const a=(t,v)=>({uptime_ms:t,ip_forwarded:-1,ifs:[{name:'e0',rx_bytes:v,tx_bytes:v}]});
const b=(t,v)=>({uptime_ms:t,ip_forwarded:-1,ifs:[{name:'e0',rx_bytes:v,tx_bytes:v},{name:'w1',rx_bytes:v,tx_bytes:v}]});
let p = d.boardUpdate(s, null, a(1000, 0)); p = d.boardUpdate(s, p, a(2000, 100));
p = d.boardUpdate(s, p, b(3000, 200)); p = d.boardUpdate(s, p, b(4000, 300));
const late = JSON.parse(JSON.stringify(s.history));
p = d.boardUpdate(s, p, b(500, 1));        // uptime went back: reboot
console.log(JSON.stringify({late, after:s.history, fw:s.ipForwarded}));
""")
        self.assertEqual(out["late"]["t"], [2000, 3000, 4000])
        self.assertEqual(out["late"]["ifs"]["w1"]["rx"], [None, None, 100])   # padded, aligned
        self.assertEqual(out["after"], {"t": [], "ifs": {}})
        self.assertIsNone(out["fw"])

    def test_safe_local_path(self):
        out = node_eval("""
console.log(JSON.stringify([d.safeLocalPath('/o','../../etc/passwd'), d.safeLocalPath('/o','a\\\\b/./c')]));
""")
        self.assertEqual(out, ["/o/etc/passwd", "/o/a/b/c"])


@unittest.skipUnless(HAVE_NODE and HAVE_DIST, "node or Strata build missing (npm ci && npm run build)")
class StrataApiTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        subprocess.check_call(["make", "-s", "-C", os.path.join(RTT, "host"), "dataserver_c"])
        cls.work = tempfile.mkdtemp(prefix="strata_d133_")
        cls.root = os.path.join(cls.work, "root")
        cls.data = {"clip/a.bin": os.urandom(1_500_000), "clip/sub/b.bin": os.urandom(5),
                    "clip/empty.bin": b"", "slow/big.bin": os.urandom(8_000_000)}
        for rel, blob in cls.data.items():
            p = os.path.join(cls.root, *rel.split("/"))
            os.makedirs(os.path.dirname(p), exist_ok=True)
            with open(p, "wb") as f:
                f.write(blob)
        cls.port, cls.sport, cls.web = free_port(), free_port(), free_port()
        cls.ds = subprocess.Popen([SERVER, "-p", str(cls.port), "-s", "2", "-q", "4", "-b", "512",
                                   "-r", cls.root, "-S", str(cls.sport)],
                                  stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
        assert cls.ds.stdout.readline().startswith("READY")
        cls.out = os.path.join(cls.work, "out")
        env = dict(os.environ, STRATA_PORT=str(cls.web), HOME=os.path.join(cls.work, "home"),
                   DATASERVER_HOST="127.0.0.1", DATASERVER_PORT=str(cls.port),
                   DATASERVER_STAT_PORT=str(cls.sport), DASHBOARD_OUT=cls.out)
        cls.host = subprocess.Popen(["node", "server/index.mjs", "--dist", "dist"], cwd=STRATA, env=env,
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        cls.base = f"http://127.0.0.1:{cls.web}"
        for _ in range(100):
            try:
                cls.get("/api/d133/config")
                return
            except OSError:
                time.sleep(0.1)
        raise RuntimeError("Strata host did not start")

    @classmethod
    def tearDownClass(cls):
        cls.host.terminate()
        cls.ds.terminate()
        cls.host.wait(5)
        cls.ds.wait(5)
        cls.ds.stdout.close()
        shutil.rmtree(cls.work, ignore_errors=True)

    @classmethod
    def get(cls, path):
        return json.load(urllib.request.urlopen(cls.base + path, timeout=5))

    def post(self, path, body):
        req = urllib.request.Request(self.base + path, data=json.dumps(body).encode(),
                                     headers={"Content-Type": "application/json"}, method="POST")
        try:
            return 200, json.load(urllib.request.urlopen(req, timeout=5))
        except urllib.error.HTTPError as e:
            return e.code, json.loads(e.read() or b"{}")

    def wait_pull(self, timeout=30):
        end = time.time() + timeout
        while time.time() < end:
            s = self.get("/api/d133/pull")
            if s["status"] not in ("CONNECTING", "RECEIVING"):
                return s
            time.sleep(0.05)
        self.fail("pull did not finish")

    def test_1_config(self):
        c = self.get("/api/d133/config")
        self.assertEqual((c["host"], c["dataPort"], c["statPort"], c["outDir"]),
                         ("127.0.0.1", self.port, self.sport, self.out))

    def test_2_pull_and_checksums(self):
        self.assertEqual(self.post("/api/d133/pull", {"directory": "clip"})[0], 200)
        s = self.wait_pull()
        self.assertEqual(s["status"], "SUCCESS", s["log"])
        got = {f["path"]: f for f in s["files"]}
        self.assertEqual(set(got), {k for k in self.data if k.startswith("clip/")})
        for path, f in got.items():
            blob = self.data[path]
            self.assertTrue(f["done"])
            self.assertEqual(f["size"], len(blob))
            self.assertEqual(f["received"], f["nblocks"])
            self.assertEqual(f["sha256"], hashlib.sha256(blob).hexdigest())
            with open(os.path.join(self.out, *path.split("/")), "rb") as fh:
                self.assertEqual(fh.read(), blob)
        self.assertEqual(s["bytes"], sum(len(v) for k, v in self.data.items() if k.startswith("clip/")))
        self.assertNotIn("local", s["files"][0])            # no PC paths in the API

    def test_3_busy_and_bad_requests(self):
        self.assertEqual(self.post("/api/d133/pull", {"directory": ""})[0], 400)
        self.assertEqual(self.post("/api/d133/pull", {"directory": "slow"})[0], 200)
        self.assertEqual(self.post("/api/d133/pull", {"directory": "clip"})[0], 409)
        self.assertEqual(self.wait_pull()["status"], "SUCCESS")
        self.assertEqual(self.post("/api/d133/pull", {"directory": "nope"})[0], 200)
        s = self.wait_pull()
        self.assertEqual(s["status"], "FAILED")
        self.assertIn("Nothing received", s["log"][-1])

    def test_4_dead_dataserver(self):
        self.assertEqual(self.post("/api/d133/pull", {"directory": "clip", "port": free_port()})[0], 200)
        s = self.wait_pull()
        self.assertEqual(s["status"], "ERROR")
        self.assertIn("ECONNREFUSED", s["log"][-1])

    def test_5_board_stats(self):
        for _ in range(3):
            b = self.get("/api/d133/board")
            time.sleep(1.05)
        self.assertTrue(b["ok"], b.get("error"))
        lo = next(i for i in b["ifs"] if i["name"] == "lo")
        self.assertIsInstance(lo["rx_rate"], (int, float))
        self.assertGreaterEqual(len(b["history"]["t"]), 2)
        self.assertEqual(len(b["history"]["ifs"]["lo"]["rx"]), len(b["history"]["t"]))
        self.assertEqual(b["dataserver"]["running"], 1)

    def test_6_board_unreachable_and_bad_port(self):
        dead = free_port()
        b = self.get(f"/api/d133/board?host=127.0.0.1&port={dead}")
        self.assertFalse(b["ok"])
        self.assertIn(str(dead), b["error"])
        with self.assertRaises(urllib.error.HTTPError) as cm:
            self.get("/api/d133/board?port=abc")
        self.assertEqual(cm.exception.code, 400)

    # ---- /api/d133/stream (server/stream/engine.mjs) -------------------------------------------
    def test_7_stream_idle_and_validation(self):
        st = self.get("/api/d133/stream")
        self.assertEqual(st["role"], "idle"); self.assertIsNone(st["stats"]); self.assertIsNone(st["path"])
        for body, word in (({}, "role"), ({"role": "send", "port": 5600}, "host"), ({"role": "send", "host": "1.2.3.4", "port": 70000}, "port"),
                           ({"role": "recv", "port": 0}, "port")):
            code, r = self.post("/api/d133/stream/start", body)
            self.assertEqual(code, 400, body); self.assertIn(word, r["error"])

    def test_8_stream_receiver_and_sender_through_the_api(self):
        rport = free_port()
        code, st = self.post("/api/d133/stream/start", {"role": "recv", "port": rport, "reportMs": 100})
        self.assertEqual(code, 200); self.assertEqual(st["role"], "recv")
        code, r = self.post("/api/d133/stream/start", {"role": "recv", "port": rport})
        self.assertEqual(code, 409); self.assertIn("already running", r["error"])
        # a sender is a second run: stop the receiver, then send to nobody; the numbers must still be coherent
        self.post("/api/d133/stream/stop", {})
        code, st = self.post("/api/d133/stream/start", {"role": "send", "host": "127.0.0.1", "port": rport, "bitrate": 1e6})
        self.assertEqual(code, 200); self.assertEqual(st["role"], "send")
        time.sleep(1.5)
        st = self.get("/api/d133/stream")
        self.assertEqual(st["stats"]["role"], "send"); self.assertGreater(st["stats"]["txPackets"], 20)
        self.assertEqual(st["path"]["located"]["where"], None)                         # no receiver answering: nothing can be located
        self.assertEqual(self.post("/api/d133/stream/stop", {})[1]["role"], "idle")

    def test_9_stream_controls_refuse_other_sites(self):
        for path, body in (("/api/d133/stream/start", {"role": "recv", "port": free_port()}), ("/api/d133/stream/stop", {})):
            req = urllib.request.Request(self.base + path, data=json.dumps(body).encode(), method="POST",
                                         headers={"Content-Type": "application/json", "Origin": "http://evil.example"})
            with self.assertRaises(urllib.error.HTTPError) as cm:
                urllib.request.urlopen(req, timeout=5)
            self.assertEqual(cm.exception.code, 403)
        self.assertEqual(self.get("/api/d133/stream")["role"], "idle")                  # and nothing was started


if __name__ == "__main__":
    unittest.main(verbosity=2)
