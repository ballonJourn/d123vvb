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
