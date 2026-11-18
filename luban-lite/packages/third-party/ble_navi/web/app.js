// app.js - D133 BLE Navi web sender
// SPDX-License-Identifier: Apache-2.0
import { BnpSender, UUID, FMT, LAYER, NAV, ERR } from './bnp_sender.js';
import { SimLink } from './sim_link.js';

const $ = (id) => document.getElementById(id);
const css = (v) => getComputedStyle(document.documentElement).getPropertyValue(v).trim();
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// ---------------------------------------------------------------- log
function log(msg, err = false) {
  const el = $('log'), line = document.createElement('div');
  line.textContent = `${new Date().toLocaleTimeString()}  ${msg}`;
  if (err) line.className = 'err';
  el.appendChild(line);
  el.scrollTop = el.scrollHeight;
}

// ---------------------------------------------------------------- Web Bluetooth link
class WebBleLink {
  async connect() {
    if (!navigator.bluetooth) throw new Error('此浏览器不支持 Web Bluetooth：请用桌面版 Chrome / Edge，并通过 http://localhost 或 https 打开');
    this.dev = await navigator.bluetooth.requestDevice({
      filters: [{ services: [UUID.svc] }, { namePrefix: 'D133' }],
      optionalServices: [UUID.svc],
    });
    this.dev.addEventListener('gattserverdisconnected', () => { this.connected = false; ui.onLinkDown(); });
    await this._open();
  }
  async _open() {
    const srv = await this.dev.gatt.connect();
    const svc = await srv.getPrimaryService(UUID.svc);
    this.ctrl = await svc.getCharacteristic(UUID.ctrl);
    this.data = await svc.getCharacteristic(UUID.data);
    await this.ctrl.startNotifications();
    this.ctrl.addEventListener('characteristicvaluechanged', (e) => {
      const v = e.target.value;
      this.cb && this.cb(new Uint8Array(v.buffer, v.byteOffset, v.byteLength).slice());
    });
    this.connected = true;
    this.name = this.dev.name || 'D133';
  }
  writeCtrl(u8) { return this.ctrl.writeValueWithResponse(u8); }
  async writeData(u8) {
    // Chrome rejects when its write-without-response queue is full: back off and retry
    for (let i = 0; ; i++) {
      try { return await this.data.writeValueWithoutResponse(u8); }
      catch (e) { if (!this.connected || i > 40) throw e; await sleep(4); }
    }
  }
  onNotify(cb) { this.cb = cb; }
  disconnect() { this.dev?.gatt.disconnect(); }
  async reconnect() { await this._open(); }
}

// ---------------------------------------------------------------- chunk map
const ST = { PEND: 0, SENT: 1, LOST: 2, RETX: 3, OK: 4 };
const map = {
  n: 0, s: new Uint8Array(0), dirty: false,
  reset(n) { this.n = n; this.s = new Uint8Array(n); this.dirty = true; },
  event(k, a, b) {
    if (k === 'start') { if (this.n !== a) this.reset(a); }
    else if (k === 'sent') { if (this.s[a] !== ST.OK) this.s[a] = b ? ST.RETX : ST.SENT; }
    else if (k === 'lost') { for (let i = a; i < a + b && i < this.n; i++) this.s[i] = ST.LOST; }
    else if (k === 'ok') { for (let i = 0; i < a; i++) this.s[i] = ST.OK; }
    else if (k === 'status') {
      // a = device STATUS, b = highest seq sent so far. Device lists missing
      // ranges from its contiguous mark; if they don't add up to n - have the
      // list was truncated, so only trust it up to the last listed range.
      const st = a, hi = b;
      const listed = st.ranges.reduce((t, r) => t + r[1], 0);
      const lim = listed >= st.n - st.have ? st.n
                : (st.ranges.length ? st.ranges.at(-1)[0] + st.ranges.at(-1)[1] : st.contig);
      const miss = new Uint8Array(this.n);
      for (const [f, c] of st.ranges) for (let i = f; i < f + c && i < this.n; i++) miss[i] = 1;
      for (let i = 0; i < st.contig; i++) this.s[i] = ST.OK;
      for (let i = st.contig; i < Math.min(lim, hi + 1); i++) this.s[i] = miss[i] ? ST.LOST : ST.OK;
    }
    else if (k === 'done') this.s.fill(ST.OK);
    this.dirty = true;
  },
  draw() {
    const cv = $('chunkmap'), dpr = window.devicePixelRatio || 1;
    const W = Math.floor(cv.clientWidth * dpr), H = Math.floor(cv.clientHeight * dpr);
    if (cv.width !== W || cv.height !== H) { cv.width = W; cv.height = H; this.dirty = true; }
    if (!this.dirty) return;
    this.dirty = false;
    const g = cv.getContext('2d');
    g.clearRect(0, 0, W, H);
    if (!this.n) return;
    let s = Math.max(1, Math.floor(Math.sqrt((W * H) / this.n)));
    while (s > 1 && Math.floor(W / s) * Math.floor(H / s) < this.n) s--;
    const cols = Math.floor(W / s), gap = s >= 5 ? 1 : 0;
    const col = [css('--pend'), css('--sent'), css('--lost'), css('--retx'), css('--ok')];
    for (let st = 0; st < 5; st++) {        // batch by colour: one fillStyle per state
      g.fillStyle = col[st];
      for (let i = 0; i < this.n; i++) {
        if (this.s[i] !== st) continue;
        g.fillRect((i % cols) * s, Math.floor(i / cols) * s, s - gap, s - gap);
      }
    }
  },
};
(function loop() { map.draw(); requestAnimationFrame(loop); })();
addEventListener('resize', () => { map.dirty = true; });

