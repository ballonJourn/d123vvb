/**
 * Board calibration: checks what the D133's counters really do, on the real board.
 *
 * PC1 sends exactly `count` UDP datagrams of a known size to PC2 through the board and reads the
 * board's stats line (dsstat, port 9001) before and after. The differences are compared with what
 * was sent:
 *
 *   arrived     packets that reached e0 (rx_pkts + rx_drop)      should be `count` (plus a little other traffic)
 *   l2          bytes per packet on e0                           14 bytes above the IP packet = Ethernet header counted
 *   forwarded   packets w1 sent (tx_pkts)                        should be `count`: the board forwards them
 *   refused     frames the stack refused at e0 / the driver at w1  should be 0 at a gentle rate
 *   symmetry    bytes per packet at w1 equals e0                 both counters see the same framing
 *   lwip        ip.fw delta (only with RT_LWIP_STATS)            should be `count`
 *
 * Run it with PC2 reachable (any receiver, or none: unanswered UDP is fine) and nothing else
 * using the board. evaluate() is pure and tested against the simulated board.
 */
import dgram from 'node:dgram';
import * as P from './protocol.mjs';
import { delta32, readBoardStats } from '../d133.mjs';
import { pickInterfaces } from './locate.mjs';

const ifOf = (sample, name) => sample.ifs?.find(i => i.name === name) ?? null;
const d = (a, b, k) => Number.isFinite(a?.[k]) && Number.isFinite(b?.[k]) ? delta32(b[k], a[k]) : null;

