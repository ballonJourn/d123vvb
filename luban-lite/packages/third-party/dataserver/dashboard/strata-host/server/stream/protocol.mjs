/**
 * D133 stream wire format (UDP, big endian). Two datagram types.
 *
 * DATA, PC1 -> PC2, header 32 bytes then the payload (a fragment of one frame):
 *    0 u16 magic 0xD133      2 u8 version (1)        3 u8 type (1)
 *    4 u16 stream id         6 u8 flags (bit0 key)   7 u8 reserved (0)
 *    8 u32 seq               per datagram, +1 each, wraps at 2^32
 *   12 u32 frame id          +1 per frame, wraps
 *   16 u16 fragment index    18 u16 fragment count (>= 1)
 *   20 u64 capture time      microseconds since the Unix epoch, sender clock
 *   28 u32 send delay        microseconds from capture to the moment this datagram was sent
 *
 * REPORT, PC2 -> PC1, 80 bytes, one per interval (receiver -> sender feedback):
 *    0..7 as DATA with type 2 (stream id echoed)
 *    8 u32 report seq        12 u32 interval ms
 *   16 u32 expected          20 u32 received (unique)   24 u32 lost
 *   28 u32 reordered         32 u32 duplicates          36 u32 jitter us
 *   40 u32 frames ok         44 u32 frames dropped (incomplete)
 *   48 u32 frames dropped by the receiver's bounded queue (oldest first)
 *   52 u32 datagrams received (including duplicates)    56 u64 datagram bytes received
 *   64 u32 highest seq       68 u64 echoed send time (sender clock)   76 u32 echo delay us
 *   expected, received, lost, reordered, duplicates and the frame counters cover this interval only.
 *
 * Size limit: payload <= 1400 bytes, so a datagram is at most 1432 bytes and with UDP (8) and
 * IPv4 (20) headers 1460 bytes, inside the 1500 byte Ethernet MTU. The stream never makes the IP
 * layer fragment, on the PC or on the D133 (which forwards what it receives).
 */
export const MAGIC = 0xD133;
export const VERSION = 1;
export const TYPE_DATA = 1;
export const TYPE_REPORT = 2;
export const FLAG_KEY = 1;
export const DATA_HEADER = 32;
export const MAX_PAYLOAD = 1400;
export const REPORT_SIZE = 80;
export const UDP_IP_OVERHEAD = 28;
/**
 * Link-layer bytes the D133's interface counters add to an IP packet. 14 (the Ethernet header) is what
 * netmon.c should see, but that is an assumption about the board: `cli.mjs calibrate` measures it and
 * prints the value to use; set it with the environment variable D133_L2_BYTES. Loss location uses
 * packet counts and does not depend on it, only the Mbit/s figures do.
 */
export const ETHERNET_OVERHEAD = Number.isFinite(Number(process.env.D133_L2_BYTES ?? 14)) ? Number(process.env.D133_L2_BYTES ?? 14) : 14;
/** Bytes a datagram occupies on the link as the D133's interface counters see them. */
export const wireBytes = (datagramBytes, packets = 1, l2 = ETHERNET_OVERHEAD) => datagramBytes + packets * (UDP_IP_OVERHEAD + l2);

/** Microseconds since the Unix epoch on this machine's clock. */
export const nowUs = () => Math.round((performance.timeOrigin + performance.now()) * 1000);

const u32 = v => v >>> 0;
const sat32 = v => Math.min(0xffffffff, Math.max(0, Math.round(v)));

