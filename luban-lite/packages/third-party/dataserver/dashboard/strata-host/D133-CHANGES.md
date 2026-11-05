# D133 changes to Strata apps/host

Upstream: Niko1221/Strata `apps/host` at ffa80d73 (MIT, `LICENSE-Strata`),
imported unmodified in its own commit. Everything below is on top of it.

| File | Change |
|---|---|
| `server/d133.mjs` (new) | Board stats from the dsstat port (uint32 wrap, reboot reset, aligned history) and NikosMav dataServer pulls with progress and SHA-256 |
| `server/index.mjs` | import + instance of `createD133` (env `DATASERVER_HOST/PORT/STAT_PORT`, `DASHBOARD_OUT`); `/api/d133/*` routes before `/api/monitor` |
| `src/screens/D133Board.tsx` (new) | Board screen built from `MonitorMetric`, `ReferenceSpark`, `monitor-readings`, `st-table native-performance-table`, `input-field`, `Bar`, `Pill`, `Empty` |
| `src/screens/Monitor.tsx` | `MonitorMetric` exported |
| `src/components/charts.tsx` | `ReferenceSpark` optional `primaryLabel` (default `Decode`, upstream behaviour unchanged) |
| `src/lib/router.ts` | `board` route, landing route `board` instead of `chat` |
| `src/app/App.tsx` | `board` in `RouteView`; first-run modal not forced on the board screen |
| `src/lib/store.ts` | boot does not redirect to `#/setup` while on the board screen |
| `src/app/Sidebar.tsx` | "D133 board" entry |
| `src/main.tsx`, `src/styles/d133.css` (new) | pull form grid, current-file bar spacing, log box; Strata tokens only |
| `.gitignore` | `dist/` |

Chat, models, monitor and the rest of Strata are untouched and still reachable
from the sidebar (inference is simulated without `STRATA_REAL_ENGINE=1`).

Fonts: `public/fonts` holds GT Walsheim Pro as shipped by upstream. Check its
licence before publishing; `--st-font` in `src/styles/kit/tokens.css` can point
to Outfit (`../web/fonts`, SIL OFL) instead.

## Stream and board counters (see D133-STREAM.md)

| File | Change |
|---|---|
| `server/stream/` (new) | UDP stream library: protocol, sender, receiver, bounded queue, adaptive rate, rate-limit search, loss location, calibration, CLI, dashboard engine; `sim/` = simulated D133 path and demo; `test/` = 88 Node tests |
| `server/d133.mjs` | `boardUpdate`: packet rates, refused-frame / driver-error rates, lwIP counter rates (history series aligned with `history.t`); `boardPeek()` |
| `server/index.mjs` | `/api/d133/stream`, `/stream/start`, `/stream/stop` (same Origin guard as the rest) |
| `src/screens/D133Stream.tsx`, `D133Lwip.tsx`, `d133-types.ts`, `d133-format.ts` (new) | stream panel with the three-segment path, rate-limit search table, lwIP counters and pool limits |
| `src/screens/D133Board.tsx` | stream panel first, interface table with packets and refused columns, lwIP panel; Pull moved to a Batch section |
| `src/styles/d133.css` | stream form, path, pools |

Board side (`../rtthread`, `../Kconfig`, `lwipopts.h`, defconfig): `netmon.c` counts a frame the stack or the
driver refuses (`rx_dropped`, `tx_errors`) instead of adding it to the byte counters and reads lwIP's counters
and limits; `dsstat` reports them (`rx_drop`, `tx_err`, `lwip`); `DATASERVER_LWIP_STATS` selects `RT_LWIP_STATS`;
`LWIP_STATS_LARGE` makes lwIP's counters 32 bit.
