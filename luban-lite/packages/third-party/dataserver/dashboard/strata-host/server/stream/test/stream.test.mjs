/**
 * End to end over real UDP sockets on loopback: StreamSender -> simulated D133 path -> StreamReceiver,
 * with known losses injected at known places.
 */
import test from 'node:test';
import assert from 'node:assert/strict';
import { StreamSender } from '../sender.mjs';
import { StreamReceiver } from '../receiver.mjs';
import { createPath } from '../sim/path.mjs';
import { locateLoss } from '../locate.mjs';
import * as P from '../protocol.mjs';

const sleep = ms => new Promise(r => setTimeout(r, ms));
const until = async (fn, ms, step = 25) => { for (let t = 0; t < ms; t += step) { if (fn()) return true; await sleep(step); } return fn(); };

async function rig({ path = {}, receiver = {}, sender = {} } = {}) {
  const rx = new StreamReceiver({ port: 0, host: '127.0.0.1', reportMs: 100, ...receiver }); await rx.start();
  const p = await createPath({ receiver: { port: rx.port }, ...path });
  const tx = new StreamSender({ host: '127.0.0.1', port: p.port, ...sender }); await tx.start();
  return { rx, tx, p, async settle(ms = 400) { tx.stop(); await sleep(ms); rx.tick(); }, stop() { tx.stop(); rx.stop(); p.close(); } };
}

test('a clean path: nothing lost, reports come back, round trip measured', async () => {
  const r = await rig({ sender: { bitrate: 2e6, fps: 30 } });
  await sleep(2000); await r.settle();
  const s = r.rx.stats();
  assert.ok(s.framesOk >= 50, `frames ${s.framesOk}`); assert.ok(s.lost <= 1, `lost ${s.lost}`);
  assert.equal(s.unique, r.p.counters.delivered);
  assert.ok(r.tx.reports >= 10, `reports ${r.tx.reports}`);
  assert.ok(r.tx.stats().rttMs !== null && r.tx.stats().rttMs < 30, `rtt ${r.tx.stats().rttMs}`);
  r.stop();
});

test('loss counted by the receiver equals the datagrams the path dropped', async () => {
  const r = await rig({ path: { cable: { dropIf: i => i % 10 === 5 } }, sender: { bitrate: 2e6, fps: 30 } });
  await sleep(2000); await r.settle();
  const s = r.rx.stats(), drops = r.p.counters.cableDrops;
  assert.ok(drops > 20, `drops ${drops}`);
  assert.ok(Math.abs(s.lost - drops) <= 1, `lost ${s.lost} vs dropped ${drops}`);          // a drop at the very end cannot be seen
  assert.equal(s.expected, s.unique + s.lost);
  r.stop();
});

test('incomplete frames are dropped, complete frames arrive whole', async () => {
  const r = await rig({ path: { cable: { loss: 0.03 }, seed: 7 }, sender: { bitrate: 2e6, fps: 30 } });
  await sleep(2000); await r.settle();
  const s = r.rx.stats(); let whole = 0, f;
  while ((f = r.rx.takeFrame())) { assert.equal(f.data.length, 8333); whole++; }
  assert.ok(s.framesDropped >= 1, `dropped ${s.framesDropped}`); assert.ok(s.framesOk > s.framesDropped);
  assert.ok(whole >= 1);
  r.stop();
});

test('sender queue is bounded and keeps the newest frames', async () => {
  const r = await rig({ sender: { source: 'external', queueFrames: 4, frameIntervalUs: 200_000, pacing: 1 } });
  for (let i = 0; i < 100; i++) r.tx.push(Buffer.alloc(4200, i));                    // 3 fragments each, 0.2 s per frame
  assert.equal(r.tx.queue.length, 4); assert.equal(r.tx.queue.dropped, 95);
  await until(() => r.rx.stats()?.framesOk >= 5, 4000);
  const got = []; let f; while ((f = r.rx.takeFrame())) got.push(f.data[0]);
  assert.deepEqual(got, [0, 96, 97, 98, 99]);                                          // the first one was already on its way; of the rest only the newest survived
  r.stop();
});

test('receiver queue is bounded too: a consumer that never reads costs old frames, not delay', async () => {
  const r = await rig({ receiver: { frameQueue: 2 }, sender: { bitrate: 1e6, fps: 30 } });
  await sleep(1500); await r.settle();
  const s = r.rx.stats();
  assert.equal(s.queued, 2); assert.ok(s.queueDropped > 10, `queueDropped ${s.queueDropped}`);
  r.stop();
});

test('no datagram is larger than 1432 bytes (1460 as IP): the stream never makes IP fragment', async () => {
  const r = await rig({ sender: { bitrate: 4e6, fps: 30 } });
  await sleep(800); await r.settle();
  assert.ok(r.p.counters.maxLen <= 1432 && r.p.counters.maxLen > 1000, `max ${r.p.counters.maxLen}`);
  assert.ok(r.p.counters.maxLen + P.UDP_IP_OVERHEAD <= 1500);
  r.stop();
  const small = await rig({ sender: { bitrate: 2e6, fps: 30, mtuPayload: 500 } });
  await sleep(600); await small.settle();
  assert.ok(small.p.counters.maxLen <= 532); small.stop();
});

