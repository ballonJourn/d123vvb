# D133 dataServer dashboard

Web dashboard for pulling ADAS files off the D133 board over the dataServer
protocol (NikosMav/System-Programming-3 wire format, see `../rtthread/dataserver.c`).

The UI is adapted from [MusadiqPasha/file_transfer](https://github.com/MusadiqPasha/file_transfer)
(Flask backend polled at `/api/status`, stats cards, progress bar, block grid, log).
The transfer underneath is replaced by `rtthread/remote_client.py`.

## Run (on the PC joined to the board's WiFi AP or plugged into its Ethernet)

    pip install flask
    python client_web.py                 # http://localhost:5002

Environment overrides: `DATASERVER_HOST` (default 192.168.169.1),
`DATASERVER_PORT` (9000), `DASHBOARD_PORT` (5002), `DASHBOARD_OUT` (./received).

Enter the board IP, port and a directory below the board's `DATASERVER_ROOT`
(default `/data/adas`), then press **Pull**. Files land in `received/<dir>/...`.

## What it shows

| Panel | Source |
|---|---|
| Throughput curve (now / peak / total) | 0.5 s samples of received bytes |
| Files list | one row per file announced by the board |
| Blocks of the current file | one box per received block |
| SHA-256 of each file | computed on the PC after the file is written |
| Log | connect, block size, per-file result, errors |

The protocol itself has no checksum. The SHA-256 is for comparing against
`sha256sum` on the source; it does not prove the board sent the right bytes.

## API

| Route | Method | Purpose |
|---|---|---|
| `/` | GET | the page |
| `/api/config` | GET | default board, port, output dir |
| `/api/pull` | POST `{board, port, directory}` | start a pull (409 if one is running) |
| `/api/status` | GET | full transfer state |
| `/api/reset` | POST | clear state |

## Notes

- Listens on `127.0.0.1` only; no web fonts or CDN, works with no internet.
- File names from the board are rendered as text, never as HTML.
- `server.py`, `server_ui.html`, `client.png`, `server.png` and `utils.py` are
  the unmodified upstream TCP-upload demo. The D133 dashboard does not use
  them; they are kept for reference and can be deleted.
- `d133_dashboard.png` was taken against the host build of the C server
  (`make -C ../rtthread/host`), not against the board.
- Tests: `python3 -m unittest discover -s ../rtthread/tests -v`
