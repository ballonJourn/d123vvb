// erya_sender.js - phone (领骑摩托 app) side of 尔雅 BLE 导航协议 Ver 1.2.
// No DOM, no Web Bluetooth: runs in the browser and in node (tools/erya_js_test.mjs).
//   "EY" | u32 BE n | n bytes UTF-8 JSON
//   "EB" | u32 BE n | u32 BE m | n bytes JSON | m bytes binary
// SPDX-License-Identifier: Apache-2.0

export const uuid16 = (x) => `0000${x.toString(16).padStart(4, '0')}-0000-1000-8000-00805f9b34fb`;
export const ERYA = {
  svc: uuid16(0xaaa0),             // not in the document: firmware BLE_NAVI_ERYA_SVC_UUID
  AAA4: uuid16(0xaaa4), AAA5: uuid16(0xaaa5), AAA6: uuid16(0xaaa6),
  AAA7: uuid16(0xaaa7), AAA8: uuid16(0xaaa8),
};
export const PHONE = { ANDROID: 0x00, IOS: 0x01 };
export const NAVI_TYPE = { START: '0', INFO: '1', END: '2' };

const enc = new TextEncoder();
const dec = new TextDecoder();

function be32(u8, off, v) {
  u8[off] = v >>> 24; u8[off + 1] = (v >>> 16) & 255; u8[off + 2] = (v >>> 8) & 255; u8[off + 3] = v & 255;
}
const rd32 = (u8, off) => ((u8[off] << 24) | (u8[off + 1] << 16) | (u8[off + 2] << 8) | u8[off + 3]) >>> 0;

/** "EY" frame around a JSON object or string */
export function frameEY(json) {
  const j = enc.encode(typeof json === 'string' ? json : JSON.stringify(json));
  const out = new Uint8Array(6 + j.length);
  out[0] = 0x45; out[1] = 0x59;                  // 'E' 'Y'
  be32(out, 2, j.length);
  out.set(j, 6);
  return out;
}

/** "EB" frame: JSON object + binary */
export function frameEB(json, bin) {
  const j = enc.encode(typeof json === 'string' ? json : JSON.stringify(json));
  const out = new Uint8Array(10 + j.length + bin.length);
  out[0] = 0x45; out[1] = 0x42;                  // 'E' 'B'
  be32(out, 2, j.length);
  be32(out, 6, bin.length);
  out.set(j, 10);
  out.set(bin, 10 + j.length);
  return out;
}

/** parse an "EY" frame (AAA6 notification) -> object, or null */
export function parseEY(u8) {
  if (!u8 || u8.length < 6 || u8[0] !== 0x45 || u8[1] !== 0x59) return null;
  const n = rd32(u8, 2);
  if (u8.length < 6 + n) return null;
  try { return JSON.parse(dec.decode(u8.subarray(6, 6 + n))); } catch { return null; }
}

/**
 * Split a JPEG into EB packets {"fun":"pic","num","sum","size"} + piece.
 * Normally every packet fits one ATT write of maxWrite bytes (ATT MTU - 3).
 * With a tiny MTU (23 -> 20 bytes) the header alone does not fit: then each
 * packet carries minPiece JPEG bytes and is sent as several writes
 * (splitWrites); the device parses AAA7 as a byte stream, so that is valid.
 * Returns { packets: Uint8Array[], piece, sum, fitsOneWrite }.
 */
export function picturePackets(jpeg, maxWrite, minPiece = 128) {
  const size = jpeg.length;
  // worst-case JSON length: num and sum with as many digits as possible
  const head = (num, sum) => JSON.stringify({ fun: 'pic', num, sum, size });
  let sum = 1, piece = 0;
  for (let it = 0; it < 8; it++) {               // sum depends on piece, piece on sum's digits
    const nines = Number('9'.repeat(String(sum).length));   // num < sum: same digits at most
    const worst = enc.encode(head(nines, nines)).length;
    const natural = maxWrite - 10 - worst;               // JPEG bytes that fit one write
    piece = natural >= 32 ? natural : minPiece;
    const s2 = Math.max(1, Math.ceil(size / piece));
    if (s2 === sum) break;
    sum = s2;
  }
  const packets = [];
  for (let num = 0; num < sum; num++) {
    const bin = jpeg.subarray(num * piece, Math.min(size, (num + 1) * piece));
    packets.push(frameEB({ fun: 'pic', num, sum, size }, bin));
  }
  const fitsOneWrite = packets.every((p) => p.length <= maxWrite);
  return { packets, piece, sum, fitsOneWrite };
}

/** navigation message for AAA8 (field names exactly as in the document) */
export function naviMessage(type, d = {}) {
  const msg = { fun: 'navi', type: String(type) };
  if (String(type) !== NAVI_TYPE.END) {
    msg.data = {
      curStepRetainDistance: String(d.curStepRetainDistance ?? ''),
      nextRoadName: String(d.nextRoadName ?? ''),
      pathRetainTime: String(d.pathRetainTime ?? ''),
      pathRetainDistance: String(d.pathRetainDistance ?? ''),
      iconType: String(d.iconType ?? '0'),
      arriveTime: String(d.arriveTime ?? ''),
    };
  }
  return frameEY(msg);
}

/** split any byte stream into writes of at most maxWrite (the device parses a stream) */
export function splitWrites(u8, maxWrite) {
  const out = [];
  for (let i = 0; i < u8.length; i += maxWrite) out.push(u8.subarray(i, Math.min(u8.length, i + maxWrite)));
  return out;
}

/** "q" of the AAA6 format (120..200, ratio = q - 100) -> canvas.toBlob quality */
export const canvasQuality = (q) => Math.min(1, Math.max(0.05, ((+q || 160) - 100) / 100));

/** AAA5 value "MAC&key" -> { mac, key } ; "0" -> null */
export function parseMacKey(text) {
  if (!text || text === '0') return null;
  const i = text.indexOf('&');
  return i < 0 ? { mac: text, key: '' } : { mac: text.slice(0, i), key: text.slice(i + 1) };
}
