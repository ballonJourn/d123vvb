/**
 * Stream sender (runs on PC1).
 *
 *  - Frames come from the application (push) or from a synthetic generator (fps x frame size from
 *    the target bitrate). Each frame is cut into fragments of at most MAX_PAYLOAD bytes and sent
 *    as UDP datagrams carrying seq, frame id, fragment index/count, capture time and send delay.
 *  - Frames wait in a bounded queue; when the sender cannot keep up the OLDEST frame is dropped
 *    (queueDropped). Nothing is ever retransmitted.
 *  - The fragments of a frame are spread over `pacing` x one frame interval instead of leaving in
 *    one burst, because a burst is what overflows the D133's forwarding queue.
 *  - A monitor notices when THIS process was paused (a click inside the Windows console, power saving,
 *    antivirus): the sender sent nothing meanwhile and the reports that piled up are handled late, which would
 *    show up as a huge round trip time, as "no reports" and as a distorted rate-limit step. While the process was
 *    recently late those reports do not feed the round trip, adaptation or the ramp, the ramp step is measured
 *    again, and a 'stall' event says how long the pause was.
 *  - The receiver's REPORTs feed (a) the round trip time, measured on this machine's clock alone
 *    from the echoed send time, (b) optionally AdaptiveRate (lower the rate on loss, raise it on
 *    clean reports, back off when the reports stop) or (c) RampSearch (raise it step by step to
 *    find where loss begins).
 *
 * Bitrate always means application bits per second: the frame payload, without any header.
 */
import dgram from 'node:dgram';
import { EventEmitter } from 'node:events';
import * as P from './protocol.mjs';
import { BoundedQueue } from './queue.mjs';
import { AdaptiveRate } from './adapt.mjs';
import { RampSearch } from './ramp.mjs';

const HISTORY = 180;
const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));

export class StreamSender extends EventEmitter {
  constructor({ host, port, streamId = 1, localPort = 0, source = 'synthetic', fps = 30, bitrate = 4e6, minBitrate = 2.5e5, maxBitrate,
                queueFrames = 4, pacing = 0.5, frameIntervalUs = null, mtuPayload = P.MAX_PAYLOAD, keyInterval = 30, seqStart = 0,
                adaptive = false, ramp = null, staleMs = 1500, clock = P.nowUs } = {}) {
    super();
    if (!host || !port) throw new TypeError('host and port are required');
    if (mtuPayload < 1 || mtuPayload > P.MAX_PAYLOAD) throw new RangeError(`mtuPayload must be 1..${P.MAX_PAYLOAD}`);
    if (adaptive && ramp) throw new TypeError('adaptive and ramp exclude each other');
    this.opts = { host, port, streamId, localPort, source, fps, queueFrames, pacing, frameIntervalUs, mtuPayload, keyInterval, staleMs };
    this.clock = clock;
    this.maxBitrate = ramp ? Math.max(ramp.maxBps, bitrate) : (maxBitrate ?? Math.max(bitrate, 2e7));
    this.minBitrate = Math.min(minBitrate, bitrate);
    this.bitrate = clamp(ramp ? ramp.startBps : bitrate, this.minBitrate, this.maxBitrate);
    this.adapt = adaptive ? new AdaptiveRate({ start: this.bitrate, min: this.minBitrate, max: this.maxBitrate, staleMs }) : null;
    this.ramp = ramp ? new RampSearch(ramp) : null;
    this.queue = new BoundedQueue(queueFrames);
    this.seq = seqStart >>> 0; this.frameId = 0; this.inflight = null;
    this.c = { txPackets: 0, txBytes: 0, txPayload: 0, txFrames: 0, sendErrors: 0, pushed: 0 };
    this.lastReport = null; this.lastReportAtMs = null; this.reports = 0; this.rttUs = null; this.rampDoneEmitted = false;
    this.history = { t: [], txWire: [], rxWire: [], txPps: [], rxPps: [], loss: [], jitterMs: [], rttMs: [], target: [] };
    this.prevSample = null; this.running = false;
    this.lag = { last: 0, until: 0, resumeAt: 0, stalls: 0, maxMs: 0 };
    this.socket = null; this.timers = { produce: null, send: null, watch: null, sample: null, lag: null };
    this.interval = { emaUs: 1e6 / fps };
  }

