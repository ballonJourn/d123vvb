import test from 'node:test';
import assert from 'node:assert/strict';
import * as P from '../protocol.mjs';
import { StreamReceiver } from '../receiver.mjs';

const RINFO = { address: '10.0.0.1', port: 4000 };
let seqCounter = 0;
/** One DATA datagram. */
const pkt = ({ seq, frameId = 0, fragIdx = 0, fragCount = 1, captureTs = 1_000_000, sendDelayUs = 0, payload = 'x', key = false }) =>
  P.encodeData({ streamId: 1, seq, frameId, fragIdx, fragCount, captureTs, sendDelayUs, payload: Buffer.from(payload), flags: key ? 1 : 0 });
const rx = (o = {}) => new StreamReceiver({ clock: () => 0, ...o });
/** frames of `frags` fragments each, numbered from seq0, returns the datagrams in order */
function frames(n, frags, seq0 = 0, f0 = 0) {
  const out = []; let seq = seq0;
  for (let f = 0; f < n; f++) for (let i = 0; i < frags; i++) out.push({ seq: seq++, frameId: f0 + f, fragIdx: i, fragCount: frags, payload: `f${f0 + f}p${i}`, captureTs: 1_000_000 + f * 33_000 });
  return out;
}
const feed = (r, list, at = 0) => list.forEach((o, i) => r.handlePacket(pkt(o), RINFO, typeof at === 'function' ? at(i, o) : at));

test('in-order stream: every frame completes, nothing lost', () => {
  const r = rx(); feed(r, frames(3, 2));
  const s = r.stats();
  assert.equal(s.framesOk, 3); assert.equal(s.framesDropped, 0); assert.equal(s.lost, 0); assert.equal(s.expected, 6); assert.equal(s.unique, 6);
  assert.equal(r.takeFrame().data.toString(), 'f0p0f0p1');                     // fragments joined in index order
});

test('loss is counted exactly from the sequence numbers', () => {
  const r = rx(); const all = frames(10, 1).filter(o => o.seq !== 3 && o.seq !== 7);
  feed(r, all);
  const s = r.stats();
  assert.equal(s.expected, 10); assert.equal(s.unique, 8); assert.equal(s.lost, 2); assert.equal(s.lossFraction, 0.2);
  assert.equal(r.tick(1000)[0].lost, 2);
});

test('a hole that is filled later is reordered, not lost', () => {
  const r = rx(); const f = frames(5, 1);
  feed(r, [f[0], f[1], f[3], f[2], f[4]]);
  const s = r.stats();
  assert.equal(s.reordered, 1); assert.equal(s.lost, 0); assert.equal(s.unique, 5);
});

test('duplicates are counted and ignored (no second frame, no extra unique)', () => {
  const r = rx(); const f = frames(2, 1);
  feed(r, [f[0], f[0], f[1]]);
  const s = r.stats();
  assert.equal(s.duplicates, 1); assert.equal(s.unique, 2); assert.equal(s.framesOk, 2); assert.equal(s.packets, 3);
});

test('an incomplete frame is dropped the moment a newer frame shows up, nothing waits', () => {
  const r = rx({ frameTimeoutMs: 60_000 });                                    // so only the "newer frame" rule can drop it
  const f = frames(2, 3);                                                      // frame 0: seq 0..2, frame 1: seq 3..5
  feed(r, [f[0], f[1]]);                                                       // frame 0 misses its third fragment
  assert.equal(r.stats().openFrames, 1);
  feed(r, [f[3]]);                                                             // first fragment of frame 1
  const s = r.stats();
  assert.equal(s.framesDropped, 1); assert.equal(s.openFrames, 1); assert.equal(s.framesOk, 0);
  feed(r, [f[2]]);                                                             // the missing fragment finally arrives: too late
  assert.equal(r.stats().lateFragments, 1); assert.equal(r.stats().framesOk, 0);
  assert.equal(r.takeFrame(), null);                                           // and nothing was delivered for frame 0
});

test('the last frame, if it never completes, is dropped after the timeout (not before)', () => {
  const r = rx({ frameTimeoutMs: 200 });
  r.handlePacket(pkt({ seq: 0, frameId: 0, fragIdx: 0, fragCount: 2 }), RINFO, 0);
  r.sweep(199_000); assert.equal(r.stats().openFrames, 1);
  r.sweep(201_000); assert.equal(r.stats().openFrames, 0); assert.equal(r.stats().framesDropped, 1);
});

test('fragments may arrive out of order inside a frame', () => {
  const r = rx();
  r.handlePacket(pkt({ seq: 1, fragIdx: 1, fragCount: 2, payload: 'B' }), RINFO, 0);
  r.handlePacket(pkt({ seq: 0, fragIdx: 0, fragCount: 2, payload: 'A' }), RINFO, 0);
  assert.equal(r.takeFrame().data.toString(), 'AB');
});

