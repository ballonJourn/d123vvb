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
  - added: live board network panel. A poller reads the board's dsstat
    endpoint (one JSON line per connection, port 9001, rtthread/dsstat.c)
    every second while the page is open and turns the e0/w0/w1 counters into
    rx/tx rates, so FTP, dataServer and e0 <-> w1 forwarding traffic show up
  - runs offline (no web fonts) and only listens on 127.0.0.1

    pip install flask
    python client_web.py            → http://localhost:5002
"""

import hashlib
import json
import os
import socket
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
STAT_PORT     = int(os.environ.get("DATASERVER_STAT_PORT", "9001"))
FLASK_PORT    = int(os.environ.get("DASHBOARD_PORT", "5002"))
OUT_DIR       = os.environ.get("DASHBOARD_OUT", os.path.join(HERE, "received"))
SAMPLE_S      = 0.5          # throughput sampling period
HISTORY_LEN   = 240          # 2 minutes of samples
MAX_CHUNKS_UI = 2000         # boxes drawn for the current file
BOARD_POLL_S  = 1.0          # board stats period
BOARD_HIST    = 180          # 3 minutes of board samples
BOARD_IDLE_S  = 10.0         # stop polling when no page asked for this long

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

@app.route("/web/<path:name>")
def web_asset(name):
    # CSS, JS and fonts of the page; send_from_directory refuses paths outside web/
    return send_from_directory(os.path.join(HERE, "web"), name)

@app.route("/api/config")
def api_config():
    return jsonify({"board": DEFAULT_BOARD, "port": DEFAULT_PORT, "stat_port": STAT_PORT,
                    "out": OUT_DIR})

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


# ── Board network stats (dsstat endpoint) ─────────────────────────────────────
board_state = {
    "target":       None,     # "ip:port" of the stats endpoint
    "ok":           False,
    "error":        None,
    "uptime_ms":    None,
    "ifs":          [],       # [{name, ip, up, rx_bytes, tx_bytes, rx_pkts, tx_pkts, rx_rate, tx_rate}]
    "history":      {},       # name -> [[t_s, rx_Bps, tx_Bps], ...]
    "ip_forwarded": None,
    "fw_rate":      None,     # forwarded packets / s
    "dataserver":   None,
    "updated":      None,
}
board_lock = threading.Lock()
_board = {"thread": None, "asked": 0.0, "prev": None, "host": DEFAULT_BOARD, "port": STAT_PORT}


def read_board_stats(host, port, timeout=2.0):
    """One sample from the board: connect, read one JSON line, close."""
    with socket.create_connection((host, port), timeout=timeout) as s:
        s.settimeout(timeout)
        data = b""
        while not data.endswith(b"\n") and len(data) < 65536:
            chunk = s.recv(4096)
            if not chunk:
                break
            data += chunk
    return json.loads(data.decode("utf-8", "replace"))


def delta32(new, old):
    """Difference of two wrapping uint32 counters."""
    return (int(new) - int(old)) % (1 << 32)


def board_update(state, prev, sample):
    """Fold one sample into state. prev is the previous sample (or None).
    Rates use the board's own uptime, so a slow network does not skew them.
    Returns the sample to keep as prev."""
    t_ms = int(sample.get("uptime_ms") or 0)
    if prev is not None and t_ms <= int(prev.get("uptime_ms") or 0):
        prev = None                                   # board rebooted: restart
        state["history"] = {}
    old = {i["name"]: i for i in (prev or {}).get("ifs", [])}
    dt = (t_ms - int(prev["uptime_ms"])) / 1000.0 if prev else 0.0
    ifs = []
    for i in sample.get("ifs", []):
        row = dict(i)
        o = old.get(i["name"])
        if o is not None and dt > 0:
            row["rx_rate"] = int(delta32(i["rx_bytes"], o["rx_bytes"]) / dt)
            row["tx_rate"] = int(delta32(i["tx_bytes"], o["tx_bytes"]) / dt)
            h = state["history"].setdefault(i["name"], [])
            h.append([round(t_ms / 1000.0, 1), row["rx_rate"], row["tx_rate"]])
            del h[:-BOARD_HIST]
        else:
            row["rx_rate"] = row["tx_rate"] = None
        ifs.append(row)
    fw = sample.get("ip_forwarded")
    fw_rate = None
    if prev is not None and dt > 0 and isinstance(fw, int) and fw >= 0 \
            and isinstance(prev.get("ip_forwarded"), int) and prev["ip_forwarded"] >= 0:
        fw_rate = round(delta32(fw, prev["ip_forwarded"]) / dt, 1)
    state.update(ok=True, error=None, uptime_ms=t_ms, ifs=ifs,
                 ip_forwarded=fw if isinstance(fw, int) and fw >= 0 else None,
                 fw_rate=fw_rate, dataserver=sample.get("dataserver"),
                 updated=datetime.now().strftime("%H:%M:%S"))
    return sample


def board_poller():
    while time.time() - _board["asked"] < BOARD_IDLE_S:
        host, port = _board["host"], _board["port"]
        try:
            sample = read_board_stats(host, port)
            with board_lock:
                _board["prev"] = board_update(board_state, _board["prev"], sample)
        except Exception as e:  # noqa: BLE001  (shown in the dashboard)
            with board_lock:
                board_state.update(ok=False, error=f"{host}:{port}: {e}")
                _board["prev"] = None
        time.sleep(BOARD_POLL_S)
    with board_lock:
        _board["thread"] = None


@app.route("/api/board")
def api_board():
    host = (request.args.get("board") or _board["host"]).strip()
    try:
        port = int(request.args.get("port") or _board["port"])
    except ValueError:
        return jsonify({"error": "bad port"}), 400
    with board_lock:
        if (host, port) != (_board["host"], _board["port"]):
            _board.update(host=host, port=port, prev=None)
            board_state.update(ok=False, error=None, ifs=[], history={},
                               ip_forwarded=None, fw_rate=None, dataserver=None)
        board_state["target"] = f"{host}:{port}"
        _board["asked"] = time.time()
        if _board["thread"] is None:
            _board["thread"] = threading.Thread(target=board_poller, daemon=True)
            _board["thread"].start()
        return jsonify(json.loads(json.dumps(board_state)))


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
