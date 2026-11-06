import test from 'node:test';
import assert from 'node:assert/strict';
import { AdaptiveRate } from '../adapt.mjs';
import { RampSearch } from '../ramp.mjs';
import { locateLoss, meanLast, pickInterfaces } from '../locate.mjs';

const M = 1e6;
const rep = (expected, lost) => ({ expected, lost });

test('adaptive: loss at or above the threshold cuts the rate and holds off increases', () => {
  const a = new AdaptiveRate({ start: 8 * M, min: 1 * M, max: 10 * M });
  assert.equal(a.onReport(rep(100, 2)), 8 * M * 0.85);           // 2 % is the threshold
  assert.equal(a.last, 'loss');
  assert.equal(a.onReport(rep(100, 0)), 8 * M * 0.85); assert.equal(a.last, 'hold');
  assert.equal(a.onReport(rep(100, 0)), 8 * M * 0.85);
  assert.ok(a.onReport(rep(100, 0)) > 8 * M * 0.85); assert.equal(a.last, 'increase');
});

test('adaptive: climbs by a fixed step on clean reports and stops at max', () => {
  const a = new AdaptiveRate({ start: 1 * M, min: 1 * M, max: 2 * M, increase: 0.25 });
  assert.equal(a.onReport(rep(50, 0)), 1.5 * M); assert.equal(a.onReport(rep(50, 0)), 2 * M); assert.equal(a.onReport(rep(50, 0)), 2 * M);
});

test('adaptive: a little loss below the threshold keeps the rate', () => {
  const a = new AdaptiveRate({ start: 5 * M, min: 1 * M, max: 10 * M });
  assert.equal(a.onReport(rep(1000, 5)), 5 * M); assert.equal(a.last, 'steady');
});

test('adaptive: never below min, and "nothing arrived" is a blackout, not zero loss', () => {
  const a = new AdaptiveRate({ start: 2 * M, min: 1.5 * M, max: 10 * M });
  assert.equal(a.onReport(rep(0, 0)), 1.5 * M); assert.equal(a.last, 'silence');
  for (let i = 0; i < 20; i++) a.onReport(rep(100, 50));
  assert.equal(a.rate, 1.5 * M);
});

test('adaptive: no reports for staleMs backs off, once per staleMs', () => {
  const a = new AdaptiveRate({ start: 8 * M, min: 1 * M, max: 10 * M, staleMs: 1500 });
  assert.equal(a.onSilence(1000, 1000), 8 * M);
  assert.equal(a.onSilence(1600, 1600), 4 * M); assert.equal(a.last, 'stale');
  assert.equal(a.onSilence(2000, 2000), 4 * M);                  // too soon for another cut
  assert.equal(a.onSilence(3200, 3200), 2 * M);
});

test('adaptive: bad parameters are refused', () => { assert.throws(() => new AdaptiveRate({ start: 1, min: 0, max: 2 }), RangeError); });

/** drives a ramp with a path that carries `limit` bit/s and loses the excess */
function runRamp(limit, opts = {}) {
  const r = new RampSearch({ startBps: 2 * M, stepBps: 1 * M, maxBps: 12 * M, stepMs: 1000, minReports: 2, settleReports: 1, ...opts });
  let t = 0; r.begin(t);
  while (!r.done && t < 600_000) {
    t += 500;
    const loss = r.rate > limit ? (r.rate - limit) / r.rate : 0;
    r.onReport(rep(1000, Math.round(1000 * loss)), t);
  }
  return r;
}

test('ramp: stops at the first step that loses and reports the step below as the knee', () => {
  const r = runRamp(6.5 * M);
  assert.equal(r.done, true); assert.equal(r.reason, 'loss'); assert.equal(r.knee, 6 * M);
  assert.equal(r.rate, 6 * M);                                    // settles at what the path carried
  assert.deepEqual(r.results.map(x => x.bps), [2, 3, 4, 5, 6, 7].map(x => x * M));
  assert.ok(r.results.at(-1).loss > 0.01 && r.results.at(-2).loss === 0);
});

test('ramp: a path that carries everything ends at max', () => {
  const r = runRamp(100 * M);
  assert.equal(r.reason, 'max'); assert.equal(r.knee, 12 * M);
});

