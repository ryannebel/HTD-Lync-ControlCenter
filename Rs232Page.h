#pragma once

#include <Arduino.h>

static const char LYNC_RS232_PAGE[] PROGMEM = R"RS232PAGE(
<!doctype html>
<html>
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>Lync RS232 Test</title>
  <style>
	body { font-family: Arial, sans-serif; margin: clamp(10px, 3vw, 16px); background:#111; color:#eee; }
	h1 { margin-top: 0; }
	.row { display:flex; gap:8px; align-items:center; margin:8px 0; flex-wrap:wrap; }
	.command-row input[type=text] { flex:1 1 360px; width:100%; min-width:0; }
	input[type=text] { width:min(900px, 95vw); min-height:40px; padding:8px; background:#171717; color:#eee; border:1px solid #444; border-radius:6px; }
	button { padding:7px 11px; min-height:40px; }
	#out { margin-top:10px; border:1px solid #333; border-radius:8px; background:#090909; padding:10px; min-height:240px; white-space:pre-wrap; font-family:Consolas, monospace; }
	.hint { color:#bbb; font-size:12px; }
	@media (max-width: 640px) {
	  .command-row button { flex:1 1 calc(50% - 8px); }
	  #out { min-height:180px; }
	}
  </style>
</head>
<body>
  <h1>RS232 Test</h1>
  <div class="row">
	<button onclick="location.href='/'">Back</button>
	<label><input id="paused" type="checkbox" onchange="setPaused(this.checked)" /> Pause normal RS232 communication</label>
  </div>

	<div class="row command-row">
	<input id="cmd" type="text" placeholder="Enter hex bytes, e.g. 02 00 01 15 C4 DC" />
	<button onclick="sendRaw()">Send Raw</button>
	<button onclick="clearOut()">Clear Output</button>
  </div>
  <div class="hint">Use spaces/commas/newlines. 0x prefix is allowed.</div>

  <div id="out"></div>

<script>
const out = document.getElementById('out');
const paused = document.getElementById('paused');

function append(line) {
  const ts = new Date().toLocaleTimeString();
  out.textContent = `[${ts}] ${line}\n` + out.textContent;
}

async function getState() {
  const r = await fetch('/api/rs232/state');
  const j = await r.json();
  paused.checked = !!j.paused;
}

async function setPaused(v) {
  const r = await fetch(`/api/rs232/state?paused=${v ? 1 : 0}`);
  const j = await r.json();
  append(`pause=${j.paused}`);
}

async function sendRaw() {
  const bytes = document.getElementById('cmd').value.trim();
  if (!bytes) {
	append('No command bytes provided');
	return;
  }

  append(`SEND ${bytes}`);
  const r = await fetch(`/api/rs232/send?bytes=${encodeURIComponent(bytes)}`);
  const j = await r.json();
  append(JSON.stringify(j));
  await refreshLog();
}

async function refreshLog() {
  const r = await fetch('/api/rs232/log');
  const j = await r.json();
  if (j.log !== undefined) {
	out.textContent = j.log;
  }
  paused.checked = !!j.paused;
}

async function clearOut() {
  await fetch('/api/rs232/log/clear');
  await refreshLog();
}

setInterval(refreshLog, 1000);
getState();
refreshLog();
</script>
</body>
</html>
)RS232PAGE";
