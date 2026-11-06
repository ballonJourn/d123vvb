/**
 * The dashboard's stream engine: runs ONE sender or receiver for the browser, samples a history
 * once a second and, for a sender, lines its numbers up with the board's counters to say where the
 * path loses datagrams (locate.mjs).
 *
 *   start({ role: 'send', host, port, mode: 'fixed' | 'adaptive' | 'ramp', bitrate, fps, ... })
 *   start({ role: 'recv', port, reportMs, ... })
 *   stop(); state()
 *
 * peekBoard() returns the newest dsstat sample the dashboard has (d133.boardPeek), or null.
 */
import { StreamSender } from './sender.mjs';
import { StreamReceiver } from './receiver.mjs';
import { locateLoss, meanLast, pickInterfaces } from './locate.mjs';
import * as P from './protocol.mjs';

const HISTORY = 180, EVENTS = 30, WINDOW = 5;
const bad = message => Object.assign(new Error(message), { status: 400 });
const num = (v, name, lo, hi, dflt) => {
  if (v === undefined || v === null || v === '') { if (dflt === undefined) throw bad(`${name} is required`); return dflt; }
  const n = Number(v);
  if (!Number.isFinite(n) || n < lo || n > hi) throw bad(`${name} must be between ${lo} and ${hi}`);
  return n;
};
const HOST_RE = /^[A-Za-z0-9]([A-Za-z0-9.-]{0,251}[A-Za-z0-9])?$/;

export function validate(p) {
  if (p?.role === 'send') {
    const host = String(p.host ?? '').trim();
    if (!HOST_RE.test(host)) throw bad('host must be an IP address or a host name');
    const mode = p.mode ?? 'fixed';
    if (!['fixed', 'adaptive', 'ramp'].includes(mode)) throw bad('mode must be fixed, adaptive or ramp');
    const o = { role: 'send', host, port: num(p.port, 'port', 1, 65535), mode, streamId: num(p.streamId, 'streamId', 0, 65535, 1),
      fps: num(p.fps, 'fps', 1, 120, 30), bitrate: num(p.bitrate, 'bitrate', 5e4, 1e9, 4e6),
      queueFrames: num(p.queueFrames, 'queueFrames', 1, 64, 4), pacing: num(p.pacing, 'pacing', 0, 1, 0.5) };
    if (mode === 'ramp') {
      o.ramp = { startBps: num(p.startBps, 'startBps', 5e4, 1e9, 1e6), stepBps: num(p.stepBps, 'stepBps', 1e4, 1e9, 1e6), maxBps: num(p.maxBps, 'maxBps', 1e5, 1e9, 2e7),
                 stepMs: num(p.stepMs, 'stepMs', 500, 60000, 3000), lossThreshold: num(p.lossThreshold, 'lossThreshold', 0, 1, 0.01) };
      if (o.ramp.maxBps < o.ramp.startBps) throw bad('maxBps must not be below startBps');
    }
    if (mode === 'adaptive') o.maxBitrate = num(p.maxBps, 'maxBps', 1e5, 1e9, Math.max(o.bitrate, 2e7));
    return o;
  }
  if (p?.role === 'recv') return { role: 'recv', port: num(p.port, 'port', 1, 65535), reportMs: num(p.reportMs, 'reportMs', 50, 5000, 500),
                                  frameQueue: num(p.frameQueue, 'frameQueue', 1, 256, 8), streamId: null };
  throw bad('role must be send or recv');
}

