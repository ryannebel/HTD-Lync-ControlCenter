#pragma once

#include <Arduino.h>

static const char LYNC_ETHERNET_PAGE[] PROGMEM = R"LYNCETH(
<!doctype html>
<html>
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>Lync Ethernet Settings</title>
  <style>
	body { font-family: Arial, sans-serif; margin:clamp(10px, 3vw, 16px); background:#111; color:#eee; }
	a { color:#8ec5ff; }
	.row { display:flex; flex-wrap:wrap; align-items:center; gap:10px; margin:8px 0; }
	.form-row {
	  display:grid;
	  grid-template-columns:100px minmax(0, 1fr);
	  align-items:center;
	  gap:10px;
	}
	label { width:auto; }
	input[type=text],
	select {
	  width:100%;
	  min-width:0;
	  min-height:40px;
	  box-sizing:border-box;
	  padding:6px;
	  background:#171717;
	  color:#eee;
	  border:1px solid #444;
	  border-radius:6px;
	}
	button { padding:7px 12px; min-height:40px; }
	.card { border:1px solid #333; border-radius:10px; padding:12px; background:#1c1c1c; max-width:720px; }
	.action-row button { flex:1 1 220px; }
	#status { margin-top:10px; font-family:monospace; white-space:pre-wrap; }
	@media (max-width: 640px) {
	  .form-row { grid-template-columns:1fr; gap:6px; }
	  .action-row button { flex:1 1 100%; }
	}
  </style>
</head>
<body>
  <h1>Ethernet Settings</h1>
  <div><a href="/">Back to Control Center</a></div>
  <div class="card">
	<div class="row"><strong>Active interface:</strong><span id="active">-</span></div>
	<div class="row"><strong>Device IP:</strong><span id="deviceIp">-</span></div>
	<div class="row form-row">
	  <label for="dhcp">Mode</label>
	  <select id="dhcp" onchange="updateMode()">
		<option value="1">DHCP</option>
		<option value="0">Static</option>
	  </select>
	</div>
	<div class="row form-row static-row"><label for="ip">IP</label><input id="ip" type="text" /></div>
	<div class="row form-row static-row"><label for="gateway">Gateway</label><input id="gateway" type="text" /></div>
	<div class="row form-row static-row"><label for="subnet">Subnet</label><input id="subnet" type="text" /></div>
	<div class="row form-row static-row"><label for="dns1">DNS 1</label><input id="dns1" type="text" /></div>
	<div class="row form-row static-row"><label for="dns2">DNS 2</label><input id="dns2" type="text" /></div>
	<div class="row action-row">
	  <button onclick="save()">Save & Restart</button>
	  <button onclick="clearWifiCreds()">Clear Saved Wi-Fi & Restart</button>
	</div>
  </div>
  <div id="status"></div>
<script>
const statusEl = document.getElementById('status');

function note(msg) {
  const t = new Date().toLocaleTimeString();
  statusEl.textContent = `[${t}] ${msg}\n` + statusEl.textContent;
}

function setValue(id, value) {
  const el = document.getElementById(id);
  if (el) el.value = value || '';
}

function updateMode() {
  const staticRows = document.querySelectorAll('.static-row');
  const dhcp = document.getElementById('dhcp').value === '1';
  for (const row of staticRows) {
	row.hidden = dhcp;
  }
}

async function load() {
  const res = await fetch('/api/network/ethernet');
  const json = await res.json();
  document.getElementById('active').textContent = json.active || '-';
  document.getElementById('deviceIp').textContent = json.deviceIp || '-';
  setValue('dhcp', json.dhcp ? '1' : '0');
  setValue('ip', json.ip);
  setValue('gateway', json.gateway);
  setValue('subnet', json.subnet);
  setValue('dns1', json.dns1);
  setValue('dns2', json.dns2);
  updateMode();
}

async function save() {
  const params = new URLSearchParams();
  const dhcp = document.getElementById('dhcp').value;
  params.set('dhcp', dhcp);

  if (dhcp === '0') {
	params.set('ip', document.getElementById('ip').value.trim());
	params.set('gateway', document.getElementById('gateway').value.trim());
	params.set('subnet', document.getElementById('subnet').value.trim());
	params.set('dns1', document.getElementById('dns1').value.trim());
	params.set('dns2', document.getElementById('dns2').value.trim());
  }

  note('Saving settings... device will restart');
  try {
	const res = await fetch(`/api/network/ethernet?${params.toString()}`);
	const json = await res.json();
	note(json.message || (json.ok ? 'saved' : 'failed'));
  } catch (e) {
	note(`Save error: ${e}`);
  }
}

async function clearWifiCreds() {
  if (!confirm('Clear saved Wi-Fi credentials and restart device?')) {
	return;
  }

  note('Clearing saved Wi-Fi credentials... device will restart');
  try {
	const res = await fetch('/api/network/wifi/clear');
	const json = await res.json();
	note(json.message || (json.ok ? 'cleared' : 'failed'));
  } catch (e) {
	note(`Clear error: ${e}`);
  }
}

load();
</script>
</body>
</html>
)LYNCETH";
