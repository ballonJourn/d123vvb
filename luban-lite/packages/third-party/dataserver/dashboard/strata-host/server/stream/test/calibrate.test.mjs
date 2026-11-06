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

test('the WiFi driver refusing frames: packets accepted at e0 never leave w1', async () => {
  const r = await rig({ d133: { w1Loss: 0.15 }, seed: 6 }); const out = await r.run(); r.close();
  assert.equal(status(out, 'forwarded'), 'warn'); assert.match(text(out, 'forwarded'), /never left w03.*refused by the driver/);
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