test('completed frames wait in a bounded queue: when full the OLDEST is discarded', () => {
  const r = rx({ frameQueue: 3 });
  feed(r, frames(10, 1));
  const s = r.stats();
  assert.equal(s.queued, 3); assert.equal(s.queueDropped, 7); assert.equal(s.framesOk, 10);
  assert.equal(r.takeFrame().frameId, 7);                                      // 0..6 were thrown away, 7 is the oldest kept
});

test('open frames are bounded too (maxOpenFrames, oldest dropped first)', () => {
  const r = rx({ maxOpenFrames: 2, tolerateFrames: 100, frameTimeoutMs: 60_000 });
  for (let f = 0; f < 5; f++) r.handlePacket(pkt({ seq: f, frameId: f, fragIdx: 0, fragCount: 2 }), RINFO, 0);
  assert.equal(r.stats().openFrames, 2); assert.equal(r.stats().framesDropped, 3);
});

test('jitter: constant transit time gives zero, alternating transit converges to the swing', () => {
  const f = frames(50, 1).map((o, i) => ({ ...o, captureTs: 1_000_000 + i * 10_000 }));
  const steady = rx(); feed(steady, f, i => 9_000_000 + i * 10_000); assert.equal(steady.stats().jitterUs, 0);
  const swing = rx(); feed(swing, f.concat(frames(150, 1, 50, 50).map((o, i) => ({ ...o, captureTs: 1_000_000 + (50 + i) * 10_000 }))),
    i => 9_000_000 + i * 10_000 + (i % 2 ? 1000 : 0));                                           // every other datagram 1 ms late
  assert.ok(swing.stats().jitterUs > 900 && swing.stats().jitterUs <= 1000, `jitter ${swing.stats().jitterUs}`);
});

test('REPORT covers one interval: expected / received / lost, then the next interval', () => {
  const r = rx(); const f = frames(100, 1);
  feed(r, f.filter(o => ![10, 20, 30, 40, 50].includes(o.seq)));
  const a = r.tick(500_000)[0];
  assert.equal(a.expected, 100); assert.equal(a.received, 95); assert.equal(a.lost, 5);
  assert.equal(r.stats().lastReport.lossFraction, 0.05);
  feed(r, frames(50, 1, 100, 100));
  const b = r.tick(1_000_000)[0];
  assert.equal(b.expected, 50); assert.equal(b.received, 50); assert.equal(b.lost, 0); assert.equal(b.reportSeq, 1);
});

test('a datagram that arrives in a later interval than its hole was reported: interval loss clamps, cumulative loss heals', () => {
  const r = rx(); const f = frames(10, 1);
  feed(r, f.filter(o => o.seq !== 4));
  assert.equal(r.tick(100)[0].lost, 1);
  feed(r, [f[4]]);                                                             // the late one
  const b = r.tick(200)[0];
  assert.equal(b.expected, 0); assert.equal(b.received, 1); assert.equal(b.lost, 0); assert.equal(b.reordered, 1);
  assert.equal(r.stats().lost, 0);
});

test('sequence numbers wrap at 2^32 without being taken for loss', () => {
  const r = rx();
  [0xfffffffe, 0xffffffff, 0, 1].forEach((seq, i) => r.handlePacket(pkt({ seq, frameId: i }), RINFO, 0));
  const s = r.stats();
  assert.equal(s.expected, 4); assert.equal(s.unique, 4); assert.equal(s.lost, 0);
  assert.equal(r.tick(1)[0].highestSeq, 1);
});

test('a datagram older than the window is "too late": it is neither unique nor a duplicate', () => {
  const r = rx({ window: 16 }); feed(r, frames(41, 1).filter(o => o.seq !== 5));
  const before = r.stats().unique;
  r.handlePacket(pkt({ seq: 5 }), RINFO, 0);
  assert.equal(r.stats().tooLate, 1); assert.equal(r.stats().unique, before);
});

test('malformed datagrams are counted, never thrown on', () => {
  const r = rx();
  for (const junk of [Buffer.alloc(0), Buffer.from('hello'), Buffer.alloc(100), pkt({ seq: 0 }).subarray(0, 10)]) r.handlePacket(junk, RINFO, 0);
  assert.equal(r.malformed, 4); assert.equal(r.stats(), null);
});

test('REPORT echoes the send time of the newest datagram and how long it was held', () => {
  const r = rx();
  r.handlePacket(pkt({ seq: 0, captureTs: 5_000_000, sendDelayUs: 300 }), RINFO, 7_000_000);
  const rep = r.tick(7_000_250)[0];
  assert.equal(rep.echoSendTs, 5_000_300); assert.equal(rep.echoDelayUs, 250);
});