// ---------------------------------------------------------------- chart
const chart = new Chart($('chart'), {
  data: {
    labels: [],
    datasets: [
      { type: 'line', label: '有效吞吐 KB/s（设备确认）', data: [], borderColor: css('--ok'), backgroundColor: css('--ok'), pointRadius: 0, borderWidth: 2, tension: 0.25, yAxisID: 'y' },
      { type: 'line', label: '发送速率 KB/s', data: [], borderColor: css('--sent'), backgroundColor: css('--sent'), pointRadius: 0, borderWidth: 1.5, borderDash: [5, 4], tension: 0.25, yAxisID: 'y' },
      { type: 'bar', label: '本区间重传包', data: [], backgroundColor: css('--retx'), yAxisID: 'y1', barPercentage: 1, categoryPercentage: 1 },
    ],
  },
  options: {
    animation: false, maintainAspectRatio: false, interaction: { mode: 'index', intersect: false },
    scales: {
      x: { title: { display: true, text: '时间 s' }, ticks: { maxTicksLimit: 10, color: css('--ink-2') }, grid: { color: css('--rule') } },
      y: { beginAtZero: true, title: { display: true, text: 'KB/s' }, ticks: { color: css('--ink-2') }, grid: { color: css('--rule') } },
      y1: { beginAtZero: true, position: 'right', title: { display: true, text: '重传' }, grid: { display: false }, ticks: { color: css('--ink-2') } },
    },
    plugins: { legend: { labels: { color: css('--ink'), boxWidth: 12 } } },
  },
});

// ---------------------------------------------------------------- image pipeline
const preview = $('preview');
let srcImage = null;

function drawSampleMap(g, W, H) {
  g.fillStyle = '#E8E4DA'; g.fillRect(0, 0, W, H);
  g.strokeStyle = '#FFFFFF'; g.lineWidth = 10;
  for (let x = -200; x < W; x += 64) { g.beginPath(); g.moveTo(x, 0); g.lineTo(x + 200, H); g.stroke(); }
  g.strokeStyle = '#2F7BF5'; g.lineWidth = 18; g.lineJoin = 'round';
  g.beginPath(); g.moveTo(W * .08, H * .93); g.lineTo(W * .41, H * .55); g.lineTo(W * .68, H * .5); g.lineTo(W * .96, H * .1); g.stroke();
  g.fillStyle = '#14324A'; g.fillRect(0, 0, W, 40);
  g.fillStyle = '#FFFFFF'; g.font = '600 24px Barlow, sans-serif'; g.fillText('示例地图  300 m 后左转  中山北路', 16, 28);
}

function renderPreview() {
  const W = +$('w').value, H = +$('h').value;
  preview.width = W; preview.height = H;
  preview.style.aspectRatio = `${W} / ${H}`;
  const g = preview.getContext('2d');
  if (srcImage) {                                  // cover-fit
    const r = Math.max(W / srcImage.width, H / srcImage.height);
    const w = srcImage.width * r, h = srcImage.height * r;
    g.drawImage(srcImage, (W - w) / 2, (H - h) / 2, w, h);
  } else drawSampleMap(g, W, H);
}

