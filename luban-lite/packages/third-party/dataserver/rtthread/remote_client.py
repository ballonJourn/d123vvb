#!/usr/bin/env python3
"""
remoteClient in Python: port of NikosMav/System-Programming-3 client.cpp,
same command line and wire protocol, runs on Windows without a compiler.

    python remote_client.py -i 192.168.169.1 -p 9000 -d log
    python remote_client.py -i 192.168.1.1   -p 9000 -d calib -o D:\\adas_pull

Downloads every file below <dir> on the board into <out>/<dir>/... (default
out dir "results", as upstream). Protocol summary in dataserver.c.
"""
import argparse
import os
import socket
import struct
import sys
import time

ACK = b"}#%\x00"
PATH_FIELD = 512


class ProtocolError(Exception):
    pass


def recv_exact(sock, n):
    buf = bytearray()
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ProtocolError("connection closed")
        buf += chunk
    return bytes(buf)


def read_data(sock, size):
    """read exactly size bytes, then ack (upstream read_data + respond)"""
    data = recv_exact(sock, size)
    sock.sendall(ACK)
    return data


def send_data(sock, data):
    """send a whole chunk, then wait for the ack (upstream send_data)"""
    sock.sendall(data)
    ack = recv_exact(sock, 4)
    if ack[:3] != ACK[:3]:
        raise ProtocolError("bad ack")


def safe_local_path(out_dir, server_path):
    """never write outside out_dir, whatever the server sends"""
    parts = [p for p in server_path.replace("\\", "/").split("/") if p not in ("", ".", "..")]
    if not parts:
        raise ProtocolError("empty file name from server")
    return os.path.join(out_dir, *parts)


def get_file(sock, block, out_dir, verbose=True, on_file=None, on_block=None):
    """receive one file; returns (path, size) or None when the server is done.
    on_file(server_path, size, nblocks) is called before the data,
    on_block(server_path, index, nblocks, nbytes) after every block."""
    try:
        raw = read_data(sock, PATH_FIELD)
    except ProtocolError:
        return None                          # server closed: directory complete
    path = raw.split(b"\x00", 1)[0].decode("utf-8", "replace")
    (size,) = struct.unpack("<i", read_data(sock, 4))
    local = safe_local_path(out_dir, path)
    os.makedirs(os.path.dirname(local), exist_ok=True)
    nblocks = (size + block - 1) // block
    if on_file:
        on_file(path, size, nblocks)
    left = size
    with open(local, "wb") as f:
        for i in range(nblocks):
            b = read_data(sock, block)
            n = min(block, left)
            f.write(b[:n])                   # last block is zero padded
            left -= block
            if on_block:
                on_block(path, i, nblocks, n)
    if verbose:
        print("Received: %s (%d bytes)" % (local, size))
    return local, size


def fetch(ip, port, directory, out_dir="results", timeout=15.0, verbose=True,
          on_file=None, on_block=None, on_connect=None):
    """download a whole directory; returns list of (local_path, size).
    on_connect(block_size) is called once the server announced its block size."""
    sock = socket.create_connection((ip, port), timeout=timeout)
    try:
        (block,) = struct.unpack("<i", read_data(sock, 4))
        if on_connect:
            on_connect(block)
        req = directory.encode("utf-8")
        if len(req) >= PATH_FIELD:
            raise ProtocolError("path too long")
        send_data(sock, req + b"\x00" * (PATH_FIELD - len(req)))
        files = []
        while True:
            got = get_file(sock, block, out_dir, verbose, on_file, on_block)
            if got is None:
                break
            files.append(got)
        return files
    finally:
        sock.close()


def main(argv=None):
    ap = argparse.ArgumentParser(description="dataServer remote client (NikosMav protocol)")
    ap.add_argument("-i", dest="ip", default="192.168.169.1", help="server IP")
    ap.add_argument("-p", dest="port", type=int, default=9000, help="server port")
    ap.add_argument("-d", dest="directory", required=True, help="directory on the board, relative to its root")
    ap.add_argument("-o", dest="out", default="results", help="local output directory")
    ap.add_argument("-q", dest="quiet", action="store_true")
    a = ap.parse_args(argv)
    print("Connecting to %s on port %d" % (a.ip, a.port))
    t0 = time.time()
    try:
        files = fetch(a.ip, a.port, a.directory, a.out, verbose=not a.quiet)
    except (OSError, ProtocolError) as e:
        print("error: %s" % e, file=sys.stderr)
        return 1
    total = sum(s for _, s in files)
    dt = max(time.time() - t0, 1e-6)
    print("%d files, %d bytes in %.2f s (%.1f KB/s)" % (len(files), total, dt, total / dt / 1024))
    if not files:
        print("nothing received: directory empty, missing or outside the server root")
    return 0


if __name__ == "__main__":
    sys.exit(main())
