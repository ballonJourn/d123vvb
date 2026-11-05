"use strict";
// D133 dataServer dashboard. Polls /api/status (client_web.py) and renders with
// textContent only: file names come from the board and are never parsed as HTML.
const $ = id => document.getElementById(id);
const MAX_BLOCKS = 2000;                       // same cap as client_web.py (MAX_CHUNKS_UI)
const LABEL = {IDLE: "Idle", CONNECTING: "Connecting…", RECEIVING: "Receiving", SUCCESS: "Done",
               FAILED: "Nothing received", ERROR: "Error"};
let st = null, selected = null, follow = true, filesStamp = "", blocksStamp = "";

try { document.documentElement.dataset.theme = localStorage.getItem("d133.theme") || "dark"; } catch (e) {}

const text = (id, v) => { const el = $(id); v = String(v); if (el.textContent !== v) el.textContent = v; };
const bytes = n => {
  if (typeof n !== "number") return "—";
  if (n < 1024) return n + " B";
  if (n < 1048576) return (n / 1024).toFixed(1) + " KB";
  return (n / 1048576).toFixed(2) + " MB";
};
const rate = n => typeof n === "number" ? (n >= 1048576 ? (n / 1048576).toFixed(2) + " MB/s" : (n / 1024).toFixed(1) + " KB/s") : "—";
const busy = s => s === "CONNECTING" || s === "RECEIVING";

function notice(msg) { const el = $("error"); el.hidden = !msg; el.textContent = msg || ""; }

async function call(path, body) {
  const r = await fetch(path, {cache: "no-store", ...(body !== undefined ? {
    method: "POST", headers: {"Content-Type": "application/json"}, body: JSON.stringify(body)} : {})});
  const v = await r.json().catch(() => ({}));
  if (!r.ok) throw new Error(v.error || "HTTP " + r.status);
  return v;
}

function cards(s) {
  const box = $("status-value");
  box.classList.toggle("busy", busy(s.status));
  box.classList.toggle("loaded", s.status === "SUCCESS");
  box.classList.toggle("status-error", s.status === "ERROR" || s.status === "FAILED");
  text("status", LABEL[s.status] || s.status);
  text("target", s.board ? s.board + " / " + (s.directory || "") : "\u00a0");
  const files = s.files || [], done = files.filter(f => f.done).length;
  text("files-count", files.length ? done + " / " + files.length : "—");
  text("files-sub", files.length ? (busy(s.status) ? "Receiving " + (s.filename || "") : "Finished") : "Nothing requested yet");
  const hist = s.speed_history || [], peak = hist.reduce((m, p) => Math.max(m, p[1]), 0);
  text("speed", busy(s.status) ? rate(s.current_speed) : "—");
  text("speed-sub", "Peak " + (peak ? rate(peak) : "—"));
  text("bytes", s.bytes_total ? bytes(s.bytes_total) : "—");
  text("time-sub", "Average " + (s.transfer_speed ? rate(s.transfer_speed) : "—") + " · " +
       (s.transfer_time != null ? s.transfer_time + " s" : "— s"));
  text("outdir", s.output_file ? "→ " + s.output_file : "");
  $("pull").disabled = busy(s.status);
}

function chart(s) {
  const svg = $("chart"), h = s.speed_history || [];
  const peak = h.reduce((m, p) => Math.max(m, p[1]), 0), tmax = h.length ? h[h.length - 1][0] : 0;
  const NS = "http://www.w3.org/2000/svg";
  svg.replaceChildren();
  for (const y of [0, 55, 110, 165, 220]) {
    const g = document.createElementNS(NS, "line");
    g.setAttribute("class", "grid"); g.setAttribute("x1", 0); g.setAttribute("x2", 1000);
    g.setAttribute("y1", y); g.setAttribute("y2", y); svg.append(g);
  }
  text("chart-max", peak ? rate(peak) : "—");
  text("chart-t", tmax ? tmax.toFixed(1) + " s" : "—");
  if (h.length < 2 || !peak || !tmax) return;
  const pts = h.map(p => [(p[0] / tmax) * 1000, 220 - (p[1] / peak) * 210]);
  const line = pts.map(p => p[0].toFixed(1) + "," + p[1].toFixed(1)).join(" ");
  const area = document.createElementNS(NS, "polygon");
  area.setAttribute("class", "area"); area.setAttribute("points", "0,220 " + line + " 1000,220");
  const pl = document.createElementNS(NS, "polyline");
  pl.setAttribute("class", "line"); pl.setAttribute("points", line);
  svg.append(area, pl);
}