export function evaluate({ before, after, count, datagramBytes, ingress, egress, noisePkts }) {
  // other traffic during the run (ARP, the two reads of the stats port): about ten packets; never more tolerance than 2 % of what was sent
  noisePkts ??= Math.max(20, Math.round(count * 0.02));
  ({ ingress, egress } = pickInterfaces(after.ifs, { ingress, egress }));
  const ipBytes = datagramBytes + P.UDP_IP_OVERHEAD;
  const e0a = ifOf(before, ingress), e0b = ifOf(after, ingress), w1a = ifOf(before, egress), w1b = ifOf(after, egress);
  const checks = [];
  const add = (id, status, text) => checks.push({ id, status, text });
  if (!e0a || !e0b || !w1a || !w1b) {
    const seen = (after.ifs ?? []).map(i => `${i.name} ${i.ip}${i.up ? '' : ' (down)'}`).join(', ') || 'none';
    add('interfaces', 'fail', `cannot find the ${!ingress || !e0b ? `ingress (Ethernet, "${ingress ?? 'e...'}")` : `egress (WiFi, "${egress ?? 'w...'}")`} interface; the board lists: ${seen}. Name them with --ingress / --egress (D133_INGRESS_IF / D133_EGRESS_IF for the dashboard)`);
    return { checks, verdict: 'fail', measured: null };
  }
  const m = {
    e0RxPkts: d(e0a, e0b, 'rx_pkts'), e0RxBytes: d(e0a, e0b, 'rx_bytes'), e0RxDrop: d(e0a, e0b, 'rx_drop') ?? 0,
    w1TxPkts: d(w1a, w1b, 'tx_pkts'), w1TxBytes: d(w1a, w1b, 'tx_bytes'), w1TxErr: d(w1a, w1b, 'tx_err') ?? 0,
  };
  const arrived = m.arrived = m.e0RxPkts + m.e0RxDrop;
  const clean = n => Math.abs(n - count) <= noisePkts;           // "this is the stream and little else"
  const lw = k => before.lwip?.stats === 1 && after.lwip?.stats === 1 ? d(before.lwip, after.lwip, k) : null;

  // what left the egress interface is the cleanest view of the stream: nothing else is normally sent that way
  if (arrived === 0) add('arrived', 'fail', `nothing arrived at ${ingress}: the counters are not counting (is the netmon hook installed? is this PC on that port?)`);
  else if (clean(arrived)) add('arrived', 'pass', `${arrived} packets arrived at ${ingress} for ${count} sent`);
  else if (arrived < count) add('arrived', 'warn', `only ${arrived} of ${count} packets arrived at ${ingress}: lost on the cable, or the counter misses frames`);
  else {
    // more arrived than were sent: say what the extra ones look like and where lwIP says they went
    const extraPkts = m.e0RxPkts - m.w1TxPkts, extraBytes = m.e0RxBytes - m.w1TxBytes;
    const avg = clean(m.w1TxPkts) && extraPkts > 0 ? `, averaging ${Math.round(extraBytes / extraPkts)} bytes` : '';
    const rt = lw('ip_rterr'), dr = lw('ip_drop'), n = arrived - count;
    const why = rt !== null && rt >= 0.8 * n ? `lwIP counted +${rt} "no route" (ip_rterr): packets for addresses the board cannot reach, i.e. this PC sends other traffic (internet, updates) to the board as its default gateway. Give the PC a route for the WiFi subnet only and no default gateway (runbook section 0)`
      : dr !== null && dr >= 0.8 * n ? `lwIP dropped +${dr} (ip_drop)`
      : rt === null ? 'lwIP counters are off, so where they went is unknown'
      : `lwIP counted no routing errors for them (ip_rterr +${rt}, ip_drop +${dr}): the stack took them itself (ARP, ICMP, broadcasts, connections to the board)`;
    add('arrived', 'warn', `${arrived} packets at ${ingress} for ${count} sent: ${n} more than sent${avg}. ${why}`);
  }

  // framing: the egress side is clean when what left it is the stream; the ingress side only when what arrived is
  const sides = [[egress, m.w1TxBytes, m.w1TxPkts, clean(m.w1TxPkts)], [ingress, m.e0RxBytes, m.e0RxPkts, clean(arrived) && clean(m.e0RxPkts)]];
  const side = sides.find(x => x[3] && x[2] > 0);
  if (!side) add('l2', 'warn', `cannot be measured now: ${arrived} packets arrived at ${ingress} and ${m.w1TxPkts} left ${egress} for ${count} sent, so other traffic is mixed in. Run it again on an idle link; nothing is suggested from a mixed average`);
  else {
    const perPkt = side[1] / side[2], extra = perPkt - ipBytes; m.bytesPerPacket = perPkt; m.l2Measured = Math.round(extra); m.l2From = side[0];
    if (Math.abs(extra - 14) <= 3) add('l2', 'pass', `${perPkt.toFixed(1)} bytes per packet on ${side[0]} = IP packet ${ipBytes} + 14: the Ethernet header is counted (D133_L2_BYTES=14, the default)`);
    else if (Math.abs(extra) <= 3) add('l2', 'warn', `${perPkt.toFixed(1)} bytes per packet on ${side[0]} = the IP packet without the Ethernet header: set D133_L2_BYTES=0 so the Mbit/s figures match the link`);
    else add('l2', 'warn', `${perPkt.toFixed(1)} bytes per packet on ${side[0]}, ${extra.toFixed(1)} away from the IP packet (${ipBytes}); the framing is not one of the two expected, nothing is suggested`);
  }

  // forwarded: compare with what was SENT, not with what e0 counted (that may include other traffic)
  if (m.w1TxPkts === 0) add('forwarded', 'fail', `nothing left ${egress}: the board did not forward (RT_LWIP_IP_FORWARD, the receiver's address on the AP's subnet, ARP?)`);
  else if (clean(m.w1TxPkts)) add('forwarded', 'pass', `${m.w1TxPkts} packets left ${egress} for ${count} sent`);
  else if (m.w1TxPkts < count) add('forwarded', 'warn', `only ${m.w1TxPkts} of ${count} packets left ${egress}${m.w1TxErr ? `; the driver refused ${m.w1TxErr}` : ''}${m.e0RxDrop ? `; the stack refused ${m.e0RxDrop} at ${ingress}` : ''}${clean(arrived) ? `; all ${arrived} arrived, so ${arrived - m.w1TxPkts} were lost inside the board` : ''}`);
  else add('forwarded', 'warn', `${m.w1TxPkts} packets left ${egress} for ${count} sent: ${m.w1TxPkts - count} more (other traffic, e.g. ARP or the PC on the other side)`);

  if (m.e0RxDrop > 0 || m.w1TxErr > 0) add('refused', 'warn', `${m.e0RxDrop} frames refused by the stack at ${ingress}, ${m.w1TxErr} by the driver at ${egress}: the board dropped at this gentle rate, so the forwarding path is already short of buffers`);
  else add('refused', 'pass', 'no frame was refused at the calibration rate');

  if (clean(arrived) && clean(m.w1TxPkts) && m.e0RxPkts > 0 && m.w1TxPkts > 0) {
    // Compare the framing without letting a few extra small packets (ARP, ICMP) move the average: if the
    // ingress side saw some packets the egress did not, they account for the difference in bytes, and what
    // matters is whether THEIR average is a real frame size. With equal packet counts the averages are compared.
    const extraPkts = m.e0RxPkts - m.w1TxPkts, extraBytes = m.e0RxBytes - m.w1TxBytes;
    const a = m.e0RxBytes / m.e0RxPkts, b = m.w1TxBytes / m.w1TxPkts;
    m.bytesPerPacketOut = b;
    if (extraPkts === 0) add('symmetry', Math.abs(a - b) <= 3 ? 'pass' : 'warn', Math.abs(a - b) <= 3 ? `both interfaces count the same framing (${a.toFixed(1)} / ${b.toFixed(1)} bytes per packet)` : `${ingress} counts ${a.toFixed(1)} bytes per packet, ${egress} ${b.toFixed(1)}: the two interfaces frame differently, so byte rates are not comparable across them (packet counts still are)`);
    else if (extraPkts > 0) {
      const avg = extraBytes / extraPkts, ok = avg >= 42 && avg <= 1514;
      add('symmetry', ok ? 'pass' : 'warn', ok ? `both interfaces count the same framing: ${ingress} saw ${extraPkts} more packets than ${egress} sent, averaging ${avg.toFixed(0)} bytes, a normal frame size (the plain averages ${a.toFixed(1)} / ${b.toFixed(1)} differ only because of them)` : `${ingress} saw ${extraPkts} more packets than ${egress} sent but they would average ${avg.toFixed(0)} bytes, which is no frame size: the two interfaces frame differently, so byte rates are not comparable across them (packet counts still are)`);
    }
  }
  const fw = before.ip_forwarded >= 0 && after.ip_forwarded >= 0 ? delta32(after.ip_forwarded, before.ip_forwarded) : null;
  if (fw === null) add('lwip', 'warn', 'ip_forwarded is -1: the board was built without RT_LWIP_STATS');
  else { m.ipForwarded = fw; add('lwip', clean(fw) ? 'pass' : 'warn', `lwIP counted ${fw} forwarded packets for ${count} sent`); }
  const verdict = checks.some(c => c.status === 'fail') ? 'fail' : checks.some(c => c.status === 'warn') ? 'warn' : 'pass';
  return { checks, verdict, measured: m };
}

