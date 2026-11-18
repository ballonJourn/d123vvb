/*
 * sim_link.js - "Simulate (no board)": a fake BLE link whose far end is the
 * firmware receiver firmware/ble_navi/ble_img_rx.c compiled to WebAssembly
 * (tools/wasm). Implements the same transport interface as the Web Bluetooth
 * link, so BnpSender cannot tell the difference.
 * SPDX-License-Identifier: Apache-2.0
 */
import { RSP } from './bnp_sender.js';

export class SimLink {
  static async create(wasmSource, { mtu = 247, loss = 0.02, kbps = 60, maxFrame = 1024 * 600 * 4 } = {}) {
    const link = new SimLink();
    link.mtu = mtu; link.loss = loss; link.kbps = kbps;
    link.connected = true; link.notifyCb = null; link.onFrame = () => {};
    link.dropped = 0;
    const t0 = Date.now();
    const imports = { env: {
      js_notify: (p, n) => {
        const msg = new Uint8Array(link.mem.buffer, p, n).slice();
        // deliver async, like a BLE notification
        if (link.connected && link.notifyCb) queueMicrotask(() => link.notifyCb(msg));
      },
      js_now: () => (Date.now() - t0) >>> 0,
      js_frame: (layer, fmt, w, h, p, len, ms) =>
        link.onFrame({ layer, fmt, w, h, ms, data: new Uint8Array(link.mem.buffer, p, len).slice() }),
    } };
    const bytes = wasmSource instanceof ArrayBuffer || ArrayBuffer.isView(wasmSource)
      ? wasmSource : await (await fetch(wasmSource)).arrayBuffer();
    const { instance } = await WebAssembly.instantiate(bytes, imports);
    link.w = instance.exports;
    link.mem = instance.exports.memory;
    link.w.init(maxFrame, mtu);
    link.budget = 0; link.tLast = Date.now();
    return link;
  }

  _push(fn, u8) {
    const p = this.w.inbuf();
    new Uint8Array(this.mem.buffer, p, u8.length).set(u8);
    fn(u8.length);
  }

  // pace to `kbps` (KB/s) so throughput charts behave like a real link
  async _pace(n) {
    if (!this.kbps) return;
    const now = Date.now();
    // new burst after idle (e.g. start of a transfer): restart the rate window.
    // Bug fixed: the window used to start at link creation, so the first
    // transfer ran unpaced and finished before the first chart sample.
    if (!this.tLastWrite || now - this.tLastWrite > 200) { this.budget = 0; this.tLast = now; }
    this.tLastWrite = now;
    this.budget += n;
    const due = this.budget / (this.kbps * 1024) * 1000;
    const spent = now - this.tLast;
    if (due - spent > 8) await new Promise((r) => setTimeout(r, due - spent));
    if (this.budget > 64 * 1024) { this.budget = 0; this.tLast = Date.now(); }
  }

  _guard() { if (!this.connected) throw new Error('disconnected'); }

  async writeCtrl(u8) {
    this._guard();
    if (u8[0] === 0x20) {          // GET_PARAMS lives in ble_service.c on target
      const p = new Uint8Array(16), dv = new DataView(p.buffer);
      p[0] = RSP.PARAMS; dv.setUint16(1, this.mtu, true); dv.setUint16(3, 12, true);
      dv.setUint16(7, 400, true); p[9] = 2; p[10] = 2; dv.setUint16(11, 251, true);
      dv.setUint16(13, 251, true); p[15] = 0xFF;   // 0xFF = simulated backend
      queueMicrotask(() => this.notifyCb && this.notifyCb(p));
      return;
    }
    if (u8[0] === 0x21 || u8[0] === 0x22) return;   // PHY / conn update: no-op
    this._push(this.w.ctrl, u8);
    await new Promise((r) => setTimeout(r, 2));     // ATT write response round trip
  }

  async writeData(u8) {
    this._guard();
    if (Math.random() >= this.loss) this._push(this.w.data, u8); else this.dropped++;
    await this._pace(u8.length);
  }

  onNotify(cb) { this.notifyCb = cb; }

  disconnect() { this.connected = false; this.w.disconnect(); }
  reconnect() { this.connected = true; }
  stats() {
    const n = ['frames_ok', 'frames_crc_err', 'resumes', 'aborts', 'chunks_rx', 'chunks_dup', 'chunks_bad', 'bytes_rx'];
    return Object.fromEntries(n.map((k, i) => [k, this.w.stat(i)]));
  }
}
