// Run once: npm i --prefix tools jsdom@24 @napi-rs/canvas   (headless browser stand-in)
// Headless run of erya.html + erya_app.js with a fake Web Bluetooth device that
// behaves like the firmware; every write is recorded for tools/erya_replay.
import { JSDOM } from 'jsdom';
import { createCanvas } from '@napi-rs/canvas';
import { readFileSync, writeFileSync } from 'node:fs';
import { Blob as NodeBlob } from 'node:buffer';

const WEB = new URL('../web', import.meta.url).pathname;
const html = readFileSync(WEB + '/erya.html', 'utf8').replace(/<script[\s\S]*?<\/script>/g, '');
const dom = new JSDOM(html, { pretendToBeVisual: true });
const w = dom.window;
Object.assign(globalThis, { window: w, document: w.document, getComputedStyle: w.getComputedStyle.bind(w),
  addEventListener: w.addEventListener.bind(w), devicePixelRatio: 1, requestAnimationFrame: (f) => setTimeout(f, 16) });
globalThis.Chart = class { constructor(_, cfg) { this.data = cfg.data; } update() {} };

// canvas: back jsdom's <canvas> with @napi-rs/canvas
let lastJpeg = null;
w.HTMLCanvasElement.prototype.getContext = function () {
  if (!this._c || this._c.width !== this.width || this._c.height !== this.height) this._c = createCanvas(this.width || 1, this.height || 1);
  return this._c.getContext('2d');
};
w.HTMLCanvasElement.prototype.toBlob = function (cb, type, q) {
  const buf = this._c.toBuffer('image/jpeg', Math.round(q * 100));
  lastJpeg = new Uint8Array(buf);
  cb(new NodeBlob([buf]));
};
globalThis.createImageBitmap = async () => createCanvas(10, 10);

// fake device = firmware behaviour
const rec = [];
const record = (u, v) => { const u8 = new Uint8Array(v.buffer ? v.buffer.slice(v.byteOffset ?? 0, (v.byteOffset ?? 0) + v.byteLength) : v);
  const h = new Uint8Array([u & 255, u >> 8, u8.length & 255, u8.length >> 8]); rec.push(h, u8); };
const enc = new TextEncoder();
const ey = (s) => { const j = enc.encode(s); const o = new Uint8Array(6 + j.length); o[0] = 69; o[1] = 89; o[5] = j.length; o.set(j, 6); return o; };
const dv = (u8) => new DataView(u8.buffer, u8.byteOffset, u8.byteLength);
let writesNoRsp = 0, writesRsp = 0;
class Chr {
  constructor(u, val) { this.u = u; this.val = val; this.l = []; }
  async readValue() { return dv(this.val()); }
  async startNotifications() { return this; }
  addEventListener(_, f) { this.l.push(f); }
  notify(u8) { for (const f of this.l) f({ target: { value: dv(u8) } }); }
  async writeValueWithResponse(v) { writesRsp++; return this.write(v); }
  async writeValueWithoutResponse(v) { writesNoRsp++; return this.write(v); }
  async write(v) {
    if (v.byteLength > 512) throw new Error('write > 512');
    record(this.u, v);
    if (this.u === 0xaaa5) setTimeout(() => { chr[0xaaa4].notify(enc.encode('-3'));
      chr[0xaaa6].notify(ey('{"fun":"pic","w":800,"h":480,"q":160,"type":"420"}')); }, 20);
  }
}
const chr = {};
for (let u = 0xaaa1; u <= 0xaaaa; u++) chr[u] = new Chr(u, () => new Uint8Array(0));
chr[0xaaa5].val = () => enc.encode('CCC4B2B64DA0&');
chr[0xaaa4].val = () => enc.encode('-3');
chr[0xaaa6].val = () => ey('{"fun":"pic","w":800,"h":480,"q":160,"type":"420"}');
const stat = new Chr(0, () => { const b = new Uint8Array(20); b[2] = 247; return b; });
const id16 = (s) => parseInt(s.slice(4, 8), 16);
const device = {
  name: 'EY_CCC4B2B64DA0', addEventListener() {},
  gatt: { connect: async () => ({
    getPrimaryService: async (u) => ({
      getCharacteristic: async (c) => (u.startsWith('d1330001') ? stat : chr[id16(c)]) }) }), disconnect() {} },
};
let reqOpts;
Object.defineProperty(globalThis, 'navigator', { value: { bluetooth: { requestDevice: async (o) => { reqOpts = o; return device; } } } });

await import(WEB + '/erya_app.js');
const $ = (id) => w.document.getElementById(id);
const until = async (f, ms = 20000) => { const t = Date.now(); while (!f()) { if (Date.now() - t > ms) throw new Error('timeout'); await new Promise((r) => setTimeout(r, 20)); } };
const logText = () => $('log').textContent;
let fails = 0; const check = (c, m) => { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) fails++; };

$('btnConnect').click();
await until(() => $('st4').classList.contains('done') && !$('btnPic').disabled);
check(reqOpts.filters.some((f) => f.namePrefix === 'EY_'), 'requestDevice filters EY_ name prefix');
check($('hMac').textContent === 'CCC4B2B64DA0', 'MAC from AAA5');
check($('hA4').textContent === '-3', 'AAA4 "-3" shown');
check($('hFmt').textContent.startsWith('800×480'), 'AAA6 format applied: ' + $('hFmt').textContent);
check($('maxw').value === '244', 'write size from MTU 247: ' + $('maxw').value);
check($('preview').width === 800 && $('preview').height === 480, 'preview resized to AAA6 w/h');

$('ntype').value = '0'; $('btnNavi').click();
await until(() => logText().includes('写 AAA8'));
$('btnPic').click();
await until(() => $('sPics').textContent === '1');
check(writesNoRsp > 0, 'AAA7 uses write without response by default');

$('maxw').value = '20';                     // MTU 23 path: packets split over writes
$('btnPic').click();
await until(() => $('sPics').textContent === '2');
$('maxw').value = '244';
$('interval').value = '0';
$('btnLoop').click();
await until(() => +$('sPics').textContent >= 4);
$('btnStop').click();
await until(() => logText().includes('连续发送已停止'));
$('ntype').value = '2'; $('btnNavi').click();
await until(() => (logText().match(/写 AAA8/g) || []).length === 2);
check($('sFail').textContent === '0', 'no write failures');

writeFileSync(process.argv[2] || '/tmp/erya_ui_writes.bin', Buffer.concat(rec.map((u) => Buffer.from(u))));
writeFileSync(process.argv[3] || '/tmp/erya_ui_last.jpg', lastJpeg);
console.log(logText().split('\n').filter((l) => /AAA|张写完/.test(l)).slice(0, 14).join('\n'));
console.log(fails ? 'UI CHECKS FAILED ' + fails : 'UI checks passed');
process.exit(fails ? 1 : 0);