  start() {
    return new Promise((resolve, reject) => {
      const sock = dgram.createSocket('udp4');
      this.socket = sock;
      sock.on('message', buf => this.handleReport(buf));
      sock.on('error', e => this.emit('socket-error', e));
      sock.once('error', reject);
      sock.bind(this.opts.localPort, () => {
        sock.off('error', reject);
        try { sock.setSendBufferSize(4 * 1024 * 1024); } catch { /* keep the OS default */ }
        this.running = true; this.startedAtMs = performance.now();
        this.prevSample = { t: this.startedAtMs, ...this.counters() };
        this.ramp?.begin(this.startedAtMs);
        this.lag.last = performance.now();
        this.timers.lag = setInterval(() => this.checkLag(), 100);
        this.timers.watch = setInterval(() => this.watch(), 250);
        this.timers.sample = setInterval(() => this.sample(), 1000);
        if (this.opts.source === 'synthetic') this.produce(this.startedAtMs);
        resolve(sock.address().port);
      });
    });
  }

  stop() {
    this.running = false;
    for (const k of Object.keys(this.timers)) { clearTimeout(this.timers[k]); clearInterval(this.timers[k]); this.timers[k] = null; }
    const s = this.socket; this.socket = null;
    if (s) try { s.close(); } catch { /* already closed */ }
  }

  setBitrate(bps) { this.bitrate = clamp(bps, this.minBitrate, this.maxBitrate); this.emit('rate', this.bitrate); return this.bitrate; }

  /** An application frame (any size). Returns how many older queued frames were dropped to make room. */
  push(data, { key = false, captureTs = this.clock() } = {}) {
    if (!Buffer.isBuffer(data)) throw new TypeError('frame must be a Buffer');
    this.c.pushed++;
    const now = performance.now();
    if (this.lastPushMs) this.interval.emaUs += ((now - this.lastPushMs) * 1000 - this.interval.emaUs) / 8;
    this.lastPushMs = now;
    const dropped = this.queue.push({ data, key, captureTs });
    this.kick();
    return dropped;
  }

  // ----------------------------------------------------------------- synthetic source
  produce(due) {
    if (!this.running) return;
    const bytes = Math.max(1, Math.round(this.bitrate / 8 / this.opts.fps));
    const data = Buffer.allocUnsafe(bytes).fill(this.frameId & 0xff);
    this.push(data, { key: this.frameId % this.opts.keyInterval === 0 });
    const next = due + 1000 / this.opts.fps, now = performance.now();
    this.timers.produce = setTimeout(() => this.produce(Math.max(next, now - 1000 / this.opts.fps)), Math.max(0, next - now));
  }

  // ----------------------------------------------------------------- sending
  /** Start the next frame when none is being sent. */
  kick() {
    if (this.inflight || !this.running) return;
    const frame = this.queue.shift();
    if (!frame) return;
    const count = Math.max(1, Math.ceil(frame.data.length / this.opts.mtuPayload));
    const frameUs = this.opts.frameIntervalUs ?? (this.opts.source === 'synthetic' ? 1e6 / this.opts.fps : this.interval.emaUs);
    this.inflight = { frame, id: this.frameId++ >>> 0, count, next: 0, t0: this.clock(), spreadUs: this.opts.pacing * frameUs };
    this.sendDue();
  }

  sendDue() {
    const f = this.inflight;
    if (!f || !this.running) return;
    const now = this.clock();
    while (f.next < f.count && f.t0 + (f.next / f.count) * f.spreadUs <= now) this.sendFragment(f, f.next++);
    if (f.next >= f.count) { this.inflight = null; this.c.txFrames++; this.kick(); return; }
    const wait = (f.t0 + (f.next / f.count) * f.spreadUs - now) / 1000;
    this.timers.send = setTimeout(() => this.sendDue(), Math.max(0, wait));
  }

  sendFragment(f, idx) {
    const { mtuPayload, streamId, host, port } = this.opts;
    const payload = f.frame.data.subarray(idx * mtuPayload, (idx + 1) * mtuPayload);
    const buf = P.encodeData({ streamId, flags: f.frame.key ? P.FLAG_KEY : 0, seq: this.seq, frameId: f.id, fragIdx: idx, fragCount: f.count,
                               captureTs: f.frame.captureTs, sendDelayUs: Math.max(0, this.clock() - f.frame.captureTs), payload });
    this.seq = (this.seq + 1) >>> 0;
    this.c.txPackets++; this.c.txBytes += buf.length; this.c.txPayload += payload.length;
    this.socket.send(buf, port, host, err => { if (err) { this.c.sendErrors++; this.emit('send-error', err); } });
  }

