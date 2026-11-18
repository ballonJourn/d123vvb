/*
 * bnp_sender.js - BLE Navi Protocol sender (central side)
 *
 * Runs unchanged in the browser (Web Bluetooth, index.html) and in node
 * (tools/sim/sim.mjs drives it against the real C receiver).
 *
 * Flow control idea taken from tsl0922/EPD-nRF5 html/js/main.js writeImage():
 *   N x writeValueWithoutResponse, then one write *with* response as a barrier
 *   so the OS BLE queue can't silently overflow. Here the barrier is a QUERY,
 *   so each barrier also returns the receiver bitmap (selective NACK).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

export const UUID = {
  svc:  'd1330001-4e41-5649-b1e0-0000d1330000',
  ctrl: 'd1330002-4e41-5649-b1e0-0000d1330000',
  data: 'd1330003-4e41-5649-b1e0-0000d1330000',
  stat: 'd1330004-4e41-5649-b1e0-0000d1330000',
};
export const OP = { START: 1, QUERY: 2, COMMIT: 3, ABORT: 4, NAV_TEXT: 0x10,
                    GET_PARAMS: 0x20, REQ_PHY: 0x21, REQ_CONN: 0x22 };
export const RSP = { START_ACK: 0x81, STATUS: 0x82, RESULT: 0x83, PARAMS: 0xA0, ERROR: 0xEE };
export const FMT = { RGB565: 0, JPEG: 1, ICON1BPP: 2, ARGB8888: 3 };
export const LAYER = { MAP: 0, TURN: 1 };
export const NAV = { ROAD: 1, DIST: 2, ETA: 3, DURATION: 4, TURN: 5, SPEED: 6, ACTIVE: 7 };
export const ERR = ['OK', 'BUSY', 'TOO_BIG', 'BAD_FMT', 'NO_FRAME', 'INCOMPLETE', 'CRC', 'DISPLAY', 'LEN'];

let CRC_T = null;
export function crc32(buf, crc = 0) {
  if (!CRC_T) {
    CRC_T = new Uint32Array(256);
    for (let i = 0; i < 256; i++) {
      let c = i;
      for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1;
      CRC_T[i] = c >>> 0;
    }
  }
  crc = ~crc >>> 0;
  for (let i = 0; i < buf.length; i++) crc = CRC_T[(crc ^ buf[i]) & 0xFF] ^ (crc >>> 8);
  return ~crc >>> 0;
}

export function parseRsp(u8) {
  const dv = new DataView(u8.buffer, u8.byteOffset, u8.byteLength);
  const t = u8[0];
  const ranges = (off, n) => {
    const r = [];
    for (let i = 0; i < n; i++) r.push([dv.getUint16(off + 4 * i, true), dv.getUint16(off + 4 * i + 2, true)]);
    return r;
  };
  switch (t) {
    case RSP.START_ACK: return { t, err: u8[1], id: dv.getUint16(2, true), chunk: dv.getUint16(4, true),
                                 n: dv.getUint16(6, true), have: dv.getUint16(8, true) };
    case RSP.STATUS: return { t, id: dv.getUint16(1, true), n: dv.getUint16(3, true), have: dv.getUint16(5, true),
                              contig: dv.getUint16(7, true), dup: dv.getUint32(9, true), bad: dv.getUint32(13, true),
                              ranges: ranges(18, u8[17]) };
    case RSP.RESULT: return { t, err: u8[1], id: dv.getUint16(2, true), crc: dv.getUint32(4, true),
                              ms: dv.getUint32(8, true), ranges: ranges(13, u8[12]) };
    case RSP.PARAMS: return { t, mtu: dv.getUint16(1, true), ci: dv.getUint16(3, true), lat: dv.getUint16(5, true),
                              sto: dv.getUint16(7, true), txPhy: u8[9], rxPhy: u8[10],
                              txOct: dv.getUint16(11, true), rxOct: dv.getUint16(13, true), backend: u8[15] };
    case RSP.ERROR: return { t, op: u8[1], err: u8[2] };
    default: return { t, raw: u8 };
  }
}

export function navTlv(fields) {
  const enc = new TextEncoder();
  const out = [OP.NAV_TEXT];
  for (const [tag, v] of fields) {
    const b = typeof v === 'number' ? Uint8Array.of(v) : enc.encode(v).slice(0, 255);
    out.push(tag, b.length, ...b);
  }
  return Uint8Array.from(out);
}

/*
 * transport = {
 *   writeCtrl(u8) : Promise  (ATT write request, i.e. with response)
 *   writeData(u8) : Promise  (ATT write command, i.e. without response)
 *   onNotify(cb)             (cb(u8) for every CTRL notification)
 * }
 */