test('ramp: loss already at the first step means no sustainable rate (knee null)', () => {
  const r = runRamp(1 * M);
  assert.equal(r.reason, 'loss'); assert.equal(r.knee, null);
});

test('ramp: the first report after a rate change is not counted (it still describes the old rate)', () => {
  const r = new RampSearch({ startBps: 2 * M, stepBps: 1 * M, maxBps: 5 * M, stepMs: 1000, minReports: 1, settleReports: 1, minExpected: 0, minLost: 0 });
  r.begin(0);
  r.onReport(rep(100, 100), 500);                                 // settling report, ignored even though it is all loss
  assert.equal(r.done, false);
  r.onReport(rep(100, 0), 1000); assert.equal(r.rate, 3 * M);
});

test('ramp: a step in which the receiver saw nothing is "no-data", not a loss measurement', () => {
  const r = new RampSearch({ startBps: 2 * M, stepBps: 1 * M, maxBps: 5 * M, stepMs: 500, minReports: 1, settleReports: 0 });
  r.begin(0); r.onReport(rep(0, 0), 500);
  assert.equal(r.reason, 'no-data'); assert.equal(r.knee, null); assert.equal(r.results.length, 0);
});

test('ramp: a stray lost datagram or two is not an overloaded path (minLost)', () => {
  const r = new RampSearch({ startBps: 1 * M, stepBps: 1 * M, maxBps: 5 * M, stepMs: 500, minReports: 1, settleReports: 0, minExpected: 0 });
  r.begin(0);
  r.onReport(rep(270, 3), 500);                                                  // 1.1 %, but only 3 lost
  assert.equal(r.done, false); assert.equal(r.rate, 2 * M);
  r.onReport(rep(270, 6), 1000);                                                 // 6 lost: a real loss
  assert.equal(r.done, true); assert.equal(r.reason, 'loss'); assert.equal(r.knee, 1 * M);
});

test('ramp: a step is judged only after enough datagrams were accounted for (minExpected)', () => {
  const r = new RampSearch({ startBps: 1 * M, stepBps: 1 * M, maxBps: 5 * M, stepMs: 500, minReports: 1, settleReports: 0, minExpected: 300 });
  r.begin(0);
  r.onReport(rep(100, 0), 600); r.onReport(rep(100, 0), 1100);
  assert.equal(r.rate, 1 * M);                                                   // 200 so far: keep measuring this step
  r.onReport(rep(100, 0), 1600);
  assert.equal(r.rate, 2 * M);                                                   // 300: judged, next step
});

test('ramp: no feedback at all ends it with reason no-feedback', () => {
  const r = new RampSearch({ startBps: 2 * M, stepBps: 1 * M, maxBps: 5 * M, noFeedbackMs: 5000 });
  r.begin(0); r.onSilence(4000); assert.equal(r.done, false);
  r.onSilence(5000); assert.equal(r.reason, 'no-feedback'); assert.equal(r.knee, null);
});

test('locate: no loss', () => {
  const l = locateLoss({ tx: 1000, e0Rx: 1000, w1Tx: 1000, rx: 1000 });
  assert.equal(l.where, null); assert.deepEqual(l.flagged, []); assert.equal(l.text, 'No loss on the path');
});

test('locate: each segment is found where the loss is', () => {
  assert.equal(locateLoss({ tx: 1000, e0Rx: 800, w1Tx: 800, rx: 800 }).where, 'cable');
  assert.equal(locateLoss({ tx: 1000, e0Rx: 1000, w1Tx: 700, rx: 700 }).where, 'd133');
  assert.equal(locateLoss({ tx: 1000, e0Rx: 1000, w1Tx: 1000, rx: 900 }).where, 'wifi');
});

test('locate: losses in several segments are all listed, the biggest first', () => {
  const l = locateLoss({ tx: 1000, e0Rx: 950, w1Tx: 800, rx: 770 });
  assert.deepEqual(l.flagged, ['d133', 'cable', 'wifi']);
  assert.equal(l.where, 'd133');
  assert.ok(Math.abs(l.segments[1].fraction - 150 / 950) < 1e-9);
});

