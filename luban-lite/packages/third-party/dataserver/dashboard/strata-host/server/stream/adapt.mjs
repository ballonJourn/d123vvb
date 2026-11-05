/**
 * Receiver feedback -> sender bitrate (the sender calls onReport for every REPORT).
 *
 *   loss >= hiLoss in the interval ........ rate *= decrease, and no increase for holdReports reports
 *   nothing arrived in the interval ....... rate *= staleDecrease (a blackout is not "0 % loss")
 *   no REPORT for staleMs ................. rate *= staleDecrease, at most once per staleMs
 *   zero loss and not held ................ rate += increase * max
 *   anything in between ................... keep the rate
 *
 * Without this, a WiFi that gets worse only makes the stream keep pushing at the old rate and
 * delay grows; with it the stream settles at what the path carries.
 */
const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));

export class AdaptiveRate {
  constructor({ start, min, max, hiLoss = 0.02, decrease = 0.85, staleDecrease = 0.5, increase = 0.05, holdReports = 2, staleMs = 1500 }) {
    if (!(min > 0 && max >= min)) throw new RangeError('need 0 < min <= max');
    Object.assign(this, { min, max, hiLoss, decrease, staleDecrease, increase, holdReports, staleMs });
    this.rate = clamp(start ?? max, min, max);
    this.hold = 0; this.last = 'init'; this.lastStaleAt = -Infinity;
  }
  down(factor, why) { this.rate = Math.max(this.min, this.rate * factor); this.last = why; return this.rate; }
  /** r: { expected, lost } of one report interval. Returns the new target rate. */
  onReport(r) {
    if (!(r.expected > 0)) return this.down(this.staleDecrease, 'silence');
    if (r.lost / r.expected >= this.hiLoss) { this.hold = this.holdReports; return this.down(this.decrease, 'loss'); }
    if (this.hold > 0) { this.hold--; this.last = 'hold'; return this.rate; }
    if (r.lost === 0) { this.rate = Math.min(this.max, this.rate + this.increase * this.max); this.last = 'increase'; return this.rate; }
    this.last = 'steady';
    return this.rate;
  }
  /** Call regularly with the time since the last REPORT (or since sending began). */
  onSilence(ageMs, nowMs) {
    if (ageMs >= this.staleMs && nowMs - this.lastStaleAt >= this.staleMs) { this.lastStaleAt = nowMs; return this.down(this.staleDecrease, 'stale'); }
    return this.rate;
  }
}
