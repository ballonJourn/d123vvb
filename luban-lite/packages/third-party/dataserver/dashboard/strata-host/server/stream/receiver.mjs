/**
 * Stream receiver (runs on PC2).
 *
 *  - Counts loss and reordering exactly from the sequence numbers: a hole that is filled later is
 *    "reordered", a hole that stays open is "lost". Duplicates are counted and ignored.
 *  - Reassembles frames from their fragments. An incomplete frame is DROPPED THE MOMENT a newer
 *    frame shows up (tolerateFrames = 0): nothing waits, nothing is retransmitted. A frame that is
 *    still incomplete after frameTimeoutMs is dropped as well, so the last frame cannot linger.
 *  - Completed frames wait in a bounded queue for the application; when it is full the OLDEST
 *    frame is discarded (queueDropped), so a slow consumer costs old data, never delay.
 *  - Every reportMs it sends a REPORT back to the sender: what arrived, what was lost, jitter.
 *
 * Jitter is RFC 3550 interarrival jitter on the network transit time of each new-highest datagram,
 * transit = arrival time (receiver clock) - send time (sender clock). The two clocks differ by a
 * constant that cancels in the difference of two transits.
 */
import dgram from 'node:dgram';
import { EventEmitter } from 'node:events';
import * as P from './protocol.mjs';
import { BoundedQueue } from './queue.mjs';

const sdelta = (a, b) => (a - b) | 0;        // a - b on a wrapping u32 axis, signed

class StreamState {
  constructor(streamId, o) {
    Object.assign(this, {
      streamId, peer: null, started: false,
      base: 0, maxExt: 0, ring: new Float64Array(o.window), unique: 0,
      packets: 0, bytes: 0, reordered: 0, duplicates: 0, tooLate: 0, lateFragments: 0, inconsistent: 0,
      framesOk: 0, framesDropped: 0, queueDropped: 0,
      hasFrame: false, newest: 0, open: new Map(), out: new BoundedQueue(o.frameQueue),
      jitter: 0, prevTransit: null, lastArrival: 0, lastSendTs: 0, reportSeq: 0, lastReport: null,
      prev: null,
    });
  }
}

export class StreamReceiver extends EventEmitter {
  constructor({ port = 5600, host = '0.0.0.0', reportMs = 500, frameQueue = 8, maxOpenFrames = 8, frameTimeoutMs = 200,
                tolerateFrames = 0, window = 4096, clock = P.nowUs, onFrame = null } = {}) {
    super();
    Object.assign(this, { port, host, reportMs, frameQueue, maxOpenFrames, frameTimeoutMs, tolerateFrames, window, clock, onFrame });
    this.streams = new Map();
    this.malformed = 0;
    this.socket = null; this.timer = null; this.drainScheduled = false;
  }

  start() {
    return new Promise((resolve, reject) => {
      const sock = dgram.createSocket('udp4');
      this.socket = sock;
      sock.on('message', (buf, rinfo) => this.handlePacket(buf, rinfo));
      sock.on('error', e => this.emit('error', e));
      sock.once('error', reject);
      sock.bind(this.port, this.host, () => {
        sock.off('error', reject);
        try { sock.setRecvBufferSize(4 * 1024 * 1024); } catch { /* the OS keeps its default */ }
        this.port = sock.address().port;
        this.timer = setInterval(() => this.tick(), this.reportMs);
        resolve(this.port);
      });
    });
  }

  stop() {
    clearInterval(this.timer); this.timer = null;
    const s = this.socket; this.socket = null;
    if (s) try { s.close(); } catch { /* already closed */ }
  }

  state(streamId, rinfo) {
    let st = this.streams.get(streamId);
    if (!st) { st = new StreamState(streamId, this); this.streams.set(streamId, st); }
    if (rinfo) st.peer = { address: rinfo.address, port: rinfo.port };
    return st;
  }

  /** Classifies a sequence number against the window: 'new' | 'reordered' | 'duplicate' | 'toolate'. */
  track(st, seq) {
    const W = this.window, mark = e => { st.ring[e % W] = e + 1; };
    if (!st.started) { st.started = true; st.base = st.maxExt = seq; mark(seq); st.unique = 1; return 'new'; }
    const delta = (seq - (st.maxExt >>> 0)) | 0;
    if (delta > 0) { st.maxExt += delta; mark(st.maxExt); st.unique++; return 'new'; }
    const ext = st.maxExt + delta;
    if (st.maxExt - ext >= W) { st.tooLate++; return 'toolate'; }
    if (st.ring[ext % W] === ext + 1) { st.duplicates++; return 'duplicate'; }
    mark(ext); st.unique++; st.reordered++;
    return 'reordered';
  }

  /** Feed one datagram. `at` is the arrival time in microseconds (tests pass their own). */
  handlePacket(buf, rinfo, at = this.clock()) {
    const d = P.decode(buf);
    if (!d) { this.malformed++; return; }
    if (d.type !== 'data') return;
    const st = this.state(d.streamId, rinfo);
    st.packets++; st.bytes += buf.length;
    const cls = this.track(st, d.seq);
    if (cls === 'new') {
      const sendTs = d.captureTs + d.sendDelayUs, transit = at - sendTs;
      if (st.prevTransit !== null) st.jitter += (Math.abs(transit - st.prevTransit) - st.jitter) / 16;
      st.prevTransit = transit;
      st.lastArrival = at; st.lastSendTs = sendTs;
    }
    if (cls === 'duplicate' || cls === 'toolate') return;
    this.assemble(st, d, at);
  }

