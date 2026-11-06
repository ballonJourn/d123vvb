/**
 * End-to-end stream panel of the D133 board screen.
 *
 * A UDP stream PC1 -> D133 -> PC2, measured from both ends and from the board's own counters:
 * rate, loss (exact, from sequence numbers), reordering, jitter, round trip, frames completed or
 * dropped, queue drops, and WHERE on the path the datagrams are lost (cable / inside the D133 / WiFi).
 * Run the sender on PC1 and the receiver on PC2; the sender's screen shows the whole path because
 * the receiver reports back what it got.
 *
 * Data: server/stream/engine.mjs  GET /api/d133/stream, POST /start, POST /stop
 */
import { tr } from '../lib/i18n';
import { useEffect, useState } from 'react';
import { api } from '../lib/client';
import { ReferenceSpark } from '../components/charts';
import { Btn, Empty, Pill } from '../components/ui';
import { MonitorMetric } from './Monitor';
import type { PathSegment, Series, StreamState } from './d133-types';
import { bps, last, ms, num, pct, pps } from './d133-format';

const toMbit = (s: Series | undefined): Series => (s ?? []).map(v => num(v) ? v * 8 / 1e6 : null);
const toPct = (s: Series | undefined): Series => (s ?? []).map(v => num(v) ? v * 100 : null);

const MODES = [
  { id: 'fixed', label: 'Fixed rate', hint: 'Sends at the rate you set.' },
  { id: 'adaptive', label: 'Adaptive', hint: 'Lowers the rate when the receiver reports loss and raises it again on clean reports.' },
  { id: 'ramp', label: 'Find the limit', hint: 'Raises the rate step by step and stops where loss begins.' },
] as const;

function Node({ title, value, sub }: { title: string; value: string; sub?: string }) {
  return <div className="d133-node"><div className="xs muted">{tr(title)}</div><div className="d133-node__value num">{value}</div>{sub ? <div className="xs muted">{sub}</div> : null}</div>;
}
function Seg({ s }: { s: PathSegment }) {
  const state = !s.known ? 'unknown' : s.flagged ? 'loss' : 'ok';
  return <div className="d133-seg" data-state={state} title={tr(s.label)}>
    <span className="d133-seg__line"/>
    <span className="d133-seg__text">{!s.known ? 'no data' : s.flagged ? `\u2212${pct(s.fraction)}` : 'ok'}</span>
    {s.known && s.flagged ? <span className="xs">{pps(s.lost)} lost</span> : null}
  </div>;
}

