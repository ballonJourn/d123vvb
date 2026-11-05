import test from 'node:test';
import assert from 'node:assert/strict';
import { boardUpdate, LWIP_COUNTERS } from '../../d133.mjs';

const fresh = () => ({ history: { t: [], ifs: {} } });
const IF = (name, o = {}) => ({ name, ip: '0.0.0.0', up: 1, rx_bytes: 0, tx_bytes: 0, rx_pkts: 0, tx_pkts: 0, rx_drop: 0, tx_err: 0, ...o });
const S = (t, ifs, extra = {}) => ({ uptime_ms: t, ip_forwarded: -1, ifs, ...extra });
const LW = (o = {}) => ({ stats: 1, ip_drop: 0, ip_rterr: 0, ip_chkerr: 0, ip_lenerr: 0, ip_memerr: 0, link_drop: 0, link_memerr: 0, link_chkerr: 0, link_lenerr: 0,
  pbuf_err: 0, pbuf_used: 1, pbuf_max: 5, inpkt_err: 0, inpkt_used: 1, inpkt_max: 3, limits: { pbuf_pool: 64, inpkt_pool: 128, mbox: 8 }, ...o });

test('bytes, packets, refused frames and driver errors all become per-second rates', () => {
  const s = fresh();
  let p = boardUpdate(s, null, S(1000, [IF('e0', { rx_bytes: 0, rx_pkts: 0, rx_drop: 0 })]));
  assert.equal(s.ifs[0].rx_pps, null);
  p = boardUpdate(s, p, S(3000, [IF('e0', { rx_bytes: 4000, tx_bytes: 2000, rx_pkts: 200, tx_pkts: 20, rx_drop: 30, tx_err: 6 })]));
  const e = s.ifs[0];
  assert.deepEqual([e.rx_rate, e.tx_rate, e.rx_pps, e.tx_pps, e.rx_drop_rate, e.tx_err_rate], [2000, 1000, 100, 10, 15, 3]);   // over 2 s
  assert.deepEqual(s.history.ifs.e0, { rx: [2000], tx: [1000], rx_pps: [100], tx_pps: [10], rx_drop: [15], tx_err: [3] });
});

test('32-bit counters wrap without producing a spike (packets, drops, errors too)', () => {
  const s = fresh(), M = 2 ** 32;
  let p = boardUpdate(s, null, S(1000, [IF('w1', { rx_pkts: M - 100, rx_drop: M - 4, tx_err: M - 2 })]));
  p = boardUpdate(s, p, S(2000, [IF('w1', { rx_pkts: 400, rx_drop: 6, tx_err: 3 })]));
  assert.equal(s.ifs[0].rx_pps, 500); assert.equal(s.ifs[0].rx_drop_rate, 10); assert.equal(s.ifs[0].tx_err_rate, 5);
});

test('lwIP counters become rates; the limits and watermarks pass through', () => {
  const s = fresh();
  let p = boardUpdate(s, null, S(1000, [IF('e0')], { lwip: LW({ inpkt_err: 10, ip_drop: 5 }) }));
  assert.equal(s.lwip.rates.inpkt_err, null);                                   // first sample: no rate yet
  p = boardUpdate(s, p, S(3000, [IF('e0')], { lwip: LW({ inpkt_err: 50, ip_drop: 9, pbuf_max: 60 }) }));
  assert.equal(s.lwip.rates.inpkt_err, 20); assert.equal(s.lwip.rates.ip_drop, 2); assert.equal(s.lwip.rates.pbuf_err, 0);
  assert.equal(s.lwip.pbuf_max, 60); assert.deepEqual(s.lwip.limits, { pbuf_pool: 64, inpkt_pool: 128, mbox: 8 });
  assert.equal(Object.keys(s.lwip.rates).length, LWIP_COUNTERS.length);
});

test('a board built without RT_LWIP_STATS: limits only, no rates, no crash', () => {
  const s = fresh();
  let p = boardUpdate(s, null, S(1000, [IF('e0')], { lwip: { stats: 0, limits: { pbuf_pool: 64, inpkt_pool: 128, mbox: 8 } } }));
  p = boardUpdate(s, p, S(2000, [IF('e0')], { lwip: { stats: 0, limits: { pbuf_pool: 64, inpkt_pool: 128, mbox: 8 } } }));
  assert.equal(s.lwip.stats, 0); assert.ok(Object.values(s.lwip.rates).every(v => v === null)); assert.equal(s.lwip.limits.mbox, 8);
});

test('an older firmware without lwip / rx_drop / tx_err still works (those rates stay null)', () => {
  const s = fresh();
  const old = (t, rx) => ({ uptime_ms: t, ip_forwarded: -1, ifs: [{ name: 'e0', ip: '1.2.3.4', up: 1, rx_bytes: rx, tx_bytes: rx, rx_pkts: rx / 10, tx_pkts: rx / 10 }] });
  let p = boardUpdate(s, null, old(1000, 0)); p = boardUpdate(s, p, old(2000, 1000));
  assert.equal(s.ifs[0].rx_rate, 1000); assert.equal(s.ifs[0].rx_drop_rate, null); assert.equal(s.ifs[0].tx_err_rate, null); assert.equal(s.lwip, null);
});

test('every series stays aligned with history.t, late interfaces are padded, a reboot clears all of it', () => {
  const s = fresh();
  const a = (t, v) => S(t, [IF('e0', { rx_pkts: v })]), b = (t, v) => S(t, [IF('e0', { rx_pkts: v }), IF('w1', { rx_pkts: v })]);
  let p = boardUpdate(s, null, a(1000, 0)); p = boardUpdate(s, p, a(2000, 100)); p = boardUpdate(s, p, b(3000, 200)); p = boardUpdate(s, p, b(4000, 300));
  assert.deepEqual(s.history.t, [2000, 3000, 4000]);
  for (const h of Object.values(s.history.ifs)) for (const k of ['rx', 'tx', 'rx_pps', 'tx_pps', 'rx_drop', 'tx_err']) assert.equal(h[k].length, 3, k);
  assert.deepEqual(s.history.ifs.w1.rx_pps, [null, null, 100]);
  p = boardUpdate(s, p, b(500, 1));
  assert.deepEqual(s.history, { t: [], ifs: {} });
});
