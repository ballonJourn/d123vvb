/* Drives bnp-core.js against the branch's real ble_img_rx.c (sim_device). */
const { spawn } = require('child_process');
const BNP = require('../../web/bnp-sender/bnp-core.js');

function device(mtu) {
  const p = spawn(__dirname + '/sim_device');
  const log = [];
  let buf = Buffer.alloc(0), sender = null;
  p.stdout.on('data', (d) => {
    buf = Buffer.concat([buf, d]);
    while (buf.length >= 2) {
      const n = buf.readUInt16LE(0);
      if (buf.length < 2 + n) break;
      const pkt = new Uint8Array(buf.subarray(2, 2 + n)); buf = buf.subarray(2 + n);
      if (sender && !dev.down) sender.handleNotify(pkt);
    }
  });
  let errText = '';
  p.stderr.on('data', (d) => { errText += d.toString(); });
  p.on('close', () => log.push(...errText.trim().split('\n')));
  const send = (t, b) => { const h = Buffer.alloc(3); h[0] = t; h.writeUInt16LE(b.length, 1); p.stdin.write(Buffer.concat([h, Buffer.from(b)])); };
  const dev = { p, log, down: false, drop: 0, corruptOnce: -1, cutAfter: -1, dataCount: 0,
    attach(s) { sender = s; },
    io: {
      writeCtrl: async (b) => { if (dev.down) throw new Error('disconnected'); send(1, b); },
      writeData: async (b) => {
        if (dev.down) throw new Error('disconnected');
        dev.dataCount++;
        if (dev.cutAfter >= 0 && dev.dataCount === dev.cutAfter) { dev.down = true; send(3, []); sender.linkLost(); throw new Error('disconnected'); }
        if (Math.random() < dev.drop) return;               /* lost write-without-response */
        const seq = b[1] | (b[2] << 8);
        if (seq === dev.corruptOnce) { b = Uint8Array.from(b); b[10] ^= 0x10; dev.corruptOnce = -1; }
        send(2, b);
        if (dev.dataCount % 64 === 0) await new Promise((r) => setImmediate(r));
      },
    },
    reconnect() { dev.down = false; },
    close() { p.stdin.end(); return new Promise((r) => p.on('close', r)); },
  };
  if (mtu) { const m = Buffer.alloc(2); m.writeUInt16LE(mtu); send(4, m); }
  return dev;
}

const rnd = (n) => { const a = new Uint8Array(n); for (let i = 0; i < n; i++) a[i] = (Math.random() * 256) | 0; return a; };
const frame = (n, fmt = BNP.FMT.JPEG, w = 1024, h = 600) => ({ fmt, layer: 0, width: w, height: h, x: 0, y: 0, data: rnd(n) });
let failed = 0;
const check = (ok, msg) => { console.log((ok ? 'PASS ' : 'FAIL ') + msg); if (!ok) failed++; };
const frameLineNow = (dev, f) => dev.log.find((l) => l === `FRAME ${f.id} ${f.fmt} ${f.data.length} ${BNP.crc32(f.data).toString(16).padStart(8, '0')}`);

let pending = [];
const frameLine = (dev, f) => { pending.push([dev, { id: f.id, fmt: f.fmt, data: f.data }]); return true; };
async function run(name, mtu, body) {
  const dev = device(mtu); const s = new BNP.Sender(dev.io, { barrier: 16 }); dev.attach(s);
  try { await body(dev, s); } catch (e) { check(false, `${name}: threw ${e.message}`); }
  await dev.close();
  const want = pending.filter(([d]) => d === dev); pending = pending.filter(([d]) => d !== dev);
  const shown = want.filter(([d, f]) => frameLineNow(d, f)).length;
  check(shown === want.length, `${name}: ${shown}/${want.length} frames reached the display with matching CRC`);
  if (dev.expectNav) check(dev.log.some((l) => /^NAV \d+/.test(l)), `${name}: device parsed NAV_TEXT (UTF-8 road name)`);
  const bad = dev.log.filter((l) => /TOO_LONG/.test(l));
  check(bad.length === 0, `${name}: no oversize writes/notifies ${bad.join(' ')}`);
  console.log('      device ' + dev.log.filter((l) => l.startsWith('STATS')).join(''));
}

(async () => {
  await run('clean 60 KB JPEG @MTU247', 247, async (dev, s) => {
    const f = frame(60000); const st = await s.sendFrame(f);
    check(st.chunk === 241 && st.n === Math.ceil(60000 / 241), `chunk=${st.chunk} n=${st.n}`);
    check(frameLine(dev, f) && st.retx === 0, `frame on display, crc ok, retx=${st.retx}`);
  });
  await run('5% dropped writes', 247, async (dev, s) => {
    dev.drop = 0.05; const f = frame(80000); const st = await s.sendFrame(f);
    check(!!frameLine(dev, f), `delivered after ${st.rounds} rounds, nacks=${st.nacks}, retx=${st.retx}/${st.n}`);
  });
  await run('one corrupted chunk (H4 UART bit error)', 247, async (dev, s) => {
    dev.corruptOnce = 100; const f = frame(60000); const st = await s.sendFrame(f);
    check(!!frameLine(dev, f) && st.crcErr === 1, `crcErr=${st.crcErr}, resent ${st.retx} of ${st.n} chunks`);
  });
  await run('disconnect at 50% then resume', 247, async (dev, s) => {
    const f = frame(100000); const n = Math.ceil(100000 / 241);
    dev.cutAfter = Math.floor(n / 2);
    let threw = false; try { await s.sendFrame(f); } catch (e) { threw = true; }
    check(threw, 'first attempt fails on link loss');
    dev.reconnect(); dev.cutAfter = -1;
    const st = await s.sendFrame(f, { resume: true });
    check(!!frameLine(dev, f) && st.sent < n * 0.6, `resumed: sent ${st.sent} of ${n} chunks on reconnect`);
  });
  await run('full screen RGB565 1024x600, 2% drop', 247, async (dev, s) => {
    dev.drop = 0.02; const f = frame(1024 * 600 * 2, BNP.FMT.RGB565);
    const st = await s.sendFrame(f);
    check(!!frameLine(dev, f), `1.2 MB in ${st.n} chunks, rounds=${st.rounds}, retx=${st.retx}`);
  });
  await run('three frames back to back', 247, async (dev, s) => {
    let ok = 0; for (let i = 0; i < 3; i++) { const f = frame(30000); await s.sendFrame(f); if (frameLine(dev, f)) ok++; }
    check(true, 'three START/COMMIT cycles on one layer');
  });
  await run('no MTU exchange (23)', 23, async (dev, s) => {
    const f = frame(4000); const st = await s.sendFrame(f);
    check(st.chunk === 17 && !!frameLine(dev, f), `chunk=${st.chunk}`);
  });
  await run('nav text + params', 247, async (dev, s) => {
    const p = await s.getParams();
    check(p.mtu === 247 && p.txPhy === 2, `params mtu=${p.mtu} phy=${p.txPhy} ci=${p.ciMs}ms`);
    await s.sendNav([[BNP.NAV.ROAD, '中山北路'], [BNP.NAV.DIST, '300 米'], [BNP.NAV.TURN, 2]]);
    dev.expectNav = true;
  });
  console.log(failed ? `\n${failed} FAILED` : '\nALL PASS'); process.exit(failed ? 1 : 0);
})();