export class BnpSender {
  constructor(transport, { window = 24, timeoutMs = 3000, log = () => {}, onStat = () => {}, onChunk = () => {} } = {}) {
    this.tx = transport;
    this.window = window;          // EPD-nRF5 "interleavedcount"
    this.timeoutMs = timeoutMs;
    this.log = log;
    this.onStat = onStat;
    // onChunk(kind, a, b): 'start' (n) | 'sent' (seq, retx) | 'lost' (first, count) | 'ok' (upto) | 'done'
    this.onChunk = onChunk;
    this.waiters = [];
    this.params = null;
    this.frameId = 1;
    transport.onNotify((u8) => this._rx(u8));
  }

  _rx(u8) {
    const m = parseRsp(u8);
    const i = this.waiters.findIndex((w) => w.types.includes(m.t));
    if (i >= 0) {
      const w = this.waiters.splice(i, 1)[0];
      clearTimeout(w.to);
      w.res(m);
    }
  }

  _wait(types) {
    return new Promise((res, rej) => {
      const w = { types, res };
      w.to = setTimeout(() => {
        this.waiters.splice(this.waiters.indexOf(w), 1);
        rej(new Error('timeout waiting for 0x' + types.map((x) => x.toString(16)).join('/')));
      }, this.timeoutMs);
      this.waiters.push(w);
    });
  }

  async _ctrl(u8, expect) {
    const p = expect ? this._wait(expect) : null;
    await this.tx.writeCtrl(u8);
    return p;
  }

  async getParams() {
    this.params = await this._ctrl(Uint8Array.of(OP.GET_PARAMS), [RSP.PARAMS, RSP.ERROR]);
    return this.params;
  }

  async requestPhy2M() { await this._ctrl(Uint8Array.of(OP.REQ_PHY, 2)); }

  async requestConn(minMs = 15, maxMs = 15, latency = 0, timeoutMs = 4000) {
    const b = new Uint8Array(9), dv = new DataView(b.buffer);
    b[0] = OP.REQ_CONN;
    dv.setUint16(1, Math.round(minMs / 1.25), true);
    dv.setUint16(3, Math.round(maxMs / 1.25), true);
    dv.setUint16(5, latency, true);
    dv.setUint16(7, Math.round(timeoutMs / 10), true);
    await this._ctrl(b);
  }

  async sendNav(fields) { await this._ctrl(navTlv(fields)); }

  _startPkt(f) {
    const b = new Uint8Array(23), dv = new DataView(b.buffer);
    b[0] = OP.START;
    dv.setUint16(1, f.id, true);
    b[3] = f.fmt; b[4] = f.layer;
    dv.setUint16(5, f.width, true); dv.setUint16(7, f.height, true);
    dv.setUint16(9, f.x || 0, true); dv.setUint16(11, f.y || 0, true);
    dv.setUint32(13, f.data.length, true);
    dv.setUint32(17, f.crc, true);
    dv.setUint16(21, f.chunk, true);
    return b;
  }

  _dataPkt(f, seq) {
    const off = seq * f.chunk;
    const pl = f.data.subarray(off, Math.min(off + f.chunk, f.data.length));
    const b = new Uint8Array(3 + pl.length);
    b[0] = f.id & 0xFF; b[1] = seq & 0xFF; b[2] = seq >> 8;
    b.set(pl, 3);
    return b;
  }

