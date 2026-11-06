#!/usr/bin/env node
/**
 * Try the dashboard without the board: a simulated PC1 -> D133 -> PC2 path with losses you choose,
 * and the board's stats line served on a TCP port exactly like the real dsstat endpoint.
 *
 *   node sim/demo.mjs --to 5600 --cable 0.02 --d133 0.08 --wifi 0.03 [--rate-mbit 6]
 *   node sim/demo.mjs --to 5600 --wifi-shaper-mbit 6 --queue-ms 300      a WiFi that queues, then drops: delay rises before loss
 *
 * Start a receiver on UDP 5600 (dashboard "Receive" or `cli.mjs recv --port 5600`), then point
 * the dashboard's Board IP at 127.0.0.1 with the stats port printed here, and send to the stream
 * address printed here.
 */
import { createPath } from './path.mjs';

const a = Object.fromEntries(process.argv.slice(2).reduce((acc, v, i, arr) => v.startsWith('--') ? [...acc, [v.slice(2), arr[i + 1]]] : acc, []));
const path = await createPath({
  receiver: { port: Number(a.to ?? 5600) },
  cable: { loss: Number(a.cable ?? 0), delayMs: Number(a['cable-delay'] ?? 0) },
  d133: { loss: Number(a.d133 ?? 0), rateBps: a['rate-mbit'] ? Number(a['rate-mbit']) * 1e6 : null },
  wifi: { loss: Number(a.wifi ?? 0), jitterMs: Number(a['wifi-jitter'] ?? 0), shaperBps: a['wifi-shaper-mbit'] ? Number(a['wifi-shaper-mbit']) * 1e6 : null, queueMs: Number(a['queue-ms'] ?? 300) },
  seed: Number(a.seed ?? 1),
});
const stats = await path.serveDsstat(Number(a['stats-port'] ?? 0));
console.log(`simulated D133 path\n  send the stream to    127.0.0.1:${path.port}\n  board stats port      127.0.0.1:${stats.port}   (dashboard: Board IP 127.0.0.1, Stats port ${stats.port})\n  forwards to           127.0.0.1:${a.to ?? 5600}\n  injected loss         cable ${a.cable ?? 0}, inside D133 ${a.d133 ?? 0}${a['rate-mbit'] ? ` + policer ${a['rate-mbit']} Mbit/s` : ''}, WiFi ${a.wifi ?? 0}`);
process.on('SIGINT', () => { path.close(); stats.close(); process.exit(0); });