  // ----------------------------------------------------------------- feedback
  handleReport(buf, at = this.clock()) {
    const r = P.decode(buf);
    if (!r || r.type !== 'report' || r.streamId !== this.opts.streamId) return;
    const nowMs = performance.now();
    if (r.echoSendTs > 0 && nowMs >= this.lag.until) {
      const rtt = at - r.echoSendTs - r.echoDelayUs;
      if (rtt >= 0) this.rttUs = this.rttUs === null ? rtt : this.rttUs + (rtt - this.rttUs) / 4;   // the first sample often includes ARP resolution; a quarter weight forgets it in a few reports
    }
    this.reports++; this.lastReportAtMs = nowMs;
    this.lastReport = { ...r, lossFraction: r.expected > 0 ? r.lost / r.expected : null, at: nowMs };   // null: the receiver saw nothing in that interval
    this.emit('report', this.lastReport);
    if (nowMs < this.lag.until) return;                    // this process was just late: what the report describes is distorted
    if (this.adapt) { const before = this.bitrate; this.setBitrate(this.adapt.onReport(r)); if (this.bitrate !== before) this.emit('adapt', { bitrate: this.bitrate, why: this.adapt.last }); }
    if (this.ramp) this.stepRamp(this.ramp.onReport(r, nowMs));
  }

  stepRamp(rate) {
    if (rate !== this.bitrate) { this.setBitrate(rate); this.emit('ramp-step', { bitrate: this.bitrate, results: this.ramp.results }); }
    if (this.ramp.done && !this.rampDoneEmitted) {
      this.rampDoneEmitted = true;
      this.emit('ramp-done', { knee: this.ramp.knee, reason: this.ramp.reason, results: this.ramp.results });
    }
  }

  /** Every 100 ms: how late was this timer? A big lag means the whole process was paused, not the network. */
  checkLag() {
    const now = performance.now(), late = now - this.lag.last - 100;
    this.lag.last = now;
    this.lag.maxMs = Math.max(this.lag.maxMs * 0.9, late);
    if (late > 50) this.lag.until = now + 1000;           // reports handled around now describe the pause, not the path
    if (late > 500) {
      this.lag.stalls++; this.lag.resumeAt = now;
      this.ramp?.invalidate(now);                         // measure the step again
      this.emit('stall', { ms: Math.round(late) });
    }
  }

  /** Called every 250 ms: no reports means the receiver or the path is gone. */
  watch() {
    const now = performance.now(), age = now - Math.max(this.lastReportAtMs ?? this.startedAtMs, this.lag.resumeAt);   // a pause of this process is not silence
    if (this.adapt) { const before = this.bitrate; this.setBitrate(this.adapt.onSilence(age, now)); if (this.bitrate !== before) this.emit('adapt', { bitrate: this.bitrate, why: this.adapt.last }); }
    if (this.ramp) { this.ramp.onSilence(now); this.stepRamp(this.ramp.rate); }
  }

  // ----------------------------------------------------------------- numbers
  counters() { return { packets: this.c.txPackets, bytes: this.c.txBytes, payload: this.c.txPayload, frames: this.c.txFrames }; }

  /** Once a second: rates for the history the dashboard draws. */
  sample() {
    const now = performance.now(), cur = this.counters(), p = this.prevSample, dt = (now - p.t) / 1000;
    this.prevSample = { t: now, ...cur };
    if (!(dt > 0)) return;
    const txWire = P.wireBytes(cur.bytes - p.bytes, cur.packets - p.packets) / dt;
    const r = this.lastReport, fresh = r && now - r.at < 3000 && r.intervalMs > 0;
    const rxWire = fresh ? P.wireBytes(r.recvBytes, r.recvPackets) / (r.intervalMs / 1000) : null;
    const h = this.history;
    h.t.push(Date.now()); h.txWire.push(txWire); h.rxWire.push(rxWire); h.loss.push(fresh ? r.lossFraction : null);
    h.txPps.push((cur.packets - p.packets) / dt); h.rxPps.push(fresh ? r.recvPackets / (r.intervalMs / 1000) : null);
    h.jitterMs.push(fresh ? r.jitterUs / 1000 : null); h.rttMs.push(this.rttUs === null ? null : this.rttUs / 1000); h.target.push(this.bitrate);
    if (h.t.length > HISTORY) for (const k of Object.keys(h)) h[k].shift();
  }

  stats() {
    const r = this.lastReport, now = performance.now();
    return {
      role: 'send', running: this.running, target: `${this.opts.host}:${this.opts.port}`, streamId: this.opts.streamId,
      targetBps: this.bitrate, minBps: this.minBitrate, maxBps: this.maxBitrate, fps: this.opts.fps,
      ...this.c, queued: this.queue.length, queueDropped: this.queue.dropped,
      rttMs: this.rttUs === null ? null : this.rttUs / 1000, reports: this.reports, stalls: this.lag.stalls,
      reportAgeMs: this.lastReportAtMs === null ? null : Math.round(now - this.lastReportAtMs),
      lastReport: r ? { ...r } : null,
      adapt: this.adapt ? { last: this.adapt.last, hold: this.adapt.hold } : null,
      ramp: this.ramp ? { done: this.ramp.done, knee: this.ramp.knee, reason: this.ramp.reason, results: this.ramp.results, rate: this.ramp.rate } : null,
    };
  }
}
