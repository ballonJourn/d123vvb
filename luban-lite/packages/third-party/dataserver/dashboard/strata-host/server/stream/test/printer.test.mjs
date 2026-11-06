import test from 'node:test';
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { createPrinter } from '../printer.mjs';
import { StreamSender } from '../sender.mjs';
import { StreamReceiver } from '../receiver.mjs';
import { createPath } from '../sim/path.mjs';
import * as P from '../protocol.mjs';

const sleep = ms => new Promise(r => setTimeout(r, ms));
const STUCK = ['-e', 'setTimeout(() => {}, 4000)'];        // a child that never reads its input: a console stuck in "select" mode

test('without a child it is console.log', () => {
  const seen = []; const orig = console.log; console.log = l => seen.push(l);
  try { const p = createPrinter({ child: false }); p.log('a'); p.log('b'); assert.equal(p.usesChild, false); } finally { console.log = orig; }
  assert.deepEqual(seen, ['a', 'b']);
});

test('a stuck console never blocks: log() returns at once, the surplus is dropped and counted', async () => {
  const p = createPrinter({ child: true, childArgs: STUCK, maxBacklog: 64 * 1024 });
  assert.equal(p.usesChild, true);
  const line = 'x'.repeat(1000), t0 = performance.now();
  for (let i = 0; i < 5000; i++) p.log(line);                               // 5 MB into a child that reads nothing
  const ms = performance.now() - t0;
  assert.ok(ms < 1000, `log() took ${ms} ms`); assert.ok(p.dropped > 4000, `dropped ${p.dropped}`);
  await p.close(200);                                                        // and closing does not wait for ever
});

test('the sender keeps sending while the console is stuck', async () => {
  const rx = new StreamReceiver({ port: 0, host: '127.0.0.1', reportMs: 100 }); await rx.start();
  const path = await createPath({ receiver: { port: rx.port } });
  const tx = new StreamSender({ host: '127.0.0.1', port: path.port, bitrate: 2e6, fps: 30 });
  const p = createPrinter({ child: true, childArgs: STUCK });
  const stalls = []; tx.on('stall', e => stalls.push(e.ms));
  try {
    await tx.start();
    const t0 = performance.now();
    while (performance.now() - t0 < 1500) { for (let i = 0; i < 200; i++) p.log('line '.repeat(50)); await sleep(20); }
    assert.deepEqual(stalls, []);
    assert.ok(tx.c.txPackets > 250, `sent ${tx.c.txPackets} datagrams in 1.5 s`);   // about 2 Mbit/s = 190 datagrams/s
    assert.ok(rx.stats().framesOk >= 30);
  } finally { tx.stop(); rx.stop(); path.close(); await p.close(200); }
});

test('the child prints what it is given (the CLI with D133_PRINT_CHILD=1)', async () => {
  const cli = new URL('../cli.mjs', import.meta.url).pathname;
  const c = spawn('node', [cli, 'recv', '--port', '47881', '--duration', '1.2'], { env: { ...process.env, D133_PRINT_CHILD: '1' } });
  let out = ''; c.stdout.on('data', d => { out += d; });
  await once(c, 'exit');
  assert.match(out, /receiving on UDP 47881/);
});

test('a report handled before the lag timer has run is still not taken as good data after a pause', () => {
  const tx = new StreamSender({ host: '127.0.0.1', port: 9, fps: 30, ramp: { startBps: 2e6, stepBps: 1e6, maxBps: 6e6, stepMs: 500, minReports: 1, settleReports: 0, minExpected: 0 } });
  const now = performance.now();
  tx.running = true; tx.startedAtMs = now; tx.ramp.begin(now);
  tx.lag.last = now - 9400;                                                   // the process was away for 9.4 s and the 100 ms timer has not run yet
  const stalls = []; tx.on('stall', e => stalls.push(e.ms));
  tx.handleReport(P.encodeReport({ streamId: 1, reportSeq: 7, intervalMs: 500, expected: 200, received: 200, lost: 0, echoSendTs: P.nowUs() - 9_000_000, echoDelayUs: 0 }));
  assert.equal(stalls.length, 1); assert.ok(stalls[0] > 9000, `stall ${stalls[0]} ms`);
  assert.equal(tx.rttUs, null);                                               // the 9 s "round trip" was not taken
  assert.equal(tx.ramp.reports.length, 0); assert.equal(tx.ramp.done, false); assert.equal(tx.ramp.rate, 2e6);   // and the ramp did not move on
  tx.stop();
});
