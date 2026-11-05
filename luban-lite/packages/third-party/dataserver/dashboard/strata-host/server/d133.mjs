/**
 * D133 board: live network stats and dataServer pulls for the Strata host.
 *
 *   GET  /api/d133/config                       defaults (board, ports, output dir)
 *   GET  /api/d133/board?host=IP&port=9001      one sample from the board's dsstat
 *                                               endpoint folded into per-target history
 *   POST /api/d133/pull {host, port, directory} start pulling a directory (409 if busy)
 *   GET  /api/d133/pull                         progress of the current / last pull
 *   POST /api/d133/pull/reset                   clear it
 *
 * Board side: luban-lite/packages/third-party/dataserver/rtthread/{dsstat,netmon,dataserver}.c
 * The pull speaks the NikosMav/System-Programming-3 protocol exactly as
 * rtthread/remote_client.py does (same safe_local_path rule).
 */
import net from 'node:net';
import { createHash } from 'node:crypto';
import { mkdirSync, openSync, writeSync, closeSync, createReadStream } from 'node:fs';
import { join, dirname } from 'node:path';

const ACK = Buffer.from('}#%\0', 'latin1');
const PATH_FIELD = 512;
const HISTORY = 180;          // board samples kept (1 s each)
const SPEED_SAMPLE_MS = 500;  // pull throughput sampling
const SPEED_HISTORY = 240;
const MAX_BLOCKS_UI = 2000;

/** Difference of two wrapping uint32 counters. */
export const delta32 = (a, b) => ((a - b) % 2 ** 32 + 2 ** 32) % 2 ** 32;

/** Never write outside out, whatever the board sends (remote_client.safe_local_path). */
export function safeLocalPath(out, serverPath) {
  const parts = serverPath.replace(/\\/g, '/').split('/').filter(p => p && p !== '.' && p !== '..');
  if (!parts.length) throw new Error('empty file name from server');
  return join(out, ...parts);
}

/** Byte-exact reads over a socket. */
function reader(sock) {
  let buf = Buffer.alloc(0), wait = null, closed = null;
  const pump = () => {
    if (!wait) return;
    if (buf.length >= wait.n) { const out = buf.subarray(0, wait.n); buf = buf.subarray(wait.n); const w = wait; wait = null; w.resolve(Buffer.from(out)); }
    else if (closed) { const w = wait; wait = null; w.reject(closed); }
  };
  sock.on('data', d => { buf = buf.length ? Buffer.concat([buf, d]) : d; pump(); });
  // a clean close (FIN) ends a directory; a timeout or reset is an error
  sock.on('end', () => { closed ??= Object.assign(new Error('connection closed'), { clean: true }); pump(); });
  sock.on('close', () => { closed ??= new Error('connection lost'); pump(); });
  sock.on('error', e => { closed = e; pump(); });
  return n => new Promise((resolve, reject) => { wait = { n, resolve, reject }; pump(); });
}

const connect = (host, port, timeoutMs) => new Promise((resolve, reject) => {
  const sock = net.connect({ host, port });
  const t = setTimeout(() => { sock.destroy(); reject(new Error(`${host}:${port}: timeout`)); }, timeoutMs);
  sock.once('connect', () => { clearTimeout(t); resolve(sock); });
  sock.once('error', e => { clearTimeout(t); reject(new Error(`${host}:${port}: ${e.code ?? e.message}`)); });
});

/** One JSON line from the board's dsstat endpoint. */
export async function readBoardStats(host, port, timeoutMs = 2000) {
  const sock = await connect(host, port, timeoutMs);
  return await new Promise((resolve, reject) => {
    let data = '';
    sock.setTimeout(timeoutMs, () => { sock.destroy(); reject(new Error(`${host}:${port}: timeout`)); });
    sock.setEncoding('utf8');
    sock.on('data', d => { data += d; if (data.length > 65536) sock.destroy(); });
    sock.on('close', () => { try { resolve(JSON.parse(data)); } catch { reject(new Error(`${host}:${port}: not a dsstat endpoint`)); } });
    sock.on('error', e => reject(new Error(`${host}:${port}: ${e.code ?? e.message}`)));
  });
}