test('only the newest datagram sets the echo (a reordered old one must not inflate the RTT)', () => {
  const r = rx();
  r.handlePacket(pkt({ seq: 1, captureTs: 2_000_000 }), RINFO, 2_100_000);
  r.handlePacket(pkt({ seq: 0, captureTs: 1_000_000 }), RINFO, 2_200_000);     // older, arrives later
  assert.equal(r.tick(2_300_000)[0].echoSendTs, 2_000_000);
});

// ---- a sender that starts over (the case from the first real ramp: the receiver was left running) ----
const FROM = port => ({ address: '10.0.0.1', port });
const feedFrom = (r, rinfo, list, at = 0) => list.forEach(o => r.handlePacket(pkt(o), rinfo, at));

test('a sender started again (new process, new port, sequence back at 0) gets fresh counters, not "ancient datagrams"', () => {
  const r = rx();
  feedFrom(r, FROM(4000), frames(200, 1));                                      // the first run: seq 0..199
  feedFrom(r, FROM(4001), frames(50, 1));                                       // the second run: seq 0..49 again
  const s = r.stats();                                                          // the screens show the sender heard from last
  assert.equal(s.unique, 50); assert.equal(s.lost, 0); assert.equal(s.tooLate, 0); assert.equal(s.framesOk, 50);
  assert.equal(r.streams.size, 2);
  assert.equal(r.tick(1000).find(x => x.expected === 50)?.lost, 0);
});

test('a sender that starts over from the SAME port is recognised after a run of far-behind datagrams', () => {
  const r = rx({ window: 64 });
  feedFrom(r, FROM(4000), frames(1000, 1));                                      // highest seq 999
  feedFrom(r, FROM(4000), frames(100, 1));                                       // starts over at 0
  const s = r.stats();
  assert.equal(s.lost, 0); assert.equal(s.tooLate, 0);                           // the new run is counted from where it was recognised
  assert.ok(s.unique >= 60 && s.unique <= 70, `unique ${s.unique}`);
  assert.equal(s.framesOk, s.unique);
});

test('one stray old datagram is not a restart', () => {
  const r = rx({ window: 64 });
  feedFrom(r, FROM(4000), frames(500, 1));
  r.handlePacket(pkt({ seq: 3 }), FROM(4000), 0);                                // far behind, but alone
  feedFrom(r, FROM(4000), frames(10, 1, 500, 500));
  const s = r.stats();
  assert.equal(s.tooLate, 1); assert.equal(s.expected, 510);
});

test('a silent stream stops being reported to, and its numbers stay', () => {
  const r = rx({ idleMs: 5000 });
  feedFrom(r, FROM(4000), frames(10, 1), 1_000_000);
  assert.equal(r.tick(1_100_000).length, 1);
  assert.equal(r.tick(7_000_000).length, 0);                                     // 6 s of silence
  assert.equal(r.stats().unique, 10);
});

test('an interval in which nothing arrived is "no data", not 0 % loss', () => {
  const r = rx({ idleMs: 60_000 });
  feedFrom(r, FROM(4000), frames(5, 1), 0);
  assert.equal(r.tick(500_000)[0].expected, 5);
  r.tick(1_000_000);                                                             // nothing new
  assert.equal(r.stats().lastReport.expected, 0); assert.equal(r.stats().lastReport.lossFraction, null);
  assert.equal(new StreamReceiver({ clock: () => 0 }).stats(), null);
});

test('two senders at the same time are counted apart', () => {
  const r = rx();
  feedFrom(r, FROM(4000), frames(30, 1)); feedFrom(r, FROM(4001), frames(10, 1, 0, 100));
  assert.equal(r.streams.size, 2);
  assert.deepEqual(r.tick(1).map(x => x.expected).sort((a, b) => a - b), [10, 30]);
});

test('a restart inside the window looks like duplicates: a run of them starts the sender over', () => {
  const r = rx();                                                                // window 4096, the first run is shorter than that
  feedFrom(r, FROM(4000), frames(300, 1));
  feedFrom(r, FROM(4000), frames(100, 1));                                       // seq 0..99 again: every one is a duplicate of the first run
  const s = r.stats();
  assert.equal(s.lost, 0); assert.equal(s.duplicates, 0);
  assert.ok(s.unique >= 60 && s.unique <= 70, `unique ${s.unique}`);             // counted from where the restart was recognised
  assert.equal(s.framesOk, s.unique);
});

test('a few real duplicates are still duplicates, not a restart', () => {
  const r = rx(); const f = frames(100, 1);
  feedFrom(r, FROM(4000), f); feedFrom(r, FROM(4000), f.slice(10, 20));          // 10 repeated datagrams
  assert.equal(r.stats().duplicates, 10); assert.equal(r.stats().unique, 100);
});
