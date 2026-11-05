"""
client_web.py
  Flask web app  → port 5002  (serves client_ui.html + /api/pull)

Adapted from MusadiqPasha/file_transfer for the D133 dataServer
(NikosMav/System-Programming-3 protocol, see ../rtthread/remote_client.py):
  - the browser asks the board for a whole directory instead of uploading a file
  - the transfer runs in a thread through remote_client.fetch(); its progress
    hooks fill the same shared state the upstream dashboard already polls
  - added: per-file list, live throughput history (KB/s every 0.5 s),
    SHA-256 of each received file (the protocol itself has no checksum)
  - runs offline (no web fonts) and only listens on 127.0.0.1

    pip install flask
    python client_web.py            → http://localhost:5002
"""

import hashlib
import os
import sys
import threading
import time
from datetime import datetime
from flask import Flask, jsonify, request, send_from_directory

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "rtthread"))
import remote_client  # noqa: E402

DEFAULT_BOARD = os.environ.get("DATASERVER_HOST", "192.168.169.1")
DEFAULT_PORT  = int(os.environ.get("DATASERVER_PORT", "9000"))
FLASK_PORT    = int(os.environ.get("DASHBOARD_PORT", "5002"))
OUT_DIR       = os.environ.get("DASHBOARD_OUT", os.path.join(HERE, "received"))
SAMPLE_S      = 0.5          # throughput sampling period
HISTORY_LEN   = 240          # 2 minutes of samples
MAX_CHUNKS_UI = 2000         # boxes drawn for the current file

# ── Shared state ───────────────────────────────────────────────────────────────
transfer_state = {
    "status":             "IDLE",
    "board":              None,     # "ip:port"
    "directory":          None,
    "block_size":         0,
    "filename":           None,     # file being received
    "file_size":          0,
    "total_chunks":       0,        # blocks of the current file
    "chunks_received":    [],       # block indices of the current file
    "files":              [],       # [{"path", "size", "done", "sha256"}]
    "bytes_total":        0,
    "actual_checksum":    None,     # SHA-256 of the last finished file
    "verified":           None,
    "output_file":        None,     # local output directory
    "transfer_speed":     None,     # average bytes/sec
    "current_speed":      None,     # bytes/sec over the last sample
    "speed_history":      [],       # [[t_seconds, bytes_per_sec], ...]
    "transfer_time":      None,     # seconds
    "logs":               [],
}
state_lock = threading.Lock()

def log(msg: str):
    ts = datetime.now().strftime("%H:%M:%S")
    entry = f"[{ts}]  {msg}"
    with state_lock:
        transfer_state["logs"].append(entry)
    print(entry)

def set_state(**kwargs):
    with state_lock:
        transfer_state.update(kwargs)


# ── Flask ──────────────────────────────────────────────────────────────────────
app = Flask(__name__, static_folder=None)

@app.route("/")
def index():
    return send_from_directory(HERE, "client_ui.html")

@app.route("/api/config")
def api_config():
    return jsonify({"board": DEFAULT_BOARD, "port": DEFAULT_PORT, "out": OUT_DIR})

@app.route("/api/pull", methods=["POST"])
def api_pull():
    body = request.get_json(silent=True) or {}
    ip        = (body.get("board") or DEFAULT_BOARD).strip()
    port      = int(body.get("port") or DEFAULT_PORT)
    directory = (body.get("directory") or "").strip()
    if not directory:
        return jsonify({"error": "directory required"}), 400
    with state_lock:
        busy = transfer_state["status"] in ("CONNECTING", "RECEIVING")
    if busy:
        return jsonify({"error": "a transfer is already running"}), 409
    t = threading.Thread(target=run_pull, args=(ip, port, directory), daemon=True)
    t.start()
    return jsonify({"ok": True})

@app.route("/api/status")
def api_status():
    with state_lock:
        return jsonify(dict(transfer_state))

@app.route("/api/reset", methods=["POST"])
def api_reset():
    set_state(
        status="IDLE", board=None, directory=None, block_size=0,
        filename=None, file_size=0, total_chunks=0, chunks_received=[],
        files=[], bytes_total=0, actual_checksum=None, verified=None,
        output_file=None, transfer_speed=None, current_speed=None,
        speed_history=[], transfer_time=None, logs=[],
    )
    return jsonify({"ok": True})