export function encodeData({ streamId = 1, flags = 0, seq, frameId, fragIdx = 0, fragCount = 1, captureTs, sendDelayUs = 0, payload }) {
  if (!Buffer.isBuffer(payload)) throw new TypeError('payload must be a Buffer');
  if (payload.length > MAX_PAYLOAD) throw new RangeError(`payload ${payload.length} > ${MAX_PAYLOAD}`);
  if (!Number.isInteger(fragCount) || fragCount < 1 || fragCount > 0xffff) throw new RangeError('bad fragment count');
  if (!Number.isInteger(fragIdx) || fragIdx < 0 || fragIdx >= fragCount) throw new RangeError('bad fragment index');
  if (!Number.isInteger(streamId) || streamId < 0 || streamId > 0xffff) throw new RangeError('bad stream id');
  const b = Buffer.allocUnsafe(DATA_HEADER + payload.length);
  b.writeUInt16BE(MAGIC, 0); b.writeUInt8(VERSION, 2); b.writeUInt8(TYPE_DATA, 3);
  b.writeUInt16BE(streamId, 4); b.writeUInt8(flags & 0xff, 6); b.writeUInt8(0, 7);
  b.writeUInt32BE(u32(seq), 8); b.writeUInt32BE(u32(frameId), 12);
  b.writeUInt16BE(fragIdx, 16); b.writeUInt16BE(fragCount, 18);
  b.writeBigUInt64BE(BigInt(Math.trunc(captureTs)), 20); b.writeUInt32BE(sat32(sendDelayUs), 28);
  payload.copy(b, DATA_HEADER);
  return b;
}

export function encodeReport(r) {
  const b = Buffer.alloc(REPORT_SIZE);
  b.writeUInt16BE(MAGIC, 0); b.writeUInt8(VERSION, 2); b.writeUInt8(TYPE_REPORT, 3);
  b.writeUInt16BE(r.streamId ?? 1, 4);
  b.writeUInt32BE(u32(r.reportSeq), 8); b.writeUInt32BE(sat32(r.intervalMs), 12);
  for (const [i, k] of ['expected', 'received', 'lost', 'reordered', 'duplicates', 'jitterUs', 'framesOk', 'framesDropped', 'queueDropped', 'recvPackets'].entries())
    b.writeUInt32BE(sat32(r[k] ?? 0), 16 + 4 * i);
  b.writeBigUInt64BE(BigInt(Math.trunc(r.recvBytes ?? 0)), 56);
  b.writeUInt32BE(u32(r.highestSeq ?? 0), 64);
  b.writeBigUInt64BE(BigInt(Math.trunc(r.echoSendTs ?? 0)), 68);
  b.writeUInt32BE(sat32(r.echoDelayUs ?? 0), 76);
  return b;
}

/** Parse a datagram. Returns null for anything that is not a well-formed v1 DATA or REPORT. */
export function decode(buf) {
  if (buf.length < 8 || buf.readUInt16BE(0) !== MAGIC || buf.readUInt8(2) !== VERSION) return null;
  const type = buf.readUInt8(3), streamId = buf.readUInt16BE(4);
  if (type === TYPE_DATA) {
    if (buf.length < DATA_HEADER || buf.length > DATA_HEADER + MAX_PAYLOAD) return null;
    const fragIdx = buf.readUInt16BE(16), fragCount = buf.readUInt16BE(18);
    if (fragCount < 1 || fragIdx >= fragCount) return null;
    return {
      type: 'data', streamId, flags: buf.readUInt8(6), key: (buf.readUInt8(6) & FLAG_KEY) !== 0,
      seq: buf.readUInt32BE(8), frameId: buf.readUInt32BE(12), fragIdx, fragCount,
      captureTs: Number(buf.readBigUInt64BE(20)), sendDelayUs: buf.readUInt32BE(28),
      payload: buf.subarray(DATA_HEADER), size: buf.length,
    };
  }
  if (type === TYPE_REPORT) {
    if (buf.length !== REPORT_SIZE) return null;
    const r = { type: 'report', streamId, reportSeq: buf.readUInt32BE(8), intervalMs: buf.readUInt32BE(12) };
    for (const [i, k] of ['expected', 'received', 'lost', 'reordered', 'duplicates', 'jitterUs', 'framesOk', 'framesDropped', 'queueDropped', 'recvPackets'].entries())
      r[k] = buf.readUInt32BE(16 + 4 * i);
    r.recvBytes = Number(buf.readBigUInt64BE(56)); r.highestSeq = buf.readUInt32BE(64);
    r.echoSendTs = Number(buf.readBigUInt64BE(68)); r.echoDelayUs = buf.readUInt32BE(76);
    return r;
  }
  return null;
}