test('jitter appears when the path adds variable delay', async () => {
  const calm = await rig({ sender: { bitrate: 2e6, fps: 30 } });
  await sleep(1800); await calm.settle(); const base = calm.rx.stats().jitterUs; calm.stop();
  const rough = await rig({ path: { wifi: { jitterMs: 6 }, seed: 3 }, sender: { bitrate: 2e6, fps: 30 } });
  await sleep(1800); await rough.settle(); const j = rough.rx.stats().jitterUs; rough.stop();
  assert.ok(j > 500 && j > base * 5, `jitter ${j} us vs calm ${base} us`);   // reordered datagrams do not feed the estimate, so it stays below the 2 ms mean swing
});

test('round trip time follows the path delay', async () => {
  const r = await rig({ path: { cable: { delayMs: 25 } }, sender: { bitrate: 1e6, fps: 30 } });
  await sleep(1500);
  const rtt = r.tx.stats().rttMs;
  assert.ok(rtt >= 23 && rtt < 80, `rtt ${rtt}`);
  r.stop();
});

test('adaptive: a path that carries less makes the sender lower its rate', async () => {
  const r = await rig({ path: { d133: { rateBps: 4e6 } }, sender: { bitrate: 8e6, minBitrate: 5e5, maxBitrate: 8e6, fps: 30, adaptive: true } });
  const why = []; r.tx.on('adapt', e => why.push(e.why));
  await sleep(5000);
  assert.ok(why.includes('loss'), `adapt events ${why}`);
  assert.ok(r.tx.bitrate < 6e6 && r.tx.bitrate >= 5e5, `rate ${r.tx.bitrate}`);
  r.stop();
});

test('adaptive: no reports at all makes the sender back off', async () => {
  const tx = new StreamSender({ host: '127.0.0.1', port: 9, bitrate: 8e6, fps: 30, adaptive: true, staleMs: 400, minBitrate: 1e6 });
  await tx.start(); await sleep(1500);
  assert.ok(tx.bitrate <= 2.1e6, `rate ${tx.bitrate}`); assert.equal(tx.adapt.last, 'stale');
  tx.stop();
});

test('ramp finds the bitrate where the path starts to lose', async () => {
  const r = await rig({ path: { d133: { rateBps: 6e6 } },
    sender: { fps: 30, ramp: { startBps: 2e6, stepBps: 1e6, maxBps: 9e6, stepMs: 700, minReports: 3, settleReports: 1 } } });
  const done = new Promise(res => r.tx.on('ramp-done', res));
  const out = await Promise.race([done, sleep(20_000).then(() => null)]);
  assert.ok(out, 'ramp did not finish');
  assert.equal(out.reason, 'loss');
  assert.ok(out.knee >= 4e6 && out.knee <= 5e6, `knee ${out.knee} (the path carries about 5.7 Mbit/s of payload)`);
  assert.equal(r.tx.bitrate, out.knee);
  r.stop();
});

test('loss is located in the segment where it was injected (cable 5 %, inside D133 10 %, WiFi 3 %)', async () => {
  const r = await rig({ path: { cable: { loss: 0.05 }, d133: { loss: 0.10 }, wifi: { loss: 0.03 }, seed: 11 }, sender: { bitrate: 3e6, fps: 30 } });
  await sleep(4000); await r.settle(600);
  const c = r.p.counters, rxs = r.rx.stats();
  const total = { tx: r.tx.c.txPackets, e0Rx: c.arrivedE0, w1Tx: c.w1TxPkts, rx: rxs.packets };
  const l = locateLoss({ ...total, noise: 0 });
  assert.deepEqual(l.flagged.sort(), ['cable', 'd133', 'wifi']);
  assert.equal(l.where, 'd133');
  const [cable, d133, wifi] = l.segments.map(s => s.fraction);
  assert.ok(Math.abs(cable - 0.05) < 0.02, `cable ${cable}`); assert.ok(Math.abs(d133 - 0.10) < 0.03, `d133 ${d133}`); assert.ok(Math.abs(wifi - 0.03) < 0.02, `wifi ${wifi}`);
  // the board's own counters agree: frames refused at e0 are rx_drop, and arrivals = rx_pkts + rx_drop
  const e0 = r.p.snapshot().ifs[0];
  assert.equal(e0.rx_pkts + e0.rx_drop, c.arrivedE0); assert.equal(e0.rx_drop, c.e0RxDrop);
  r.stop();
});

test('a forwarding queue that overflows shows up as rx_drop on e0 and is located inside the D133', async () => {
  const r = await rig({ path: { d133: { rateBps: 3e6 } }, sender: { bitrate: 5e6, fps: 30 } });      // pushes 5 Mbit/s into a 3 Mbit/s forwarder
  await sleep(3000); await r.settle(600);
  const c = r.p.counters, e0 = r.p.snapshot().ifs[0];
  assert.ok(e0.rx_drop > 100, `rx_drop ${e0.rx_drop}`);
  const l = locateLoss({ tx: r.tx.c.txPackets, e0Rx: c.arrivedE0, w1Tx: c.w1TxPkts, rx: r.rx.stats().packets, noise: 0 });
  assert.equal(l.where, 'd133'); assert.deepEqual(l.flagged, ['d133']);
  r.stop();
});

