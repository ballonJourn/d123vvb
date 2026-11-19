// erya_app.js - the computer plays the "领骑摩托" app: 尔雅 BLE 导航协议 Ver 1.2.
// Protocol packing lives in erya_sender.js (tested against the firmware engine).
// SPDX-License-Identifier: Apache-2.0
import { ERYA, NAVI_TYPE, parseEY, picturePackets, naviMessage, splitWrites,
         canvasQuality, parseMacKey } from './erya_sender.js';
import { UUID as BNP } from './bnp_sender.js';

const $ = (id) => document.getElementById(id);
const css = (v) => getComputedStyle(document.documentElement).getPropertyValue(v).trim();
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
// let the UI and other tasks run without a timer (not throttled in background tabs)
const yieldTask = () => new Promise((r) => { const c = new MessageChannel(); c.port1.onmessage = () => { c.port1.close(); r(); }; c.port2.postMessage(0); });
const dec = new TextDecoder();

function log(msg, err = false) {
  const el = $('log'), line = document.createElement('div');
  line.textContent = `${new Date().toLocaleTimeString()}  ${msg}`;
  if (err) line.className = 'err';
  el.appendChild(line);
  el.scrollTop = el.scrollHeight;
}
const step = (n) => $(`st${n}`).classList.add('done');

// ---------------------------------------------------------------- state
const st = {
  dev: null, chr: {}, connected: false, busy: false, loop: false,
  fmt: { w: 800, h: 480, q: 160, type: '420' }, pics: 0, fails: 0, a6: null,
};

function setLinked(on) {
  st.connected = on;
  $('btnConnect').disabled = on;
  $('btnDisconnect').disabled = !on;
  $('btnPic').disabled = $('btnLoop').disabled = $('btnNavi').disabled = !on || st.busy;
  $('btnStop').disabled = !st.loop;
}

// ---------------------------------------------------------------- connect + handshake
async function connect() {
  if (!navigator.bluetooth) throw new Error('此浏览器不支持 Web Bluetooth：请用桌面版 Chrome / Edge，并通过 http://localhost 打开');
  for (let i = 1; i <= 4; i++) $(`st${i}`).classList.remove('done');
  st.dev = await navigator.bluetooth.requestDevice({
    filters: [{ namePrefix: 'EY_' }, { services: [ERYA.svc] }],
    optionalServices: [ERYA.svc, BNP.svc],
  });
  st.dev.addEventListener('gattserverdisconnected', () => {
    st.loop = false; setLinked(false); $('linkline').textContent = '连接已断开。'; log('设备断开', true);
  });
  const srv = await st.dev.gatt.connect();
  let svc;
  try { svc = await srv.getPrimaryService(ERYA.svc); }
  catch { throw new Error('设备上没有服务 0xAAA0：固件 BLE_NAVI_ERYA 未打开，或服务 UUID 被改过'); }
  for (const k of ['AAA4', 'AAA5', 'AAA6', 'AAA7', 'AAA8']) st.chr[k] = await svc.getCharacteristic(ERYA[k]);
  // notifications first, so AAA4/AAA6 sent after the phone-type write are not missed
  for (const k of ['AAA4', 'AAA6']) {
    await st.chr[k].startNotifications();
    st.chr[k].addEventListener('characteristicvaluechanged', (e) => onNotify(k, e.target.value));
  }
  setLinked(true);
  $('linkline').textContent = `${st.dev.name}：已连接`;
  log(`已连接 ${st.dev.name}`);

  // MTU: read the BNP status characteristic of the same firmware (att_mtu at offset 2)
  try {
    const s2 = await (await srv.getPrimaryService(BNP.svc)).getCharacteristic(BNP.stat);
    const v = await s2.readValue();
    const mtu = v.getUint16(2, true);
    if (mtu >= 23) { $('maxw').value = mtu - 3; $('hMtu').textContent = `${mtu}（每次写 ${mtu - 3} 字节）`; }
  } catch { $('hMtu').textContent = `未知，按 ${$('maxw').value} 字节写`; }

  // step 1: poll AAA5 until it is not "0" (document: phone keeps reading)
  let mk = null;
  for (let i = 0; i < 20 && !mk; i++) {
    const t = dec.decode(await st.chr.AAA5.readValue());
    log(`读 AAA5 #${i + 1}: "${t}"`);
    mk = parseMacKey(t);
    if (!mk) await sleep(300);
  }
  if (!mk) throw new Error('AAA5 一直是 "0"：设备未发布 MAC');
  step(1);
  $('hMac').textContent = mk.mac;
  $('hKey').textContent = mk.key || '（空：真机 App 会提示未激活，这里继续）';
  $('hKey').className = mk.key ? 'ok' : 'bad';

  // step 2: phone type
  const phone = +$('phone').value;
  await st.chr.AAA5.writeValueWithResponse(new Uint8Array([phone]));
  step(2);
  log(`写 AAA5: 0x0${phone}（${phone ? '苹果' : '安卓'}）`);

  // steps 3/4 arrive as notifications; fall back to reads after 1.5 s
  await sleep(1500);
  if (!st.a6) onNotify('AAA6', await st.chr.AAA6.readValue(), true);
  if ($('hA4').textContent === '–') onNotify('AAA4', await st.chr.AAA4.readValue(), true);
}

