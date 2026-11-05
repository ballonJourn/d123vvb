import test from 'node:test';
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { parseArgs, parseRate } from '../cli.mjs';

const CLI = new URL('../cli.mjs', import.meta.url).pathname;
const sleep = ms => new Promise(r => setTimeout(r, ms));

test('rates take k / M / G suffixes', () => {
  assert.equal(parseRate('800k'), 8e5); assert.equal(parseRate('4M'), 4e6); assert.equal(parseRate('1.5M'), 1.5e6); assert.equal(parseRate('2g'), 2e9); assert.equal(parseRate('1000'), 1000);
  assert.throws(() => parseRate('fast'), /bad rate/); assert.throws(() => parseRate('-1M'), /bad rate/);
});

test('arguments: command, valued flags, --json, errors', () => {
  assert.deepEqual(parseArgs(['send', '--to', 'h:1', '--json', '--fps', '25']), { cmd: 'send', flags: { to: 'h:1', json: true, fps: '25' } });
  assert.throws(() => parseArgs(['send', '--to']), /needs a value/); assert.throws(() => parseArgs(['send', 'oops']), /unexpected/);
});

test('recv and send as real processes: the receiver reports a clean stream, the sender sees reports and a round trip', async () => {
  const rx = spawn('node', [CLI, 'recv', '--port', '47821', '--report-ms', '100', '--duration', '4', '--json']);
  const tx = spawn('node', [CLI, 'send', '--to', '127.0.0.1:47821', '--bitrate', '2M', '--duration', '3', '--json']);
  const lines = p => { const a = []; let b = ''; p.stdout.on('data', d => { b += d; let i; while ((i = b.indexOf('\n')) >= 0) { try { a.push(JSON.parse(b.slice(0, i))); } catch { /* the banner */ } b = b.slice(i + 1); } }); return a; };
  const r = lines(rx), s = lines(tx);
  await Promise.all([once(rx, 'exit'), once(tx, 'exit')]);
  const lastR = r.at(-1), lastS = s.at(-1);
  assert.ok(lastR.framesOk > 40, `frames ${lastR.framesOk}`); assert.ok(lastR.lost <= 1, `lost ${lastR.lost}`);
  assert.ok(lastS.rttMs !== null && lastS.rttMs < 50, `rtt ${lastS.rttMs}`); assert.ok(lastS.loss !== null);
  assert.ok(r.some(x => x.mbit > 1.5), 'the receiver measured about 2 Mbit/s');
});

test('stdin source: length-prefixed frames from another program arrive whole', async () => {
  const rx = spawn('node', [CLI, 'recv', '--port', '47822', '--report-ms', '100', '--duration', '3', '--json']);
  await sleep(300);
  const tx = spawn('node', [CLI, 'send', '--to', '127.0.0.1:47822', '--source', 'stdin', '--duration', '2.5', '--json']);
  await sleep(300);
  for (let i = 0; i < 10; i++) { const body = Buffer.alloc(3000, i), len = Buffer.alloc(4); len.writeUInt32BE(body.length); tx.stdin.write(Buffer.concat([len, body])); await sleep(40); }
  const out = []; let b = ''; rx.stdout.on('data', d => { b += d; let i; while ((i = b.indexOf('\n')) >= 0) { try { out.push(JSON.parse(b.slice(0, i))); } catch { /* banner */ } b = b.slice(i + 1); } });
  await Promise.all([once(rx, 'exit'), once(tx, 'exit')]);
  assert.equal(out.at(-1).framesOk, 10); assert.equal(out.at(-1).framesDropped, 0);
});

test('a bad command line exits non-zero with a message', async () => {
  const p = spawn('node', [CLI, 'send', '--to', 'nonsense']); let err = ''; p.stderr.on('data', d => { err += d; });
  const [code] = await once(p, 'exit');
  assert.equal(code, 2); assert.match(err, /--to HOST:PORT/);
});
