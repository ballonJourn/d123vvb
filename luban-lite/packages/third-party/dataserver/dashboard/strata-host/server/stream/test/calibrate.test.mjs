import test from 'node:test';
import assert from 'node:assert/strict';
import dgram from 'node:dgram';
import { calibrate, evaluate } from '../calibrate.mjs';
import { createPath } from '../sim/path.mjs';

async function rig(pathOpts = {}) {
  const sink = dgram.createSocket('udp4'); await new Promise(r => sink.bind(0, r));
  const path = await createPath({ receiver: { port: sink.address().port }, ...pathOpts });
  const stats = await path.serveDsstat();
  const run = (o = {}) => calibrate({ to: ['127.0.0.1', path.port], board: ['127.0.0.1', stats.port], count: 300, payload: 1000, pps: 3000, settleMs: 300, ...o });
  return { run, path, close() { sink.close(); path.close(); stats.close(); } };
}
const status = (r, id) => r.checks.find(c => c.id === id)?.status;
const text = (r, id) => r.checks.find(c => c.id === id)?.text;

test('a board that counts as designed: every check passes', async () => {
  const r = await rig(); const out = await r.run(); r.close();
  assert.equal(out.verdict, 'pass', JSON.stringify(out.checks)); assert.equal(out.sendErrors, 0);
  assert.equal(out.measured.arrived, 300); assert.equal(out.measured.w1TxPkts, 300); assert.equal(out.measured.l2Measured, 14);
});

test('counters without the Ethernet header: the l2 check says so and names the setting', async () => {
  const r = await rig({ l2: 0 }); const out = await r.run(); r.close();
  assert.equal(status(out, 'l2'), 'warn'); assert.match(text(out, 'l2'), /D133_L2_BYTES=0/); assert.equal(out.measured.l2Measured, 0);
  assert.equal(out.verdict, 'warn');
});

test('loss on the cable shows as fewer packets arrived at e0', async () => {
  const r = await rig({ cable: { loss: 0.10 }, seed: 4 }); const out = await r.run(); r.close();
  assert.equal(status(out, 'arrived'), 'warn'); assert.match(text(out, 'arrived'), /only \d+ of 300/);
});

test('a forwarding queue that refuses frames: arrived still counts them, refused is reported', async () => {
  const r = await rig({ d133: { loss: 0.2 }, seed: 9 }); const out = await r.run(); r.close();
  assert.equal(status(out, 'arrived'), 'pass', text(out, 'arrived'));            // refused frames did arrive at e0
  assert.equal(status(out, 'refused'), 'warn'); assert.match(text(out, 'refused'), /refused by the stack/);
  assert.ok(out.measured.e0RxDrop > 20);
});

test('the WiFi driver refusing frames: packets accepted at e01 never leave w03', async () => {
  const r = await rig({ d133: { w1Loss: 0.15 }, seed: 6 }); const out = await r.run(); r.close();
  assert.equal(status(out, 'forwarded'), 'warn'); assert.match(text(out, 'forwarded'), /only \d+ of 300 packets left w03; the driver refused \d+/);
  assert.equal(status(out, 'refused'), 'warn');
});

const S = (up, e0, w1, extra = {}) => ({ uptime_ms: up, ip_forwarded: -1, ifs: [{ name: 'e0', ...e0 }, { name: 'w1', ...w1 }], ...extra });
const Z = { rx_pkts: 0, tx_pkts: 0, rx_bytes: 0, tx_bytes: 0, rx_drop: 0, tx_err: 0 };

test('evaluate: nothing counted at e0 is a failure that points at the hook', () => {
  const r = evaluate({ before: S(1, Z, Z), after: S(2, Z, Z), count: 100, datagramBytes: 1032 });
  assert.equal(r.verdict, 'fail'); assert.equal(status(r, 'arrived'), 'fail'); assert.match(text(r, 'arrived'), /netmon hook/);
});

test('evaluate: counted at e0 but nothing left w1 is a failure that points at forwarding', () => {
  const r = evaluate({ before: S(1, Z, Z), after: S(2, { ...Z, rx_pkts: 100, rx_bytes: 100 * 1074 }, Z), count: 100, datagramBytes: 1032 });
  assert.equal(status(r, 'forwarded'), 'fail'); assert.match(text(r, 'forwarded'), /IP_FORWARD/); assert.equal(r.verdict, 'fail');
});

