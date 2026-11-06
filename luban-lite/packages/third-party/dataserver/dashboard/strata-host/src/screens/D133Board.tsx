/**
 * D133 board: the whole data path of a stream through the board, and the files on its SD card.
 *
 *   stream (D133Stream)  UDP PC1 -> D133 -> PC2: rate, loss, reorder, jitter, round trip, frames,
 *                        and where on the path the loss happens
 *   interfaces           e01 (Ethernet) and w03 (WiFi AP) on this board: bytes, packets, refused frames, driver errors, per second
 *   lwIP (D133Lwip)      drop counters, buffer pools and the limits they are read against
 *   batch                after the fact: pull a directory off the SD card over dataServer (TCP)
 *
 * Data: server/d133.mjs
 *   /api/d133/board  counters from the board's dsstat port (9001), folded into rates and history
 *   /api/d133/pull   NikosMav dataServer pull (port 9000) with per-file progress
 * and server/stream/engine.mjs (/api/d133/stream, see D133Stream.tsx).
 */
import { tr } from '../lib/i18n';
import { useEffect, useState } from 'react';
import { api } from '../lib/client';
import { Screen } from '../components/layout';
import { ReferenceSpark } from '../components/charts';
import { Bar, Btn, Empty, Pill } from '../components/ui';
import { MonitorMetric } from './Monitor';
import { D133Stream } from './D133Stream';
import { D133Lwip } from './D133Lwip';
import type { BoardState, Config, PullState } from './d133-types';
import { busy, bytes, kb, num, pps, rate } from './d133-format';

const TONES = ['var(--st-accent)', 'var(--st-info)', 'var(--st-warn)', 'var(--st-danger)'];
const STATUS: Record<PullState['status'], string> = { IDLE: 'Idle', CONNECTING: 'Connecting', RECEIVING: 'Receiving', SUCCESS: 'Done', FAILED: 'Nothing received', ERROR: 'Error' };
/** The interfaces a person cares about first: the AP and Ethernet ports of the board. */
const ifLabel = (name: string) => name.startsWith('w') ? `WiFi ${name}` : name.startsWith('e') ? `Ethernet ${name}` : name;
const perSec = (v: number | null | undefined) => num(v) ? (v === 0 ? '0/s' : `${v < 10 ? v.toFixed(2) : v.toFixed(0)}/s`) : '\u2014';