function fileList(s) {
  const files = s.files || [];
  if (follow && s.filename) selected = s.filename;
  const stamp = JSON.stringify([files.map(f => [f.path, f.done, f.size]), selected, s.status]);
  if (stamp === filesStamp) return;
  filesStamp = stamp;
  const box = $("files"); box.replaceChildren();
  if (!files.length) {
    const e = document.createElement("div"); e.className = "empty";
    e.textContent = "No files yet. Press Pull to request a directory."; box.append(e); return;
  }
  for (const f of files) {
    const b = document.createElement("button");
    b.className = "request" + (selected === f.path ? " selected" : "");
    const row = document.createElement("div"); row.className = "row";
    const sz = document.createElement("span"); sz.textContent = bytes(f.size);
    const tag = document.createElement("span");
    tag.className = "tag " + (f.done ? "completed" : "loading"); tag.textContent = f.done ? "done" : "receiving";
    row.append(sz, tag);
    const name = document.createElement("div"); name.className = "endpoint"; name.textContent = f.path;
    b.append(row, name);
    b.onclick = () => { selected = f.path; follow = false; filesStamp = ""; render(); };
    box.append(b);
  }
}

function detail(s) {
  const f = (s.files || []).find(x => x.path === selected);
  $("no-selection").hidden = !!f; $("selection").hidden = !f;
  if (!f) return;
  const bs = s.block_size || 0, n = bs ? Math.ceil(f.size / bs) : 0;
  const current = f.path === s.filename && !f.done;
  const got = f.done ? n : (current ? (s.chunks_received || []).length : 0);
  text("d-title", f.path);
  const tag = $("d-tag"); tag.className = "tag " + (f.done ? "completed" : "loading"); tag.textContent = f.done ? "done" : "receiving";
  text("d-size", bytes(f.size) + (f.size >= 1024 ? " (" + f.size + " B)" : ""));
  text("d-blocks", f.size ? got + " / " + n : "0 (empty file)");
  text("d-bs", bs ? bs + " B" : "—");
  text("d-state", f.done ? "Verified on disk" : (current ? "Receiving" : "Waiting"));
  text("d-sha", f.sha256 || "pending…");
  const shown = Math.min(n, MAX_BLOCKS), on = Math.min(got, shown);
  const stamp = f.path + "|" + shown + "|" + on;
  if (stamp === blocksStamp) return;
  blocksStamp = stamp;
  const box = $("blocks"); box.replaceChildren();
  const frag = document.createDocumentFragment();
  for (let i = 0; i < shown; i++) { const c = document.createElement("i"); if (i < on) c.className = "on"; c.title = "block " + i; frag.append(c); }
  box.append(frag);
}

function logbox(s) {
  const el = $("log"), t = (s.logs || []).join("\n");
  if (el.textContent === t) return;
  const atEnd = el.scrollTop + el.clientHeight >= el.scrollHeight - 8;
  el.textContent = t;
  if (atEnd) el.scrollTop = el.scrollHeight;
}

function render() {
  if (!st) return;
  cards(st); chart(st); fileList(st); detail(st); logbox(st);
  const d = $("dir"); if (!d.value && st.directory) d.value = st.directory;
}

async function poll() {
  try {
    st = await call("/api/status");
    notice(""); render();
    text("updated", "Updated " + new Date().toLocaleTimeString());
  } catch (e) { notice("Dashboard backend not reachable: " + e.message); text("updated", "Disconnected"); }
}

$("pull").onclick = async () => {
  const dir = $("dir").value.trim();
  if (!dir) { notice("Enter a directory below the board root."); $("dir").focus(); return; }
  follow = true; selected = null; filesStamp = ""; blocksStamp = "";
  try { await call("/api/pull", {board: $("board").value.trim(), port: parseInt($("port").value, 10) || 0, directory: dir}); notice(""); }
  catch (e) { notice(e.message); }
  poll();
};
$("reset").onclick = async () => {
  try { await call("/api/reset", {}); } catch (e) { notice(e.message); }
  follow = true; selected = null; filesStamp = ""; blocksStamp = ""; $("dir").value = ""; poll();
};
$("theme").onclick = () => {
  const t = document.documentElement.dataset.theme === "dark" ? "light" : "dark";
  document.documentElement.dataset.theme = t;
  try { localStorage.setItem("d133.theme", t); } catch (e) {}
};
$("dir").addEventListener("keydown", e => { if (e.key === "Enter") $("pull").click(); });

// ── Board network panel: /api/board (client_web.py polls the board's dsstat port) ──
const IF_COLORS = ["var(--st-info)", "var(--st-accent)", "var(--st-warn)", "var(--st-danger)", "var(--st-ink-soft)"];
const ifColors = {};
const ifColor = name => ifColors[name] || (ifColors[name] = IF_COLORS[Object.keys(ifColors).length % IF_COLORS.length]);
let netStamp = "";

function el(tag, cls, txt) { const e = document.createElement(tag); if (cls) e.className = cls; if (txt !== undefined) e.textContent = txt; return e; }