async function encodeFrame() {
  renderPreview();
  const W = preview.width, H = preview.height, fmt = +$('fmt').value;
  if (fmt === FMT.JPEG) {
    const blob = await new Promise((r) => preview.toBlob(r, 'image/jpeg', +$('quality').value));
    return { data: new Uint8Array(await blob.arrayBuffer()), fmt, width: W, height: H };
  }
  const px = preview.getContext('2d').getImageData(0, 0, W, H).data;
  const out = new Uint8Array(W * H * 2);
  for (let i = 0, o = 0; i < px.length; i += 4, o += 2) {   // RGB888 -> RGB565 LE
    const v = ((px[i] & 0xF8) << 8) | ((px[i + 1] & 0xFC) << 3) | (px[i + 2] >> 3);
    out[o] = v & 0xFF; out[o + 1] = v >> 8;
  }
  return { data: out, fmt, width: W, height: H };
}

async function updateInfo() {
  const f = await encodeFrame();
  const chunk = (ui.params?.mtu ?? 247) - 6;
  $('imginfo').textContent = `${f.width}×${f.height} ${f.fmt === FMT.JPEG ? 'JPEG' : 'RGB565'}：` +
    `${(f.data.length / 1024).toFixed(1)} KB，${Math.ceil(f.data.length / chunk)} 个分块（MTU ${chunk + 6}）`;
}

// ---------------------------------------------------------------- turn icons (48x48, chronos 1bpp)
const TURNS = [
  { code: 1, name: '直行', draw: (g) => { g.fillRect(20, 14, 8, 30); tri(g, 24, 4, 12); } },
  { code: 2, name: '左转', draw: (g) => { g.fillRect(26, 18, 8, 26); g.fillRect(14, 18, 20, 8); triL(g, 4, 22, 12); } },
  { code: 3, name: '右转', draw: (g) => { g.fillRect(14, 18, 8, 26); g.fillRect(14, 18, 20, 8); triR(g, 44, 22, 12); } },
  { code: 4, name: '掉头', draw: (g) => { g.fillRect(30, 14, 7, 30); g.fillRect(12, 14, 25, 7); g.fillRect(12, 14, 7, 18); tri(g, 15, 42, -11); } },
];
function tri(g, x, y, s) { g.beginPath(); g.moveTo(x, y); g.lineTo(x - s, y + s); g.lineTo(x + s, y + s); g.fill(); }
function triL(g, x, y, s) { g.beginPath(); g.moveTo(x, y); g.lineTo(x + s, y - s); g.lineTo(x + s, y + s); g.fill(); }
function triR(g, x, y, s) { g.beginPath(); g.moveTo(x, y); g.lineTo(x - s, y - s); g.lineTo(x - s, y + s); g.fill(); }
let turnSel = TURNS[1];
for (const t of TURNS) {
  const c = document.createElement('canvas');
  c.width = c.height = 48; c.title = t.name; c.tabIndex = 0; c.setAttribute('role', 'button');
  const g = c.getContext('2d'); g.fillStyle = '#000'; g.fillRect(0, 0, 48, 48); g.fillStyle = '#fff'; t.draw(g);
  t.canvas = c;
  const pick = () => { turnSel = t; for (const u of TURNS) u.canvas.setAttribute('aria-pressed', u === t); };
  c.onclick = pick; c.onkeydown = (e) => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); pick(); } };
  $('icons').appendChild(c);
}
TURNS[1].canvas.setAttribute('aria-pressed', 'true');

function icon1bpp(c) {               // MSB first, same layout as chronos 0xEE icon
  const px = c.getContext('2d').getImageData(0, 0, 48, 48).data, out = new Uint8Array(288);
  for (let i = 0; i < 48 * 48; i++) if (px[i * 4] > 127) out[i >> 3] |= 0x80 >> (i & 7);
  return out;
}

// ---------------------------------------------------------------- UI / transfer
const ui = {
  link: null, sim: false, params: null, busy: false, cutArmed: false, frameId: 1,
  setLinked(on) {
    for (const id of ['btnSend', 'btnNav', 'btnPhy', 'btnConn', 'btnDisconnect']) $(id).disabled = !on || this.busy;
    $('btnCut').disabled = !on || !this.busy;
    $('btnConnect').disabled = $('btnSim').disabled = on;
  },
  onLinkDown() { if (!this.busy) { $('linkline').textContent = '连接已断开。'; this.setLinked(false); } },
  async afterConnect() {
    const s = new BnpSender(this.link);
    this.params = await s.getParams();
    const p = this.params;
    $('linkline').textContent = `${this.sim ? '模拟链路' : this.link.name}：MTU ${p.mtu}，间隔 ${(p.ci * 1.25).toFixed(2)} ms，` +
      `PHY ${p.txPhy}M/${p.rxPhy}M，DLE ${p.txOct}/${p.rxOct}，后端 ${p.backend === 0xFF ? 'wasm 模拟' : p.backend ? 'AIC' : 'NimBLE'}`;
    log(`已连接。${$('linkline').textContent}`);
    this.setLinked(true);
    updateInfo();
  },
};

