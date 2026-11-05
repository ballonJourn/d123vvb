import test from 'node:test';
import assert from 'node:assert/strict';
import * as P from '../protocol.mjs';

const data = (o = {}) => ({ streamId: 7, seq: 5, frameId: 9, fragIdx: 1, fragCount: 3, captureTs: 1_700_000_000_123_456, sendDelayUs: 321, payload: Buffer.from('abc'), flags: P.FLAG_KEY, ...o });

test('DATA carries stream id, seq, frame id, fragment index/count, capture time, send delay', () => {
  const d = P.decode(P.encodeData(data()));
  assert.equal(d.type, 'data');
  assert.equal(d.streamId, 7); assert.equal(d.seq, 5); assert.equal(d.frameId, 9);
  assert.equal(d.fragIdx, 1); assert.equal(d.fragCount, 3);
  assert.equal(d.captureTs, 1_700_000_000_123_456); assert.equal(d.sendDelayUs, 321);
  assert.equal(d.key, true); assert.equal(d.payload.toString(), 'abc');
});

test('header is 32 bytes and big endian on the wire', () => {
  const b = P.encodeData(data({ payload: Buffer.alloc(0) }));
  assert.equal(b.length, 32);
  assert.deepEqual([...b.subarray(0, 4)], [0xD1, 0x33, 1, 1]);          // magic, version, type
  assert.equal(b.readUInt32BE(8), 5);
});

test('payload is limited to 1400 bytes so a datagram never needs IP fragmentation', () => {
  assert.equal(P.MAX_PAYLOAD, 1400);
  assert.doesNotThrow(() => P.encodeData(data({ payload: Buffer.alloc(1400) })));
  assert.throws(() => P.encodeData(data({ payload: Buffer.alloc(1401) })), RangeError);
  const max = P.encodeData(data({ payload: Buffer.alloc(1400), fragIdx: 0, fragCount: 1 }));
  assert.ok(max.length + P.UDP_IP_OVERHEAD <= 1500, `${max.length + P.UDP_IP_OVERHEAD} on the wire as IP`);
  assert.equal(max.length + P.UDP_IP_OVERHEAD, 1460);
});

test('decode refuses oversize, short, wrong magic, wrong version, bad fragment numbers', () => {
  const ok = P.encodeData(data());
  assert.equal(P.decode(ok.subarray(0, 20)), null);                       // short
  const bad = Buffer.from(ok); bad.writeUInt16BE(0xBEEF, 0); assert.equal(P.decode(bad), null);
  const ver = Buffer.from(ok); ver.writeUInt8(2, 2); assert.equal(P.decode(ver), null);
  const idx = Buffer.from(ok); idx.writeUInt16BE(3, 16); assert.equal(P.decode(idx), null);   // idx >= count
  const cnt = Buffer.from(ok); cnt.writeUInt16BE(0, 18); assert.equal(P.decode(cnt), null);
  assert.equal(P.decode(Buffer.concat([ok, Buffer.alloc(1400)])), null);   // payload > 1400
  assert.equal(P.decode(Buffer.alloc(0)), null);
});

test('encode validates fragment numbers and stream id', () => {
  assert.throws(() => P.encodeData(data({ fragIdx: 3, fragCount: 3 })), RangeError);
  assert.throws(() => P.encodeData(data({ fragCount: 0 })), RangeError);
  assert.throws(() => P.encodeData(data({ streamId: 70000 })), RangeError);
});

test('seq and frame id wrap at 2^32', () => {
  const d = P.decode(P.encodeData(data({ seq: 2 ** 32 + 3, frameId: 2 ** 32 + 4 })));
  assert.equal(d.seq, 3); assert.equal(d.frameId, 4);
});

test('REPORT round trip with every field', () => {
  const r = { streamId: 7, reportSeq: 4, intervalMs: 500, expected: 100, received: 90, lost: 10, reordered: 2, duplicates: 1, jitterUs: 1234,
    framesOk: 20, framesDropped: 3, queueDropped: 1, recvPackets: 91, recvBytes: 2 ** 40 + 5, highestSeq: 777, echoSendTs: 1_700_000_000_000_000, echoDelayUs: 55 };
  const b = P.encodeReport(r);
  assert.equal(b.length, P.REPORT_SIZE);
  assert.deepEqual(P.decode(b), { type: 'report', ...r });
});

test('REPORT of the wrong size is refused', () => {
  assert.equal(P.decode(P.encodeReport({ reportSeq: 1 }).subarray(0, 79)), null);
});

test('wireBytes adds UDP, IPv4 and Ethernet headers per datagram', () => {
  assert.equal(P.wireBytes(1000, 1), 1000 + 8 + 20 + 14);
  assert.equal(P.wireBytes(3000, 3), 3000 + 3 * 42);
});
