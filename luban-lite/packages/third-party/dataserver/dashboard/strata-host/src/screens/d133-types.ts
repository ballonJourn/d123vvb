/** Shapes returned by server/d133.mjs (/api/d133/board, /pull) and server/stream/engine.mjs (/api/d133/stream). */
export type Series = (number | null)[];

export interface BoardIf {
  name: string; ip: string; up: number; rx_bytes: number; tx_bytes: number; rx_pkts: number; tx_pkts: number;
  rx_drop?: number; tx_err?: number;
  rx_rate: number | null; tx_rate: number | null; rx_pps: number | null; tx_pps: number | null; rx_drop_rate: number | null; tx_err_rate: number | null;
}
export interface BoardLwip {
  stats: number; ip_drop?: number; ip_rterr?: number; ip_chkerr?: number; ip_lenerr?: number; ip_memerr?: number;
  link_drop?: number; link_memerr?: number; link_chkerr?: number; link_lenerr?: number;
  pbuf_err?: number; pbuf_used?: number; pbuf_max?: number; inpkt_err?: number; inpkt_used?: number; inpkt_max?: number;
  limits: { pbuf_pool: number; inpkt_pool: number; mbox: number };
  rates: Record<string, number | null>;
}
export interface BoardState {
  target: string; ok: boolean; error: string | null; uptimeMs?: number; ifs: BoardIf[];
  history: { t: number[]; ifs: Record<string, { rx: Series; tx: Series; rx_pps: Series; tx_pps: Series; rx_drop: Series; tx_err: Series }> };
  ipForwarded?: number | null; fwRate?: number | null; lwip?: BoardLwip | null;
  dataserver?: { running: number; connections: number; requests_done: number; files_sent: number; rejected: number; queued: number; bytes_sent: number } | null;
}

export interface PullFile { path: string; size: number; nblocks: number; received: number; done: boolean; sha256: string | null }
export interface PullState {
  status: 'IDLE' | 'CONNECTING' | 'RECEIVING' | 'SUCCESS' | 'FAILED' | 'ERROR'; target: string | null; directory: string | null;
  outDir: string; blockSize: number; current: string | null; files: PullFile[]; bytes: number;
  elapsedS: number | null; avgBps: number | null; nowBps: number | null; speed: { t: number[]; bps: number[] }; log: string[];
}
export interface Config { host: string; dataPort: number; statPort: number; outDir: string }

export interface StreamReport {
  expected: number; received: number; lost: number; reordered: number; duplicates: number; jitterUs: number;
  framesOk: number; framesDropped: number; queueDropped: number; recvPackets: number; recvBytes: number; intervalMs: number; lossFraction: number | null;
}
export interface RampResult { bps: number; loss: number; expected: number; lost: number; rttMed?: number | null; rttEnd?: number | null }
export interface StreamStats {
  role: 'send' | 'recv';
  // sender
  running?: boolean; target?: string; targetBps?: number; minBps?: number; maxBps?: number; fps?: number;
  txPackets?: number; txBytes?: number; txPayload?: number; txFrames?: number; sendErrors?: number; queued?: number; queueDropped?: number;
  rttMs?: number | null; reports?: number; reportAgeMs?: number | null; lastReport?: StreamReport | null;
  adapt?: { last: string; hold: number } | null;
  ramp?: { done: boolean; knee: number | null; delayKnee?: number | null; recommend?: number | null; reason: string | null; results: RampResult[]; rate: number } | null;
  // receiver
  listening?: number; packets?: number; bytes?: number; unique?: number; expected?: number; lost?: number; lossFraction?: number | null; reordered?: number;
  duplicates?: number; tooLate?: number; lateFragments?: number; framesOk?: number; framesDropped?: number; openFrames?: number; jitterUs?: number;
  frameBytes?: number; malformed?: number;
}
export interface PathSegment { id: string; label: string; inRate: number | null; outRate: number | null; lost: number | null; fraction: number | null; flagged: boolean; known: boolean }
export interface StreamPath {
  window: number; ingressIf: string; egressIf: string;
  rates: { tx: number | null; e0Rx: number | null; w1Tx: number | null; rx: number | null };
  located: { segments: PathSegment[]; where: string | null; flagged: string[]; unknown: string[]; text: string };
  boardRefused: { e0RxDrop: number | null; w1TxErr: number | null };
}
export interface StreamHistory {
  t: number[]; rxWire: Series; rxPps: Series; loss: Series; jitterMs: Series;
  txWire?: Series; txPps?: Series; rttMs?: Series; target?: Series; framesOk?: Series; framesDropped?: Series;
}
export interface StreamState {
  role: 'idle' | 'send' | 'recv'; params: Record<string, unknown> | null; startedAt?: number; error: string | null;
  events: { t: number; text: string }[]; stats: StreamStats | null; history: StreamHistory | null;
  rampResult?: { knee: number | null; delayKnee?: number | null; recommend?: number | null; reason: string; results: RampResult[] } | null; path: StreamPath | null;
}