$('btnConnect').onclick = async () => {
  try { ui.sim = false; ui.link = new WebBleLink(); await ui.link.connect(); await ui.afterConnect(); }
  catch (e) { log(`连接失败：${e.message}`, true); }
};
$('btnSim').onclick = async () => {
  try {
    ui.sim = true;
    ui.link = await SimLink.create('ble_img_rx.wasm', { loss: +$('simLoss').value, kbps: +$('simKbps').value });
    ui.link.onFrame = (f) => log(`[设备] 收到 layer ${f.layer} fmt ${f.fmt} ${f.w}×${f.h}，${f.data.length} B，设备侧用时 ${f.ms} ms`);
    ui.link.name = '模拟';
    await ui.afterConnect();
  } catch (e) { log(`模拟启动失败：${e.message}（需通过 http://localhost 打开以加载 wasm）`, true); }
};
$('btnDisconnect').onclick = () => { ui.link?.disconnect(); ui.link = null; ui.params = null; ui.setLinked(false); $('linkline').textContent = '已断开。'; };
$('btnPhy').onclick = async () => { await new BnpSender(ui.link).requestPhy2M(); log('已请求 2M PHY；用“连接”行查看结果'); setTimeout(() => ui.afterConnect(), 800); };
$('btnConn').onclick = async () => { await new BnpSender(ui.link).requestConn(15, 15, 0, 4000); log('已请求 15 ms 连接间隔'); setTimeout(() => ui.afterConnect(), 800); };
$('btnCut').onclick = () => { ui.cutArmed = true; log('将在下一包断开链路，随后自动重连并续传'); };
$('file').onchange = async (e) => {
  const f = e.target.files[0];
  if (!f) return;
  srcImage = await createImageBitmap(f);
  updateInfo();
};
for (const id of ['fmt', 'quality', 'w', 'h']) $(id).onchange = updateInfo;
$('simLoss').onchange = () => { if (ui.sim && ui.link) ui.link.loss = +$('simLoss').value; };
$('simKbps').onchange = () => { if (ui.sim && ui.link) ui.link.kbps = +$('simKbps').value; };

function resetStats(n) {
  map.reset(n);
  chart.data.labels = []; chart.data.datasets.forEach((d) => (d.data = [])); chart.update();
  for (const id of ['sThr', 'sSent', 'sRetx', 'sLost', 'sDup', 'sTime', 'sResume', 'sResult']) $(id).textContent = '–';
}

