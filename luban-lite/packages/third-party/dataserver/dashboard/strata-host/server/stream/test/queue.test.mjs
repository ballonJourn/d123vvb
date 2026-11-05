import test from 'node:test';
import assert from 'node:assert/strict';
import { BoundedQueue } from '../queue.mjs';

test('keeps the newest: the oldest entries go first when full', () => {
  const q = new BoundedQueue(3);
  for (const x of ['a', 'b', 'c']) assert.equal(q.push(x), 0);
  assert.equal(q.push('d'), 1);
  assert.equal(q.push('e'), 1);
  assert.equal(q.dropped, 2);
  assert.deepEqual([q.shift(), q.shift(), q.shift(), q.shift()], ['c', 'd', 'e', undefined]);
});

test('never exceeds its limit, however much is pushed', () => {
  const q = new BoundedQueue(5);
  for (let i = 0; i < 1000; i++) { q.push(String(i)); assert.ok(q.length <= 5); }
  assert.equal(q.dropped, 995);
  assert.equal(q.shift(), '995');
});

test('byte limit drops oldest until the new entry fits', () => {
  const q = new BoundedQueue(100, { maxBytes: 10 });
  q.push('aaaa'); q.push('bbbb');
  assert.equal(q.push('cccc'), 1);               // 4+4+4 > 10 -> drop 'aaaa'
  assert.equal(q.bytes, 8);
  assert.equal(q.push('x'.repeat(30)), 2);       // larger than the limit: everything older goes, the newest is kept
  assert.equal(q.length, 1);
});

test('rejects a limit below one', () => { assert.throws(() => new BoundedQueue(0), RangeError); });