export function createStreamEngine({ peekBoard = () => null, ingressIf, egressIf } = {}) {
  let run = null;

  const note = (r, text) => { r.events.push({ t: Date.now(), text }); if (r.events.length > EVENTS) r.events.shift(); };
  const mbit = v => `${(v / 1e6).toFixed(2)} Mbit/s`;

  function sampleReceiver(r) {
    const s = r.rx.stats(), now = performance.now();
    if (!s) return;
    const p = r.prev ?? { t: now, packets: 0, bytes: 0, framesOk: 0, framesDropped: 0 };
    const dt = (now - p.t) / 1000;
    r.prev = { t: now, packets: s.packets, bytes: s.bytes, framesOk: s.framesOk, framesDropped: s.framesDropped };
    if (!(dt > 0)) return;
    const h = r.history, rep = s.lastReport;
    h.t.push(Date.now()); h.rxWire.push(P.wireBytes(s.bytes - p.bytes, s.packets - p.packets) / dt); h.rxPps.push((s.packets - p.packets) / dt);
    h.loss.push(rep ? rep.lossFraction : null); h.jitterMs.push(s.jitterUs / 1000);
    h.framesOk.push((s.framesOk - p.framesOk) / dt); h.framesDropped.push((s.framesDropped - p.framesDropped) / dt);
    if (h.t.length > HISTORY) for (const k of Object.keys(h)) h[k].shift();
  }

  function start(raw) {
    if (run) throw Object.assign(new Error(`a ${run.params.role === 'send' ? 'sender' : 'receiver'} is already running`), { status: 409 });
    const params = validate(raw);
    const r = { params, startedAt: Date.now(), error: null, events: [], rampResult: null, prev: null, history: { t: [], rxWire: [], rxPps: [], loss: [], jitterMs: [], framesOk: [], framesDropped: [] } };
    run = r;
    if (params.role === 'send') {
      const tx = new StreamSender({
        host: params.host, port: params.port, streamId: params.streamId, fps: params.fps, bitrate: params.bitrate, queueFrames: params.queueFrames, pacing: params.pacing,
        adaptive: params.mode === 'adaptive', maxBitrate: params.maxBitrate, ramp: params.ramp ?? null,
      });
      r.tx = tx;
      tx.on('adapt', e => note(r, `rate ${mbit(e.bitrate)} (${e.why})`));
      tx.on('ramp-step', e => note(r, `ramp step: ${mbit(e.bitrate)}`));
      tx.on('ramp-done', e => { r.rampResult = e; note(r, e.reason === 'no-data' ? 'ramp stopped (no-data): the receiver saw nothing during a step, so nothing was measured'
        : e.reason === 'max' ? `ramp finished: no loss up to ${mbit(e.lossFreeTo)}, the highest rate tested, so the limit is higher${e.recommend === null ? '' : `; a safe sustained rate is at least ${mbit(e.recommend)}`}`
        : e.knee === null && e.delayKnee === null ? `ramp finished (${e.reason}): no sustainable rate found` : `ramp finished (${e.reason}): ${e.knee === null ? 'no loss-free step' : `loss begins above ${mbit(e.knee)}`}${e.delayKnee === null ? '' : `; the round trip starts to rise above ${mbit(e.delayKnee)}`}${e.recommend === null ? '' : `; suggested sustained rate about ${mbit(e.recommend)}`}`); });
      tx.on('stall', e => note(r, `this PC paused the sender for ${(e.ms / 1000).toFixed(1)} s (not the network); the measurement restarts`));
      tx.on('socket-error', e => { r.error = e.message; });
      tx.on('send-error', e => { r.error = `send failed: ${e.code ?? e.message}`; });
      tx.start().catch(e => { r.error = e.message; });
      note(r, `sending to ${params.host}:${params.port} (${params.mode}, ${mbit(params.ramp?.startBps ?? params.bitrate)})`);
    } else {
      const rx = new StreamReceiver({ port: params.port, host: '0.0.0.0', reportMs: params.reportMs, frameQueue: params.frameQueue, onFrame: f => { r.frameBytes = (r.frameBytes ?? 0) + f.data.length; } });
      r.rx = rx;
      rx.on('error', e => { r.error = e.message; });
      rx.start().then(port => note(r, `listening on UDP ${port}`)).catch(e => { r.error = e.message; });
    }
    r.timer = setInterval(() => { if (r.rx) sampleReceiver(r); }, 1000);
    return state();
  }

  function stop() {
    if (!run) return state();
    clearInterval(run.timer); run.tx?.stop(); run.rx?.stop();
    run = null;
    return state();
  }

  /** The four packet rates of the path, from the sender's numbers and the board's last samples. */
  function pathOf(r) {
    const tx = r.tx, h = tx.history;
    const board = peekBoard(), bh = board?.history?.ifs ?? {};
    const { ingress: inName, egress: outName, ambiguous } = pickInterfaces(board?.ifs, { ingress: ingressIf, egress: egressIf });
    const rate = (name, key) => meanLast(bh[name]?.[key], WINDOW);
    const sum = (a, b) => a == null ? null : a + (b ?? 0);
    const e0Rx = inName ? sum(rate(inName, 'rx_pps'), rate(inName, 'rx_drop')) : null;     // arrived on the Ethernet side = accepted + refused
    const w1Tx = outName ? rate(outName, 'tx_pps') : null;
    const m = { tx: meanLast(h.txPps, WINDOW), e0Rx: board?.ok ? e0Rx : null, w1Tx: board?.ok ? w1Tx : null, rx: meanLast(h.rxPps, WINDOW) };
    return { window: WINDOW, ingressIf: inName ?? ingressIf ?? 'e?', egressIf: outName ?? egressIf ?? 'w?', ambiguous, rates: m, located: locateLoss(m),
             boardRefused: { e0RxDrop: inName ? rate(inName, 'rx_drop') : null, w1TxErr: outName ? rate(outName, 'tx_err') : null } };
  }

  function state() {
    if (!run) return { role: 'idle', params: null, error: null, events: [], stats: null, history: null, path: null };
    const r = run, send = !!r.tx;
    return {
      role: send ? 'send' : 'recv', params: r.params, startedAt: r.startedAt, error: r.error, events: r.events,
      stats: send ? r.tx.stats() : { role: 'recv', ...(r.rx.stats() ?? { packets: 0 }), listening: r.rx.port, frameBytes: r.frameBytes ?? 0, malformed: r.rx.malformed },
      history: send ? r.tx.history : r.history, rampResult: r.rampResult, path: send ? pathOf(r) : null,
    };
  }

  return { start, stop, state, validate };
}
