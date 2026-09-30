#pragma once

#include <Arduino.h>

static const char LYNC_NAMES_PAGE[] PROGMEM = R"LYNCNAMES(
<!doctype html>
<html>
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>Lync Name Manager</title>
  <style>
	body { font-family: Arial, sans-serif; margin:clamp(10px, 3vw, 16px); background:#111; color:#eee; }
	a { color:#8ec5ff; }
	h1, h2 { margin-bottom:8px; }
	.table-wrap { width:100%; overflow-x:auto; margin-bottom:18px; }
	table { width:100%; border-collapse:collapse; min-width:520px; }
	th, td { border:1px solid #333; padding:6px; text-align:left; }
	input[type=text] { width:100%; box-sizing:border-box; min-height:40px; padding:6px; background:#171717; color:#eee; border:1px solid #444; border-radius:6px; }
	input[type=checkbox] { transform: scale(1.1); }
	button { padding:6px 10px; min-height:40px; }
	.status { margin-top:8px; font-family:monospace; white-space:pre-wrap; }
	.mask-table td { vertical-align: top; }
	.mask-grid { display:grid; grid-template-columns: repeat(6, minmax(0, 1fr)); gap:8px 10px; }
	.mask-item { display:flex; gap:4px; align-items:center; font-size:12px; }
	@media (max-width: 700px) {
	  table { min-width:460px; }
	  .mask-grid { grid-template-columns: repeat(3, minmax(0, 1fr)); }
	}
	@media (max-width: 480px) {
	  .mask-grid { grid-template-columns: repeat(2, minmax(0, 1fr)); }
	}
  </style>
</head>
<body>
  <h1>Lync Name Manager</h1>
  <div><a href="/">Back to Control Center</a></div>

	<h2>Zone Names</h2>
	<div class="table-wrap">
  <table>
	<thead><tr><th>Zone</th><th>Full Name</th><th>Short Name (max 10)</th></tr></thead>
	<tbody id="zoneRows"></tbody>
  </table>
	</div>

  <h2>Source Names (Global 1..18)</h2>
	<div class="table-wrap">
  <table>
	<thead><tr><th>Source</th><th>Full Name</th><th>Short Name (max 10)</th></tr></thead>
	<tbody id="sourceRows"></tbody>
  </table>
  </div>

  <h2>Source Visibility by Zone</h2>
	<div class="table-wrap">
  <table class="mask-table">
	<thead><tr><th>Zone</th><th>Visible Sources</th></tr></thead>
	<tbody id="zoneSourceMaskRows"></tbody>
  </table>
	</div>

  <div id="status" class="status"></div>

<script>
const statusEl = document.getElementById('status');
const sourceNameById = {};

function note(msg) {
  const t = new Date().toLocaleTimeString();
  statusEl.textContent = `[${t}] ${msg}\n` + statusEl.textContent;
}

function esc(v) {
  return String(v || '').replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}

function zoneRow(item) {
  return `<tr>
	<td>${item.zone}</td>
	<td><input id="zf_${item.zone}" type="text" value="${esc(item.full)}" oninput="queueSaveZone(${item.zone})" /></td>
	<td><input id="zs_${item.zone}" type="text" maxlength="10" value="${esc(item.short)}" oninput="queueSaveZone(${item.zone})" /></td>
  </tr>`;
}

function sourceRow(item) {
  return `<tr>
	<td>${item.source}</td>
	<td><input id="sf_${item.source}" type="text" value="${esc(item.full)}" oninput="queueSaveSource(${item.source})" /></td>
	<td><input id="ss_${item.source}" type="text" maxlength="10" value="${esc(item.short)}" oninput="queueSaveSource(${item.source})" /></td>
  </tr>`;
}

const ALL_SOURCES_MASK = (1 << 18) - 1;

function sourceVisible(mask, source) {
  const normalized = Number.isInteger(Number(mask)) ? Number(mask) : ALL_SOURCES_MASK;
  return ((normalized >>> (source - 1)) & 1) === 1;
}

function zoneSourceMaskRow(item) {
  const mask = Number(item.sourceMask);
  let checks = '';
  for (let source = 1; source <= 18; source++) {
	const sourceName = (sourceNameById[source] && sourceNameById[source].full)
	  ? sourceNameById[source].full
	  : `Source ${source}`;
	checks += `<label class="mask-item"><input id="zm_${item.zone}_${source}" type="checkbox" ${sourceVisible(mask, source) ? 'checked' : ''} onchange="queueSaveZoneSourceMask(${item.zone})" />${esc(sourceName)}</label>`;
  }
  return `<tr><td>${item.zone}</td><td><div class="mask-grid">${checks}</div></td></tr>`;
}

const zoneSaveTimers = {};
const sourceSaveTimers = {};
const zoneMaskSaveTimers = {};
const AUTO_SAVE_DELAY_MS = 500;

function queueSaveZone(zone) {
  clearTimeout(zoneSaveTimers[zone]);
  zoneSaveTimers[zone] = setTimeout(() => saveZone(zone), AUTO_SAVE_DELAY_MS);
}

function queueSaveSource(source) {
  clearTimeout(sourceSaveTimers[source]);
  sourceSaveTimers[source] = setTimeout(() => saveSource(source), AUTO_SAVE_DELAY_MS);
}

function queueSaveZoneSourceMask(zone) {
  clearTimeout(zoneMaskSaveTimers[zone]);
  zoneMaskSaveTimers[zone] = setTimeout(() => saveZoneSourceMask(zone), AUTO_SAVE_DELAY_MS);
}

async function loadNames() {
  const res = await fetch('/api/names');
  const json = await res.json();
	for (const source of (json.sources || [])) {
	sourceNameById[source.source] = source;
  }
  document.getElementById('zoneRows').innerHTML = (json.zones || []).map(zoneRow).join('');
  document.getElementById('sourceRows').innerHTML = (json.sources || []).map(sourceRow).join('');
	document.getElementById('zoneSourceMaskRows').innerHTML = (json.zones || []).map(zoneSourceMaskRow).join('');
  note('Loaded names');
}

async function saveZone(zone) {
  const full = document.getElementById(`zf_${zone}`).value;
  const short = document.getElementById(`zs_${zone}`).value.substring(0, 10);
  document.getElementById(`zs_${zone}`).value = short;

  const url = `/api/names/zone?zone=${zone}&full=${encodeURIComponent(full)}&short=${encodeURIComponent(short)}`;
  const res = await fetch(url);
  const json = await res.json();
  note(`Zone ${zone}: ${json.message || (json.ok ? 'ok' : 'failed')}`);
}

async function saveSource(source) {
  const full = document.getElementById(`sf_${source}`).value;
  const short = document.getElementById(`ss_${source}`).value.substring(0, 10);
  document.getElementById(`ss_${source}`).value = short;

  const url = `/api/names/source?source=${source}&full=${encodeURIComponent(full)}&short=${encodeURIComponent(short)}`;
  const res = await fetch(url);
  const json = await res.json();
  note(`Source ${source}: ${json.message || (json.ok ? 'ok' : 'failed')}`);
}

async function saveZoneSourceMask(zone) {
  let mask = 0;
  for (let source = 1; source <= 18; source++) {
	const checkbox = document.getElementById(`zm_${zone}_${source}`);
	if (checkbox && checkbox.checked) {
	  mask |= (1 << (source - 1));
	}
  }

  const url = `/api/names/zone/sources?zone=${zone}&mask=${mask}`;
  const res = await fetch(url);
  const json = await res.json();
  note(`Zone ${zone} sources: ${json.message || (json.ok ? 'ok' : 'failed')}`);
}

loadNames();
</script>
</body>
</html>
)LYNCNAMES";
