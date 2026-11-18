/*
 * bnp-core.js - BNP v1 sender (central side), wire format = rtthread/bnp_proto.h
 *
 * Transport-agnostic: the web page plugs in Web Bluetooth, the host test plugs
 * in the real ble_img_rx.c compiled as a simulated device.
 *
 * Flow per frame:
 *   START (write w/ rsp) -> START_ACK (chunk size, n_chunks, have>0 = resume)
 *   DATA x N (write w/o rsp), every `barrier` chunks one QUERY (write w/ rsp)
 *        -> STATUS: flow-control barrier (EPD-nRF5 style) + progress source
 *   COMMIT -> RESULT ok | INCOMPLETE + missing ranges (resend, commit again)
 *                       | CRC (device cleared bitmap, resend everything)
 *
 * SPDX-License-Identifier: Apache-2.0
 */
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.BNP = factory();
})(typeof self !== 'undefined' ? self : this, function () {
  'use strict';

  const uuid = (x) => 'd133' + x.toString(16).padStart(4, '0') + '-4e41-5649-b1e0-0000d1330000';
  const UUID = { SVC: uuid(1), CTRL: uuid(2), DATA: uuid(3), STAT: uuid(4) };

  const OP = { START: 0x01, QUERY: 0x02, COMMIT: 0x03, ABORT: 0x04, NAV_TEXT: 0x10,
               GET_PARAMS: 0x20, REQ_PHY: 0x21, REQ_CONN: 0x22 };
  const RSP = { START_ACK: 0x81, STATUS: 0x82, RESULT: 0x83, PARAMS: 0xA0, ERROR: 0xEE };
  const ERR = ['ok', 'busy', 'too big', 'bad format', 'no frame', 'incomplete', 'crc',
               'display', 'length'];
  const FMT = { RGB565: 0, JPEG: 1, ICON1BPP: 2, ARGB8888: 3 };
  const LAYER = { MAP: 0, TURN: 1 };
  const NAV = { ROAD: 1, DIST: 2, ETA: 3, DURATION: 4, TURN: 5, SPEED: 6, ACTIVE: 7 };
  const DATA_HDR = 3;

  const CRC_TAB = (() => {
    const t = new Uint32Array(256);
    for (let i = 0; i < 256; i++) {
      let c = i;
      for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1;
      t[i] = c >>> 0;
    }
    return t;
  })();
  function crc32(buf) {
    let c = 0xFFFFFFFF;
    for (let i = 0; i < buf.length; i++) c = CRC_TAB[(c ^ buf[i]) & 0xFF] ^ (c >>> 8);
    return (c ^ 0xFFFFFFFF) >>> 0;
  }

  const u16 = (b, o) => b[o] | (b[o + 1] << 8);
  const u32 = (b, o) => (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0;

  function parse(b) {
    switch (b[0]) {
      case RSP.START_ACK:
        return { rsp: 'start_ack', err: b[1], id: u16(b, 2), chunk: u16(b, 4),
                 n: u16(b, 6), have: u16(b, 8) };
      case RSP.STATUS: {
        const r = { rsp: 'status', id: u16(b, 1), n: u16(b, 3), have: u16(b, 5),
                    contig: u16(b, 7), dup: u32(b, 9), bad: u32(b, 13), ranges: [] };
        for (let i = 0, k = b[17]; i < k; i++) r.ranges.push([u16(b, 18 + 4 * i), u16(b, 20 + 4 * i)]);
        return r;
      }
      case RSP.RESULT: {
        const r = { rsp: 'result', err: b[1], id: u16(b, 2), crc: u32(b, 4), rxMs: u32(b, 8),
                    ranges: [] };
        for (let i = 0, k = b[12]; i < k; i++) r.ranges.push([u16(b, 13 + 4 * i), u16(b, 15 + 4 * i)]);
        return r;
      }
      case RSP.PARAMS:
        return { rsp: 'params', mtu: u16(b, 1), ciMs: u16(b, 3) * 1.25, latency: u16(b, 5),
                 timeoutMs: u16(b, 7) * 10, txPhy: b[9], rxPhy: b[10], dleTx: u16(b, 11),
                 dleRx: u16(b, 13), backend: b[15] };
      case RSP.ERROR:
        return { rsp: 'error', op: b[1], err: b[2] };
      default:
        return { rsp: 'unknown', raw: b };
    }
  }

  function startPacket(f, id) {
    const b = new Uint8Array(23), v = new DataView(b.buffer);
    b[0] = OP.START; v.setUint16(1, id, true); b[3] = f.fmt; b[4] = f.layer;
    v.setUint16(5, f.width, true); v.setUint16(7, f.height, true);
    v.setUint16(9, f.x || 0, true); v.setUint16(11, f.y || 0, true);
    v.setUint32(13, f.data.length, true); v.setUint32(17, f.crc, true);
    v.setUint16(21, f.chunk || 0, true);
    return b;
  }
  const idPacket = (op, id) => Uint8Array.of(op, id & 0xFF, id >> 8);

  function navPacket(fields) {
    const enc = new TextEncoder(), out = [OP.NAV_TEXT];
    for (const [tag, val] of fields) {
      const bytes = typeof val === 'number' ? [val & 0xFF] : Array.from(enc.encode(val)).slice(0, 255);
      out.push(tag, bytes.length, ...bytes);
    }
    return Uint8Array.from(out);
  }

  const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

  class Sender {
    /* io: { writeCtrl(u8) -> Promise, writeData(u8) -> Promise }
     * opts.on(event, data): 'tx' (seq, retx), 'status', 'result', 'log', 'params' */
    constructor(io, opts = {}) {
      this.io = io;
      this.on = opts.on || (() => {});
      this.barrier = opts.barrier || 16;
      this.timeoutMs = opts.timeoutMs || 3000;
      this.waiters = [];
      this.frameId = (Math.random() * 0xFFFF) | 0;
      this.cancelled = false;
    }

    handleNotify(bytes) {
      const m = parse(bytes);
      if (m.rsp === 'status') this.on('status', m);
      if (m.rsp === 'params') this.on('params', m);
      for (const w of this.waiters.slice()) {
        if (w.match(m)) {
          clearTimeout(w.t);
          this.waiters.splice(this.waiters.indexOf(w), 1);
          w.resolve(m);
        }
      }
    }

    linkLost() {
      for (const w of this.waiters.splice(0)) { clearTimeout(w.t); w.reject(new Error('link lost')); }
    }

    wait(match, what) {
      return new Promise((resolve, reject) => {
        const w = { match, resolve, reject };
        w.t = setTimeout(() => {
          this.waiters.splice(this.waiters.indexOf(w), 1);
          reject(new Error('timeout waiting for ' + what));
        }, this.timeoutMs);
        this.waiters.push(w);
      });
    }

    /* register the waiter before writing: the notify can beat the write promise */
    async request(pkt, match, what) {
      const p = this.wait(match, what);
      try { await this.io.writeCtrl(pkt); } catch (e) { p.catch(() => {}); throw e; }
      return p;
    }

    async getParams() {
      return this.request(Uint8Array.of(OP.GET_PARAMS), (m) => m.rsp === 'params', 'params');
    }

    async sendNav(fields) { await this.io.writeCtrl(navPacket(fields)); }

    async writeChunk(id, seq, data, chunk, retx) {
      const off = seq * chunk, pl = data.subarray(off, Math.min(off + chunk, data.length));
      const pkt = new Uint8Array(DATA_HDR + pl.length);
      pkt[0] = id & 0xFF; pkt[1] = seq & 0xFF; pkt[2] = seq >> 8; pkt.set(pl, DATA_HDR);
      for (let tries = 0; ; tries++) {
        try { await this.io.writeData(pkt); break; } catch (e) {
          /* Chrome: "GATT operation already in progress" / queue full: back off */
          if (tries > 50 || /disconnect|link lost/i.test(e.message)) throw e;
          await sleep(4);
        }
      }
      this.on('tx', { seq, retx, bytes: pkt.length });
    }

    async pushChunks(id, seqs, f, chunk, retx) {
      let k = 0;
      for (const seq of seqs) {
        if (this.cancelled) throw new Error('cancelled');
        await this.writeChunk(id, seq, f.data, chunk, retx);
        if (++k % this.barrier === 0)
          await this.request(idPacket(OP.QUERY, id), (m) => m.rsp === 'status' && m.id === id, 'status');
      }
    }

    /* f: { fmt, layer, width, height, x, y, data: Uint8Array }
     * resume: reuse f.id (same id + crc => device keeps what it has) */
    async sendFrame(f, { resume = false } = {}) {
      this.cancelled = false;
      f.crc = f.crc ?? crc32(f.data);
      if (!resume || f.id === undefined) f.id = this.frameId = (this.frameId + 1) & 0xFFFF;
      const id = f.id, t0 = Date.now();
      const stats = { id, rounds: 0, sent: 0, retx: 0, nacks: 0, crcErr: 0 };

      const ack = await this.request(startPacket(f, id),
        (m) => (m.rsp === 'start_ack' && m.id === id) || (m.rsp === 'error' && m.op === OP.START),
        'start ack');
      if (ack.rsp === 'error' || ack.err) throw new Error('START refused: ' + (ERR[ack.err] || ack.err));
      const chunk = f.chunk = ack.chunk;
      stats.n = ack.n; stats.chunk = chunk;
      this.on('start', { id, n: ack.n, chunk, have: ack.have, resumed: ack.have > 0 });

      let todo;
      if (ack.have > 0) {
        const st = await this.request(idPacket(OP.QUERY, id),
          (m) => m.rsp === 'status' && m.id === id, 'status');
        todo = expand(st.ranges, st.contig, ack.n);
        this.on('log', `续传：设备已有 ${ack.have}/${ack.n} 块，补发 ${todo.length} 块`);
      } else {
        todo = [...Array(ack.n).keys()];
      }

      let retx = false;
      for (let round = 0; round < 32; round++) {
        stats.rounds = round + 1;
        await this.pushChunks(id, todo, f, chunk, retx);
        stats.sent += todo.length;
        if (retx) stats.retx += todo.length;

        const res = await this.request(idPacket(OP.COMMIT, id),
          (m) => (m.rsp === 'result' && m.id === id) || (m.rsp === 'error' && m.op === OP.COMMIT),
          'result');
        this.on('result', res);
        if (res.rsp === 'error') throw new Error('COMMIT error: ' + ERR[res.err]);
        if (res.err === 0) {
          stats.ms = Date.now() - t0; stats.rxMs = res.rxMs; stats.bytes = f.data.length;
          return stats;
        }
        retx = true;
        if (res.err === 5) {             /* incomplete: device lists (some of) the holes */
          stats.nacks++;
          todo = expand(res.ranges, null, ack.n);
          if (!todo.length) throw new Error('device reported holes but no ranges');
        } else if (res.err === 6) {      /* crc: bitmap cleared on device, send all again */
          stats.crcErr++;
          todo = [...Array(ack.n).keys()];
        } else {
          throw new Error('frame rejected: ' + (ERR[res.err] || res.err));
        }
      }
      throw new Error('gave up after 32 rounds');
    }

    async abort(id) { this.cancelled = true; try { await this.io.writeCtrl(idPacket(OP.ABORT, id)); } catch (e) {} }
  }

  /* STATUS ranges start at `contig`; RESULT ranges are absolute. Both [first, count]. */
  function expand(ranges, _contig, n) {
    const out = [];
    for (const [first, count] of ranges)
      for (let s = first; s < first + count && s < n; s++) out.push(s);
    return out;
  }

  /* canvas RGBA -> RGB565 little-endian, what disp_sink feeds GE as MPP_FMT_RGB_565 */
  function rgba8888ToRgb565(rgba) {
    const out = new Uint8Array((rgba.length / 4) * 2);
    for (let i = 0, j = 0; i < rgba.length; i += 4, j += 2) {
      const v = ((rgba[i] & 0xF8) << 8) | ((rgba[i + 1] & 0xFC) << 3) | (rgba[i + 2] >> 3);
      out[j] = v & 0xFF; out[j + 1] = v >> 8;
    }
    return out;
  }

  return { UUID, OP, RSP, ERR, FMT, LAYER, NAV, crc32, parse, startPacket, navPacket,
           rgba8888ToRgb565, Sender };
});