function onNotify(k, dv, viaRead = false) {
  const u8 = new Uint8Array(dv.buffer, dv.byteOffset, dv.byteLength).slice();
  const how = viaRead ? '读' : '通知';
  if (k === 'AAA4') {
    const t = dec.decode(u8);
    $('hA4').textContent = t; step(3);
    log(`${how} AAA4: "${t}"${t === '-3' ? '（仪表仅支持 BLE）' : ''}`);
  } else if (k === 'AAA6') {
    const f = parseEY(u8);
    if (!f) { log(`${how} AAA6: 不是 EY 帧 (${u8.length} B)`, true); return; }
    st.a6 = f; step(4);
    st.fmt = { w: +f.w || 800, h: +f.h || 480, q: +f.q || 160, type: String(f.type || '420') };
    $('hFmt').textContent = `${st.fmt.w}×${st.fmt.h}，q ${st.fmt.q}（JPEG 质量 ${canvasQuality(st.fmt.q)}），${st.fmt.type}`;
    log(`${how} AAA6: ${JSON.stringify(f)}`);
    renderPreview();
  }
}

// ---------------------------------------------------------------- picture source
const preview = $('preview');
let fileImg = null;

function drawSample(g, W, H, n) {
  g.fillStyle = '#1b2733'; g.fillRect(0, 0, W, H);
  g.strokeStyle = '#2f4357'; g.lineWidth = Math.max(6, W / 60);
  for (let i = 1; i < 6; i++) { g.beginPath(); g.moveTo(0, (H * i) / 6); g.lineTo(W, (H * i) / 6 + (i % 2 ? 30 : -20)); g.stroke(); }
  for (let i = 1; i < 8; i++) { g.beginPath(); g.moveTo((W * i) / 8, 0); g.lineTo((W * i) / 8 - 40, H); g.stroke(); }
  // route moves a little every frame so consecutive pictures differ
  const off = (n * 12) % 120;
  g.strokeStyle = '#3fb0a5'; g.lineWidth = Math.max(10, W / 45); g.lineCap = 'round'; g.lineJoin = 'round';
  g.beginPath(); g.moveTo(W * 0.5, H + 10); g.lineTo(W * 0.5, H * 0.45 + off * 0.3); g.lineTo(W * 0.82, H * 0.3); g.stroke();
  g.fillStyle = '#ffffff';
  g.beginPath(); g.moveTo(W * 0.5, H * 0.78); g.lineTo(W * 0.47, H * 0.86); g.lineTo(W * 0.53, H * 0.86); g.fill();
  g.fillStyle = 'rgba(0,0,0,.55)'; g.fillRect(0, 0, W, H * 0.16);
  g.fillStyle = '#fff'; g.font = `600 ${Math.round(H * 0.08)}px sans-serif`; g.textBaseline = 'middle';
  g.fillText(`${$('nStep').value} 米后 进入 ${$('nRoad').value}`, W * 0.03, H * 0.08);
  g.font = `${Math.round(H * 0.045)}px sans-serif`; g.textAlign = 'right';
  g.fillText(`#${n}  ${new Date().toLocaleTimeString()}`, W * 0.97, H * 0.08); g.textAlign = 'left';
}

