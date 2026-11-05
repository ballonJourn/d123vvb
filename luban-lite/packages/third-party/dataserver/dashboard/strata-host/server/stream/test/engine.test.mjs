import test from 'node:test';
import assert from 'node:assert/strict';
import { createStreamEngine, validate } from '../engine.mjs';
import { createPath } from '../sim/path.mjs';
import { boardUpdate } from '../../d133.mjs';

const sleep = ms => new Promise(r => setTimeout(r, ms));
const rejects = (p, status, re) => assert.throws(() => validate(p), e => e.status === status && re.test(e.message), JSON.stringify(p));

test('validate: defaults are filled, bad input is refused with a 400 and a reason', () => {
  assert.deepEqual(validate({ role: 'send', host: '192.168.1.2', port: 5600 }), { role: 'send', host: '192.168.1.2', port: 5600, mode: 'fixed', streamId: 1, fps: 30, bitrate: 4e6, queueFrames: 4, pacing: 0.5 });
  assert.equal(validate({ role: 'recv', port: 5600 }).reportMs, 500);
  rejects({}, 400, /role/); rejects({ role: 'x' }, 400, /role/);
  rejects({ role: 'send', port: 5600 }, 400, /host/); rejects({ role: 'send', host: 'a b', port: 1 }, 400, /host/); rejects({ role: 'send', host: '1.2.3.4' }, 400, /port is required/);
  rejects({ role: 'send', host: '1.2.3.4', port: 70000 }, 400, /port/); rejects({ role: 'send', host: '1.2.3.4', port: 1, bitrate: 10 }, 400, /bitrate/);
  rejects({ role: 'send', host: '1.2.3.4', port: 1, fps: 0 }, 400, /fps/); rejects({ role: 'send', host: '1.2.3.4', port: 1, mode: 'warp' }, 400, /mode/);
  rejects({ role: 'send', host: '1.2.3.4', port: 1, mode: 'ramp', startBps: 5e6, maxBps: 1e6 }, 400, /maxBps/);
  rejects({ role: 'recv', port: 0 }, 400, /port/);
});

test('one run at a time: a second start is a 409, stop frees it', async () => {
  const e = createStreamEngine();
  e.start({ role: 'recv', port: 47811 });
  assert.throws(() => e.start({ role: 'recv', port: 47812 }), x => x.status === 409);
  await sleep(100);
  assert.equal(e.stop().role, 'idle');
  e.start({ role: 'recv', port: 47811 }); await sleep(100); e.stop();                  // the port was released
});

test('a sender and a receiver engine through the simulated D133: loss is located inside the D133', async () => {
  const rxe = createStreamEngine();
  rxe.start({ role: 'recv', port: 47813, reportMs: 100 });
  await sleep(150);
  const path = await createPath({ receiver: { port: 47813 }, d133: { loss: 0.10 }, seed: 5 });
  // the dashboard polls the board once a second: here the same boardUpdate, fed from the simulated board
  const board = { target: 'sim', ok: false, ifs: [], history: { t: [], ifs: {} } }; let prev = null;
  const poll = setInterval(() => { prev = boardUpdate(board, prev, path.snapshot()); }, 1000);
  const txe = createStreamEngine({ peekBoard: () => board });
  txe.start({ role: 'send', host: '127.0.0.1', port: path.port, mode: 'fixed', bitrate: 3e6, fps: 30 });
  await sleep(8000);
  const s = txe.state(), r = rxe.state();
  clearInterval(poll); txe.stop(); rxe.stop(); path.close();

  assert.equal(s.role, 'send'); assert.equal(s.error, null);
  assert.ok(s.stats.reports > 20, `reports ${s.stats.reports}`);
  assert.equal(s.path.located.where, 'd133', s.path.located.text);
  assert.deepEqual(s.path.located.flagged, ['d133']);
  const f = s.path.located.segments[1].fraction;
  assert.ok(f > 0.05 && f < 0.16, `inside-D133 loss ${f}`);
  assert.ok(s.path.boardRefused.e0RxDrop > 5, `rx_drop/s ${s.path.boardRefused.e0RxDrop}`);
  assert.ok(s.history.txPps.length >= 6 && s.history.rxPps.some(v => v > 100));
  assert.equal(r.role, 'recv'); assert.ok(r.stats.packets > 1000); assert.ok(r.stats.frameBytes > 0);
  assert.ok(r.history.t.length >= 6); assert.ok(r.history.rxPps.at(-1) > 100);
});

test('without board data the path says which segments are unknown instead of guessing', async () => {
  const rxe = createStreamEngine(); rxe.start({ role: 'recv', port: 47814, reportMs: 100 }); await sleep(150);
  const txe = createStreamEngine();                                                     // no peekBoard
  txe.start({ role: 'send', host: '127.0.0.1', port: 47814, bitrate: 2e6 });
  await sleep(3500);
  const p = txe.state().path; txe.stop(); rxe.stop();
  // every segment needs at least one number from the board, so with none of them all three are unknown
  assert.deepEqual(p.located.unknown, ['cable', 'd133', 'wifi']); assert.equal(p.located.where, null); assert.equal(p.located.text, 'No data yet');
  assert.ok(p.rates.tx > 100 && p.rates.rx > 100, 'the sender still knows its own numbers');
  assert.equal(p.rates.e0Rx, null); assert.equal(p.rates.w1Tx, null);
});