# ── Core transfer ──────────────────────────────────────────────────────────────
def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for blk in iter(lambda: f.read(65536), b""):
            h.update(blk)
    return h.hexdigest()


def run_pull(ip: str, port: int, directory: str):
    set_state(
        status="CONNECTING", board=f"{ip}:{port}", directory=directory,
        block_size=0, filename=None, file_size=0, total_chunks=0,
        chunks_received=[], files=[], bytes_total=0, actual_checksum=None,
        verified=None, output_file=OUT_DIR, transfer_speed=None,
        current_speed=None, speed_history=[], transfer_time=None, logs=[],
    )
    log(f"Connecting to dataServer at {ip}:{port} ...")
    t0 = time.time()
    meter = {"t": t0, "bytes": 0}          # bytes since the last sample

    def finish_previous_file():
        # called when a new file starts and at the end: hash the finished one
        with state_lock:
            files = transfer_state["files"]
            last = files[-1] if files else None
        if last and not last["done"]:
            local = remote_client.safe_local_path(OUT_DIR, last["path"])
            digest = sha256_file(local)
            with state_lock:
                last["done"] = True
                last["sha256"] = digest
                transfer_state["actual_checksum"] = digest
            log(f"Received {last['path']}  ({last['size']} bytes)  sha256 {digest[:16]}…")

    def on_connect(block):
        set_state(status="RECEIVING", block_size=block)
        log(f"Connected. Block size {block} bytes. Requesting '{directory}' ...")

    def on_file(path, size, nblocks):
        finish_previous_file()
        with state_lock:
            transfer_state["files"].append({"path": path, "size": size, "done": False, "sha256": None})
            transfer_state.update(filename=path, file_size=size,
                                  total_chunks=nblocks, chunks_received=[])
        if nblocks == 0:
            log(f"{path}: empty file")

    def on_block(path, index, nblocks, nbytes):
        now = time.time()
        with state_lock:
            if index < MAX_CHUNKS_UI:
                transfer_state["chunks_received"].append(index)
            transfer_state["bytes_total"] += nbytes
            elapsed = now - t0
            transfer_state["transfer_time"] = round(elapsed, 2)
            transfer_state["transfer_speed"] = int(transfer_state["bytes_total"] / max(elapsed, 1e-6))
            meter["bytes"] += nbytes
            if now - meter["t"] >= SAMPLE_S:
                bps = int(meter["bytes"] / (now - meter["t"]))
                transfer_state["current_speed"] = bps
                hist = transfer_state["speed_history"]
                hist.append([round(elapsed, 2), bps])
                del hist[:-HISTORY_LEN]
                meter.update(t=now, bytes=0)

    try:
        files = remote_client.fetch(ip, port, directory, OUT_DIR, verbose=False,
                                    on_file=on_file, on_block=on_block, on_connect=on_connect)
        finish_previous_file()
        elapsed = time.time() - t0
        with state_lock:
            total = transfer_state["bytes_total"]
            transfer_state["speed_history"].append([round(elapsed, 2), 0])
        set_state(transfer_time=round(elapsed, 2),
                  transfer_speed=int(total / max(elapsed, 1e-6)), current_speed=0)
        if files:
            set_state(status="SUCCESS", verified=True)
            log(f"Done: {len(files)} files, {total} bytes in {elapsed:.2f} s → {OUT_DIR}")
        else:
            set_state(status="FAILED", verified=False)
            log("Nothing received: directory empty, missing or outside the board root")
    except Exception as e:  # noqa: BLE001  (shown in the dashboard)
        log(f"ERROR: {e}")
        set_state(status="ERROR")


# ── Entry point ────────────────────────────────────────────────────────────────
if __name__ == "__main__":
    print(f"\n  Client UI →  http://localhost:{FLASK_PORT}   (board {DEFAULT_BOARD}:{DEFAULT_PORT})\n")
    app.run(host="127.0.0.1", port=FLASK_PORT, debug=False, threaded=True)
