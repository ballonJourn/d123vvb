#!/usr/bin/env node
/**
 * D133 stream tool, no dependencies beyond Node 20.
 *
 *   node cli.mjs recv --port 5600                                  on PC2
 *   node cli.mjs send --to 192.168.169.5:5600 --bitrate 4M         on PC1, synthetic frames
 *   node cli.mjs send --to ... --mode adaptive --max 20M           lowers/raises the rate from the receiver's reports
 *   node cli.mjs send --to ... --mode ramp --ramp-start 1M --ramp-step 1M --max 30M     finds where loss begins
 *   producer | node cli.mjs send --to ... --source stdin           your own frames: each is a 4 byte big endian length, then the bytes
 *   node cli.mjs calibrate --to PC2:5600 --board 192.168.1.1       checks the board's counters against a known number of packets
 *
 * calibrate: --board HOST[:STATPORT] (9001) --count 1000 --payload 1000 --pps 500 --ingress NAME --egress NAME   (default: found automatically; this board: e01 and w03)
 * Options: --fps 30 --queue 4 --pacing 0.5 --mtu 1400 --stream 1 --duration SECONDS --json
 *          recv: --report-ms 500 --queue 8
 * Rates take k / M / G suffixes (bits per second of frame payload).
 */
import { StreamSender } from './sender.mjs';
import { StreamReceiver } from './receiver.mjs';
import * as P from './protocol.mjs';
import { calibrate } from './calibrate.mjs';
import { createPrinter } from './printer.mjs';

export function parseRate(text) {
  const m = /^(\d+(?:\.\d+)?)\s*([kKmMgG]?)$/.exec(String(text).trim());
  if (!m) throw new Error(`bad rate "${text}" (examples: 800k, 4M, 1.5M)`);
  return Number(m[1]) * { '': 1, k: 1e3, m: 1e6, g: 1e9 }[m[2].toLowerCase()];
}

export function parseArgs(argv) {
  const [cmd, ...rest] = argv, o = { cmd, flags: {} };
  for (let i = 0; i < rest.length; i++) {
    if (!rest[i].startsWith('--')) throw new Error(`unexpected argument ${rest[i]}`);
    const k = rest[i].slice(2);
    if (k === 'json') o.flags.json = true;
    else { if (i + 1 >= rest.length) throw new Error(`--${k} needs a value`); o.flags[k] = rest[++i]; }
  }
  return o;
}

const fmt = (v, d = 2) => (v === null || v === undefined || !Number.isFinite(v) ? '-' : v.toFixed(d));
const mbit = bytesPerSec => bytesPerSec === null || bytesPerSec === undefined ? null : bytesPerSec * 8 / 1e6;

/** Reads length-prefixed frames from stdin and pushes them to the sender. */
function feedStdin(tx) {
  let buf = Buffer.alloc(0);
  process.stdin.on('data', d => {
    buf = Buffer.concat([buf, d]);
    for (;;) {
      if (buf.length < 4) return;
      const n = buf.readUInt32BE(0);
      if (n > 64 * 1024 * 1024) { console.error(`frame of ${n} bytes refused`); process.exit(2); }
      if (buf.length < 4 + n) return;
      tx.push(Buffer.from(buf.subarray(4, 4 + n))); buf = buf.subarray(4 + n);
    }
  });
}

