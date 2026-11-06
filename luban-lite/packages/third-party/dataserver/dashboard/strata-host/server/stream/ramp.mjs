/**
 * Finds the bitrate at which loss begins: raises the target in steps and watches the receiver's
 * loss in each step. The result, `knee`, is the highest step whose loss stayed <= lossThreshold
 * (null when even the first step lost more). It is an application bitrate (frame payload bits/s);
 * the link carries about 5 % more with the headers.
 *
 * Each step lasts stepMs and needs minReports reports; the first settleReports reports after a
 * change are skipped because they still describe the previous rate. A step is judged only after the
 * receiver has accounted for minExpected datagrams (at 1 Mbit/s that is more than one step length), and
 * it counts as "loss" only if at least minLost datagrams were lost: WiFi loses a stray datagram now and
 * then, and 3 lost out of 270 is 1.1 % but not an overloaded path.
 * A step in which the receiver saw nothing at all ends the search with reason "no-data": that is a
 * receiver that is not running or not answering, not a loss measurement.
 */
export class RampSearch {
  constructor({ startBps, stepBps, maxBps, stepMs = 3000, minReports = 2, settleReports = 1, lossThreshold = 0.01, noFeedbackMs = 5000, minExpected = 300, minLost = 5 }) {
    if (!(startBps > 0 && stepBps > 0 && maxBps >= startBps)) throw new RangeError('need 0 < start <= max and step > 0');
    Object.assign(this, { stepBps, maxBps, stepMs, minReports, settleReports, lossThreshold, noFeedbackMs, minExpected, minLost });
    this.rate = startBps; this.results = []; this.done = false; this.knee = null; this.reason = null;
    this.startedAt = null; this.stepStart = null; this.reports = []; this.skipped = 0; this.lastReportAt = null;
  }
  begin(nowMs) { this.startedAt = this.stepStart = this.lastReportAt = nowMs; return this.rate; }
  finish(reason, knee) { this.done = true; this.reason = reason; this.knee = knee; return this.rate; }
  /** r: { expected, lost } of one report interval. Returns the target rate to use now. */
  onReport(r, nowMs) {
    if (this.done) return this.rate;
    this.lastReportAt = nowMs;
    if (this.skipped < this.settleReports) { this.skipped++; return this.rate; }
    this.reports.push(r);
    if (this.reports.length < this.minReports || nowMs - this.stepStart < this.stepMs) return this.rate;
    const expected = this.reports.reduce((a, x) => a + x.expected, 0), lost = this.reports.reduce((a, x) => a + x.lost, 0);
    if (expected === 0) return this.finish('no-data', null);       // nothing arrived: the receiver is not answering
    if (expected < this.minExpected) return this.rate;             // too few datagrams to judge: keep measuring this step
    const loss = lost / expected;
    this.results.push({ bps: this.rate, loss, expected, lost });
    if (loss > this.lossThreshold && lost >= this.minLost) {
      const knee = this.results.length > 1 ? this.results.at(-2).bps : null;
      this.finish('loss', knee);
      if (knee !== null) this.rate = knee;                         // settle at what the path carried
      return this.rate;
    }
    if (this.rate + this.stepBps > this.maxBps) return this.finish('max', this.rate);
    this.rate += this.stepBps; this.stepStart = nowMs; this.reports = []; this.skipped = 0;
    return this.rate;
  }
  /** No report for noFeedbackMs: the receiver is not answering, the ramp cannot measure anything. */
  onSilence(nowMs) {
    if (!this.done && nowMs - this.lastReportAt >= this.noFeedbackMs) this.finish('no-feedback', null);
    return this.rate;
  }
}
