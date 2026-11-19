// erya_js_test.mjs - web/erya_sender.js -> recorded writes -> firmware engine (erya_replay)
//   node tools/erya_js_test.mjs <jpeg> <out.bin>
import { readFileSync, writeFileSync } from 'node:fs';
import { frameEY, frameEB, parseEY, picturePackets, naviMessage, splitWrites, parseMacKey, canvasQuality,
         PHONE, NAVI_TYPE } from '../web/erya_sender.js';

let fails = 0;
const check = (c, m) => { if (!c) { fails++; console.log('FAIL', m); } };
const [jpgPath, out] = process.argv.slice(2);
const jpeg = new Uint8Array(readFileSync(jpgPath));
const rec = [];
const write = (uuid, u8) => { const h = new Uint8Array(4); h[0] = uuid & 255; h[1] = uuid >> 8; h[2] = u8.length & 255; h[3] = u8.length >> 8; rec.push(h, u8); };

// unit checks
const ey = frameEY({ fun: 'pic', w: 800, h: 480, q: 160, type: '420' });
check(ey[0] === 0x45 && ey[1] === 0x59 && parseEY(ey).w === 800, 'EY round trip');
const eb = frameEB({ a: 1 }, new Uint8Array([1, 2, 3]));
check(eb[1] === 0x42 && eb[9] === 3 && eb.length === 10 + 7 + 3, 'EB layout');
check(parseMacKey('CCC4B2B64DA0&') .mac === 'CCC4B2B64DA0' && parseMacKey('0') === null, 'mac&key');
check(canvasQuality(160) === 0.6 && canvasQuality(200) === 1, 'quality mapping');
const cn = naviMessage(NAVI_TYPE.START, { nextRoadName: '世纪路' });
check(new TextDecoder().decode(cn.subarray(6)).includes('世纪路') && cn[5] === cn.length - 6, 'UTF-8 length counts bytes');

// session as the web page performs it
write(0xaaa5, new Uint8Array([PHONE.ANDROID]));
for (const w of splitWrites(naviMessage(NAVI_TYPE.START, { curStepRetainDistance: 100, nextRoadName: '世纪路',
  pathRetainTime: 3700, pathRetainDistance: 12345, iconType: 3, arriveTime: '13:40' }), 20)) write(0xaaa8, w);
for (const mtu of [23, 185, 247, 517]) {
  const { packets, piece, sum, fitsOneWrite } = picturePackets(jpeg, mtu - 3);
  check(fitsOneWrite === (mtu > 100), `MTU ${mtu}: one write per packet = ${fitsOneWrite}`);
  let writes = 0;
  for (const p of packets) for (const w of splitWrites(p, mtu - 3)) { write(0xaaa7, w); writes++; }
  console.log(`MTU ${mtu}: ${sum} packets x ${piece} JPEG bytes, ${writes} writes`);
}
for (const w of splitWrites(naviMessage(NAVI_TYPE.END), 244)) write(0xaaa8, w);
writeFileSync(out, Buffer.concat(rec.map((u) => Buffer.from(u))));
console.log(fails ? `JS CHECKS FAILED: ${fails}` : 'JS checks passed');
process.exit(fails ? 1 : 0);