export async function main(argv) {
  const { cmd, flags: f } = parseArgs(argv);
  const printer = createPrinter();                      // a stuck console (a click in the window) must not stop the stream
  const say = line => printer.log(line);
  const json = !!f.json, t0 = Date.now(), out = o => say(json ? JSON.stringify(o) : o.line);
  const duration = f.duration ? Number(f.duration) * 1000 : Infinity;
  let stop = () => {};
  const done = new Promise(res => { stop = res; });
  process.on('SIGINT', () => stop()); process.on('SIGTERM', () => stop());
  if (Number.isFinite(duration)) setTimeout(stop, duration).unref();

  if (cmd === 'recv') {
    const rx = new StreamReceiver({ port: Number(f.port ?? 5600), host: '0.0.0.0', reportMs: Number(f['report-ms'] ?? 500), frameQueue: Number(f.queue ?? 8), onFrame: () => {} });
    rx.on('error', e => { console.error(e.message); process.exit(1); });
    const port = await rx.start();
    if (!json) say(`receiving on UDP ${port}`);
    let prev = null;
    const timer = setInterval(() => {
      const s = rx.stats(); if (!s) return;
      const now = Date.now(), dt = prev ? (now - prev.t) / 1000 : 1;
      const wire = prev ? P.wireBytes(s.bytes - prev.bytes, s.packets - prev.packets) / dt : null, rep = s.lastReport;
      out({ t: Math.round((now - t0) / 1000), role: 'recv', mbit: mbit(wire), loss: rep?.lossFraction ?? null, lost: s.lost, expected: s.expected, reordered: s.reordered, duplicates: s.duplicates,
            jitterMs: s.jitterUs / 1000, framesOk: s.framesOk, framesDropped: s.framesDropped, queueDropped: s.queueDropped,
            line: `t=${String(Math.round((now - t0) / 1000)).padStart(3)}s  recv ${fmt(mbit(wire))} Mbit/s  loss ${rep?.lossFraction == null ? '-' : fmt(rep.lossFraction * 100, 1)} % (${s.lost}/${s.expected})  reordered ${s.reordered}  jitter ${fmt(s.jitterUs / 1000)} ms  frames ${s.framesOk} ok / ${s.framesDropped} dropped  queue drops ${s.queueDropped}` });
      prev = { t: now, bytes: s.bytes, packets: s.packets };
    }, 1000);
    await done; clearInterval(timer); rx.stop(); await printer.close(); return 0;
  }

  if (cmd === 'send') {
    if (!f.to || !/^[^:]+:\d+$/.test(f.to)) throw new Error('send needs --to HOST:PORT');
    const [host, port] = [f.to.slice(0, f.to.lastIndexOf(':')), Number(f.to.slice(f.to.lastIndexOf(':') + 1))];
    const mode = f.mode ?? 'fixed', source = f.source ?? 'synthetic';
    if (!['fixed', 'adaptive', 'ramp'].includes(mode)) throw new Error('--mode must be fixed, adaptive or ramp');
    if (!['synthetic', 'stdin'].includes(source)) throw new Error('--source must be synthetic or stdin');
    const bitrate = parseRate(f.bitrate ?? '4M'), max = f.max ? parseRate(f.max) : undefined;
    const tx = new StreamSender({ host, port, streamId: Number(f.stream ?? 1), source, fps: Number(f.fps ?? 30), bitrate, maxBitrate: max, queueFrames: Number(f.queue ?? 4),
      pacing: Number(f.pacing ?? 0.5), mtuPayload: Number(f.mtu ?? P.MAX_PAYLOAD), adaptive: mode === 'adaptive',
      ramp: mode === 'ramp' ? { startBps: parseRate(f['ramp-start'] ?? '1M'), stepBps: parseRate(f['ramp-step'] ?? '1M'), maxBps: max ?? parseRate('30M'), stepMs: Number(f['ramp-ms'] ?? 3000), lossThreshold: Number(f['loss-threshold'] ?? 0.01) } : null });
    tx.on('socket-error', e => console.error(e.message));
    tx.on('stall', e => { if (!json) say(`  ! this PC paused the sender for ${(e.ms / 1000).toFixed(1)} s (not the network; the step is measured again). A click inside this console window does that: press Esc. Power saving and antivirus can too.`); });
    tx.on('adapt', e => { if (!json) say(`  rate -> ${fmt(e.bitrate / 1e6)} Mbit/s (${e.why})`); });
    tx.on('ramp-done', e => {
      if (json) say(JSON.stringify({ event: 'ramp-done', ...e }));
      else say(e.reason === 'no-data' ? 'ramp stopped (no-data): the receiver saw no datagrams during a step. Is `recv` running on the other PC and reachable (firewall, route)? This is not a loss measurement.'
        : e.knee === null && e.delayKnee === null ? `ramp finished (${e.reason}): no sustainable rate found`
        : [`ramp finished (${e.reason}): ${e.knee === null ? 'no loss-free step' : `loss begins above ${fmt(e.knee / 1e6)} Mbit/s`} (application bitrate)`,
           e.delayKnee === null ? '  the round trip did not rise before the loss' : `  the round trip starts to rise above ${fmt(e.delayKnee / 1e6)} Mbit/s: a queue builds before anything is lost`,
           e.recommend === null ? '' : `  suggested sustained rate: about ${fmt(e.recommend / 1e6, 1)} Mbit/s (70 % of the lower of the two)`,
           ...e.results.map(r => `  step ${fmt(r.bps / 1e6, 1).padStart(5)} Mbit/s  loss ${fmt(r.loss * 100, 1).padStart(5)} %  round trip ${r.rttMed === null ? '-' : fmt(r.rttMed, 0)} ms (end of step ${r.rttEnd === null ? '-' : fmt(r.rttEnd, 0)} ms)`)].filter(Boolean).join('\n'));
      stop();
    });
    await tx.start();
    if (source === 'stdin') feedStdin(tx);
    if (!json) say(`sending to ${host}:${port}  mode ${mode}${source === 'stdin' ? '  source stdin' : `  ${fmt(tx.bitrate / 1e6)} Mbit/s ${tx.opts.fps} fps`}`);
    const timer = setInterval(() => {
      const s = tx.stats(), h = tx.history, r = s.lastReport, now = Date.now();
      out({ t: Math.round((now - t0) / 1000), role: 'send', mbit: mbit(h.txWire.at(-1) ?? null), targetMbit: s.targetBps / 1e6, loss: r?.lossFraction ?? null, jitterMs: r ? r.jitterUs / 1000 : null,
            rttMs: s.rttMs, queueDropped: s.queueDropped, sendErrors: s.sendErrors,
            line: `t=${String(Math.round((now - t0) / 1000)).padStart(3)}s  sent ${fmt(mbit(h.txWire.at(-1) ?? null))} Mbit/s  target ${fmt(s.targetBps / 1e6)}  loss ${r?.lossFraction == null ? '-' : fmt(r.lossFraction * 100, 1)} %  jitter ${r ? fmt(r.jitterUs / 1000) : '-'} ms  rtt ${fmt(s.rttMs)} ms  queue drops ${s.queueDropped}${s.reports ? '' : '  (no report from the receiver yet)'}` });
    }, 1000);
    await done; clearInterval(timer); tx.stop(); await printer.close(); return 0;
  }
  if (cmd === 'calibrate') {
    const hp = (v, dflt, name) => { const m = /^([^:]+)(?::(\d+))?$/.exec(v ?? ''); if (!m) throw new Error(`calibrate needs ${name}`); return [m[1], Number(m[2] ?? dflt)]; };
    const to = f.to && /^[^:]+:\d+$/.test(f.to) ? [f.to.slice(0, f.to.lastIndexOf(':')), Number(f.to.slice(f.to.lastIndexOf(':') + 1))] : null;
    if (!to) throw new Error('calibrate needs --to PC2_ADDRESS:PORT (any free UDP port on the other side of the board)');
    const r = await calibrate({ to, board: hp(f.board, 9001, '--board HOST[:STATPORT]'), count: Number(f.count ?? 1000), payload: Number(f.payload ?? 1000), pps: Number(f.pps ?? 500),
      ingress: f.ingress, egress: f.egress, log: m => { if (!json) say(m); } });
    if (json) say(JSON.stringify(r));
    else {
      for (const c of r.checks) say(`${{ pass: 'PASS', warn: 'WARN', fail: 'FAIL' }[c.status]}  ${c.id.padEnd(10)} ${c.text}`);
      say(`\nverdict: ${r.verdict.toUpperCase()}  (${r.sent} sent in ${r.seconds.toFixed(1)} s of board time${r.sendErrors ? `, ${r.sendErrors} send errors on this PC` : ''})`);
    }
    await printer.close();
    return r.verdict === 'fail' ? 1 : 0;
  }
  console.error('usage: cli.mjs send --to HOST:PORT [...]  |  cli.mjs recv --port PORT [...]  |  cli.mjs calibrate --to HOST:PORT --board HOST   (see the header of this file)');
  return 2;
}

if (import.meta.url === `file://${process.argv[1]}` || process.argv[1]?.endsWith('cli.mjs')) {
  main(process.argv.slice(2)).then(c => process.exit(c)).catch(e => { console.error(e.message); process.exit(2); });
}