/** Fold one sample into state; rates use the board's own uptime. Returns the sample to keep. */
export function boardUpdate(state, prev, sample) {
  const t = Number(sample.uptime_ms ?? 0);
  if (prev && t <= Number(prev.uptime_ms ?? 0)) { prev = null; state.history = { t: [], ifs: {} }; }   // board rebooted
  const old = new Map((prev?.ifs ?? []).map(i => [i.name, i]));
  const dt = prev ? (t - Number(prev.uptime_ms)) / 1000 : 0;
  if (prev && dt > 0) state.history.t.push(t);
  state.ifs = (sample.ifs ?? []).map(i => {
    const o = old.get(i.name), row = { ...i, rx_rate: null, tx_rate: null };
    if (o && dt > 0) {
      row.rx_rate = delta32(i.rx_bytes, o.rx_bytes) / dt;
      row.tx_rate = delta32(i.tx_bytes, o.tx_bytes) / dt;
    }
    return row;
  });
  if (prev && dt > 0) {
    // every series stays aligned with history.t; an interface that appears late is padded with gaps
    const n = state.history.t.length;
    for (const row of state.ifs) {
      const h = state.history.ifs[row.name] ??= { rx: [], tx: [] };
      while (h.rx.length < n - 1) { h.rx.push(null); h.tx.push(null); }
      h.rx.push(row.rx_rate); h.tx.push(row.tx_rate);
    }
    for (const h of Object.values(state.history.ifs)) while (h.rx.length < n) { h.rx.push(null); h.tx.push(null); }
    if (n > HISTORY) {
      state.history.t.splice(0, n - HISTORY);
      for (const h of Object.values(state.history.ifs)) { h.rx.splice(0, n - HISTORY); h.tx.splice(0, n - HISTORY); }
    }
  }
  const fw = sample.ip_forwarded, pfw = prev?.ip_forwarded;
  state.ipForwarded = Number.isInteger(fw) && fw >= 0 ? fw : null;
  state.fwRate = prev && dt > 0 && state.ipForwarded !== null && Number.isInteger(pfw) && pfw >= 0 ? delta32(fw, pfw) / dt : null;
  Object.assign(state, { ok: true, error: null, uptimeMs: t, dataserver: sample.dataserver ?? null, updated: Date.now() });
  return sample;
}

const sha256 = path => new Promise((resolve, reject) => {
  const h = createHash('sha256');
  createReadStream(path).on('data', d => h.update(d)).on('end', () => resolve(h.digest('hex'))).on('error', reject);
});