test('adaptive with the application\'s own frames: the sender announces each new rate so the producer can lower its quality', async () => {
  const r = await rig({ path: { d133: { rateBps: 2e6 } }, sender: { source: 'external', bitrate: 6e6, minBitrate: 5e5, maxBitrate: 6e6, adaptive: true, frameIntervalUs: 33_333, pacing: 0.5 } });
  const rates = []; r.tx.on('rate', b => rates.push(b));
  const producer = setInterval(() => r.tx.push(Buffer.alloc(Math.round(r.tx.bitrate / 8 / 30))), 33);   // a "codec" that follows the announced rate
  await sleep(4000); clearInterval(producer);
  assert.ok(rates.length >= 3, `rate events ${rates.length}`); assert.ok(Math.min(...rates) < 4e6, `lowest announced rate ${Math.min(...rates)}`);
  assert.ok(r.tx.bitrate < 6e6);
  r.stop();
});

test('a receiver left running: a second sender (new process) is measured from its first datagram, and a rate-limit search on it works', async () => {
  const rx = new StreamReceiver({ port: 0, host: '127.0.0.1', reportMs: 100 }); await rx.start();
  const path = await createPath({ receiver: { port: rx.port }, d133: { rateBps: 6e6 }, seed: 8 });
  const live = [];
  try {
    const first = new StreamSender({ host: '127.0.0.1', port: path.port, bitrate: 2e6, fps: 30 }); live.push(first); await first.start();
    await sleep(1500); first.stop(); await sleep(300);                           // run one: seq 0..~280, then it ends and the receiver stays up
    assert.ok(rx.stats().framesOk >= 30, `frames after the first run ${rx.stats()?.framesOk}`);

    const second = new StreamSender({ host: '127.0.0.1', port: path.port, fps: 30,
      ramp: { startBps: 2e6, stepBps: 1e6, maxBps: 9e6, stepMs: 700, minReports: 3, settleReports: 1 } }); live.push(second); await second.start();
    const done = new Promise(res => second.on('ramp-done', res));
    const out = await Promise.race([done, sleep(20_000).then(() => null)]);
    assert.ok(out, 'the ramp did not finish');
    assert.notEqual(out.reason, 'no-data', 'the receiver treated the new sender as old datagrams');
    assert.equal(out.reason, 'loss'); assert.ok(out.knee >= 4e6 && out.knee <= 5e6, `knee ${out.knee}`);
    assert.ok(second.lastReport.expected > 0);
  } finally { for (const t of live) t.stop(); rx.stop(); path.close(); }               // always, or a failure keeps the test process alive
});

test('a pause of the sender process is announced, and the reports around it do not feed the round trip, adaptation or the ramp', async () => {
  const rx = new StreamReceiver({ port: 0, host: '127.0.0.1', reportMs: 100 }); await rx.start();
  const path = await createPath({ receiver: { port: rx.port } });
  const tx = new StreamSender({ host: '127.0.0.1', port: path.port, fps: 30,
    ramp: { startBps: 1e6, stepBps: 1e6, maxBps: 4e6, stepMs: 600, minReports: 2, settleReports: 1 } });
  const stalls = []; tx.on('stall', e => stalls.push(e.ms));
  try {
    await tx.start();
    await sleep(900);
    const rttBefore = tx.stats().rttMs;
    assert.ok(rttBefore !== null && rttBefore < 50, `rtt before ${rttBefore}`);
    const end = Date.now() + 800; while (Date.now() < end) { /* the whole process is paused, like a click in the console window */ }
    await sleep(400);
    assert.equal(stalls.length, 1); assert.ok(stalls[0] >= 700, `stall ${stalls[0]} ms`);
    assert.equal(tx.stats().stalls, 1);
    assert.ok(tx.stats().rttMs < 100, `round trip after the pause ${tx.stats().rttMs} ms (the pause must not be counted into it)`);
    assert.equal(tx.ramp.done, false);
  } finally { tx.stop(); rx.stop(); path.close(); }
});

test('a pause of the sender is not "no reports": an adaptive sender does not back off because of it', async () => {
  const rx = new StreamReceiver({ port: 0, host: '127.0.0.1', reportMs: 100 }); await rx.start();
  const path = await createPath({ receiver: { port: rx.port } });
  const tx = new StreamSender({ host: '127.0.0.1', port: path.port, fps: 30, bitrate: 2e6, minBitrate: 5e5, maxBitrate: 2e6, adaptive: true, staleMs: 400 });
  try {
    await tx.start(); await sleep(800);
    const end = Date.now() + 1200; while (Date.now() < end) { /* paused: longer than staleMs */ }
    await sleep(500);
    assert.notEqual(tx.adapt.last, 'stale'); assert.ok(tx.bitrate >= 1.9e6, `rate ${tx.bitrate}`);
  } finally { tx.stop(); rx.stop(); path.close(); }
});