async function sendWithResume(frame, label) {
  ui.busy = true; ui.setLinked(true);
  const id = (ui.frameId = (ui.frameId + 1) & 0xFFFF);
  const chunk = ui.params.mtu - 6;
  resetStats(Math.ceil(frame.data.length / chunk));
  $('frameinfo').textContent = `${label}：${(frame.data.length / 1024).toFixed(1)} KB，帧号 ${id}`;
  let last = null, prev = { t: 0, have: 0, sent: 0, retx: 0 }, resumes = 0, res = null;
  const t0 = performance.now();
  const timer = setInterval(() => {
    if (!last) return;
    const t = (performance.now() - t0) / 1000, dt = t - prev.t || 0.25;
    const acked = Math.min(last.have * chunk, frame.data.length);
    chart.data.labels.push(t.toFixed(1));
    chart.data.datasets[0].data.push(+(((last.have - prev.have) * chunk) / 1024 / dt).toFixed(1));
    chart.data.datasets[1].data.push(+(((last.sent - prev.sent) * chunk) / 1024 / dt).toFixed(1));
    chart.data.datasets[2].data.push(last.retx - prev.retx);
    chart.update('none');
    prev = { t, have: last.have, sent: last.sent, retx: last.retx };
    const pct = Math.floor((100 * last.have) / (last.n || 1));
    $('progress').innerHTML = `${pct}%<small>${last.have} / ${last.n} 块</small>`;
    $('sThr').textContent = (acked / 1024 / t).toFixed(1);
    $('sSent').textContent = last.sent; $('sRetx').textContent = last.retx;
    $('sLost').textContent = Math.max(last.lostReported, map.s.reduce((t, v) => t + (v === ST.LOST), 0)); $('sDup').textContent = last.dup;
    $('sTime').textContent = t.toFixed(1); $('sResume').textContent = resumes;
  }, 250);

  // wrap transport so "cut" can drop the link exactly mid-transfer
  const base = ui.link, tx = {
    writeCtrl: (u8) => base.writeCtrl(u8),
    writeData: async (u8) => {
      if (ui.cutArmed) { ui.cutArmed = false; base.disconnect(); throw new Error('链路已手动断开'); }
      return base.writeData(u8);
    },
    onNotify: (cb) => base.onNotify(cb),
  };
  let carried = { sent: 0, retx: 0, lostReported: 0 };
  for (let attempt = 0; attempt < 4 && !res; attempt++) {
    const s = new BnpSender(tx, {
      window: +$('window').value, log,
      onChunk: (k, a, b) => map.event(k, a, b),
      onStat: (st) => { last = { ...st, sent: st.sent + carried.sent, retx: st.retx + carried.retx, lostReported: st.lostReported + carried.lostReported }; },
    });
    try {
      res = await s.sendFrame({ ...frame, id });
    } catch (e) {
      log(`第 ${attempt + 1} 次传输中断：${e.message}`, !/手动/.test(e.message));
      if (last) carried = { sent: last.sent, retx: last.retx, lostReported: last.lostReported };
      if (attempt === 3) break;
      try {
        if (ui.sim) base.reconnect(); else { await sleep(500); await base.reconnect(); }
        resumes++;
        log('已重连，同帧号续传（设备保留了已收分块）');
      } catch (e2) { log(`重连失败：${e2.message}`, true); break; }
    }
  }
  clearInterval(timer);
  const t = (performance.now() - t0) / 1000;
  if (res) {                      // closing sample so short transfers still plot
    const dt = Math.max(t - prev.t, 0.05), n = Math.ceil(frame.data.length / chunk);
    chart.data.labels.push(t.toFixed(2));
    chart.data.datasets[0].data.push(+(((n - prev.have) * chunk) / 1024 / dt).toFixed(1));
    chart.data.datasets[1].data.push(+((((last?.sent ?? n) - prev.sent) * chunk) / 1024 / dt).toFixed(1));
    chart.data.datasets[2].data.push((last?.retx ?? 0) - prev.retx);
    chart.update('none');
  }
  if (res) {
    $('progress').innerHTML = `100%<small>${(frame.data.length / 1024 / t).toFixed(1)} KB/s 平均</small>`;
    $('sResult').textContent = `${res.result} / ${res.crcOk ? 'CRC 一致' : 'CRC 不一致'}`;
    $('sTime').textContent = t.toFixed(1); $('sResume').textContent = resumes;
    $('sRetx').textContent = carried.retx + res.retx;
    log(`${label}完成：${(frame.data.length / 1024).toFixed(1)} KB，${t.toFixed(2)} s，重传 ${carried.retx + res.retx}，续传 ${resumes} 次，设备侧 ${res.deviceMs} ms`);
  } else {
    $('sResult').textContent = '失败';
  }
  ui.busy = false; ui.setLinked(!!ui.link && (ui.sim ? ui.link.connected : ui.link.connected));
  return res;
}

$('btnSend').onclick = async () => {
  try { await sendWithResume(await encodeFrame(), '画面'); }
  catch (e) { log(`发送失败：${e.message}`, true); ui.busy = false; ui.setLinked(true); }
};
$('btnNav').onclick = async () => {
  try {
    const icon = icon1bpp(turnSel.canvas);
    await sendWithResume({ data: icon, fmt: FMT.ICON1BPP, layer: LAYER.TURN, width: 48, height: 48, x: 16, y: 16 }, `转向图标（${turnSel.name}）`);
    await new BnpSender(ui.link).sendNav([[NAV.ROAD, $('road').value], [NAV.DIST, $('dist').value],
                                          [NAV.ETA, $('eta').value], [NAV.TURN, turnSel.code]]);
    log(`已发送路名：${$('road').value}，${$('dist').value}，${turnSel.name}`);
  } catch (e) { log(`发送导航信息失败：${e.message}`, true); }
};

renderPreview();
log('就绪。连接开发板，或点“模拟（无板）”在浏览器里跑同一份接收端代码。');
void ERR;