export function D133Stream() {
  const [st, setSt] = useState<StreamState | null>(null);
  const [problem, setProblem] = useState<string | null>(null);
  const [role, setRole] = useState<'send' | 'recv'>('send');
  const [f, setF] = useState({ host: '', port: '5600', mode: 'fixed', mbit: '4', fps: '30', rampStart: '1', rampStep: '1', maxMbit: '20', stepMs: '3000', recvPort: '5600' });
  const set = (k: keyof typeof f) => (e: { target: { value: string } }) => setF(v => ({ ...v, [k]: e.target.value }));

  useEffect(() => {
    let live = true, id = 0;
    const tick = () => void api.get<StreamState>('/api/d133/stream').then(s => { if (live) { setSt(s); if (s.role !== 'idle') setRole(s.role); } })
      .catch(() => {}).finally(() => { if (live) id = window.setTimeout(tick, 1000); });
    tick();
    return () => { live = false; window.clearTimeout(id); };
  }, []);

  const running = !!st && st.role !== 'idle';
  const start = async () => {
    const body = role === 'recv' ? { role, port: Number(f.recvPort) } : {
      role, host: f.host.trim(), port: Number(f.port), mode: f.mode, bitrate: Number(f.mbit) * 1e6, fps: Number(f.fps),
      startBps: Number(f.rampStart) * 1e6, stepBps: Number(f.rampStep) * 1e6, maxBps: Number(f.maxMbit) * 1e6, stepMs: Number(f.stepMs),
    };
    try { setSt(await api.post<StreamState>('/api/d133/stream/start', body)); setProblem(null); }
    catch (e: any) { setProblem(String(e?.message ?? e)); }
  };
  const stop = async () => { try { setSt(await api.post<StreamState>('/api/d133/stream/stop', {})); setProblem(null); } catch (e: any) { setProblem(String(e?.message ?? e)); } };

  const h = st?.history, s = st?.stats, send = st?.role === 'send', rep = s?.lastReport ?? null;
  const t = h?.t ?? [];
  const mode = (st?.params?.mode as string | undefined) ?? f.mode;
  const wire = send ? h?.txWire : h?.rxWire;

  return <>
    <section className="st-card">
      <div className="section-toolbar">
        <h2 className="h4">{tr('End-to-end stream')}</h2>
        {tr(running ? <Pill tone={st?.error ? 'danger' : 'accent'} dot>{tr(send ? 'Sending' : 'Receiving')}</Pill> : <Pill dot>{tr('Idle')}</Pill>)}
      </div>
      <p className="xs muted">{tr('UDP stream from PC1 through the D133 to PC2. Nothing is retransmitted: old data is dropped, never queued. Run the sender on PC1 and the receiver on PC2; the sender\u2019s screen shows the whole path.')}</p>
      <div className="d133-stream-form">
        <div className="d133-seg-control" role="group" aria-label={tr('Role')}>
          <Btn variant={role === 'send' ? 'primary' : 'secondary'} disabled={running} onClick={() => setRole('send')}>{tr('Send (PC1)')}</Btn>
          <Btn variant={role === 'recv' ? 'primary' : 'secondary'} disabled={running} onClick={() => setRole('recv')}>{tr('Receive (PC2)')}</Btn>
        </div>
        {role === 'send' ? <>
          <label className="input-field"><span>{tr('Send to (PC2 address)')}</span><input className="st-input" value={f.host} placeholder="192.168.169.x" onChange={set('host')} disabled={running} spellCheck={false} autoComplete="off"/></label>
          <label className="input-field"><span>{tr('UDP port')}</span><input className="st-input" inputMode="numeric" value={f.port} onChange={set('port')} disabled={running}/></label>
          <label className="input-field"><span>{tr('Mode')}</span>
            <select className="st-input" value={f.mode} onChange={set('mode')} disabled={running}>{MODES.map(m => <option key={m.id} value={m.id}>{m.label}</option>)}</select></label>
          {f.mode !== 'ramp' ? <label className="input-field"><span>{tr('Rate (Mbit/s)')}</span><input className="st-input" inputMode="decimal" value={f.mbit} onChange={set('mbit')} disabled={running}/></label>
            : <label className="input-field"><span>{tr('Start (Mbit/s)')}</span><input className="st-input" inputMode="decimal" value={f.rampStart} onChange={set('rampStart')} disabled={running}/></label>}
          {f.mode === 'ramp' ? <label className="input-field"><span>{tr('Step (Mbit/s)')}</span><input className="st-input" inputMode="decimal" value={f.rampStep} onChange={set('rampStep')} disabled={running}/></label> : null}
          {f.mode !== 'fixed' ? <label className="input-field"><span>{tr('Max (Mbit/s)')}</span><input className="st-input" inputMode="decimal" value={f.maxMbit} onChange={set('maxMbit')} disabled={running}/></label> : null}
          {f.mode === 'ramp' ? <label className="input-field"><span>{tr('Step length (ms)')}</span><input className="st-input" inputMode="numeric" value={f.stepMs} onChange={set('stepMs')} disabled={running}/></label> : null}
          <label className="input-field"><span>{tr('Frames per second')}</span><input className="st-input" inputMode="numeric" value={f.fps} onChange={set('fps')} disabled={running}/></label>
        </> : <label className="input-field"><span>{tr('Listen on UDP port')}</span><input className="st-input" inputMode="numeric" value={f.recvPort} onChange={set('recvPort')} disabled={running}/></label>}
        <div className="d133-form__actions">{running ? <Btn icon="refresh" onClick={() => void stop()}>{tr('Stop')}</Btn> : <Btn variant="primary" icon="download" onClick={() => void start()}>{tr('Start')}</Btn>}</div>
      </div>
      <p className="xs muted">{tr(MODES.find(m => m.id === f.mode)?.hint ?? '')}{role === 'send' ? tr(' Rates are application bits per second (frame payload); the link carries about 5 % more with headers.') : ''}</p>
      {tr(problem || st?.error ? <p className="xs inline-error">{tr(problem ?? st?.error ?? '')}</p> : null)}
    </section>

    {tr(!running ? <section className="st-card monitor-metric"><Empty icon="branch" title={tr('No stream running')} body={tr('Start a receiver on PC2, then a sender on PC1 pointing at PC2\u2019s address on the board\u2019s WiFi.')}/></section> : null)}

    {tr(running ? <div className="monitor-metrics">
      <MonitorMetric label={tr(send ? 'Sent' : 'Received')} icon="branch" source={send ? s?.target ?? '' : `UDP ${s?.listening ?? ''}`}
        value={bps(num(last(wire)) ? last(wire)! * 8 : null)}
        sub={send ? `Target ${bps(s?.targetBps)} \u00b7 ${pps(last(h?.txPps))}${s?.adapt ? ` \u00b7 ${s.adapt.last}` : ''}` : `${pps(last(h?.rxPps))} \u00b7 ${s?.packets ?? 0} datagrams`}
        chart={<ReferenceSpark label={send ? 'Stream rate' : 'Receive rate'} primaryLabel={send ? 'Sent' : 'Received'} data={toMbit(wire)} unit="Mbit/s" timestamps={t}
          secondary={send ? { data: toMbit(h?.rxWire), label: 'Received at PC2', tone: 'var(--st-info)' } : undefined}/>}/>
      <MonitorMetric label={tr('Loss')} icon="link" source="from sequence numbers"
        value={pct(send ? rep?.lossFraction : s?.lossFraction)}
        sub={send ? (rep ? (rep.expected === 0 ? 'The receiver saw no datagrams in the last interval' : `${rep.lost} lost of ${rep.expected} \u00b7 ${rep.reordered} reordered \u00b7 ${rep.duplicates} duplicates`) : 'Waiting for the receiver\u2019s report')
                  : `${s?.lost ?? 0} lost of ${s?.expected ?? 0} \u00b7 ${s?.reordered ?? 0} reordered \u00b7 ${s?.duplicates ?? 0} duplicates`}
        chart={<ReferenceSpark label="Loss" primaryLabel="Loss" data={toPct(h?.loss)} unit="%" timestamps={t} tone="var(--st-danger)"/>}/>
      <MonitorMetric label={tr('Jitter')} icon="activity" source="RFC 3550"
        value={ms(num(send ? (rep?.jitterUs ?? null) : (s?.jitterUs ?? null)) ? (send ? rep!.jitterUs : s!.jitterUs!) / 1000 : null, 2)}
        sub={send ? `Round trip ${ms(s?.rttMs)}` : `${s?.malformed ?? 0} malformed datagrams`}
        chart={<ReferenceSpark label="Jitter" primaryLabel="Jitter" data={h?.jitterMs ?? []} unit="ms" timestamps={t} tone="var(--st-warn)"
          secondary={send ? { data: h?.rttMs ?? [], label: 'Round trip', tone: 'var(--st-info)' } : undefined}/>}/>
      <MonitorMetric label={tr('Frames')} icon="download" source="reassembled"
        value={send ? `${s?.txFrames ?? 0} sent` : `${s?.framesOk ?? 0} complete`}
        sub={send ? `${s?.queueDropped ?? 0} dropped in the sender\u2019s queue \u00b7 ${s?.sendErrors ?? 0} send errors`
                  : `${s?.framesDropped ?? 0} incomplete dropped \u00b7 ${s?.lateFragments ?? 0} late fragments`}
        chart={<div className="xs muted">{send ? (rep ? `Receiver: ${rep.framesOk} complete, ${rep.framesDropped} incomplete dropped, ${rep.queueDropped} dropped from its queue in the last ${(rep.intervalMs / 1000).toFixed(1)} s. Report age ${s?.reportAgeMs ?? '\u2014'} ms.` : 'No report yet.')
                                : `${s?.queueDropped ?? 0} dropped from the receiver\u2019s queue \u00b7 ${s?.openFrames ?? 0} frames in progress \u00b7 ${s?.tooLate ?? 0} datagrams too late to classify`}</div>}/>
    </div> : null)}

    {tr(send && st?.path ? <section className="st-card">
      <div className="section-toolbar"><h2 className="h4">{tr('Where the stream loses packets')}</h2>
        <Pill tone={st.path.located.flagged.length ? 'danger' : st.path.located.unknown.length ? 'warn' : 'accent'} dot>{tr(st.path.located.flagged.length ? 'Loss' : st.path.located.unknown.length ? 'Incomplete data' : 'No loss')}</Pill></div>
      <p className={st.path.located.flagged.length ? 'small inline-error' : 'small muted'}>{tr(st.path.located.text)}</p>
      <div className="d133-path">
        <Node title="PC1 sends" value={pps(st.path.rates.tx)}/>
        <Seg s={st.path.located.segments[0]}/>
        <Node title={`D133 ${st.path.ingressIf} arrived`} value={pps(st.path.rates.e0Rx)} sub={`${pps(st.path.boardRefused.e0RxDrop)} refused by the stack`}/>
        <Seg s={st.path.located.segments[1]}/>
        <Node title={`D133 ${st.path.egressIf} sent`} value={pps(st.path.rates.w1Tx)} sub={`${pps(st.path.boardRefused.w1TxErr)} refused by the driver`}/>
        <Seg s={st.path.located.segments[2]}/>
        <Node title="PC2 receives" value={pps(st.path.rates.rx)}/>
      </div>
      <p className="xs muted">{tr(`Mean of the last ${st.path.window} s, in datagrams per second. A frame the D133 refused at ${st.path.ingressIf} did cross the cable, so it counts as arrived and its loss is inside the D133. Differences below 20 packets/s or 2 % are treated as noise (ARP, the dashboard\u2019s own polling).`)}</p>
    </section> : null)}

    {tr(send && mode === 'ramp' && s?.ramp ? <section className="st-card">
      <div className="section-toolbar"><h2 className="h4">{tr('Rate limit search')}</h2>
        <Pill tone={s.ramp.done ? (s.ramp.knee === null && s.ramp.delayKnee == null ? 'danger' : 'accent') : 'warn'} dot>{tr(s.ramp.done ? 'Finished' : 'Running')}</Pill></div>
      {tr(s.ramp.done ? (s.ramp.reason === 'no-data' ? <p className="small">{tr('The receiver saw nothing during a step, so nothing was measured. Is the receiver running and reachable?')}</p>
        : s.ramp.knee === null && s.ramp.delayKnee == null ? <p className="small">{tr(`No sustainable rate found (${s.ramp.reason}).`)}</p>
        : <div className="stack">
            <p className="small">{tr(s.ramp.knee === null ? 'No loss-free step.' : `Loss begins above ${bps(s.ramp.knee)} (application bitrate).`)}</p>
            <p className="small">{tr(s.ramp.delayKnee == null ? 'The round trip did not rise before the loss.' : `The round trip starts to rise above ${bps(s.ramp.delayKnee)}: a queue builds before anything is lost.`)}</p>
            {s.ramp.recommend != null ? <p className="small"><strong>{tr(`Suggested sustained rate: about ${bps(s.ramp.recommend)}`)}</strong>{tr(' (70 % of the lower of the two). The sender stays at the loss-free rate until you stop it.')}</p> : null}
          </div>)
        : <p className="small muted">{tr(`Testing ${bps(s.ramp.rate)} \u2026`)}</p>)}
      <div className="st-tablewrap"><table className="st-table native-performance-table">
        <thead><tr><th>{tr('Step')}</th><th>{tr('Rate')}</th><th>{tr('Loss')}</th><th>{tr('Datagrams')}</th><th>{tr('Round trip')}</th></tr></thead>
        <tbody>{tr(s.ramp.results.length ? s.ramp.results.map((r, i) => <tr key={i}><td className="num">{i + 1}</td><td className="num">{tr(bps(r.bps))}</td>
          <td><Pill tone={r.loss > 0.01 ? 'danger' : 'accent'} dot>{tr(pct(r.loss))}</Pill></td><td className="num">{tr(`${r.lost} lost of ${r.expected}`)}</td>
          <td className="num">{tr(num(r.rttMed) ? `${r.rttMed.toFixed(0)} ms` : '\u2014')}<small>{tr(num(r.rttEnd) ? `end of step ${r.rttEnd.toFixed(0)} ms` : '')}</small></td></tr>)
          : <tr><td colSpan={5}>{tr('The first step is still being measured.')}</td></tr>)}</tbody>
      </table></div>
    </section> : null)}

    {tr(running && st?.events.length ? <section className="st-card"><h2 className="h4">{tr('Stream events')}</h2>
      <pre className="d133-log">{st.events.map(e => `[${new Date(e.t).toLocaleTimeString()}]  ${e.text}`).join('\n')}</pre></section> : null)}
  </>;
}