test('evaluate: a missing interface is reported with the names the board does list', () => {
  const r = evaluate({ before: { ifs: [{ name: 'lo' }] }, after: { ifs: [{ name: 'lo' }] }, count: 1, datagramBytes: 1 });
  assert.equal(r.verdict, 'fail'); assert.match(r.checks[0].text, /the board lists: lo/); assert.match(r.checks[0].text, /--ingress/);
});

test('evaluate: 32-bit counter wrap during the run does not matter', () => {
  const M = 2 ** 32, b = { rx_pkts: M - 50, rx_bytes: M - 50_000, tx_pkts: 0, tx_bytes: 0, rx_drop: 0, tx_err: 0 };
  const a = { rx_pkts: 50, rx_bytes: 50 * 1074 - 50_000 + 0, tx_pkts: 0, tx_bytes: 0, rx_drop: 0, tx_err: 0 };
  const w = { rx_pkts: 0, rx_bytes: 0, tx_pkts: 0, tx_bytes: 0, rx_drop: 0, tx_err: 0 }, w2 = { ...w, tx_pkts: 100, tx_bytes: 100 * 1074 };
  const r = evaluate({ before: S(1, b, w), after: S(2, { ...a, rx_bytes: ((M - 50_000) + 100 * 1074) % M, rx_pkts: 50 }, w2), count: 100, datagramBytes: 1032 });
  assert.equal(r.measured.e0RxPkts, 100); assert.equal(status(r, 'arrived'), 'pass'); assert.equal(r.measured.l2Measured, 14);
});

test('evaluate: lwIP forwarded count is checked when the board has RT_LWIP_STATS', () => {
  const e0 = { ...Z, rx_pkts: 100, rx_bytes: 100 * 1074 }, w1 = { ...Z, tx_pkts: 100, tx_bytes: 100 * 1074 };
  assert.equal(status(evaluate({ before: S(1, Z, Z, { ip_forwarded: 10 }), after: S(2, e0, w1, { ip_forwarded: 110 }), count: 100, datagramBytes: 1032 }), 'lwip'), 'pass');
  assert.equal(status(evaluate({ before: S(1, Z, Z, { ip_forwarded: 10 }), after: S(2, e0, w1, { ip_forwarded: 60 }), count: 100, datagramBytes: 1032 }), 'lwip'), 'warn');
  assert.match(text(evaluate({ before: S(1, Z, Z), after: S(2, e0, w1), count: 100, datagramBytes: 1032 }), 'lwip'), /without RT_LWIP_STATS/);
});

test('the interface names are found by themselves: this board calls them e01 and w03', async () => {
  const r = await rig({ names: { e: 'e01', w: 'w03' } }); const out = await r.run(); r.close();
  assert.equal(out.verdict, 'pass', JSON.stringify(out.checks));
});

test('older names (e0 / w1) work as well, and an explicit name wins over the automatic choice', async () => {
  const r = await rig({ names: { e: 'e0', w: 'w1' } }); const a = await r.run(); const b = await r.run({ ingress: 'e0', egress: 'w1' }); r.close();
  assert.equal(a.verdict, 'pass'); assert.equal(b.verdict, 'pass');
  const r2 = await rig(); const wrong = await r2.run({ ingress: 'e9' }); r2.close();
  assert.equal(wrong.verdict, 'fail'); assert.match(wrong.checks[0].text, /"e9"/);
});

// ---- the first run on the real board (e01 / w03): 1000 sent, 465 other packets arrived on the Ethernet side ----
const REAL = (lwipAfter) => ({
  before: { uptime_ms: 679000, ip_forwarded: 5000, lwip: { stats: 1, ip_rterr: 0, ip_drop: 0, ip_memerr: 0, ip_chkerr: 0, ip_lenerr: 0, link_drop: 0, link_memerr: 0 },
            ifs: [{ name: 'w03', ip: '192.168.169.1', up: 1, ...Z }, { name: 'e01', ip: '192.168.1.1', up: 1, ...Z }] },
  after:  { uptime_ms: 682500, ip_forwarded: 6000, lwip: { stats: 1, ip_rterr: 0, ip_drop: 0, ip_memerr: 0, ip_chkerr: 0, ip_lenerr: 0, link_drop: 0, link_memerr: 0, ...lwipAfter },
            ifs: [{ name: 'w03', ip: '192.168.169.1', up: 1, ...Z, tx_pkts: 1000, tx_bytes: 1000 * 1074 }, { name: 'e01', ip: '192.168.1.1', up: 1, ...Z, rx_pkts: 1465, rx_bytes: 1465 * 778 }] },
});