  dropOpen(st, id) { st.open.delete(id); st.framesDropped++; }

  assemble(st, d, at) {
    const fid = d.frameId;
    if (!st.hasFrame) { st.hasFrame = true; st.newest = fid; }
    else {
      const fd = sdelta(fid, st.newest);
      if (fd > 0) {                                   // a newer frame began: give up on every older open one
        st.newest = fid;
        for (const id of [...st.open.keys()]) if (sdelta(id, st.newest) < -this.tolerateFrames) this.dropOpen(st, id);
      } else if (-fd > this.tolerateFrames) { st.lateFragments++; return; }   // straggler of a frame already given up
    }
    let f = st.open.get(fid);
    if (!f) {
      f = { frameId: fid, fragCount: d.fragCount, parts: new Array(d.fragCount), got: 0, key: false, captureTs: d.captureTs, firstAt: at };
      st.open.set(fid, f);
      while (st.open.size > this.maxOpenFrames) this.dropOpen(st, st.open.keys().next().value);   // oldest first
    }
    if (d.fragCount !== f.fragCount) { st.inconsistent++; return; }
    if (f.parts[d.fragIdx] === undefined) {
      f.parts[d.fragIdx] = Buffer.from(d.payload);
      f.got++; f.key ||= d.key;
    }
    if (f.got === f.fragCount) {
      st.open.delete(fid); st.framesOk++;
      const frame = { streamId: st.streamId, frameId: fid, key: f.key, captureTs: f.captureTs, fragments: f.fragCount,
                      data: Buffer.concat(f.parts), firstAt: f.firstAt, lastAt: at };
      st.queueDropped += st.out.push(frame);
      this.emit('frame-ready', st.streamId);
      this.scheduleDrain();
    }
  }

  scheduleDrain() {
    if (!this.onFrame || this.drainScheduled) return;
    this.drainScheduled = true;
    setImmediate(() => {
      this.drainScheduled = false;
      for (let f = this.takeFrame(); f; f = this.takeFrame()) this.onFrame(f);
    });
  }

  /** Oldest completed frame, or null. */
  takeFrame(streamId) {
    for (const st of streamId === undefined ? this.streams.values() : [this.streams.get(streamId)].filter(Boolean)) {
      const f = st.out.shift();
      if (f) return f;
    }
    return null;
  }

  /** Drops open frames that waited longer than frameTimeoutMs. */
  sweep(at = this.clock()) {
    for (const st of this.streams.values())
      for (const [id, f] of [...st.open]) if (at - f.firstAt > this.frameTimeoutMs * 1000) this.dropOpen(st, id);
  }

  /** Builds (and sends) the REPORT of every active stream. Returns the reports. */
  tick(at = this.clock()) {
    this.sweep(at);
    const out = [];
    for (const st of this.streams.values()) {
      if (!st.started) continue;
      const p = st.prev ?? { t: at, maxExt: st.base - 1, unique: 0, packets: 0, bytes: 0, reordered: 0, duplicates: 0, framesOk: 0, framesDropped: 0, queueDropped: 0 };
      const expected = st.maxExt - p.maxExt, received = st.unique - p.unique;
      const r = {
        streamId: st.streamId, reportSeq: st.reportSeq++, intervalMs: Math.round((at - p.t) / 1000),
        expected, received, lost: Math.max(0, expected - received),
        reordered: st.reordered - p.reordered, duplicates: st.duplicates - p.duplicates, jitterUs: Math.round(st.jitter),
        framesOk: st.framesOk - p.framesOk, framesDropped: st.framesDropped - p.framesDropped, queueDropped: st.queueDropped - p.queueDropped,
        recvPackets: st.packets - p.packets, recvBytes: st.bytes - p.bytes,
        highestSeq: st.maxExt >>> 0, echoSendTs: st.lastSendTs, echoDelayUs: Math.max(0, at - st.lastArrival),
      };
      st.prev = { t: at, maxExt: st.maxExt, unique: st.unique, packets: st.packets, bytes: st.bytes, reordered: st.reordered,
                  duplicates: st.duplicates, framesOk: st.framesOk, framesDropped: st.framesDropped, queueDropped: st.queueDropped };
      st.lastReport = { ...r, lossFraction: r.expected > 0 ? r.lost / r.expected : 0, at };
      if (st.peer && this.socket) this.socket.send(P.encodeReport(r), st.peer.port, st.peer.address, () => {});
      this.emit('report', st.lastReport);
      out.push(r);
    }
    return out;
  }

  /** Cumulative numbers of one stream (the first one when no id is given). */
  stats(streamId) {
    const st = streamId === undefined ? this.streams.values().next().value : this.streams.get(streamId);
    if (!st) return null;
    const expected = st.started ? st.maxExt - st.base + 1 : 0;
    return {
      streamId: st.streamId, peer: st.peer, packets: st.packets, bytes: st.bytes, unique: st.unique, expected,
      lost: Math.max(0, expected - st.unique), lossFraction: expected > 0 ? Math.max(0, expected - st.unique) / expected : 0,
      reordered: st.reordered, duplicates: st.duplicates, tooLate: st.tooLate, lateFragments: st.lateFragments,
      framesOk: st.framesOk, framesDropped: st.framesDropped, queueDropped: st.queueDropped, queued: st.out.length,
      openFrames: st.open.size, jitterUs: Math.round(st.jitter), malformed: this.malformed, lastReport: st.lastReport,
    };
  }
}
