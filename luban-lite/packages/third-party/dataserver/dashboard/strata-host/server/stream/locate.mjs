/**
 * Where does a stream lose datagrams? Four packet rates (packets/s) taken at four points:
 *
 *   PC1 sent -> [cable] -> arrived at D133 e0 -> [inside D133] -> left D133 on w1 -> [WiFi] -> PC2 received
 *     tx                    e0Rx                                    w1Tx                       rx
 *
 *  tx    the sender's own datagram counter
 *  e0Rx  frames that ARRIVED at e0: rx_pkts + rx_dropped. A frame the stack refused (forwarding
 *        queue full) did cross the cable, so it counts as arrived and its loss is "inside D133"
 *  w1Tx  frames w1's driver accepted (tx_pkts); a frame it refused (tx_err) never left the board
 *  rx    datagrams PC2 received, from the receiver's report
 *
 * Packets, not bytes: a datagram is lost whatever its size. A segment is flagged when more leave
 * its start than arrive at its end by more than `noise` packets/s (ARP, the dashboard's own stats
 * polling, report datagrams) and by more than `threshold` of what entered. A missing number makes
 * its segments "unknown"; it is never read as zero.
 */
export function locateLoss({ tx, e0Rx, w1Tx, rx, noise = 20, threshold = 0.02 }) {
  const seg = (id, label, inRate, outRate) => {
    if (inRate == null || outRate == null) return { id, label, inRate, outRate, lost: null, fraction: null, flagged: false, known: false };
    const lost = Math.max(0, inRate - outRate);
    return { id, label, inRate, outRate, lost, fraction: inRate > 0 ? lost / inRate : 0, flagged: lost > Math.max(noise, threshold * inRate), known: true };
  };
  const segments = [
    seg('cable', 'PC1 \u2192 D133 (Ethernet e0)', tx, e0Rx),
    seg('d133', 'Inside D133 (e0 \u2192 w1)', e0Rx, w1Tx),
    seg('wifi', 'D133 \u2192 PC2 (WiFi w1)', w1Tx, rx),
  ];
  const flagged = segments.filter(s => s.flagged).sort((a, b) => b.lost - a.lost);
  const unknown = segments.filter(s => !s.known).map(s => s.id);
  const where = flagged[0]?.id ?? null;
  const text = where ? `Loss: ${flagged.map(s => `${s.label} ${(s.fraction * 100).toFixed(1)} %`).join('; ')}`
    : unknown.length === segments.length ? 'No data yet' : unknown.length ? `No loss in the measured segments (unknown: ${unknown.join(', ')})` : 'No loss on the path';
  return { segments, where, flagged: flagged.map(s => s.id), unknown, text };
}

/** Mean of the last n numeric entries, null when there are none. */
export function meanLast(values, n = 5) {
  const v = (values ?? []).slice(-n).filter(x => typeof x === 'number' && Number.isFinite(x));
  return v.length ? v.reduce((a, b) => a + b, 0) / v.length : null;
}
