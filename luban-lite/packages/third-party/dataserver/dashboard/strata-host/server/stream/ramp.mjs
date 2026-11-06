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
 *
 * Delay comes before loss: a path that is offered a little too much first builds a queue (the round trip
 * rises step after step) and only drops when the queue is full. `delayKnee` is the last step before the round
 * trip rose by more than delayRiseMs above the lowest step AND stayed risen to the end of the search; one
 * stray spike is not a trend. `recommend` is 70 % of the lower of the two knees, rounded down to 0.1 Mbit/s.
 */
export class RampSearch {
  constructor({ startBps, stepBps, maxBps, stepMs = 3000, minReports = 2, settleReports = 1, lossThreshold = 0.01, noFeedbackMs = 5000, minExpected = 300, minLost = 5, delayRiseMs = 30 }) {
    if (!(startBps > 0 && stepBps > 0 && maxBps >= startBps)) throw new RangeError('need 0 < start <= max and step > 0');
    Object.assign(this, { stepBps, maxBps, stepMs, minReports, settleReports, lossThreshold, noFeedbackMs, minExpected, minLost, delayRiseMs });
    this.rate = startBps; this.results = []; this.done = false; this.knee = null; this.reason = null; this.delayKnee = null; this.recommend = null; this.rtts = [];
    this.startedAt = null; this.stepStart = null; this.reports = []; this.skipped = 0; this.lastReportAt = null;
  }
  begin(nowMs) { this.startedAt = this.stepStart = this.lastReportAt = nowMs; return this.rate; }
  /** A round trip sample (ms) taken while this step was running. */
  noteRtt(ms) { if (!this.done && Number.isFinite(ms) && ms >= 0) { this.rtts.push(ms); if (this.rtts.length > 64) this.rtts.shift(); } }
  finish(reason, knee) {
    this.done = true; this.reason = reason; this.knee = knee;
    this.delayKnee = this.findDelayKnee();
    const lowest = [knee, this.delayKnee].filter(v => v !== null);
    this.recommend = lowest.length ? Math.floor(Math.min(...lowest) * 0.7 / 1e5) * 1e5 : null;
    return this.rate;
  }
  findDelayKnee() {
    const steps = this.results, med = steps.map(x => x.rttMed).filter(Number.isFinite);
    if (steps.length < 2 || med.length < 2) return null;
    const limit = Math.min(...med) + this.delayRiseMs;
    let i = steps.length;
    while (i > 0 && Number.isFinite(steps[i - 1].rttEnd) && steps[i - 1].rttEnd > limit) i--;     // the run of risen steps that reaches the end
    return i === steps.length || i === 0 ? null : steps[i - 1].bps;
  }
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
    const rt = this.rtts, sorted = [...rt].sort((a, b) => a - b);
    this.results.push({ bps: this.rate, loss, expected, lost,
      rttMed: rt.length ? sorted[Math.floor(sorted.length / 2)] : null, rttEnd: rt.length ? Math.max(...rt.slice(-2)) : null });
    if (loss > this.lossThreshold && lost >= this.minLost) {
      const knee = this.results.length > 1 ? this.results.at(-2).bps : null;
      this.finish('loss', knee);
      if (knee !== null) this.rate = knee;                         // settle at what the path carried
      return this.rate;
    }
    if (this.rate + this.stepBps > this.maxBps) return this.finish('max', this.rate);
    this.rate += this.stepBps; this.stepStart = nowMs; this.reports = []; this.rtts = []; this.skipped = 0;
    return this.rate;
  }
  /** The sender's own process was paused: what was collected for this step is distorted, so measure it again. */
  invalidate(nowMs) {
    if (this.done) return;
    this.reports = []; this.rtts = []; this.skipped = 0; this.stepStart = nowMs; this.lastReportAt = nowMs;
  }
  /** No report for noFeedbackMs: the receiver is not answering, the ramp cannot measure anything. */
  onSilence(nowMs) {
    if (!this.done && nowMs - this.lastReportAt >= this.noFeedbackMs) this.finish('no-feedback', null);
    return this.rate;
  }
}