export function D133BoardScreen() {
  const [cfg, setCfg] = useState<Config | null>(null);
  const [host, setHost] = useState(''), [statPort, setStatPort] = useState(''), [dataPort, setDataPort] = useState(''), [dir, setDir] = useState('');
  const [board, setBoard] = useState<BoardState | null>(null);
  const [pull, setPull] = useState<PullState | null>(null);
  const [problem, setProblem] = useState<string | null>(null);

  useEffect(() => {
    void api.get<Config>('/api/d133/config').then(c => { setCfg(c); setHost(c.host); setStatPort(String(c.statPort)); setDataPort(String(c.dataPort)); }).catch(e => setProblem(String(e?.message ?? e)));
  }, []);
  useEffect(() => {          // board stats once a second while the screen is open
    if (!cfg || !host.trim()) return;
    let live = true;
    const tick = () => void api.get<BoardState>(`/api/d133/board?host=${encodeURIComponent(host.trim())}&port=${encodeURIComponent(statPort)}`).then(b => { if (live) setBoard(b); }).catch(() => {});
    tick(); const id = window.setInterval(tick, 1000);
    return () => { live = false; window.clearInterval(id); };
  }, [cfg, host, statPort]);
  useEffect(() => {          // pull progress: fast while receiving
    let live = true, id = 0;
    const tick = () => void api.get<PullState>('/api/d133/pull').then(p => { if (!live) return; setPull(p); id = window.setTimeout(tick, busy(p.status) ? 400 : 1500); }).catch(() => { if (live) id = window.setTimeout(tick, 2000); });
    tick();
    return () => { live = false; window.clearTimeout(id); };
  }, []);

  const start = async () => {
    if (!dir.trim()) { setProblem('Enter a directory below the board root, for example log.'); return; }
    try { await api.post('/api/d133/pull', { host: host.trim(), port: Number(dataPort), directory: dir.trim() }); setProblem(null); setPull(await api.get<PullState>('/api/d133/pull')); }
    catch (e: any) { setProblem(String(e?.message ?? e)); }
  };
  const reset = async () => { try { await api.post('/api/d133/pull/reset', {}); setPull(await api.get<PullState>('/api/d133/pull')); } catch (e: any) { setProblem(String(e?.message ?? e)); } };

  const ifs = board?.ifs ?? [], t = board?.history.t ?? [];
  const ds = board?.dataserver;
  const current = pull?.files.find(f => f.path === pull.current);
  const speed = pull?.speed ?? { t: [], bps: [] };
  const readings = ds ? [
    { label: 'dataServer', value: ds.running ? 'Running' : 'Stopped' },
    { label: 'Connections', value: String(ds.connections) },
    { label: 'Directories sent', value: String(ds.requests_done) },
    { label: 'Files sent', value: String(ds.files_sent) },
    { label: 'Queued files', value: String(ds.queued) },
    { label: 'Rejected', value: String(ds.rejected) },
    { label: 'Payload sent', value: bytes(ds.bytes_sent) },
    { label: 'Board uptime', value: board?.uptimeMs != null ? `${Math.round(board.uptimeMs / 1000)} s` : '\u2014' },
  ] : [];

  return <Screen width="wide" plainHeader title={tr('D133 board')} titleDetail={board?.target}
      sub={tr('A stream through the board, the board\u2019s own counters for it, and the files on its SD card.')}>
    <div className="stack stack--lg monitor-dashboard">
      <section className="st-card">
        <div className="section-toolbar"><h2 className="h4">{tr('Board')}</h2>{tr(board ? <Pill tone={board.ok ? 'accent' : 'danger'} dot>{tr(board.ok ? 'Live' : 'Not reachable')}</Pill> : null)}</div>
        <div className="d133-form d133-form--board">
          <label className="input-field"><span>{tr('Board IP')}</span><input className="st-input" value={host} onChange={e => setHost(e.target.value)} spellCheck={false} autoComplete="off"/></label>
          <label className="input-field"><span>{tr('Stats port')}</span><input className="st-input" inputMode="numeric" value={statPort} onChange={e => setStatPort(e.target.value)}/></label>
        </div>
        <p className="xs muted">{tr('192.168.1.1 from the PC on the Ethernet port, 192.168.169.1 from the PC on the WiFi AP.')}</p>
        {tr(board && !board.ok && board.error ? <p className="xs inline-error">{tr(board.error)}{tr(' \u2014 is the PC on the board\u2019s WiFi or Ethernet, and is DATASERVER_STAT_PORT enabled?')}</p> : null)}
        {tr(problem ? <p className="xs inline-error">{tr(problem)}</p> : null)}
      </section>

      <D133Stream />

      <div className="monitor-metrics">
        {tr(ifs.length ? ifs.map((i, n) => {
          const h = board!.history.ifs[i.name] ?? { rx: [], tx: [] }, tone = TONES[n % TONES.length];
          return <MonitorMetric key={i.name} label={ifLabel(i.name)} icon="link" source={i.ip}
            value={rate((i.rx_rate ?? 0) + (i.tx_rate ?? 0))}
            sub={`\u2193 ${rate(i.rx_rate)} \u00b7 ${pps(i.rx_pps)}  \u2191 ${rate(i.tx_rate)} \u00b7 ${pps(i.tx_pps)}${num(i.rx_drop_rate) && i.rx_drop_rate > 0 ? `  \u00b7 refused ${perSec(i.rx_drop_rate)}` : ''}${num(i.tx_err_rate) && i.tx_err_rate > 0 ? `  \u00b7 driver errors ${perSec(i.tx_err_rate)}` : ''}${i.up ? '' : ' \u00b7 down'}`}
            chart={<ReferenceSpark label={`${i.name} traffic`} primaryLabel="Received" data={h.rx.map(kb)} secondary={{ data: h.tx.map(kb), label: 'Sent', tone: `color-mix(in srgb,${tone} 55%,var(--st-ink))` }} unit="KB/s" timestamps={t} tone={tone}/>}/>;
        }) : <section className="st-card monitor-metric"><Empty icon="link" title={tr(board?.ok === false ? 'Board not reachable' : 'Waiting for the board')} body={tr('Interface counters appear once the stats port answers.')}/></section>)}
        <MonitorMetric label={tr('IP forwarding (Ethernet \u2194 WiFi)')} icon="branch" source="lwIP ip.fw"
          value={board?.fwRate != null ? `${board.fwRate.toFixed(1)}` : '\u2014'} unit={board?.fwRate != null ? 'pkt/s' : undefined}
          sub={board?.ipForwarded != null ? `${board.ipForwarded} packets forwarded` : 'The board was built without RT_LWIP_STATS: forwarded packets are not counted'}
          chart={<div className="xs muted">{tr('Ethernet PC \u2194 board \u2194 WiFi PC. Both interfaces above also count forwarded traffic.')}</div>}/>
      </div>

      {tr(readings.length ? <section className="st-card monitor-readings" aria-label={tr('dataServer on the board')}><dl>{readings.map(r => <div key={r.label}><dt>{tr(r.label)}</dt><dd className="num">{tr(r.value)}</dd></div>)}</dl></section> : null)}

      <section className="st-card">
        <h2 className="h4">{tr('Interfaces')}</h2>
        <div className="st-tablewrap"><table className="st-table native-performance-table">
          <thead><tr><th>{tr('Interface')}</th><th>{tr('IP')}</th><th>{tr('State')}</th><th>{tr('Received')}</th><th>{tr('Sent')}</th><th>{tr('Now')}</th><th>{tr('Packets now')}</th><th>{tr('Refused')}</th></tr></thead>
          <tbody>{tr(ifs.length ? ifs.map(i => <tr key={i.name}>
            <td><strong>{tr(ifLabel(i.name))}</strong></td><td className="num">{tr(i.ip)}</td>
            <td><Pill tone={i.up ? 'accent' : 'danger'} dot>{tr(i.up ? 'Up' : 'Down')}</Pill></td>
            <td className="num">{tr(bytes(i.rx_bytes))}<small>{tr(`${i.rx_pkts} packets`)}</small></td>
            <td className="num">{tr(bytes(i.tx_bytes))}<small>{tr(`${i.tx_pkts} packets`)}</small></td>
            <td className="num">{tr(`\u2193 ${rate(i.rx_rate)}`)}<small>{tr(`\u2191 ${rate(i.tx_rate)}`)}</small></td>
            <td className="num">{tr(`\u2193 ${pps(i.rx_pps)}`)}<small>{tr(`\u2191 ${pps(i.tx_pps)}`)}</small></td>
            <td className="num">{tr(num(i.rx_drop) ? `${i.rx_drop} by the stack` : '\u2014')}<small>{tr(num(i.tx_err) ? `${i.tx_err} by the driver` : '\u2014')}</small></td>
          </tr>) : <tr><td colSpan={8}>{tr('No readings yet')}</td></tr>)}</tbody>
        </table></div>
        <p className="xs muted">{tr('Counters are taken on the board (netmon hooks on lwIP). Received and Sent count frames the stack or the driver accepted; Refused counts the ones it did not (forwarding queue or driver queue full). 32-bit counters wrap; rates account for that.')}</p>
      </section>

      <D133Lwip lwip={board?.lwip} />

      <section className="st-card">
        <div className="section-toolbar"><h2 className="h4">{tr('Batch: pull files from the SD card')}</h2></div>
        <p className="xs muted">{tr('After the fact, not live: copies a directory off the board over dataServer (TCP, every block acknowledged). Use the stream above for live data.')}</p>
        <div className="d133-form d133-form--pull">
          <label className="input-field"><span>{tr('dataServer port')}</span><input className="st-input" inputMode="numeric" value={dataPort} onChange={e => setDataPort(e.target.value)}/></label>
          <label className="input-field"><span>{tr('Directory below the board root')}</span><input className="st-input" value={dir} placeholder="log" onChange={e => setDir(e.target.value)} onKeyDown={e => { if (e.key === 'Enter') void start(); }} spellCheck={false} autoComplete="off"/></label>
          <div className="d133-form__actions"><Btn variant="primary" icon="download" disabled={busy(pull?.status)} onClick={() => void start()}>{tr('Pull')}</Btn><Btn icon="refresh" disabled={busy(pull?.status)} onClick={() => void reset()}>{tr('Reset')}</Btn></div>
        </div>
        {tr(pull?.outDir ? <p className="xs muted">{tr('Files are saved to ')}{tr(pull.outDir)}</p> : null)}
        <div className="monitor-metrics d133-batch-metric">
          <MonitorMetric label={tr('Pull throughput')} icon="download" source={pull?.target ?? 'dataServer'}
            value={busy(pull?.status) ? rate(pull?.nowBps) : rate(pull?.avgBps)} sub={busy(pull?.status) ? `Receiving ${pull?.current ?? ''}` : pull?.avgBps ? `Average of the last pull \u00b7 ${pull?.elapsedS?.toFixed(2)} s` : 'No pull yet'}
            chart={<ReferenceSpark label="Pull throughput" data={speed.bps.map(kb)} unit="KB/s" timestamps={speed.t}/>}/>
        </div>
        <div className="section-toolbar"><h3 className="h4">{tr('Pulled files')}</h3>{tr(pull ? <span className="small muted">{tr(STATUS[pull.status])}{tr(pull.directory ? ` \u00b7 ${pull.directory}` : '')}{tr(pull.blockSize ? ` \u00b7 block ${pull.blockSize} B` : '')}{tr(pull.files.length ? ` \u00b7 ${pull.files.filter(f => f.done).length} / ${pull.files.length} files \u00b7 ${bytes(pull.bytes)}` : '')}</span> : null)}</div>
        {tr(current ? <div className="d133-current"><div className="small"><strong>{tr(current.path)}</strong>{tr(` \u00b7 block ${current.received} / ${current.nblocks}`)}</div><Bar label={tr('Current file')} value={current.nblocks ? current.received / current.nblocks * 100 : 100}/></div> : null)}
        <div className="st-tablewrap"><table className="st-table native-performance-table">
          <thead><tr><th>{tr('File')}</th><th>{tr('Size')}</th><th>{tr('Blocks')}</th><th>{tr('State')}</th><th>{tr('SHA-256 (computed on this PC)')}</th></tr></thead>
          <tbody>{tr(pull?.files.length ? pull.files.map(f => <tr key={f.path}>
            <td><strong>{tr(f.path)}</strong></td><td className="num">{tr(bytes(f.size))}</td><td className="num">{tr(`${f.received} / ${f.nblocks}`)}</td>
            <td><Pill tone={f.done ? 'accent' : 'warn'} dot>{tr(f.done ? 'Saved' : 'Receiving')}</Pill></td>
            <td className="num"><small title={f.sha256 ?? ''}>{tr(f.sha256 ? `${f.sha256.slice(0, 16)}\u2026` : 'pending')}</small></td>
          </tr>) : <tr><td colSpan={5}>{tr('No files yet. Enter a directory and press Pull.')}</td></tr>)}</tbody>
        </table></div>
        <p className="xs muted">{tr('The dataServer protocol has no checksum; compare the SHA-256 with sha256sum on the source.')}</p>
        <h3 className="h4">{tr('Log')}</h3>
        <pre className="d133-log">{tr(pull?.log.length ? pull.log.join('\n') : 'Nothing yet.')}</pre>
      </section>
    </div>
  </Screen>;
}