export function createD133({ host = '192.168.169.1', dataPort = 9000, statPort = 9001, outDir }) {
  const boards = new Map();   // "host:port" -> { state, prev }
  let pull = idlePull();

  function idlePull() {
    return { status: 'IDLE', target: null, directory: null, outDir, blockSize: 0, current: null,
      files: [], bytes: 0, startedAt: null, elapsedS: null, avgBps: null, nowBps: null,
      speed: { t: [], bps: [] }, log: [] };
  }
  const log = msg => { pull.log.push(`[${new Date().toLocaleTimeString()}]  ${msg}`); pull.log.splice(0, Math.max(0, pull.log.length - 200)); };

  async function board(h = host, p = statPort) {
    const key = `${h}:${p}`;
    const entry = boards.get(key) ?? { state: { target: key, ok: false, error: null, ifs: [], history: { t: [], ifs: {} } }, prev: null };
    boards.set(key, entry);
    try { entry.prev = boardUpdate(entry.state, entry.prev, await readBoardStats(h, p)); }
    catch (e) { Object.assign(entry.state, { ok: false, error: e.message }); entry.prev = null; }
    return entry.state;
  }

  async function runPull(h, p, directory) {
    pull = { ...idlePull(), status: 'CONNECTING', target: `${h}:${p}`, directory, startedAt: Date.now() };
    const run = pull;              // a reset while running must not be overwritten
    log(`Connecting to dataServer at ${h}:${p} …`);
    let sock = null, meter = { t: Date.now(), bytes: 0 };
    const finish = async f => { if (f && !f.done) { f.sha256 = await sha256(f.local); f.done = true; log(`Received ${f.path}  (${f.size} bytes)  sha256 ${f.sha256.slice(0, 16)}…`); } };
    try {
      sock = await connect(h, p, 15000);
      sock.setTimeout(15000, () => sock.destroy(new Error('board stopped sending (15 s)')));
      const read = reader(sock);
      const readData = async n => { const b = await read(n); sock.write(ACK); return b; };
      const block = (await readData(4)).readInt32LE(0);
      if (block <= 0) throw new Error(`bad block size ${block}`);
      Object.assign(run, { status: 'RECEIVING', blockSize: block });
      log(`Connected. Block size ${block} bytes. Requesting '${directory}' …`);
      const req = Buffer.from(directory, 'utf8');
      if (req.length >= PATH_FIELD) throw new Error('path too long');
      sock.write(Buffer.concat([req, Buffer.alloc(PATH_FIELD - req.length)]));
      if ((await read(4)).subarray(0, 3).compare(ACK.subarray(0, 3)) !== 0) throw new Error('bad ack');
      for (;;) {
        let raw;
        try { raw = await readData(PATH_FIELD); }
        catch (e) { if (e.clean) break; throw e; }                      // board closed: directory complete
        const path = raw.subarray(0, raw.indexOf(0) < 0 ? raw.length : raw.indexOf(0)).toString('utf8');
        const size = (await readData(4)).readInt32LE(0);
        const local = safeLocalPath(run.outDir, path), nblocks = Math.ceil(size / block);
        await finish(run.files.at(-1));
        const f = { path, size, nblocks, received: 0, done: false, sha256: null, local };
        run.files.push(f); run.current = path;
        mkdirSync(dirname(local), { recursive: true });
        const fd = openSync(local, 'w');
        try {
          for (let i = 0, left = size; i < nblocks; i++, left -= block) {
            const b = await readData(block), n = Math.min(block, left);
            writeSync(fd, b, 0, n);                                      // last block is zero padded
            f.received = i + 1; run.bytes += n; meter.bytes += n;
            const now = Date.now();
            if (now - meter.t >= SPEED_SAMPLE_MS) {
              run.nowBps = meter.bytes / ((now - meter.t) / 1000);
              run.speed.t.push(now); run.speed.bps.push(run.nowBps);
              if (run.speed.t.length > SPEED_HISTORY) { run.speed.t.shift(); run.speed.bps.shift(); }
              meter = { t: now, bytes: 0 };
            }
            run.elapsedS = (now - run.startedAt) / 1000;
            run.avgBps = run.bytes / Math.max(run.elapsedS, 1e-3);
          }
        } finally { closeSync(fd); }
        if (!size) log(`${path}: empty file`);
      }
      await finish(run.files.at(-1));
      run.elapsedS = (Date.now() - run.startedAt) / 1000;
      run.avgBps = run.bytes / Math.max(run.elapsedS, 1e-3);
      run.nowBps = 0; run.speed.t.push(Date.now()); run.speed.bps.push(0);
      run.current = null;
      run.status = run.files.length ? 'SUCCESS' : 'FAILED';
      log(run.files.length ? `Done: ${run.files.length} files, ${run.bytes} bytes in ${run.elapsedS.toFixed(2)} s → ${run.outDir}`
                           : 'Nothing received: directory empty, missing or outside the board root');
    } catch (e) {
      run.status = 'ERROR'; run.nowBps = 0;
      log(`ERROR: ${e.message}`);
    } finally { sock?.destroy(); }
  }

  return {
    config: () => ({ host, dataPort, statPort, outDir }),
    board,
    pullState: () => ({ ...pull, files: pull.files.map(({ local, ...f }) => ({ ...f, blocksShown: Math.min(f.nblocks, MAX_BLOCKS_UI) })) }),
    startPull(h = host, p = dataPort, directory = '') {
      if (!directory.trim()) return { error: 'directory required', status: 400 };
      if (['CONNECTING', 'RECEIVING'].includes(pull.status)) return { error: 'a pull is already running', status: 409 };
      void runPull(h, p, directory.trim());
      return { ok: true };
    },
    resetPull() { if (!['CONNECTING', 'RECEIVING'].includes(pull.status)) pull = idlePull(); return { ok: true }; },
  };
}
