/**
 * A simulated PC1 -> D133 -> PC2 path for tests and demos. Datagrams sent to `port` are relayed to
 * `receiver` through three stages, and the "board" counts them the way netmon.c does:
 *
 *   cable   (PC1 -> e0)     loss, delay, jitter
 *   d133    e0 input        loss and a policer (rateBps, wire bytes). A refused frame is NOT counted in
 *                           e0 rx_pkts/rx_bytes, only in rx_drop, exactly like netmon_input()
 *           w1 output       w1Loss = frames the WiFi driver refuses (tx_err, not in tx_pkts)
 *   wifi    (w1 -> PC2)     loss, delay, jitter
 *
 * Replies (the receiver's REPORTs) travel back unchanged and are counted as w1 rx / e0 tx.
 * snapshot() has the shape of the board's dsstat line; serveDsstat() serves it on a TCP port.
 */
import dgram from 'node:dgram';
import net from 'node:net';
import { wireBytes } from '../protocol.mjs';

const mulberry32 = a => () => { a |= 0; a = a + 0x6D2B79F5 | 0; let t = Math.imul(a ^ a >>> 15, 1 | a); t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t; return ((t ^ t >>> 14) >>> 0) / 4294967296; };

export async function createPath({ receiver, port = 0, cable = {}, d133 = {}, wifi = {}, seed = 1, l2 = 14 }) {
  const rnd = mulberry32(seed);
  const c = { sent: 0, cableDrops: 0, arrivedE0: 0, e0RxPkts: 0, e0RxBytes: 0, e0RxDrop: 0, e0TxPkts: 0, e0TxBytes: 0,
              w1TxPkts: 0, w1TxBytes: 0, w1TxErr: 0, w1RxPkts: 0, w1RxBytes: 0, wifiDrops: 0, delivered: 0, maxLen: 0 };
  const idx = { cable: 0, d133: 0, wifi: 0 };
  const timers = new Set();
  const bucket = { tokens: d133.burstBytes ?? 30000, t: performance.now() };
  let sender = null;

  const later = (ms, fn) => { if (ms <= 0) return fn(); const t = setTimeout(() => { timers.delete(t); fn(); }, ms); timers.add(t); };
  const delay = o => (o.delayMs ?? 0) + (o.jitterMs ? rnd() * o.jitterMs : 0);
  const lost = (o, i) => (o.dropIf ? o.dropIf(i) : false) || (o.loss ? rnd() < o.loss : false);
  const policed = bytes => {
    if (!d133.rateBps) return false;
    const now = performance.now(), cap = d133.burstBytes ?? 30000;
    bucket.tokens = Math.min(cap, bucket.tokens + (d133.rateBps / 8) * (now - bucket.t) / 1000); bucket.t = now;
    if (bucket.tokens < bytes) return true;
    bucket.tokens -= bytes; return false;
  };

  const toReceiver = dgram.createSocket('udp4');
  await new Promise(r => toReceiver.bind(0, r));
  const front = dgram.createSocket('udp4');
  await new Promise(r => front.bind(port, r));

  front.on('message', (buf, rinfo) => {
    sender = rinfo; c.sent++; c.maxLen = Math.max(c.maxLen, buf.length);
    const wire = wireBytes(buf.length, 1, l2);
    if (lost(cable, idx.cable++)) { c.cableDrops++; return; }
    later(delay(cable), () => {
      c.arrivedE0++;
      if (lost(d133, idx.d133++) || policed(wire)) { c.e0RxDrop++; return; }
      c.e0RxPkts++; c.e0RxBytes += wire;
      later(delay(d133), () => {
        if (d133.w1Loss && rnd() < d133.w1Loss) { c.w1TxErr++; return; }
        c.w1TxPkts++; c.w1TxBytes += wire;
        if (lost(wifi, idx.wifi++)) { c.wifiDrops++; return; }
        later(delay(wifi), () => { c.delivered++; toReceiver.send(buf, receiver.port, receiver.host ?? '127.0.0.1'); });
      });
    });
  });
  toReceiver.on('message', buf => {                                // reports back: w1 rx, forwarded out of e0
    const wire = wireBytes(buf.length, 1, l2);
    c.w1RxPkts++; c.w1RxBytes += wire;
    if (!sender) return;
    c.e0TxPkts++; c.e0TxBytes += wire;
    front.send(buf, sender.port, sender.address);
  });

  const t0 = performance.now();
  const u32 = v => v >>> 0;
  const snapshot = () => ({
    v: 1, uptime_ms: Math.round(performance.now() - t0) + 1000,
    ifs: [
      { name: 'e0', ip: '192.168.1.1', up: 1, rx_bytes: u32(c.e0RxBytes), tx_bytes: u32(c.e0TxBytes), rx_pkts: u32(c.e0RxPkts), tx_pkts: u32(c.e0TxPkts), rx_drop: u32(c.e0RxDrop), tx_err: 0 },
      { name: 'w1', ip: '192.168.169.1', up: 1, rx_bytes: u32(c.w1RxBytes), tx_bytes: u32(c.w1TxBytes), rx_pkts: u32(c.w1RxPkts), tx_pkts: u32(c.w1TxPkts), rx_drop: 0, tx_err: u32(c.w1TxErr) },
    ],
    ip_forwarded: u32(c.w1TxPkts),
    lwip: { stats: 1, ip_drop: 0, ip_rterr: 0, ip_chkerr: 0, ip_lenerr: 0, ip_memerr: 0, link_drop: 0, link_memerr: 0, link_chkerr: 0, link_lenerr: 0,
            pbuf_err: 0, pbuf_used: 3, pbuf_max: 40, inpkt_err: u32(c.e0RxDrop), inpkt_used: 1, inpkt_max: 9, limits: { pbuf_pool: 64, inpkt_pool: 128, mbox: 8 } },
    dataserver: { running: 1, connections: 0, requests_done: 0, files_sent: 0, rejected: 0, queued: 0, bytes_sent: 0 },
  });

  function serveDsstat(statPort = 0) {
    const srv = net.createServer(sock => { sock.on('error', () => {}); sock.end(JSON.stringify(snapshot()) + '\n'); });
    return new Promise(r => srv.listen(statPort, '127.0.0.1', () => r({ port: srv.address().port, close: () => srv.close() })));
  }

  return {
    port: front.address().port, counters: c, snapshot, serveDsstat,
    close() { for (const t of timers) clearTimeout(t); timers.clear(); front.close(); toReceiver.close(); },
  };
}