  /*
   * Send one frame. `resume` = {id, crc} from an interrupted attempt makes
   * the device keep its bitmap (same frame_id + crc + length).
   */
  async sendFrame({ data, fmt, layer = LAYER.MAP, width = 0, height = 0, x = 0, y = 0, id, chunk }) {
    if (!this.params) await this.getParams();
    const mtu = this.params.mtu || 23;
    const f = {
      data, fmt, layer, width, height, x, y,
      id: id ?? (this.frameId = (this.frameId + 1) & 0xFFFF),
      crc: crc32(data),
      chunk: chunk || (mtu - 3 - 3),
    };
    const st = { id: f.id, bytes: data.length, sent: 0, retx: 0, barriers: 0, t0: Date.now(),
                 have: 0, n: 0, lostReported: 0, dup: 0, resumed: false, done: false };
    const emit = (extra = {}) => this.onStat({ ...st, ...extra, t: Date.now() - st.t0 });

    const ack = await this._ctrl(this._startPkt(f), [RSP.START_ACK, RSP.ERROR]);
    if (ack.t === RSP.ERROR) throw new Error('START rejected: ' + ERR[ack.err]);
    f.chunk = ack.chunk;
    st.n = ack.n;
    this.onChunk('start', ack.n);
    st.have = ack.have;
    const sentOnce = new Uint8Array(ack.n);
    let pending;
    if (ack.have > 0) {
      st.resumed = true;
      this.log(`resume frame ${f.id}: device already has ${ack.have}/${ack.n}`);
      pending = await this._missing(f, sentOnce, st);
    } else {
      pending = Array.from({ length: ack.n }, (_, i) => i);
    }

    for (let round = 0; pending.length && round < 1000; round++) {
      let sinceBarrier = 0;
      for (const seq of pending) {
        await this.tx.writeData(this._dataPkt(f, seq));
        const re = !!sentOnce[seq];
        if (re) st.retx++; else sentOnce[seq] = 1;
        this.onChunk('sent', seq, re);
        if (seq > (st.hiSent ?? -1)) st.hiSent = seq;
        st.sent++;
        if (++sinceBarrier >= this.window) {
          sinceBarrier = 0;
          await this._barrier(f, st);
          emit();
        }
      }
      pending = await this._missing(f, sentOnce, st);
      emit();
    }

    const res = await this._ctrl(Uint8Array.of(OP.COMMIT, f.id & 0xFF, f.id >> 8), [RSP.RESULT, RSP.ERROR]);
    st.done = true;
    if (res.err === 0) this.onChunk('done');
    st.result = ERR[res.err] ?? res.err;
    st.deviceMs = res.ms;
    st.crcOk = res.crc === f.crc;
    emit();
    if (res.err === 6) throw new Error('device CRC mismatch, frame must be resent');
    if (res.err !== 0) throw new Error('COMMIT failed: ' + st.result);
    return st;
  }

  async _barrier(f, st) {
    st.barriers++;
    const s = await this._ctrl(Uint8Array.of(OP.QUERY, f.id & 0xFF, f.id >> 8), [RSP.STATUS, RSP.ERROR]);
    if (s.t === RSP.STATUS) {
      st.have = s.have; st.dup = s.dup;
      // full picture for visualisers: device bitmap summary + highest seq we sent
      this.onChunk('status', s, st.hiSent ?? -1);
    }
    return s;
  }

  /* ask device which chunks are missing. The device lists as many missing
   * ranges as fit in one notification (MTU bound). If they don't add up to
   * n - have, the list was truncated: resend what is listed and ask again
   * (never blindly resend the tail - that caused duplicates after resume). */
  async _missing(f, sentOnce, st) {
    const s = await this._barrier(f, st);
    if (s.t !== RSP.STATUS || s.have >= s.n) return [];
    const out = [];
    for (const [first, cnt] of s.ranges) {
      this.onChunk('lost', first, cnt);
      for (let i = 0; i < cnt; i++) out.push(first + i);
    }
    if (out.length === 0) {
      // ATT_MTU 23: 20-byte notify has no room for ranges after the 18-byte
      // status header. Only `contig` is known -> resend from there (dups ok).
      for (let q = s.contig; q < s.n; q++) out.push(q);
    }
    st.lostReported += out.filter((q) => sentOnce[q]).length;
    st.truncated = out.length < s.n - s.have;
    return out;
  }
}
