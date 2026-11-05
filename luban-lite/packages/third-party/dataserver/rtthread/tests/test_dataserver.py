#!/usr/bin/env python3
"""
Tests for the board C port of dataServer (rtthread/dataserver.c), built for the
host, against both the upstream C++ remoteClient and remote_client.py.

    make -C rtthread/host && python3 -m unittest discover -s rtthread/tests -v
"""
import filecmp
import os
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
RTT = os.path.dirname(HERE)
HOST = os.path.join(RTT, "host")
sys.path.insert(0, RTT)

import remote_client  # noqa: E402

SERVER = os.path.join(HOST, "dataserver_c")
CPP_CLIENT = os.path.join(HOST, "remoteClient")
BLOCK = 512
POOL = 2
QUEUE = 3                     # small on purpose: producers must block


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def tree_files(root):
    out = []
    for d, _dirs, files in os.walk(root):
        for f in files:
            out.append(os.path.relpath(os.path.join(d, f), root).replace(os.sep, "/"))
    return sorted(out)


class DataServerTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not (os.path.exists(SERVER) and os.path.exists(CPP_CLIENT)):
            subprocess.check_call(["make", "-C", HOST])
        cls.work = tempfile.mkdtemp(prefix="ds_")
        cls.root = os.path.join(cls.work, "root")
        cls.make_tree(cls.root)
        cls.port = free_port()
        cls.proc = subprocess.Popen(
            [SERVER, "-p", str(cls.port), "-s", str(POOL), "-q", str(QUEUE),
             "-b", str(BLOCK), "-r", cls.root],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        line = cls.proc.stdout.readline()
        if not line.startswith("READY"):
            raise RuntimeError("server did not start: " + cls.proc.stderr.read())

    @classmethod
    def tearDownClass(cls):
        cls.proc.terminate()
        cls.proc.wait(timeout=10)
        log = cls.proc.stderr.read()
        cls.proc.stdout.close()
        cls.proc.stderr.close()
        shutil.rmtree(cls.work, ignore_errors=True)
        for marker in ("Sanitizer", "runtime error:"):
            if marker in log:
                raise AssertionError("server reported a sanitizer issue:\n" + log)

    @staticmethod
    def make_tree(root):
        def put(rel, n):
            p = os.path.join(root, *rel.split("/"))
            os.makedirs(os.path.dirname(p), exist_ok=True)
            with open(p, "wb") as f:
                f.write(os.urandom(n))
        # sizes around the block size: the upstream "block%size" bug cases
        for n in (0, 1, BLOCK - 1, BLOCK, BLOCK + 1, BLOCK // 2, 2 * BLOCK, 7 * BLOCK + 3, 250_000):
            put("log/f_%d.bin" % n, n)
        put("log/2026-11-05/clip_a.bin", 33_333)
        put("log/2026-11-05/deep/er/than/that.bin", 1234)
        put("calib/front/cam0.yaml", 40)
        put("calib/rear/cam1.yaml", 41)
        for i in range(25):                           # more files than the queue
            put("event/burst/e_%02d.bin" % i, 3000 + i)
        os.makedirs(os.path.join(root, "empty"), exist_ok=True)

    def out_dir(self):
        d = tempfile.mkdtemp(prefix="out_", dir=self.work)
        return d

    def assert_same_tree(self, rel, out):
        src = os.path.join(self.root, rel)
        got = os.path.join(out, rel)
        self.assertEqual(tree_files(src), tree_files(got))
        for f in tree_files(src):
            self.assertTrue(filecmp.cmp(os.path.join(src, f), os.path.join(got, f), shallow=False), f)

    def run_cpp(self, directory, cwd, timeout=60):
        return subprocess.run([CPP_CLIENT, "-i", "127.0.0.1", "-p", str(self.port), "-d", directory],
                              cwd=cwd, capture_output=True, text=True, timeout=timeout)

    # -- protocol compatibility ------------------------------------------
    def test_upstream_cpp_client_edge_sizes(self):
        out = self.out_dir()
        r = self.run_cpp("log", out)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assert_same_tree("log", os.path.join(out, "results"))

    def test_python_client_edge_sizes(self):
        out = self.out_dir()
        files = remote_client.fetch("127.0.0.1", self.port, "log", out, verbose=False)
        self.assertEqual(len(files), len(tree_files(os.path.join(self.root, "log"))))
        self.assert_same_tree("log", out)

    def test_python_cli(self):
        out = self.out_dir()
        r = subprocess.run([sys.executable, os.path.join(RTT, "remote_client.py"), "-i", "127.0.0.1",
                            "-p", str(self.port), "-d", "calib", "-o", out, "-q"],
                           capture_output=True, text=True, timeout=30)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("2 files", r.stdout)
        self.assert_same_tree("calib", out)

    def test_subdirectory_request(self):
        out = self.out_dir()
        remote_client.fetch("127.0.0.1", self.port, "log/2026-11-05", out, verbose=False)
        self.assert_same_tree("log/2026-11-05", out)

    def test_block_size_announced(self):
        with socket.create_connection(("127.0.0.1", self.port), timeout=10) as s:
            (block,) = struct.unpack("<i", remote_client.read_data(s, 4))
            self.assertEqual(block, BLOCK)

    # -- empty / missing / forbidden ---------------------------------------
    def test_empty_and_missing_dirs_return(self):
        for d in ("empty", "does/not/exist"):
            out = self.out_dir()
            t0 = time.time()
            self.assertEqual(remote_client.fetch("127.0.0.1", self.port, d, out, verbose=False), [])
            self.assertLess(time.time() - t0, 5, d)
            r = self.run_cpp(d, out, timeout=10)
            self.assertEqual(r.returncode, 0, d)

    def test_traversal_rejected(self):
        secret = os.path.join(self.work, "secret.txt")
        with open(secret, "w") as f:
            f.write("not for clients")
        for d in ("..", "../root/..", "log/../..", "/../.."):
            out = self.out_dir()
            self.assertEqual(remote_client.fetch("127.0.0.1", self.port, d, out, verbose=False), [], d)
            self.assertEqual(tree_files(out), [], d)

    def test_absolute_request_is_relative_to_root(self):
        out = self.out_dir()
        remote_client.fetch("127.0.0.1", self.port, "/calib", out, verbose=False)
        self.assert_same_tree("calib", out)

    # -- concurrency ---------------------------------------------------------
    def test_parallel_clients_small_queue(self):
        # 6 clients x (25 or more files) through a 3-entry queue and 2 workers:
        # comm threads block on a full queue, workers skip busy sockets
        errors = []

        def job(i):
            try:
                out = os.path.join(self.work, "par_%d" % i)
                if i % 2:
                    r = self.run_cpp("event", self.out_dir_for(out))
                    if r.returncode:
                        errors.append("cpp %d rc %d" % (i, r.returncode))
                    self.assert_same_tree("event", os.path.join(out, "results"))
                else:
                    remote_client.fetch("127.0.0.1", self.port, "event", out, verbose=False)
                    self.assert_same_tree("event", out)
            except Exception as e:  # noqa: BLE001
                errors.append("%d: %r" % (i, e))

        threads = [threading.Thread(target=job, args=(i,)) for i in range(6)]
        for t in threads:
            t.start()
        for t in threads:
            t.join(120)
        self.assertEqual(errors, [])

    @staticmethod
    def out_dir_for(path):
        os.makedirs(path, exist_ok=True)
        return path

    # -- robustness -----------------------------------------------------------
    def test_client_disconnect_mid_transfer(self):
        # get the block size, ask for a big directory, read one chunk and vanish
        s = socket.create_connection(("127.0.0.1", self.port), timeout=10)
        remote_client.read_data(s, 4)
        remote_client.send_data(s, b"log" + b"\x00" * 509)
        remote_client.read_data(s, 512)
        s.close()
        time.sleep(0.5)
        # the server (and its workers) must still serve everybody else
        out = self.out_dir()
        remote_client.fetch("127.0.0.1", self.port, "log", out, verbose=False)
        self.assert_same_tree("log", out)
        self.assertIsNone(self.proc.poll(), "server died")

    def test_garbage_ack_closes_only_that_client(self):
        s = socket.create_connection(("127.0.0.1", self.port), timeout=10)
        recv = remote_client.recv_exact(s, 4)
        self.assertEqual(len(recv), 4)
        s.sendall(b"XXXX")                       # wrong ack
        s.settimeout(5)
        self.assertEqual(s.recv(1), b"")         # server closes the connection
        s.close()
        out = self.out_dir()
        remote_client.fetch("127.0.0.1", self.port, "calib", out, verbose=False)
        self.assert_same_tree("calib", out)

    def test_many_sequential_requests(self):
        for i in range(30):
            out = self.out_dir()
            remote_client.fetch("127.0.0.1", self.port, "calib", out, verbose=False)
            self.assert_same_tree("calib", out)
        self.assertIsNone(self.proc.poll())


if __name__ == "__main__":
    unittest.main(verbosity=2)