function netCards(b) {
  const box = $("ifcards");
  const stamp = JSON.stringify((b.ifs || []).map(i => [i.name, i.ip, i.up, i.rx_rate, i.tx_rate, i.rx_bytes, i.tx_bytes]));
  if (stamp === netStamp) return;
  netStamp = stamp; box.replaceChildren();
  if (!(b.ifs || []).length) { box.append(el("div", "small", b.ok ? "No interfaces reported." : "Waiting for the board…")); return; }
  for (const i of b.ifs) {
    const c = el("div", "ifcard" + (i.up ? "" : " down")); c.style.setProperty("--c", ifColor(i.name));
    const head = el("div", "row"); head.append(el("span", "ifname", i.name), el("span", "tag " + (i.up ? "completed" : "error"), i.up ? "up" : "down"));
    const rates = el("div", "rates"); rates.append(el("span", "", "↓ " + rate(i.rx_rate)), el("span", "", "↑ " + rate(i.tx_rate)));
    c.append(head, el("div", "ifip", i.ip), rates,
             el("div", "tot", "rx " + bytes(i.rx_bytes) + " / " + i.rx_pkts + " pkts · tx " + bytes(i.tx_bytes) + " / " + i.tx_pkts + " pkts"));
    box.append(c);
  }
}

function netChart(b) {
  const svg = $("netchart"), NS = "http://www.w3.org/2000/svg", hist = b.history || {};
  svg.replaceChildren();
  for (const y of [0, 55, 110, 165, 220]) {
    const g = document.createElementNS(NS, "line"); g.setAttribute("class", "grid");
    g.setAttribute("x1", 0); g.setAttribute("x2", 1000); g.setAttribute("y1", y); g.setAttribute("y2", y); svg.append(g);
  }
  let peak = 0, t0 = Infinity, t1 = 0;
  for (const h of Object.values(hist)) for (const p of h) { peak = Math.max(peak, p[1], p[2]); t0 = Math.min(t0, p[0]); t1 = Math.max(t1, p[0]); }
  text("net-max", peak ? rate(peak) : "—");
  text("net-t", t1 > t0 ? (t1 - t0).toFixed(0) + " s window" : "—");
  const legend = $("net-legend"); legend.replaceChildren();
  for (const name of Object.keys(hist)) {
    const item = el("span", ""); const sw = el("i", ""); sw.style.setProperty("--c", ifColor(name));
    item.append(sw, document.createTextNode(name + "  ↓ solid  ↑ dashed")); legend.append(item);
  }
  if (!peak || t1 <= t0) return;
  for (const [name, h] of Object.entries(hist)) {
    if (h.length < 2) continue;
    for (const [k, cls] of [[1, "rx"], [2, "tx"]]) {
      const pl = document.createElementNS(NS, "polyline");
      pl.setAttribute("class", cls); pl.style.stroke = ifColor(name);
      pl.setAttribute("points", h.map(p => (((p[0] - t0) / (t1 - t0)) * 1000).toFixed(1) + "," + (220 - (p[k] / peak) * 210).toFixed(1)).join(" "));
      svg.append(pl);
    }
  }
}

function netFacts(b) {
  const ds = b.dataserver;
  text("n-fw", b.ip_forwarded == null ? "n/a (RT_LWIP_STATS off)" : b.ip_forwarded + " pkts" + (b.fw_rate != null ? " · " + b.fw_rate + "/s" : ""));
  text("n-conn", ds ? ds.connections + (ds.running ? "" : " (stopped)") : "—");
  text("n-files", ds ? ds.files_sent + " · queued " + ds.queued : "—");
  text("n-bytes", ds ? bytes(ds.bytes_sent) : "—");
}

async function pollBoard() {
  const host = $("board").value.trim(), sport = parseInt($("sport").value, 10) || "";
  if (!host) return;
  try {
    const b = await call("/api/board?board=" + encodeURIComponent(host) + "&port=" + sport);
    const err = $("net-error"); err.hidden = b.ok || !b.error; err.textContent = b.ok ? "" : (b.error || "");
    text("net-target", b.target ? "→ " + b.target : "");
    text("net-updated", b.updated ? "· board uptime " + Math.round((b.uptime_ms || 0) / 1000) + " s · " + b.updated : "");
    netCards(b); netChart(b); netFacts(b);
  } catch (e) { const err = $("net-error"); err.hidden = false; err.textContent = "Board stats unavailable: " + e.message; }
}

call("/api/config").then(c => {
  $("board").value = c.board; $("port").value = c.port; $("sport").value = c.stat_port || 9001; pollBoard();
}).catch(() => {});
poll(); setInterval(poll, 500); setInterval(pollBoard, 1000);
