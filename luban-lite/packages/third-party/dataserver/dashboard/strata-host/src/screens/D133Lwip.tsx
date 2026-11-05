/**
 * lwIP on the board: the drop counters and buffer pools that explain WHY a forwarded stream loses
 * packets, next to the compile-time limits they are read against (PBUF_POOL_SIZE,
 * MEMP_NUM_TCPIP_MSG_INPKT, TCPIP_MBOX_SIZE).
 *
 * Source: the "lwip" object of the board's stats line (rtthread/netmon.c, netmon_lwip()).
 * The limits are tuned only after these numbers show which one runs out; a frame refused because
 * the tcpip mailbox is full is not counted by lwIP at all, it shows as "Refused" on the interface.
 */
import { tr } from '../lib/i18n';
import { Bar, Pill } from '../components/ui';
import type { BoardLwip } from './d133-types';
import { num } from './d133-format';

const ROWS: [string, string, string][] = [
  ['ip_drop', 'IP packets dropped', 'ip_drop'], ['ip_rterr', 'No route / forwarding refused', 'ip_rterr'], ['ip_memerr', 'IP: out of memory', 'ip_memerr'],
  ['ip_chkerr', 'IP: checksum errors', 'ip_chkerr'], ['ip_lenerr', 'IP: bad length', 'ip_lenerr'],
  ['link_drop', 'Link layer dropped', 'link_drop'], ['link_memerr', 'Link: out of memory', 'link_memerr'], ['link_chkerr', 'Link: checksum errors', 'link_chkerr'], ['link_lenerr', 'Link: bad length', 'link_lenerr'],
  ['pbuf_err', 'Receive buffer pool exhausted', 'pbuf_err'], ['inpkt_err', 'tcpip input messages exhausted', 'inpkt_err'],
];

const level = (used: number | undefined, limit: number, err: number | undefined) =>
  num(err) && err > 0 ? 'danger' as const : num(used) && limit > 0 && used / limit >= 0.8 ? 'warn' as const : 'accent' as const;

export function D133Lwip({ lwip }: { lwip: BoardLwip | null | undefined }) {
  if (!lwip) return <section className="st-card"><h2 className="h4">{tr('lwIP on the board')}</h2>
    <p className="small muted">{tr('The board did not send lwIP data (a firmware without the stats line extension, or the host build).')}</p></section>;
  const L = lwip.limits, on = lwip.stats === 1;
  const pool = (label: string, max: number | undefined, used: number | undefined, limit: number, err: number | undefined) => <div className="d133-pool">
    <div className="section-toolbar"><span className="small">{tr(label)}</span><Pill tone={level(max, limit, err)} dot>{tr(on ? `high-water ${max ?? 0} of ${limit}` : `limit ${limit}`)}</Pill></div>
    {on ? <Bar label={tr(label)} tone={level(max, limit, err)} value={limit ? (max ?? 0) / limit * 100 : 0}/> : null}
    {on ? <div className="xs muted">{tr(`in use now ${used ?? 0} \u00b7 ${err ?? 0} allocation failures`)}</div> : null}
  </div>;
  return <section className="st-card">
    <div className="section-toolbar"><h2 className="h4">{tr('lwIP on the board')}</h2><Pill tone={on ? 'accent' : 'warn'} dot>{tr(on ? 'Counters on' : 'Counters off (RT_LWIP_STATS)')}</Pill></div>
    <div className="d133-pools">
      {pool('Receive buffers (PBUF_POOL_SIZE)', lwip.pbuf_max, lwip.pbuf_used, L.pbuf_pool, lwip.pbuf_err)}
      {pool('tcpip input messages (MEMP_NUM_TCPIP_MSG_INPKT)', lwip.inpkt_max, lwip.inpkt_used, L.inpkt_pool, lwip.inpkt_err)}
      <div className="d133-pool"><div className="section-toolbar"><span className="small">{tr('tcpip mailbox (TCPIP_MBOX_SIZE)')}</span><Pill dot>{tr(`${L.mbox} messages`)}</Pill></div>
        <div className="xs muted">{tr('When it is full the stack refuses the frame and lwIP counts nothing; look at "Refused" on the interface.')}</div></div>
    </div>
    {on ? <div className="st-tablewrap"><table className="st-table native-performance-table">
      <thead><tr><th>{tr('Counter')}</th><th>{tr('Total')}</th><th>{tr('Per second')}</th></tr></thead>
      <tbody>{ROWS.map(([key, label]) => {
        const total = (lwip as unknown as Record<string, number | undefined>)[key], r = lwip.rates[key];
        return <tr key={key}><td>{tr(label)}</td><td className="num">{tr(num(total) ? String(total) : '\u2014')}</td>
          <td className="num">{tr(num(r) ? (r === 0 ? '0' : r.toFixed(r < 10 ? 2 : 0)) : '\u2014')}</td></tr>;
      })}</tbody>
    </table></div> : <p className="small muted">{tr('The board was built without RT_LWIP_STATS: only the limits are known.')}</p>}
    <p className="xs muted">{tr('Change PBUF_POOL_SIZE, the message pool or the mailbox size only when one of these shows it running out (high-water at its limit, or failures counting up) during a stream.')}</p>
  </section>;
}