/** Sends `count` datagrams (DATA frames of `payload` bytes) at about `pps`, reading the board before and after. */
export async function calibrate({ to, board, count = 1000, payload = 1000, pps = 500, settleMs = 1500, ingress, egress, log = () => {} }) {
  const [toHost, toPort] = to, [bHost, bPort] = board;
  const before = await readBoardStats(bHost, bPort);
  const names = pickInterfaces(before.ifs, { ingress, egress });
  log(`interfaces: ${names.ingress ?? '?'} in, ${names.egress ?? '?'} out${names.ambiguous.length ? ` (also candidates: ${names.ambiguous.join(', ')}; choose with --ingress / --egress)` : ''}`);
  log(`board ${bHost}:${bPort} read, uptime ${(before.uptime_ms / 1000).toFixed(0)} s; sending ${count} datagrams of ${payload + P.DATA_HEADER} bytes to ${toHost}:${toPort} at ${pps} pkt/s`);
  const sock = dgram.createSocket('udp4'); await new Promise(r => sock.bind(0, r));
  const body = Buffer.alloc(payload, 0x5a), t0 = performance.now(); let sent = 0, errors = 0;
  while (sent < count) {
    const due = Math.min(count, Math.floor((performance.now() - t0) / 1000 * pps) + 1);
    while (sent < due) {
      const buf = P.encodeData({ streamId: 0xC0DE, seq: sent, frameId: sent, captureTs: P.nowUs(), payload: body });
      sock.send(buf, toPort, toHost, e => { if (e) errors++; }); sent++;
    }
    await new Promise(r => setTimeout(r, 1));
  }
  await new Promise(r => setTimeout(r, settleMs));
  sock.close();
  const after = await readBoardStats(bHost, bPort);
  const r = evaluate({ before, after, count: count - errors, datagramBytes: payload + P.DATA_HEADER, ingress: names.ingress, egress: names.egress });
  return { ...r, sent: count, sendErrors: errors, seconds: (after.uptime_ms - before.uptime_ms) / 1000 };
}