test('locate: differences inside the noise floor and below the threshold are not loss', () => {
  assert.equal(locateLoss({ tx: 1000, e0Rx: 985, w1Tx: 985, rx: 985 }).where, null);          // 15 pkt/s < 20 pkt/s
  assert.equal(locateLoss({ tx: 10000, e0Rx: 9900, w1Tx: 9900, rx: 9900 }).where, null);      // 1 % < 2 %
  const more = locateLoss({ tx: 1000, e0Rx: 1040, w1Tx: 1040, rx: 1040 });                    // more arrived than was sent (other traffic): never loss
  assert.equal(more.where, null); assert.equal(more.segments[0].lost, 0);
  assert.equal(locateLoss({ tx: 1000, e0Rx: 900, w1Tx: 900, rx: 900, noise: 200 }).where, null);   // the noise floor is a parameter
});

test('locate: missing numbers are unknown, never zero', () => {
  const l = locateLoss({ tx: 1000, e0Rx: null, w1Tx: 1000, rx: 500 });
  assert.deepEqual(l.unknown, ['cable', 'd133']); assert.equal(l.where, 'wifi');
  assert.equal(locateLoss({}).text, 'No data yet');
});

test('locate: frames the D133 refused at e0 crossed the cable, so their loss is inside the D133', () => {
  // 1000 sent; e0 accepted 700 and refused 300 -> 1000 arrived at e0; w1 sent 700
  const l = locateLoss({ tx: 1000, e0Rx: 700 + 300, w1Tx: 700, rx: 700 });
  assert.equal(l.where, 'd133'); assert.deepEqual(l.flagged, ['d133']);
});

test('meanLast averages the last n numbers and skips gaps', () => {
  assert.equal(meanLast([1, 2, 3, 4, 5, 6], 3), 5);
  assert.equal(meanLast([null, 2, null, 4], 4), 3);
  assert.equal(meanLast([], 5), null); assert.equal(meanLast(undefined), null);
});

const NIC = (name, ip = '192.168.1.1', up = 1) => ({ name, ip, up });

test('interfaces: the real board names (e01 = Ethernet, w03 = WiFi AP) are found', () => {
  assert.deepEqual(pickInterfaces([NIC('w03', '192.168.169.1'), NIC('e01')]), { ingress: 'e01', egress: 'w03', ambiguous: [] });
});

test('interfaces: other naming schemes, an explicit name wins, extra candidates are listed', () => {
  assert.deepEqual(pickInterfaces([NIC('e0'), NIC('w1', '192.168.169.1')]), { ingress: 'e0', egress: 'w1', ambiguous: [] });
  assert.equal(pickInterfaces([NIC('e01'), NIC('w03'), NIC('w00', '10.0.0.5')], { egress: 'w00' }).egress, 'w00');
  const two = pickInterfaces([NIC('e01'), NIC('w03'), NIC('w00', '10.0.0.5')]);
  assert.equal(two.egress, 'w03'); assert.deepEqual(two.ambiguous, ['w00']);
});

test('interfaces: a down interface or one without an address is not chosen while another is usable, but is still better than none', () => {
  assert.equal(pickInterfaces([NIC('w00', '0.0.0.0'), NIC('w03', '192.168.169.1'), NIC('e01')]).egress, 'w03');
  assert.equal(pickInterfaces([NIC('w03', '192.168.169.1', 0), NIC('e01', '192.168.1.1', 0)]).egress, 'w03');
  assert.deepEqual(pickInterfaces([NIC('lo', '127.0.0.1')]), { ingress: null, egress: null, ambiguous: [] });
  assert.deepEqual(pickInterfaces(undefined), { ingress: null, egress: null, ambiguous: [] });
});

test('ramp: invalidate() throws away what the step collected, so a pause of the sender cannot decide it', () => {
  const r = new RampSearch({ startBps: 2 * M, stepBps: 1 * M, maxBps: 5 * M, stepMs: 1000, minReports: 2, settleReports: 1, minExpected: 0, minLost: 0 });
  r.begin(0);
  r.onReport(rep(100, 100), 500); r.onReport(rep(100, 100), 1000);              // would be total loss
  r.invalidate(1100);
  assert.equal(r.done, false); assert.equal(r.reports.length, 0);
  r.onReport(rep(100, 0), 1600);                                                 // settling report after the pause
  r.onReport(rep(100, 0), 2200); r.onReport(rep(100, 0), 2700);
  assert.equal(r.done, false); assert.equal(r.rate, 3 * M);                      // judged on the clean reports only
});
