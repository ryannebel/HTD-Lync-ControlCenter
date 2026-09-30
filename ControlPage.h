#pragma once

#include <Arduino.h>

static const char LYNC_CONTROL_PAGE[] PROGMEM = R"LYNCPAGE(
<!doctype html>
<html>
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>Lync Control Center</title>
  <style>
	body { font-family: Arial, sans-serif; margin: clamp(10px, 3vw, 16px); background:#111; color:#eee; }
	h1 { margin-top: 0; }
	.grid { display:grid; grid-template-columns: repeat(auto-fill,minmax(240px,1fr)); gap:12px; }
	.card { border:1px solid #333; border-radius:10px; padding:12px; background:#1c1c1c; }
	.card-power { border-color:#22c55e; }
	.card-mute { border-color:#eab308; }
	.card-dnd { border-color:#ef4444; }
	.card-unknown {
	  border-color:#b91c1c;
	  background:
		repeating-linear-gradient(
		  -45deg,
		  rgba(185, 28, 28, 0.25) 0px,
		  rgba(185, 28, 28, 0.25) 10px,
		  rgba(28, 28, 28, 0.95) 10px,
		  rgba(28, 28, 28, 0.95) 20px
		);
	}
	.row { display:flex; gap:8px; align-items:center; margin:8px 0; flex-wrap:wrap; }
	.slider-row {
	  display:grid;
	  grid-template-columns: 56px minmax(0, 1fr) 36px;
	  align-items:center;
	  gap:8px;
	}
	.input-row {
	  display:grid;
	  grid-template-columns: 56px minmax(0, 1fr);
	  align-items:center;
	  gap:8px;
	}
	.slider-row label { width:56px; }
	.input-row label { width:56px; }
	.input-row select { width:100%; min-width:0; }
	.slider-row input[type=range] { width:100%; min-width:0; }
	.slider-value { text-align:right; font-variant-numeric: tabular-nums; }
	.switch-row { display:grid; grid-template-columns:repeat(3, minmax(0, 1fr)); gap:8px; }
	.switch-row.collapsed { grid-template-columns:1fr; }
	.card.zone-card { cursor:pointer; }
	.card.zone-card button,
	.card.zone-card input,
	.card.zone-card select,
	.card.zone-card label {
	  cursor:auto;
	}
	#menuToggle {
	  display:none;
	  width:100%;
	  margin:8px 0;
	}
	#commandRow {
	  display:grid;
	  grid-template-columns:repeat(auto-fit, minmax(130px, 1fr));
	  width:100%;
	}
	#commandRow button { width:100%; }
	.toggle-btn {
	  width:100%;
	  border:1px solid #2f2f2f;
	  border-radius:8px;
	  padding:8px 10px;
	  background:#161616;
	  color:#eee;
	  font-weight:600;
	  cursor:pointer;
	  transition:background .15s ease, border-color .15s ease;
	}
	.toggle-btn:focus-visible {
	  outline:2px solid #8ec5ff;
	  outline-offset:2px;
	}
	.toggle-btn.power.on { background:#22c55e; border-color:#22c55e; color:#0b1f13; }
	.toggle-btn.mute.on { background:#eab308; border-color:#eab308; color:#2a2404; }
	.toggle-btn.dnd.on { background:#ef4444; border-color:#ef4444; color:#2b0b0b; }
	input[type=number] { width:80px; }
	select,
	button,
	input[type=range],
	input[type=number] {
	  min-height:40px;
	}
	input[type=range] { width:100%; }
	button { padding:6px 10px; }
	#log { margin-top:12px; white-space:pre-wrap; font-family:monospace; background:#090909; padding:10px; border-radius:8px; word-break:break-word; }
	@media (max-width: 700px) {
	  .grid { grid-template-columns: 1fr; }
	  #menuToggle { display:block; }
	  #commandRow { display:none; grid-template-columns:1fr; }
	  #commandRow.menu-open { display:grid; }
	  .switch-row { grid-template-columns:repeat(2, minmax(0, 1fr)); }
	}
	@media (max-width: 360px) {
	  .slider-row,
	  .input-row { grid-template-columns:1fr; }
	  .slider-value { text-align:left; }
	  .switch-row { grid-template-columns:repeat(2, minmax(0, 1fr)); }
	}
  </style>
</head>
<body>
	<h1 id="pageTitle">Lync Control Center</h1>
  <button id="menuToggle" type="button" onclick="toggleCommandMenu()" aria-label="Menu" title="Menu" aria-expanded="false" aria-controls="commandRow">☰</button>
  <div id="commandRow" class="row">
	<button id="btnRefresh" onclick="refreshStatus()">Refresh</button>
	<button id="btnQueryAll" onclick="cmd('/api/cmd/query')">Query All</button>
	<button id="btnNames" onclick="location.href='/names'">Names</button>
	<button id="btnEthernet" onclick="location.href='/ethernet'">Ethernet</button>
	<button id="btnRs232" onclick="location.href='/rs232'">RS232 Test</button>
  </div>
  <div id="zones" class="grid"></div>
  <div id="log"></div>
<script>
const zonesEl = document.getElementById('zones');
const logEl = document.getElementById('log');
const titleEl = document.getElementById('pageTitle');
const menuToggleEl = document.getElementById('menuToggle');
const commandRowEl = document.getElementById('commandRow');
const btnRefreshEl = document.getElementById('btnRefresh');
const btnQueryAllEl = document.getElementById('btnQueryAll');
const btnNamesEl = document.getElementById('btnNames');
const btnEthernetEl = document.getElementById('btnEthernet');
const btnRs232El = document.getElementById('btnRs232');
const state = {};
const zoneNames = {};
const sourceNames = {};
const editLocks = {};
let expandedZone = null;
const EDIT_LOCK_MS = 1200;

const VISIBLE_TITLE_BIT = 0;
const VISIBLE_BUTTON_REFRESH_BIT = 1;
const VISIBLE_BUTTON_QUERY_ALL_BIT = 2;
const VISIBLE_BUTTON_NAMES_BIT = 3;
const VISIBLE_BUTTON_RS232_BIT = 4;
const VISIBLE_LOG_BIT = 5;
const VISIBLE_ZONE_START_BIT = 6;
const TOTAL_VISIBLE_BITS = 18;
const DEFAULT_VISIBILITY_MASK = (1 << TOTAL_VISIBLE_BITS) - 1;
const ALL_SOURCES_MASK = (1 << 18) - 1;

function parseVisibilityMask() {
  const maskParam = new URLSearchParams(window.location.search).get('mask');
  if (maskParam === null || maskParam === '') {
	return DEFAULT_VISIBILITY_MASK;
  }
  const mask = Number(maskParam);
  if (!Number.isInteger(mask) || mask < 0) {
	return DEFAULT_VISIBILITY_MASK;
  }
  return mask;
}

function parseShowAllDetails() {
	const value = new URLSearchParams(window.location.search).get('showAll');
	if (value === null) {
	  return false;
	}
	const normalized = value.trim().toLowerCase();
	return normalized === '1' || normalized === 'true' || normalized === 'yes';
}

const visibilityMask = parseVisibilityMask();
const showAllDetails = parseShowAllDetails();
const visibleZones = [];

function isBitVisible(bitIndex) {
  return ((visibilityMask >>> bitIndex) & 1) === 1;
}

for (let zone = 1; zone <= 12; zone++) {
  if (isBitVisible(VISIBLE_ZONE_START_BIT + zone - 1)) {
	visibleZones.push(zone);
  }
}

function applyVisibilityConfig() {
  titleEl.hidden = !isBitVisible(VISIBLE_TITLE_BIT);
  btnRefreshEl.hidden = !isBitVisible(VISIBLE_BUTTON_REFRESH_BIT);
  btnQueryAllEl.hidden = !isBitVisible(VISIBLE_BUTTON_QUERY_ALL_BIT);
  btnNamesEl.hidden = !isBitVisible(VISIBLE_BUTTON_NAMES_BIT);
	btnEthernetEl.hidden = !isBitVisible(VISIBLE_BUTTON_NAMES_BIT);
  btnRs232El.hidden = !isBitVisible(VISIBLE_BUTTON_RS232_BIT);
  commandRowEl.hidden =
	btnRefreshEl.hidden &&
	btnQueryAllEl.hidden &&
	btnNamesEl.hidden &&
	btnEthernetEl.hidden &&
	btnRs232El.hidden;
	menuToggleEl.hidden = commandRowEl.hidden;
  logEl.hidden = !isBitVisible(VISIBLE_LOG_BIT);
}

function toggleCommandMenu() {
	const isOpen = commandRowEl.classList.toggle('menu-open');
	menuToggleEl.setAttribute('aria-expanded', isOpen ? 'true' : 'false');
}

function closeCommandMenu() {
	commandRowEl.classList.remove('menu-open');
	menuToggleEl.setAttribute('aria-expanded', 'false');
}

commandRowEl.addEventListener('click', (event) => {
	if (window.matchMedia('(max-width: 700px)').matches && event.target.closest('button')) {
	  closeCommandMenu();
	}
});

function log(line) {
	if (logEl.hidden) {
	return;
  }
  const ts = new Date().toLocaleTimeString();
  logEl.textContent = `[${ts}] ${line}\n` + logEl.textContent;
}

function sourceVisibleForZone(zone, source) {
  const zoneInfo = zoneNames[zone] || {};
  const mask = Number.isInteger(Number(zoneInfo.sourceMask)) ? Number(zoneInfo.sourceMask) : ALL_SOURCES_MASK;
  return ((mask >>> (source - 1)) & 1) === 1;
}

function inputOptions(zone, selected) {
  let out = '';
	let hasVisible = false;
  for (let i = 1; i <= 18; i++) {
	if (!sourceVisibleForZone(zone, i)) {
	  continue;
	}
	hasVisible = true;
	const label = (sourceNames[i] && sourceNames[i].full) ? sourceNames[i].full : `Input ${i}`;
	out += `<option value="${i}" ${i === Number(selected) ? 'selected' : ''}>${label}</option>`;
  }

  if (!hasVisible) {
	const selectedSource = Number(selected) || 1;
	const label = (sourceNames[selectedSource] && sourceNames[selectedSource].full)
	  ? sourceNames[selectedSource].full
	  : `Input ${selectedSource}`;
	out = `<option value="${selectedSource}" selected>${label}</option>`;
  }
  return out;
}

function volumeLevelFromDb(db) {
  return Math.max(0, Math.min(59, Number(db) + 60));
}

function zoneCard(z) {
	const s = state[z] || {known:false,powerOn:false,muteOn:false,dndOn:false,input:0,volumeDb:-60,trebleDb:0,bassDb:0,balanceDb:0};
	const zoneLabel = (zoneNames[z] && zoneNames[z].full) ? zoneNames[z].full : `Zone ${z}`;
	const stateClass = !s.known ? 'card-unknown' : (s.dndOn ? 'card-dnd' : (s.muteOn ? 'card-mute' : (s.powerOn ? 'card-power' : '')));
	const expanded = showAllDetails || expandedZone === z;
  return `
	<div class="card zone-card ${stateClass}" onclick="handleZoneCardTap(event, ${z})">
	<h3 style="margin-top: 0; margin-bottom: 0;">${zoneLabel}</h3>
	<div class="row switch-row ${expanded ? '' : 'collapsed'}">
	  <button class="toggle-btn power ${s.powerOn ? 'on' : ''}" onclick="toggleCmd('power', ${z}, ${s.powerOn ? 1 : 0})">Power ${s.powerOn ? 'On' : 'Off'}</button>
	  ${expanded ? `<button class="toggle-btn mute ${s.muteOn ? 'on' : ''}" onclick="toggleCmd('mute', ${z}, ${s.muteOn ? 1 : 0})">Mute ${s.muteOn ? 'On' : 'Off'}</button>` : ''}
	  ${expanded ? `<button class="toggle-btn dnd ${s.dndOn ? 'on' : ''}" onclick="toggleCmd('dnd', ${z}, ${s.dndOn ? 1 : 0})">DND ${s.dndOn ? 'On' : 'Off'}</button>` : ''}
	</div>
	${expanded ? `<div class="row input-row">
	  <label>Input</label>
	  <select id="in_${z}" onchange="inputSet(${z}, this.value)">${inputOptions(z, s.input || 1)}</select>
	</div>` : ''}
	<div class="row slider-row">
	  <label>Volume</label>
	  <input id="vol_${z}" type="range" min="0" max="59" value="${volumeLevelFromDb(s.volumeDb)}" onpointerdown="beginSliderEdit('volume', ${z})" onmousedown="beginSliderEdit('volume', ${z})" ontouchstart="beginSliderEdit('volume', ${z})" oninput="sliderPreview('volume', ${z}, this.value)" onchange="sliderSet('volume', ${z}, this.value)" />
	  <span class="slider-value" id="volv_${z}">${volumeLevelFromDb(s.volumeDb)}</span>
	</div>
	${expanded ? `<div class="row slider-row">
	  <label>Treble</label>
	  <input id="tr_${z}" type="range" min="-10" max="10" value="${s.trebleDb}" onpointerdown="beginSliderEdit('treble', ${z})" onmousedown="beginSliderEdit('treble', ${z})" ontouchstart="beginSliderEdit('treble', ${z})" oninput="sliderPreview('treble', ${z}, this.value)" onchange="sliderSet('treble', ${z}, this.value)" />
	  <span class="slider-value" id="trv_${z}">${s.trebleDb}</span>
	</div>` : ''}
	${expanded ? `<div class="row slider-row">
	  <label>Bass</label>
	  <input id="ba_${z}" type="range" min="-10" max="10" value="${s.bassDb}" onpointerdown="beginSliderEdit('bass', ${z})" onmousedown="beginSliderEdit('bass', ${z})" ontouchstart="beginSliderEdit('bass', ${z})" oninput="sliderPreview('bass', ${z}, this.value)" onchange="sliderSet('bass', ${z}, this.value)" />
	  <span class="slider-value" id="bav_${z}">${s.bassDb}</span>
	</div>` : ''}
  </div>`;
}

function val(id){ return document.getElementById(id).value; }

function toggleCmd(type, zone, isOn) {
  cmd(`/api/cmd/${type}?zone=${zone}&on=${isOn ? 0 : 1}`);
}

function toggleZoneDetails(zone) {
	if (showAllDetails) {
	  return;
	}
	expandedZone = (expandedZone === zone) ? null : zone;
	render();
}

function handleZoneCardTap(event, zone) {
	if (showAllDetails) {
	  return;
	}
	if (event.target.closest('button, input, select, label')) {
	  return;
	}
	toggleZoneDetails(zone);
}

function setSpan(id, value) {
  const el = document.getElementById(id);
  if (el) el.textContent = value;
}

function lockKey(zone, type) {
  return `${zone}:${type}`;
}

function setEditLock(zone, type, ms = EDIT_LOCK_MS) {
  editLocks[lockKey(zone, type)] = Date.now() + ms;
}

function beginSliderEdit(type, zone) {
  setEditLock(zone, type, EDIT_LOCK_MS);
}

function isLocked(zone, type) {
  const until = editLocks[lockKey(zone, type)] || 0;
  return until > Date.now();
}

function ensureZoneState(zone) {
  if (!state[zone]) {
	state[zone] = {known:false,powerOn:false,muteOn:false,dndOn:false,input:0,volumeDb:-60,trebleDb:0,bassDb:0,balanceDb:0};
  }
}

function sliderPreview(type, zone, value) {
  ensureZoneState(zone);
  if (type === 'volume') {
	state[zone].volumeDb = Number(value) - 60;
	setSpan(`volv_${zone}`, value);
  } else if (type === 'treble') {
	state[zone].trebleDb = Number(value);
	setSpan(`trv_${zone}`, value);
  } else if (type === 'bass') {
	state[zone].bassDb = Number(value);
	setSpan(`bav_${zone}`, value);
  }
}

function sliderSet(type, zone, value) {
	setEditLock(zone, type, EDIT_LOCK_MS);
  sliderPreview(type, zone, value);
  render();

  if (type === 'volume') {
	cmd(`/api/cmd/volume?zone=${zone}&level=${value}`, false);
	return;
  }
	cmd(`/api/cmd/${type}?zone=${zone}&db=${value}`, false);
}

function inputSet(zone, value) {
  setEditLock(zone, 'input', EDIT_LOCK_MS);
  ensureZoneState(zone);
  state[zone].input = Number(value);
  render();
  cmd(`/api/cmd/input?zone=${zone}&input=${value}`, false);
}

function mergeZoneStatus(zone, incoming) {
  const current = state[zone] || {};
  const merged = { ...incoming };

	if (isLocked(zone, 'input')) merged.input = current.input;
  if (isLocked(zone, 'volume')) merged.volumeDb = current.volumeDb;
  if (isLocked(zone, 'treble')) merged.trebleDb = current.trebleDb;
  if (isLocked(zone, 'bass')) merged.bassDb = current.bassDb;

  state[zone] = merged;
}

function render() {
  let html = '';
	for (const z of visibleZones) html += zoneCard(z);
  zonesEl.innerHTML = html;
}

async function refreshStatus() {
  const r = await fetch('/api/status');
  const j = await r.json();
  if (j.zones) {
	for (const z of j.zones) mergeZoneStatus(z.zone, z);
	render();
	log('Status refreshed');
  }
}

async function refreshNames() {
  try {
	const r = await fetch('/api/names');
	const j = await r.json();
	if (j.zones) {
	  for (const z of j.zones) zoneNames[z.zone] = z;
	}
	if (j.sources) {
	  for (const s of j.sources) sourceNames[s.source] = s;
	}
	render();
  } catch (e) {
	log(`ERR names ${e}`);
  }
}

async function cmd(url, autoRefresh = true) {
  try {
	log(`TX ${url}`);
	const r = await fetch(url);
	const j = await r.json();
	if (j.txFrame) {
	  log(`TX BYTES ${j.txFrame}`);
	}
	if (j.txFollowUp) {
	  log(`TX BYTES FOLLOW-UP ${j.txFollowUp}`);
	}
	log(`${url} => ${JSON.stringify(j)}`);
	if (autoRefresh) {
	  setTimeout(refreshStatus, 250);
	}
  } catch (e) {
	log(`ERR ${e}`);
  }
}

function connectWs() {
  const ws = new WebSocket(`ws://${location.hostname}:81/`);
  ws.onopen = () => log('WebSocket connected');
  ws.onclose = () => { log('WebSocket disconnected; retrying'); setTimeout(connectWs, 1500); };
  ws.onmessage = (evt) => {
	try {
	  const msg = JSON.parse(evt.data);
	  if (msg.type === 'zone' && msg.data && msg.data.zone) {
		mergeZoneStatus(msg.data.zone, msg.data);
		render();
	  } else if (msg.type === 'info') {
		log(msg.message || 'info');
	  }
	} catch {
	  log(evt.data);
	}
  };
}

applyVisibilityConfig();
render();
refreshNames();
refreshStatus();
connectWs();
</script>
</body>
</html>
)LYNCPAGE";