function renderPreview(n = st.pics + 1) {
  const { w, h } = st.fmt;
  if (preview.width !== w || preview.height !== h) { preview.width = w; preview.height = h; }
  preview.style.aspectRatio = `${w} / ${h}`;
  const g = preview.getContext('2d');
  if ($('src').value === 'file' && fileImg) {
    g.fillStyle = '#000'; g.fillRect(0, 0, w, h);
    const s = Math.min(w / fileImg.width, h / fileImg.height);
    const dw = fileImg.width * s, dh = fileImg.height * s;
    g.drawImage(fileImg, (w - dw) / 2, (h - dh) / 2, dw, dh);
  } else {
    drawSample(g, w, h, n);
  }
}

async function encodeJpeg() {
  const blob = await new Promise((r) => preview.toBlob(r, 'image/jpeg', canvasQuality(st.fmt.q)));
  return new Uint8Array(await blob.arrayBuffer());
}

// ---------------------------------------------------------------- packet map
const map = {
  n: 0, s: new Uint8Array(0),
  reset(n) { this.n = n; this.s = new Uint8Array(n); this.draw(); },
  set(i, v) { this.s[i] = v; },
  draw() {
    const cv = $('chunkmap'), dpr = window.devicePixelRatio || 1;
    const W = Math.floor(cv.clientWidth * dpr), H = Math.floor(cv.clientHeight * dpr);
    if (cv.width !== W || cv.height !== H) { cv.width = W; cv.height = H; }
    const g = cv.getContext('2d');
    g.clearRect(0, 0, W, H);
    if (!this.n) return;
    let s = Math.max(1, Math.floor(Math.sqrt((W * H) / this.n)));
    while (s > 1 && Math.floor(W / s) * Math.floor(H / s) < this.n) s--;
    const cols = Math.floor(W / s), gap = s >= 5 ? 1 : 0;
    const col = [css('--pend'), css('--ok'), css('--lost')];
    for (let v = 0; v < 3; v++) {
      g.fillStyle = col[v];
      for (let i = 0; i < this.n; i++) if (this.s[i] === v) g.fillRect((i % cols) * s, Math.floor(i / cols) * s, s - gap, s - gap);
    }
  },
};
addEventListener('resize', () => map.draw());

const chart = new Chart($('chart'), {
  type: 'line',
  data: { labels: [], datasets: [{ label: '每张图写入速率 KB/s', data: [], borderColor: css('--ok'), backgroundColor: css('--ok'), pointRadius: 2, borderWidth: 2, tension: 0.2 }] },
  options: {
    animation: false, maintainAspectRatio: false,
    scales: {
      x: { title: { display: true, text: '第几张' }, ticks: { color: css('--ink-2') }, grid: { color: css('--rule') } },
      y: { beginAtZero: true, title: { display: true, text: 'KB/s' }, ticks: { color: css('--ink-2') }, grid: { color: css('--rule') } },
    },
    plugins: { legend: { labels: { color: css('--ink-2') } } },
  },
});

// ---------------------------------------------------------------- writes
// No timers in the send path: in a background tab Chrome stretches every
// setTimeout to >= 1 s, which turned a 4 ms back-off into seconds per picture.
// When Chrome's write-without-response queue is full, send that packet with a
// write request instead: it resolves on the ATT response and paces the
// stream by itself.
async function writeA7(u8) {
  if ($('wmode').value === 'r') return st.chr.AAA7.writeValueWithResponse(u8);
  try {
    return await st.chr.AAA7.writeValueWithoutResponse(u8);
  } catch (e) {
    if (!st.connected) throw e;
    st.paced = (st.paced || 0) + 1;
    return st.chr.AAA7.writeValueWithResponse(u8);
  }
}