test('real board, first run: 465 foreign packets on e01 do not hide that all 1000 were forwarded with a 14 byte Ethernet header', () => {
  const r = evaluate({ ...REAL({}), count: 1000, datagramBytes: 1032 });
  assert.equal(status(r, 'forwarded'), 'pass', text(r, 'forwarded'));                 // 1000 of 1000 left w03
  assert.equal(status(r, 'l2'), 'pass'); assert.equal(r.measured.l2Measured, 14); assert.equal(r.measured.l2From, 'w03');   // measured where it is clean
  assert.doesNotMatch(text(r, 'l2'), /-282|D133_L2_BYTES=-/);                          // the nonsensical suggestion of the first version
  assert.equal(status(r, 'arrived'), 'warn'); assert.match(text(r, 'arrived'), /465 more than sent, averaging 141 bytes/);
  assert.equal(r.checks.find(c => c.id === 'symmetry'), undefined);                    // not compared while the ingress side is mixed
  assert.equal(r.verdict, 'warn');
});

test('the extra packets are explained from lwIP\'s counters: "no route" points at the PC using the board as default gateway', () => {
  const r = evaluate({ ...REAL({ ip_rterr: 465 }), count: 1000, datagramBytes: 1032 });
  assert.match(text(r, 'arrived'), /\+465 "no route".*default gateway/);
  const t = evaluate({ ...REAL({}), count: 1000, datagramBytes: 1032 });
  assert.match(text(t, 'arrived'), /took them itself/);                                // no routing errors: ARP, ICMP, broadcasts, connections to the board
  const off = REAL({}); off.after.lwip = { stats: 0 };
  assert.match(text(evaluate({ ...off, count: 1000, datagramBytes: 1032 }), 'arrived'), /lwIP counters are off/);
});

test('framing is never guessed from a mixed average', () => {
  const mixed = REAL({}); mixed.after.ifs[0].tx_pkts = 1300; mixed.after.ifs[0].tx_bytes = 1300 * 900;   // egress also mixed
  const r = evaluate({ ...mixed, count: 1000, datagramBytes: 1032 });
  assert.equal(status(r, 'l2'), 'warn'); assert.match(text(r, 'l2'), /cannot be measured now/); assert.equal(r.measured.l2Measured, undefined);
});

test('real board, second run (route fixed): 1004 arrived for 1000 sent, 1070.0 vs 1074.0 bytes per packet is not a framing difference', () => {
  const r = evaluate({
    before: { uptime_ms: 1, ip_forwarded: 0, lwip: { stats: 1 }, ifs: [{ name: 'w03', ip: '192.168.169.1', up: 1, ...Z }, { name: 'e01', ip: '192.168.1.1', up: 1, ...Z }] },
    after: { uptime_ms: 3500, ip_forwarded: 1000, lwip: { stats: 1 }, ifs: [{ name: 'w03', ip: '192.168.169.1', up: 1, ...Z, tx_pkts: 1000, tx_bytes: 1000 * 1074 },
                                                                          { name: 'e01', ip: '192.168.1.1', up: 1, ...Z, rx_pkts: 1004, rx_bytes: 1004 * 1070 }] },
    count: 1000, datagramBytes: 1032 });
  assert.equal(r.verdict, 'pass', JSON.stringify(r.checks));
  assert.match(text(r, 'symmetry'), /4 more packets than w03 sent, averaging 70 bytes/);
});

test('extra "packets" that average no frame size do show up as a framing difference', () => {
  const r = evaluate({
    before: { uptime_ms: 1, ip_forwarded: 0, ifs: [{ name: 'w03', ip: '1.1.1.1', up: 1, ...Z }, { name: 'e01', ip: '2.2.2.2', up: 1, ...Z }] },
    after: { uptime_ms: 2, ip_forwarded: 1000, ifs: [{ name: 'w03', ip: '1.1.1.1', up: 1, ...Z, tx_pkts: 1000, tx_bytes: 1000 * 1074 }, { name: 'e01', ip: '2.2.2.2', up: 1, ...Z, rx_pkts: 1004, rx_bytes: 1004 * 1060 }] },
    count: 1000, datagramBytes: 1032 });
  assert.equal(status(r, 'symmetry'), 'warn');                                    // 1004*1060 - 1000*1074 = -9760 over 4 packets: impossible
});