async function sendPicture() {
  renderPreview();
  const jpeg = await encodeJpeg();
  const maxw = Math.max(20, +$('maxw').value || 244);
  const { packets, piece, sum, fitsOneWrite } = picturePackets(jpeg, maxw);
  const writes = packets.map((p) => splitWrites(p, maxw));
  const nWrites = writes.reduce((t, w) => t + w.length, 0);
  map.reset(sum);
  $('imginfo').textContent = `${st.fmt.w}×${st.fmt.h} JPEG ${(jpeg.length / 1024).toFixed(1)} KB，` +
    `${sum} 包 × ${piece} B${fitsOneWrite ? '' : `（MTU 太小，每包拆成多次写入）`}`;
  $('frameinfo').textContent = `第 ${st.pics + 1} 张`;
  $('sBytes').textContent = (jpeg.length / 1024).toFixed(1);
  $('sPkts').textContent = `${sum} / ${nWrites}`;
  log(`发送 AAA7：${jpeg.length} B，${sum} 包，${nWrites} 次写入（每次 ≤ ${maxw} B）`);

  const t0 = performance.now();
  let done = 0, last = 0;
  for (let i = 0; i < sum; i++) {
    try {
      for (const w of writes[i]) await writeA7(w);
      map.set(i, 1);
    } catch (e) {
      map.set(i, 2); st.fails++; $('sFail').textContent = st.fails;
      log(`包 ${i} 写入失败：${e.message}`, true);
      map.draw();
      throw e;
    }
    done += packets[i].length;
    const now = performance.now();
    if (now - last > 100 || i === sum - 1) {
      last = now;
      const t = (now - t0) / 1000;
      $('progress').innerHTML = `${Math.round(((i + 1) * 100) / sum)}%<small>${i + 1} / ${sum} 包</small>`;
      $('sThr').textContent = (done / 1024 / Math.max(t, 0.001)).toFixed(1);
      $('sTime').textContent = t.toFixed(2);
      map.draw();
    }
  }
  const t = (performance.now() - t0) / 1000;
  st.pics++;
  $('sPics').textContent = st.pics;
  const now = performance.now();
  st.times = (st.times || []).concat(now).slice(-11);
  if (st.times.length > 1)
    $('sFps').textContent = ((st.times.length - 1) * 1000 / (now - st.times[0])).toFixed(2);
  $('sPaced').textContent = st.paced || 0;
  chart.data.labels.push(String(st.pics));
  chart.data.datasets[0].data.push(+(jpeg.length / 1024 / t).toFixed(1));
  if (chart.data.labels.length > 60) { chart.data.labels.shift(); chart.data.datasets[0].data.shift(); }
  chart.update();
  log(`第 ${st.pics} 张写完：${t.toFixed(2)} s，${(jpeg.length / 1024 / t).toFixed(1)} KB/s`);
}

async function sendNavi() {
  const type = $('ntype').value;
  const frame = naviMessage(type, {
    curStepRetainDistance: $('nStep').value, nextRoadName: $('nRoad').value,
    pathRetainTime: $('nTime').value, pathRetainDistance: $('nDist').value,
    iconType: $('nIcon').value, arriveTime: $('nEta').value,
  });
  const maxw = Math.max(20, +$('maxw').value || 244);
  for (const w of splitWrites(frame, maxw)) await st.chr.AAA8.writeValueWithResponse(w);
  log(`写 AAA8：${dec.decode(frame.subarray(6))}`);
}

// ---------------------------------------------------------------- buttons
async function guarded(fn) {
  st.busy = true; setLinked(st.connected);
  try { await fn(); } catch (e) { log(e.message || String(e), true); }
  st.busy = false; setLinked(st.connected);
}
$('btnConnect').onclick = () => guarded(connect);
$('btnDisconnect').onclick = () => { st.loop = false; st.dev?.gatt.disconnect(); };
$('btnPic').onclick = () => guarded(sendPicture);
$('btnNavi').onclick = () => guarded(sendNavi);
$('btnLoop').onclick = async () => {
  st.loop = true; $('btnStop').disabled = false;
  log('开始连续发送（模拟导航刷新）');
  while (st.loop && st.connected) {
    const t0 = performance.now();
    await guarded(sendPicture);
    $('btnStop').disabled = false;
    const wait = (+$('interval').value || 0) - (performance.now() - t0);
    if (wait > 0) await sleep(wait);   // interval = picture period; 0 = as fast as the link allows
    else await yieldTask();
  }
  st.loop = false; setLinked(st.connected);
  log('连续发送已停止');
};
$('btnStop').onclick = () => { st.loop = false; $('btnStop').disabled = true; };
$('src').onchange = () => renderPreview();
$('file').onchange = async (e) => {
  const f = e.target.files[0];
  if (!f) return;
  fileImg = await createImageBitmap(f);
  $('src').value = 'file';
  renderPreview();
};
for (const id of ['nStep', 'nRoad']) $(id).oninput = () => renderPreview();

document.addEventListener('visibilitychange', () => {
  if (document.hidden && st.loop)
    log('网页切到后台：Chrome 会把定时器拉到 ≥1 秒，间隔 > 0 时实际帧率会下降；间隔设 0 不受影响', true);
});

renderPreview(1);
setLinked(false);
