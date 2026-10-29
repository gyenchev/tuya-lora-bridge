#include "web_ui.h"

#include <WebServer.h>
#include <WiFi.h>
#include <Update.h>

#include <esp_system.h>
#include <string.h>

#include "boot_guard.h"
#include "config.h"
#include "config_store.h"
#include "tuya_client.h"

// Defined in remote-node.ino (global namespace).
extern TuyaClient tuya;
extern bool deviceFound;
extern bool remoteLoraConfigBusy();
extern String remoteLoraConfigStatusJson();
extern IPAddress deviceIP;
extern bool everConnected;

namespace tuya_lora {

namespace {

WebServer *server = nullptr;

bool setupApActive = false;
uint32_t rebootAtMs = 0;

const char OTA_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Tuya LoRa Bridge — Firmware update</title>
<style>
  :root { color-scheme: light dark; }
  * { box-sizing: border-box; }
  body { margin: 0; padding: 24px 16px; font: 16px/1.45 system-ui, -apple-system, Segoe UI, sans-serif; }
  main { max-width: 680px; margin: 0 auto; }
  h1 { font-size: 1.45rem; margin: 0 0 4px; }
  .muted { opacity: .7; font-size: .9rem; }
  .nav { display: flex; flex-wrap: wrap; gap: 8px; margin-bottom: 18px; }
  .nav a { padding: 8px 12px; border-radius: 8px; text-decoration: none; color: inherit; background: #8882; font-weight: 600; }
  .nav a.active { background: #2563eb; color: #fff; }
  .warn { margin: 16px 0; padding: 12px 14px; border: 1px solid #d90; border-radius: 10px; background: #d901; }
  .card { margin: 16px 0; padding: 16px; border: 1px solid #8884; border-radius: 10px; }
  label { display: block; margin: 0 0 8px; font-weight: 600; }
  input[type=file] { width: 100%; padding: 10px; border: 1px solid #8886; border-radius: 8px; background: transparent; color: inherit; }
  button { margin-top: 12px; padding: 10px 16px; border: 0; border-radius: 8px; background: #2563eb; color: #fff; font-weight: 650; cursor: pointer; }
  button:disabled { opacity: .5; cursor: wait; }
  progress { width: 100%; height: 14px; margin-top: 12px; }
  pre { white-space: pre-wrap; word-break: break-word; background: #8881; padding: 12px; border-radius: 8px; min-height: 48px; }
</style>
</head>
<body>
<main>
  <nav class="nav">{{NAV}}</nav>
  <h1>Firmware update — {{TITLE}}</h1>
  <p class="muted">Current firmware {{FW_VERSION}} · board {{BOARD}}</p>

  <div class="warn">
    Upload the <strong>application image</strong> <code>{{FILENAME}}</code>, not the
    full flash image. Do not power off during the update.
  </div>

  <div class="card">
    <form id="upload">
      <label for="file">Firmware .bin file</label>
      <input id="file" type="file" accept=".bin,application/octet-stream" required>
      <button id="send" type="submit">Upload and reboot</button>
    </form>
    <progress id="progress" max="100" value="0"></progress>
    <pre id="log">Ready.</pre>
  </div>
</main>
<script>
(() => {
  const form = document.getElementById('upload');
  const input = document.getElementById('file');
  const button = document.getElementById('send');
  const progress = document.getElementById('progress');
  const log = document.getElementById('log');

  form.addEventListener('submit', (event) => {
    event.preventDefault();
    if (!input.files || input.files.length === 0) {
      log.textContent = 'Choose a .bin file first.';
      return;
    }
    const file = input.files[0];
    const xhr = new XMLHttpRequest();
    xhr.open('POST', '/api/ota?board={{BOARD}}');
    xhr.setRequestHeader('Content-Type', 'application/octet-stream');
    xhr.upload.onprogress = (event) => {
      if (!event.lengthComputable) return;
      const percent = Math.round((event.loaded * 100) / event.total);
      progress.value = percent;
      log.textContent = `Uploading ${percent}% (${event.loaded} / ${event.total} bytes)`;
    };
    xhr.onload = () => {
      progress.value = 100;
      log.textContent = xhr.responseText || ('HTTP ' + xhr.status);
      button.disabled = false;
    };
    xhr.onerror = () => {
      log.textContent = 'Upload failed. Check the connection and try again.';
      button.disabled = false;
    };
    button.disabled = true;
    log.textContent = 'Starting upload...';
    xhr.send(file);
  });
})();
</script>
</body>
</html>)HTML";

const char DEVICE_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Tuya LoRa Bridge — Remote settings</title>
<style>
  :root { color-scheme: light dark; }
  * { box-sizing: border-box; }
  body { margin: 0; padding: 24px 16px; font: 16px/1.45 system-ui, -apple-system, Segoe UI, sans-serif; }
  main { max-width: 680px; margin: 0 auto; }
  h1 { font-size: 1.45rem; margin: 0 0 4px; }
  h2 { font-size: 1.05rem; margin: 20px 0 8px; }
  .muted { opacity: .7; font-size: .9rem; }
  .nav { display: flex; gap: 8px; margin-bottom: 18px; }
  .nav a { padding: 8px 12px; border-radius: 8px; text-decoration: none; color: inherit; background: #8882; font-weight: 600; }
  .nav a.active { background: #2563eb; color: #fff; }
  .status { margin: 16px 0; padding: 12px 14px; border: 1px solid #8884; border-radius: 10px; }
  .status div + div { margin-top: 4px; }
  .err { margin: 16px 0; padding: 12px 14px; border: 1px solid #d33; border-radius: 10px; background: #d33118; color: #fff; }
  .warn { margin: 16px 0; padding: 12px 14px; border: 1px solid #d90; border-radius: 10px; background: #d901; }
  .tabs { display: flex; flex-wrap: wrap; gap: 4px; margin: 18px 0 0; border-bottom: 1px solid #8884; }
  .tab { background: transparent; color: inherit; border: 0; border-bottom: 3px solid transparent; border-radius: 0; padding: 10px 12px; font-weight: 600; cursor: pointer; }
  .tab.active { border-bottom-color: #2563eb; color: #2563eb; }
  .tab-panel { display: none; padding-top: 8px; }
  .tab-panel.active { display: block; }
  label { display: block; margin: 14px 0 6px; font-weight: 600; }
  input, select, button { font: inherit; }
  input[type=text], input[type=password], input[type=number], select {
    width: 100%; padding: 10px 12px; border: 1px solid #8886; border-radius: 8px; background: transparent; color: inherit;
  }
  .actions { display: flex; flex-wrap: wrap; gap: 10px; margin-top: 24px; }
  button { padding: 10px 16px; border: 0; border-radius: 8px; background: #2563eb; color: #fff; font-weight: 650; cursor: pointer; }
  button.danger { background: #b91c1c; }
  .hint { font-size: .85rem; opacity: .7; margin: 6px 0 0; }
  .def { font-weight: 400; opacity: .6; font-size: .8rem; }
  hr { border: 0; border-top: 1px solid #8884; margin: 32px 0; }
</style>
</head>
<body data-active-tab="{{ACTIVE_TAB}}">
<main>
  <nav class="nav"><a class="active" href="/settings">Settings</a><a href="/">Status</a><a href="/ota">Firmware</a></nav>
  <h1>Tuya LoRa Bridge — Remote</h1>
  <p class="muted">Firmware {{FW_VERSION}} · {{IP}}</p>

  {{ERROR}}
  <div class="status">
    <div><strong>AP:</strong> {{AP_STATUS}}</div>
    <div><strong>Device:</strong> {{DEVICE_STATUS}}</div>
    <div><strong>Tuya:</strong> {{TUYA_STATUS}}</div>
    <div><strong>LoRa PSK:</strong> {{PSK_STATUS}}</div>
  </div>

  {{PSK_WARNING}}

  <div class="tabs" role="tablist">
    <button type="button" class="tab active" data-tab="ap" role="tab">Access point</button>
    <button type="button" class="tab" data-tab="tuya" role="tab">Tuya</button>
    <button type="button" class="tab" data-tab="lora" role="tab">LoRa</button>
    <button type="button" class="tab" data-tab="discovery" role="tab">Discovery</button>
    <button type="button" class="tab" data-tab="display" role="tab">Display</button>
    <button type="button" class="tab" data-tab="system" role="tab">System</button>
  </div>

  <form method="POST" action="/save" id="settings-form">
    <section class="tab-panel active" id="tab-ap" data-tab-panel="ap" role="tabpanel">
      <h2>Device access point</h2>
      <div class="warn">These must match the dummy hotspot the device was paired with. Changing the SSID or password disconnects the device until it is re-paired.</div>
      <p class="hint">If no AP credentials are configured, the node starts the configured setup AP (default <code>TuyaLoraRemote-Setup</code> at <code>192.168.4.1</code>, password <code>tuya-lora-setup</code>).</p>
      <label for="ap_ssid">AP SSID <span class="def">required</span></label>
      <input id="ap_ssid" name="ap_ssid" type="text" maxlength="32" required value="{{AP_SSID}}" autocomplete="off">
      <p class="hint">SSID of the dummy hotspot the device was paired with.</p>
      <label for="ap_password">AP password <span class="def">8-63 characters</span></label>
      <input id="ap_password" name="ap_password" type="password" maxlength="63" autocomplete="new-password" placeholder="{{AP_PASSWORD_PLACEHOLDER}}">
      <p class="hint">Password of that hotspot. Must be 8-63 characters.</p>
      <label for="ap_channel">AP channel <span class="def">default 6</span></label>
      <select id="ap_channel" name="ap_channel">{{AP_CHANNEL_OPTIONS}}</select>
      <p class="hint">Channel the device expects. Changing it may require re-pairing.</p>
      {{FIELDS_AP}}
    </section>

    <section class="tab-panel" id="tab-tuya" data-tab-panel="tuya" role="tabpanel">
      <h2>Tuya device</h2>
      <label for="tuya_dev_id">Device ID <span class="def">required</span></label>
      <input id="tuya_dev_id" name="tuya_dev_id" type="text" maxlength="63" required value="{{TUYA_DEVICE_ID}}" autocomplete="off">
      <p class="hint">Device ID from tinytuya scan or the Tuya app.</p>
      <label for="tuya_key">Local key <span class="def">required, 16 characters</span></label>
      <input id="tuya_key" name="tuya_key" type="password" maxlength="16" autocomplete="new-password" placeholder="{{TUYA_KEY_PLACEHOLDER}}">
      <p class="hint">16-character local key for the device.</p>
      <label for="tuya_version">Protocol version <span class="def">default Auto-detect</span></label>
      <select id="tuya_version" name="tuya_version">{{TUYA_VERSION_OPTIONS}}</select>
      <p class="hint">Auto-detect works for most devices. Set a version if detection fails.</p>
      <label for="tuya_ip">Static device IP <span class="def">optional</span></label>
      <input id="tuya_ip" name="tuya_ip" type="text" maxlength="15" value="{{TUYA_STATIC_IP}}" placeholder="leave empty to discover">
      <p class="hint">A static IP skips the AP client scan. Use an address on the AP subnet, for example <code>192.168.4.50</code>. Leave empty unless the device always gets the same address.</p>
      {{FIELDS_TUYA}}
    </section>

    <section class="tab-panel" id="tab-lora" data-tab-panel="lora" role="tabpanel">
      <h2>LoRa link</h2>
      <p class="hint">The base normally coordinates LoRa changes across both nodes. Editing these fields locally can break the link; use the base LoRa tab for normal changes.</p>
      <label for="lora_psk">Pre-shared key <span class="def">required</span></label>
      <input id="lora_psk" name="lora_psk" type="password" maxlength="32" pattern="[0-9A-Fa-f]{32}" autocomplete="new-password" placeholder="{{PSK_PLACEHOLDER}}">
      <button type="button" id="generate-psk">Generate new PSK</button>
      <p class="hint">32 hex characters. Must match the base. A new key generated here is only local until the base is also updated.</p>
      {{FIELDS_LORA}}
      <div class="status" id="lora-config-status">LoRa config: idle</div>
    </section>

    <section class="tab-panel" id="tab-discovery" data-tab-panel="discovery" role="tabpanel">
      <h2>Device discovery</h2>
      {{FIELDS_DISCOVERY}}
    </section>

    <section class="tab-panel" id="tab-display" data-tab-panel="display" role="tabpanel">
      <h2>Display</h2>
      {{FIELDS_DISPLAY}}
    </section>

    <section class="tab-panel" id="tab-system" data-tab-panel="system" role="tabpanel">
      <h2>Setup access point</h2>
      <label for="setup_ap_ssid">Setup AP SSID <span class="def">default TuyaLoraRemote-Setup</span></label>
      <input id="setup_ap_ssid" name="setup_ap_ssid" type="text" maxlength="32" required value="{{SETUP_AP_SSID}}" autocomplete="off">
      <p class="hint">Name of the fallback AP used when the device AP is not configured.</p>
      <label for="setup_ap_password">Setup AP password <span class="def">default tuya-lora-setup</span></label>
      <input id="setup_ap_password" name="setup_ap_password" type="password" maxlength="63" autocomplete="new-password" placeholder="{{SETUP_AP_PASSWORD_PLACEHOLDER}}">
      <p class="hint">At least 8 characters. Used only for the setup AP.</p>
      <label for="setup_ap_channel">Setup AP channel <span class="def">default 6</span></label>
      <select id="setup_ap_channel" name="setup_ap_channel">{{SETUP_AP_CHANNEL_OPTIONS}}</select>
      <p class="hint">WiFi channel for the setup AP. 1, 6, and 11 avoid overlap.</p>
      <h2>Web UI and SD update</h2>
      {{FIELDS_SYSTEM}}
    </section>

    <div class="actions">
      <button type="submit">Save and reboot</button>
    </div>
  </form>

  <hr>
  <form method="POST" action="/factory" onsubmit="return confirm('Clear all configuration and reboot into setup mode?')">
    <button class="danger" type="submit">Factory reset</button>
  </form>
</main>
<script>
const form = document.getElementById('settings-form');
const tabButtons = Array.from(document.querySelectorAll('.tab'));
const panels = Array.from(document.querySelectorAll('.tab-panel'));
const validTabs = tabButtons.map(tab => tab.dataset.tab);

function activateTab(name) {
  if (!validTabs.includes(name)) name = 'ap';
  tabButtons.forEach(tab => tab.classList.toggle('active', tab.dataset.tab === name));
  panels.forEach(panel => panel.classList.toggle('active', panel.dataset.tabPanel === name));
  if (history.replaceState) history.replaceState(null, '', '#' + name);
}

tabButtons.forEach(tab => tab.addEventListener('click', () => activateTab(tab.dataset.tab)));
const hash = location.hash ? location.hash.slice(1) : '';
activateTab(hash || document.body.dataset.activeTab || 'ap');

if (form) {
  form.addEventListener('invalid', event => {
    const panel = event.target.closest('.tab-panel');
    if (panel) activateTab(panel.dataset.tabPanel);
  }, true);
}

const generatePsk = document.getElementById('generate-psk');
if (generatePsk) {
  generatePsk.addEventListener('click', async () => {
    try {
      const response = await fetch('/api/lora/psk', {method: 'POST', cache: 'no-store'});
      if (!response.ok) throw new Error('HTTP ' + response.status);
      const data = await response.json();
      document.getElementById('lora_psk').value = data.psk;
      generatePsk.textContent = 'Generated - save locally';
    } catch (error) {
      generatePsk.textContent = 'Generation failed';
    }
  });
}

async function refreshLoraConfig() {
  const box = document.getElementById('lora-config-status');
  if (!box) return;
  try {
    const response = await fetch('/api/status', {cache: 'no-store'});
    if (!response.ok) return;
    const data = await response.json();
    const cfg = data.lora_config;
    if (!cfg) { box.textContent = 'LoRa config: unavailable'; return; }
    let text = 'LoRa config: ' + cfg.state;
    if (cfg.error) text += ' - ' + cfg.error;
    if (cfg.remaining_ms > 0) text += ' - ' + Math.ceil(cfg.remaining_ms / 1000) + ' s remaining';
    if (cfg.last_contact_age_ms >= 0) text += ' - last contact ' + Math.ceil(cfg.last_contact_age_ms / 1000) + ' s ago';
    box.textContent = text;
  } catch (error) {
    box.textContent = 'LoRa config: status unavailable';
  }
}
setInterval(refreshLoraConfig, 2000);
refreshLoraConfig();
</script>
</body>
</html>)HTML";

const char STATUS_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Tuya LoRa Bridge — Remote status</title>
<style>
  :root { color-scheme: light dark; }
  * { box-sizing: border-box; }
  body { margin: 0; padding: 24px 16px; font: 16px/1.45 system-ui, -apple-system, Segoe UI, sans-serif; }
  main { max-width: 680px; margin: 0 auto; }
  h1 { font-size: 1.45rem; margin: 0 0 4px; }
  h2 { font-size: 1.05rem; margin: 24px 0 8px; }
  .muted { opacity: .7; font-size: .9rem; }
  .nav { display: flex; gap: 8px; margin-bottom: 18px; }
  .nav a { padding: 8px 12px; border-radius: 8px; text-decoration: none; color: inherit; background: #8882; font-weight: 600; }
  .nav a.active { background: #2563eb; color: #fff; }
  .status { margin: 16px 0; padding: 12px 14px; border: 1px solid #8884; border-radius: 10px; }
  .status div + div { margin-top: 4px; }
  .err { margin: 16px 0; padding: 12px 14px; border: 1px solid #d33; border-radius: 10px; background: #d33118; color: #fff; }
  .cards { display: grid; grid-template-columns: repeat(auto-fit, minmax(150px, 1fr)); gap: 10px; margin: 16px 0; }
  .card { border: 1px solid #8884; border-radius: 10px; padding: 12px; }
  .card .label { display: block; font-size: .8rem; opacity: .7; }
  .card strong { font-size: 1.15rem; }
  .telemetry { width: 100%; border-collapse: collapse; }
  .telemetry th, .telemetry td { text-align: left; padding: 8px 6px; border-bottom: 1px solid #8883; }
  .telemetry th { font-weight: 500; opacity: .8; }
  .telemetry td:first-child { width: 45%; }
  pre { white-space: pre-wrap; word-break: break-word; background: #8881; padding: 12px; border-radius: 8px; max-height: 320px; overflow: auto; }
</style>
</head>
<body>
<main>
  <nav class="nav"><a href="/settings">Settings</a><a class="active" href="/">Status</a><a href="/ota">Firmware</a></nav>
  <h1>Tuya LoRa Bridge — Remote status</h1>
  <p class="muted">Firmware {{FW_VERSION}} · <span id="updated">loading...</span></p>

  <div class="status">
    <div><strong>AP:</strong> <span id="ap">--</span></div>
    <div><strong>Device:</strong> <span id="device">--</span></div>
    <div><strong>Tuya:</strong> <span id="tuya">--</span></div>
    <div><strong>LoRa PSK:</strong> <span id="psk">--</span></div>
  </div>

  <div id="error-box" class="err" hidden></div>

  <div class="cards">
    <div class="card"><span class="label">Device</span><strong id="card-device">--</strong></div>
    <div class="card"><span class="label">AP clients</span><strong id="card-clients">--</strong></div>
    <div class="card"><span class="label">Tuya</span><strong id="card-tuya">--</strong></div>
    <div class="card"><span class="label">Protocol</span><strong id="card-version">--</strong></div>
    <div class="card"><span class="label">Data points</span><strong id="card-dps">--</strong></div>
    <div class="card"><span class="label">Last response</span><strong id="card-last">--</strong></div>
    <div class="card"><span class="label">Queries</span><strong id="card-queries">--</strong></div>
  </div>

  <h2>Data points</h2>
  <table class="telemetry">
    <thead><tr><th>DP</th><th>Value</th></tr></thead>
    <tbody id="dp-rows"><tr><td colspan="2">--</td></tr></tbody>
  </table>

  <h2>Raw Tuya JSON</h2>
  <details><summary>Show raw JSON</summary><pre id="last-json">--</pre></details>
</main>
<script>
const $ = id => document.getElementById(id);
function set(id, text) { const el = $(id); if (el) el.textContent = text; }
function ago(seconds) {
  if (seconds === null || seconds === undefined || seconds < 0) return 'never';
  if (seconds < 60) return Math.floor(seconds) + 's ago';
  if (seconds < 3600) return Math.floor(seconds / 60) + 'm ago';
  return Math.floor(seconds / 3600) + 'h ago';
}
function tuyaText(d) {
  if (!d.tuya_configured) return 'not configured';
  if (d.tuya_connected) return 'connected, v' + d.tuya_version + ', ' + d.dp_count + ' DPs';
  return 'configured, waiting for device';
}
async function refresh() {
  try {
    const response = await fetch('/api/status', {cache: 'no-store'});
    if (!response.ok) throw new Error('HTTP ' + response.status);
    const d = await response.json();
    set('updated', 'updated ' + new Date().toLocaleTimeString());
    set('ap', d.ap_ssid + ' at ' + d.ap_ip + ' (' + d.ap_clients + ' clients)' + (d.setup_ap ? ' - setup mode' : ''));
    set('device', d.device_found ? 'found at ' + d.device_ip : 'searching on the AP');
    set('tuya', tuyaText(d));
    set('psk', d.psk_default_zero ? 'default zero (insecure)' : (d.psk_set ? 'set' : 'not set'));
    set('card-device', d.device_found ? 'online' : 'not found');
    set('card-clients', d.ap_clients);
    set('card-tuya', d.tuya_connected ? 'connected' : (d.tuya_configured ? 'waiting' : 'not configured'));
    set('card-version', d.tuya_version || '--');
    set('card-dps', d.dp_count);
    set('card-last', ago(d.last_rx_age_s));
    set('card-queries', d.query_count);
    const errorBox = $('error-box');
    if (d.last_error) {
      errorBox.textContent = d.last_error;
      errorBox.hidden = false;
    } else {
      errorBox.hidden = true;
    }
    const rows = $('dp-rows');
    rows.innerHTML = '';
    if (!d.dps || d.dps.length === 0) {
      const tr = document.createElement('tr');
      const td = document.createElement('td');
      td.colSpan = 2;
      td.textContent = 'No data points yet';
      tr.appendChild(td);
      rows.appendChild(tr);
    } else {
      for (const dp of d.dps) {
        const tr = document.createElement('tr');
        const id = document.createElement('td');
        id.textContent = dp.id;
        const value = document.createElement('td');
        value.textContent = dp.value;
        tr.appendChild(id);
        tr.appendChild(value);
        rows.appendChild(tr);
      }
    }
    set('last-json', d.last_json || '(none)');
  } catch (error) {
    set('updated', 'connection lost');
  }
}
refresh();
setInterval(refresh, 2000);
</script>
</body>
</html>)HTML";

const char RESULT_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Tuya LoRa Bridge — Remote</title>
<style>
  :root { color-scheme: light dark; }
  body { margin: 0; padding: 40px 16px; font: 16px/1.5 system-ui, -apple-system, Segoe UI, sans-serif; }
  main { max-width: 520px; margin: 0 auto; }
  h1 { font-size: 1.4rem; }
  p { opacity: .8; }
  a { color: inherit; }
</style>
</head>
<body><main><h1>{{TITLE}}</h1><p>{{DETAIL}}</p><p><a href="/settings">Settings</a> · <a href="/">Status</a></p></main></body>
</html>)HTML";

String htmlEscape(const char *text) {
  String out;
  if (!text) return out;
  for (const char *p = text; *p; ++p) {
    switch (*p) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default: out += *p; break;
    }
  }
  return out;
}

String jsonEscape(const char *text) {
  String out;
  if (!text) return out;
  for (const char *p = text; *p; ++p) {
    switch (*p) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: out += *p; break;
    }
  }
  return out;
}

enum FieldType {
  FIELD_TEXT,
  FIELD_U8,
  FIELD_U16,
  FIELD_U32,
  FIELD_I8,
  FIELD_FLOAT,
  FIELD_HEX8,
  FIELD_BOOL,
};

struct FieldSpec {
  const char *name;
  const char *label;
  const char *tab;
  FieldType type;
  size_t offset;
  size_t size;
  double min;
  double max;
  const char *step;
  uint8_t decimals;
  const char *defaultText;
  const char *hint;
};

const FieldSpec DEVICE_ADVANCED_FIELDS[] = {
    // Device AP
    {"ap_max_clients", "Maximum AP clients", "ap", FIELD_U8,
     offsetof(RemoteSettings, apMaxClients), 0, 1, 4, "1", 0, "4", nullptr},

    // Tuya
    {"tuya_port", "Tuya TCP port", "tuya", FIELD_U16,
     offsetof(RemoteSettings, tuyaPort), 0, 1, 65535, "1", 0, "6668",
     nullptr},
    {"tuya_connect_timeout_ms", "Connect timeout (ms)", "tuya", FIELD_U32,
     offsetof(RemoteSettings, tuyaConnectTimeoutMs), 0, 100, 30000, "100", 0,
     "3000", nullptr},
    {"tuya_read_timeout_ms", "Read timeout (ms)", "tuya", FIELD_U32,
     offsetof(RemoteSettings, tuyaReadTimeoutMs), 0, 100, 30000, "100", 0,
     "2500", nullptr},
    {"tuya_poll_interval_ms", "Poll interval (ms)", "tuya", FIELD_U32,
     offsetof(RemoteSettings, tuyaPollIntervalMs), 0, 1000, 3600000, "1000",
     0, "10000", nullptr},
    {"tuya_reconnect_interval_ms", "Reconnect interval (ms)", "tuya",
     FIELD_U32, offsetof(RemoteSettings, tuyaReconnectIntervalMs), 0, 1000,
     3600000, "1000", 0, "5000", nullptr},

    // LoRa
    {"lora_frequency_mhz", "Frequency (MHz)", "lora", FIELD_FLOAT,
     offsetof(RemoteSettings, loraFrequencyMhz), 0, 137, 1020, "0.1", 1,
     "868.1", "Must match the base node."},
    {"lora_sync_word", "Sync word", "lora", FIELD_HEX8,
     offsetof(RemoteSettings, loraSyncWord), 0, 0, 255, "1", 0, "0xA6",
     "Must match the base node."},
    {"lora_tx_power", "TX power (dBm)", "lora", FIELD_I8,
     offsetof(RemoteSettings, loraTxPower), 0, -1, 20, "1", 0, "14", nullptr},
    {"lora_sf", "Spreading factor", "lora", FIELD_U8,
     offsetof(RemoteSettings, loraSf), 0, 6, 12, "1", 0, "7", nullptr},
    {"lora_bandwidth_hz", "Bandwidth (Hz)", "lora", FIELD_U32,
     offsetof(RemoteSettings, loraBandwidthHz), 0, 7800, 500000, "100", 0,
     "125000", nullptr},
    {"lora_coding_rate", "Coding rate (4/N)", "lora", FIELD_U8,
     offsetof(RemoteSettings, loraCodingRate), 0, 5, 8, "1", 0, "5", nullptr},
    {"lora_preamble", "Preamble length", "lora", FIELD_U16,
     offsetof(RemoteSettings, loraPreamble), 0, 6, 65535, "1", 0, "8",
     nullptr},
    {"dp_ack_timeout_ms", "DP ACK timeout (ms)", "lora", FIELD_U32,
     offsetof(RemoteSettings, dpAckTimeoutMs), 0, 100, 30000, "100", 0,
     "2500", nullptr},
    {"dp_ack_retries", "DP ACK retries", "lora", FIELD_U8,
     offsetof(RemoteSettings, dpAckRetries), 0, 0, 20, "1", 0, "3", nullptr},

    // Discovery
    {"scan_probe_timeout_ms", "TCP probe timeout (ms)", "discovery",
     FIELD_U32, offsetof(RemoteSettings, scanProbeTimeoutMs), 0, 10, 5000,
     "10", 0, "60", nullptr},
    {"scan_interval_ms", "AP scan interval (ms)", "discovery", FIELD_U32,
     offsetof(RemoteSettings, scanIntervalMs), 0, 1000, 3600000, "1000", 0,
     "30000", nullptr},
    {"station_refresh_ms", "Station refresh (ms)", "discovery", FIELD_U32,
     offsetof(RemoteSettings, stationRefreshMs), 0, 100, 60000, "100", 0,
     "2000", nullptr},
    {"device_lost_grace_ms", "Device lost grace (ms)", "discovery",
     FIELD_U32, offsetof(RemoteSettings, deviceLostGraceMs), 0, 0, 600000,
     "1000", 0, "10000", nullptr},

    // Display
    {"splash_ms", "Splash duration (ms)", "display", FIELD_U32,
     offsetof(RemoteSettings, splashMs), 0, 0, 60000, "500", 0, "3000",
     nullptr},
    {"display_refresh_ms", "Display refresh (ms)", "display", FIELD_U32,
     offsetof(RemoteSettings, displayRefreshMs), 0, 100, 10000, "100", 0,
     "500", nullptr},
    {"display_on_ms", "Display on time (ms)", "display", FIELD_U32,
     offsetof(RemoteSettings, displayOnMs), 0, 1000, 86400000, "1000", 0,
     "600000", nullptr},
    {"dp_page_interval_ms", "DP page interval (ms)", "display", FIELD_U32,
     offsetof(RemoteSettings, dpPageIntervalMs), 0, 500, 60000, "500", 0,
     "3000", nullptr},
    {"dp_per_page", "DPs per page", "display", FIELD_U8,
     offsetof(RemoteSettings, dpPerPage), 0, 1, 8, "1", 0, "5", nullptr},

    // System
    {"web_ui_port", "Web UI port", "system", FIELD_U16,
     offsetof(RemoteSettings, webUiPort), 0, 1, 65535, "1", 0, "80",
     nullptr},
    {"sd_firmware_file", "SD firmware filename", "system", FIELD_TEXT,
     offsetof(RemoteSettings, sdFirmwareFile),
     sizeof(settings.sdFirmwareFile), 0, 0, "1", 0, SD_FIRMWARE_FILE,
     "Must start with /."},
    {"sd_update_wait_ms", "SD update countdown (ms)", "system", FIELD_U32,
     offsetof(RemoteSettings, sdUpdateWaitMs), 0, 1000, 60000, "1000", 0,
     "10000", nullptr},
};

String fieldValue(const RemoteSettings &s, const FieldSpec &f) {
  const uint8_t *base = reinterpret_cast<const uint8_t *>(&s);
  const uint8_t *p = base + f.offset;
  switch (f.type) {
    case FIELD_TEXT:
      return String(reinterpret_cast<const char *>(p));
    case FIELD_U8:
      return String(*reinterpret_cast<const uint8_t *>(p));
    case FIELD_U16:
      return String(*reinterpret_cast<const uint16_t *>(p));
    case FIELD_U32:
      return String(*reinterpret_cast<const uint32_t *>(p));
    case FIELD_I8:
      return String(*reinterpret_cast<const int8_t *>(p));
    case FIELD_FLOAT:
      return String(*reinterpret_cast<const float *>(p),
                    static_cast<unsigned int>(f.decimals));
    case FIELD_HEX8: {
      char buf[8];
      snprintf(buf, sizeof(buf), "0x%02X",
               *reinterpret_cast<const uint8_t *>(p));
      return String(buf);
    }
    case FIELD_BOOL:
      return *reinterpret_cast<const bool *>(p) ? "true" : "false";
  }
  return String();
}

const char *hintFor(const char *name) {
  if (strcmp(name, "ap_max_clients") == 0)
    return "Maximum WiFi clients the device AP accepts. Keep it at 4 unless needed.";
  if (strcmp(name, "tuya_port") == 0)
    return "TCP port of the device's local Tuya API. Usually 6668.";
  if (strcmp(name, "tuya_connect_timeout_ms") == 0)
    return "How long to wait for the Tuya TCP connection.";
  if (strcmp(name, "tuya_read_timeout_ms") == 0)
    return "How long to wait for a Tuya response.";
  if (strcmp(name, "tuya_poll_interval_ms") == 0)
    return "How often the device is polled for DP values.";
  if (strcmp(name, "tuya_reconnect_interval_ms") == 0)
    return "How long to wait before retrying a failed Tuya connection.";
  if (strcmp(name, "lora_frequency_mhz") == 0)
    return "Radio frequency in MHz. Must match the base node.";
  if (strcmp(name, "lora_sync_word") == 0)
    return "LoRa sync word. Must match the base node.";
  if (strcmp(name, "lora_tx_power") == 0)
    return "Transmit power in dBm. Higher reaches farther but uses more power; -1 uses the radio default.";
  if (strcmp(name, "lora_sf") == 0)
    return "Spreading factor 6-12. Higher values increase range and airtime.";
  if (strcmp(name, "lora_bandwidth_hz") == 0)
    return "LoRa bandwidth in Hz. Wider is faster but less sensitive.";
  if (strcmp(name, "lora_coding_rate") == 0)
    return "Forward error correction from 4/5 to 4/8. Higher values add redundancy and airtime.";
  if (strcmp(name, "lora_preamble") == 0)
    return "Preamble symbols before each packet. Longer preambles help the receiver lock on.";
  if (strcmp(name, "dp_ack_timeout_ms") == 0)
    return "How long to wait for each DP chunk acknowledgement.";
  if (strcmp(name, "dp_ack_retries") == 0)
    return "How many times unacknowledged DP chunks are resent.";
  if (strcmp(name, "scan_probe_timeout_ms") == 0)
    return "Timeout for each TCP probe during a full subnet scan.";
  if (strcmp(name, "scan_interval_ms") == 0)
    return "How often to rescan the AP subnet when the device is missing.";
  if (strcmp(name, "station_refresh_ms") == 0)
    return "How often to refresh the AP client list.";
  if (strcmp(name, "device_lost_grace_ms") == 0)
    return "How long to keep trying the last device IP before rescanning.";
  if (strcmp(name, "splash_ms") == 0)
    return "How long the boot splash stays visible.";
  if (strcmp(name, "display_refresh_ms") == 0)
    return "How often the OLED status screen is redrawn.";
  if (strcmp(name, "display_on_ms") == 0)
    return "Time after boot before the OLED turns off. Set it high to keep the display on.";
  if (strcmp(name, "dp_page_interval_ms") == 0)
    return "How often the DP pages rotate on the OLED.";
  if (strcmp(name, "dp_per_page") == 0)
    return "Number of DP rows shown on each OLED page.";
  if (strcmp(name, "web_ui_port") == 0)
    return "HTTP port for this settings page. After changing it, reconnect on the new port.";
  if (strcmp(name, "sd_firmware_file") == 0)
    return "Path of the firmware file on the SD card. Must start with /.";
  if (strcmp(name, "sd_update_wait_ms") == 0)
    return "Countdown before an SD-card update starts. Remove the card to abort.";
  return nullptr;
}

String renderField(const RemoteSettings &s, const FieldSpec &f) {
  String out;
  out += "<label for=\"";
  out += f.name;
  out += "\">";
  out += f.label;
  if (f.defaultText && *f.defaultText) {
    out += " <span class=\"def\">default ";
    out += htmlEscape(f.defaultText);
    out += "</span>";
  }
  out += "</label>";

  if (f.type == FIELD_BOOL) {
    const uint8_t *base = reinterpret_cast<const uint8_t *>(&s);
    const bool value = *reinterpret_cast<const bool *>(base + f.offset);
    out += "<label class=\"check\"><input type=\"checkbox\" id=\"";
    out += f.name;
    out += "\" name=\"";
    out += f.name;
    out += "\"";
    if (value) out += " checked";
    out += "> Enabled</label>";
  } else {
    out += "<input id=\"";
    out += f.name;
    out += "\" name=\"";
    out += f.name;
    out += "\" type=\"";
    out += (f.type == FIELD_TEXT || f.type == FIELD_HEX8) ? "text" : "number";
    out += "\"";
    if (f.type == FIELD_TEXT) {
      out += " maxlength=\"";
      out += String(f.size - 1);
      out += "\"";
    } else if (f.type == FIELD_HEX8) {
      out += " maxlength=\"4\"";
    } else {
      out += " min=\"";
      out += String(f.min, static_cast<unsigned int>(f.decimals));
      out += "\" max=\"";
      out += String(f.max, static_cast<unsigned int>(f.decimals));
      out += "\" step=\"";
      out += f.step;
      out += "\"";
    }
    out += " value=\"";
    out += htmlEscape(fieldValue(s, f).c_str());
    out += "\"";
    out += ">";
  }

  const char *hint = f.hint ? f.hint : hintFor(f.name);
  if (hint) {
    out += "<p class=\"hint\">";
    out += hint;
    out += "</p>";
  }
  return out;
}

String renderFields(const RemoteSettings &s, const char *tab) {
  String out;
  for (const auto &field : DEVICE_ADVANCED_FIELDS) {
    if (strcmp(field.tab, tab) == 0) out += renderField(s, field);
  }
  return out;
}

bool applyField(RemoteSettings &s, const FieldSpec &f, String &error) {
  uint8_t *base = reinterpret_cast<uint8_t *>(&s);
  uint8_t *p = base + f.offset;

  if (f.type == FIELD_BOOL) {
    *reinterpret_cast<bool *>(p) = server->hasArg(f.name);
    return true;
  }
  if (!server->hasArg(f.name)) return true;

  const String value = server->arg(f.name);
  char *end = nullptr;

  switch (f.type) {
    case FIELD_TEXT:
      if (value.length() == 0) {
        error = String(f.label) + " is required.";
        return false;
      }
      value.substring(0, f.size - 1)
          .toCharArray(reinterpret_cast<char *>(p), f.size);
      return true;

    case FIELD_U8:
    case FIELD_U16:
    case FIELD_U32: {
      const unsigned long n = strtoul(value.c_str(), &end, 10);
      if (value.length() == 0 || *end != '\0' || n < f.min || n > f.max) {
        error = String(f.label) + " must be between " +
                String(f.min, static_cast<unsigned int>(f.decimals)) +
                " and " +
                String(f.max, static_cast<unsigned int>(f.decimals)) + ".";
        return false;
      }
      if (f.type == FIELD_U8) {
        *reinterpret_cast<uint8_t *>(p) = static_cast<uint8_t>(n);
      } else if (f.type == FIELD_U16) {
        *reinterpret_cast<uint16_t *>(p) = static_cast<uint16_t>(n);
      } else {
        *reinterpret_cast<uint32_t *>(p) = static_cast<uint32_t>(n);
      }
      return true;
    }

    case FIELD_I8: {
      const long n = strtol(value.c_str(), &end, 10);
      if (value.length() == 0 || *end != '\0' || n < f.min || n > f.max) {
        error = String(f.label) + " must be between " +
                String(f.min, static_cast<unsigned int>(f.decimals)) +
                " and " +
                String(f.max, static_cast<unsigned int>(f.decimals)) + ".";
        return false;
      }
      *reinterpret_cast<int8_t *>(p) = static_cast<int8_t>(n);
      return true;
    }

    case FIELD_FLOAT: {
      const float n = strtof(value.c_str(), &end);
      if (value.length() == 0 || *end != '\0' || n < f.min || n > f.max) {
        error = String(f.label) + " must be between " +
                String(f.min, static_cast<unsigned int>(f.decimals)) +
                " and " +
                String(f.max, static_cast<unsigned int>(f.decimals)) + ".";
        return false;
      }
      *reinterpret_cast<float *>(p) = n;
      return true;
    }

    case FIELD_HEX8: {
      const unsigned long n = strtoul(value.c_str(), &end, 0);
      if (value.length() == 0 || *end != '\0' || n > 255) {
        error = String(f.label) + " must be between 0x00 and 0xFF.";
        return false;
      }
      *reinterpret_cast<uint8_t *>(p) = static_cast<uint8_t>(n);
      return true;
    }

    case FIELD_BOOL:
      return true;
  }
  return true;
}

bool applyAdvancedFields(RemoteSettings &s, String &error, String &tab) {
  for (const auto &field : DEVICE_ADVANCED_FIELDS) {
    if (!applyField(s, field, error)) {
      tab = field.tab;
      return false;
    }
  }
  return true;
}

String setupApChannelOptions(const RemoteSettings &s) {
  String out;
  for (uint8_t channel = 1; channel <= 13; ++channel) {
    out += "<option value=\"";
    out += String(channel);
    out += "\"";
    if (channel == s.setupApChannel) out += " selected";
    out += ">";
    out += String(channel);
    out += "</option>";
  }
  return out;
}

String apStatusText() {
  String out;
  if (setupApActive) out += "setup AP ";
  out += "<b>";
  out += htmlEscape(WiFi.softAPSSID().c_str());
  out += "</b> at ";
  out += WiFi.softAPIP().toString();
  out += " (";
  out += String(WiFi.softAPgetStationNum());
  out += " clients)";
  return out;
}

String deviceStatusText() {
  if (deviceFound) return String("found at ") + deviceIP.toString();
  return "searching on the AP";
}

String tuyaStatusText() {
  if (!tuyaConfigured()) return "device ID or local key not set";
  if (tuya.connected()) {
    return String("connected, v") + tuya.versionText() + ", " +
           String(tuya.dpCount()) + " DPs";
  }
  if (tuya.canQuery()) return "configured, waiting for device";
  return "not configured";
}

bool pskDefaultZero(const char *psk) {
  return psk && strcmp(psk, "00000000000000000000000000000000") == 0;
}

String pskStatusText(const RemoteSettings &s) {
  if (pskDefaultZero(s.loraPsk)) return "default zero (insecure)";
  return isValidPsk(s.loraPsk) ? "set" : "not set";
}

String pskWarningText(const RemoteSettings &s) {
  if (pskDefaultZero(s.loraPsk)) {
    return "<div class=\"warn\">The LoRa PSK is the all-zero default. The link "
           "works for initial setup, but anyone with this firmware can decrypt "
           "it. Generate a new key before deployment.</div>";
  }
  if (isValidPsk(s.loraPsk)) return "";
  return "<div class=\"warn\">LoRa PSK is not set. The remote node cannot "
         "talk to the base until both nodes have the same 32-character hex "
         "key.</div>";
}

String channelOptions(const RemoteSettings &s) {
  String out;
  for (uint8_t channel = 1; channel <= 13; ++channel) {
    out += "<option value=\"";
    out += String(channel);
    out += "\"";
    if (channel == s.apChannel) out += " selected";
    out += ">";
    out += String(channel);
    out += "</option>";
  }
  return out;
}

bool versionSelected(const RemoteSettings &s, float version) {
  return fabs(s.tuyaVersion - version) < 0.01f;
}

String versionOptions(const RemoteSettings &s) {
  String out;
  out += "<option value=\"0\"";
  if (versionSelected(s, 0.0f)) out += " selected";
  out += ">Auto-detect</option>";
  const float versions[] = {3.1f, 3.3f, 3.4f, 3.5f};
  for (float version : versions) {
    out += "<option value=\"";
    out += String(version, 1);
    out += "\"";
    if (versionSelected(s, version)) out += " selected";
    out += ">";
    out += String(version, 1);
    out += "</option>";
  }
  return out;
}

String keyPlaceholder(const RemoteSettings &s) {
  if (s.tuyaLocalKey[0] == '\0' ||
      strcmp(s.tuyaLocalKey, "replace-16-chars") == 0) {
    return "16 characters";
  }
  return "leave blank to keep current";
}

String pskPlaceholder(const RemoteSettings &s) {
  if (pskDefaultZero(s.loraPsk)) return "generate a key or enter 32 hex characters";
  return isValidPsk(s.loraPsk) ? "leave blank to keep current"
                               : "32 hex characters";
}

bool apPasswordConfigured(const RemoteSettings &s) {
  return s.apPassword[0] != '\0' &&
         strcmp(s.apPassword,
                "replace-with-a-strong-12-plus-character-password") != 0;
}

String apPasswordPlaceholder(const RemoteSettings &s) {
  return apPasswordConfigured(s) ? "leave blank to keep current"
                                 : "8-63 characters";
}

void sendResult(const String &title, const String &detail) {
  String page = FPSTR(RESULT_HTML);
  page.replace("{{TITLE}}", htmlEscape(title.c_str()));
  page.replace("{{DETAIL}}", htmlEscape(detail.c_str()));
  server->sendHeader("Cache-Control", "no-store");
  server->send(200, "text/html", page);
}

void renderForm(const RemoteSettings &s, const String &error, int status,
                const String &tab) {
  String page = FPSTR(DEVICE_HTML);
  page.replace("{{FW_VERSION}}", FW_VERSION);
  page.replace("{{IP}}", WiFi.softAPIP().toString() + ":" +
                            String(settings.webUiPort));
  page.replace("{{ERROR}}", error.length() ? String("<div class=\"err\">") +
                                                htmlEscape(error.c_str()) +
                                                "</div>"
                                          : String(""));
  page.replace("{{AP_STATUS}}", apStatusText());
  page.replace("{{DEVICE_STATUS}}", deviceStatusText());
  page.replace("{{TUYA_STATUS}}", tuyaStatusText());
  page.replace("{{PSK_STATUS}}", pskStatusText(s));
  page.replace("{{PSK_WARNING}}", pskWarningText(s));
  page.replace("{{ACTIVE_TAB}}", htmlEscape(tab.c_str()));
  page.replace("{{AP_SSID}}", htmlEscape(s.apSsid));
  page.replace("{{AP_CHANNEL_OPTIONS}}", channelOptions(s));
  page.replace("{{TUYA_DEVICE_ID}}", htmlEscape(s.tuyaDeviceId));
  page.replace("{{TUYA_VERSION_OPTIONS}}", versionOptions(s));
  page.replace("{{TUYA_STATIC_IP}}", htmlEscape(s.tuyaStaticIp));
  page.replace("{{TUYA_KEY_PLACEHOLDER}}", keyPlaceholder(s));
  page.replace("{{PSK_PLACEHOLDER}}", pskPlaceholder(s));
  page.replace("{{AP_PASSWORD_PLACEHOLDER}}", apPasswordPlaceholder(s));
  page.replace("{{FIELDS_AP}}", renderFields(s, "ap"));
  page.replace("{{FIELDS_TUYA}}", renderFields(s, "tuya"));
  page.replace("{{FIELDS_LORA}}", renderFields(s, "lora"));
  page.replace("{{FIELDS_DISCOVERY}}", renderFields(s, "discovery"));
  page.replace("{{FIELDS_DISPLAY}}", renderFields(s, "display"));
  page.replace("{{FIELDS_SYSTEM}}", renderFields(s, "system"));
  page.replace("{{SETUP_AP_SSID}}", htmlEscape(s.setupApSsid));
  page.replace("{{SETUP_AP_PASSWORD_PLACEHOLDER}}",
               "leave blank to keep current");
  page.replace("{{SETUP_AP_CHANNEL_OPTIONS}}", setupApChannelOptions(s));
  server->sendHeader("Cache-Control", "no-store");
  server->send(status, "text/html", page);
}

void handleSettings() { renderForm(settings, "", 200, "ap"); }

void handleStatus() {
  String page = FPSTR(STATUS_HTML);
  page.replace("{{FW_VERSION}}", FW_VERSION);
  server->sendHeader("Cache-Control", "no-store");
  server->send(200, "text/html", page);
}

void handleApiStatus() {
  String json;
  json.reserve(6144);
  json += "{\"fw_version\":\"" FW_VERSION "\",";
  json += "\"ap_ssid\":\"";
  json += jsonEscape(WiFi.softAPSSID().c_str());
  json += "\",\"ap_ip\":\"";
  json += WiFi.softAPIP().toString();
  json += "\",\"ap_clients\":";
  json += String(WiFi.softAPgetStationNum());
  json += ",\"setup_ap\":";
  json += (setupApActive ? "true" : "false");
  json += ",\"device_found\":";
  json += (deviceFound ? "true" : "false");
  json += ",\"device_ip\":\"";
  json += deviceFound ? deviceIP.toString() : String("-");
  json += "\",\"ever_connected\":";
  json += (everConnected ? "true" : "false");
  json += ",\"tuya_configured\":";
  json += (tuyaConfigured() ? "true" : "false");
  json += ",\"tuya_connected\":";
  json += (tuya.connected() ? "true" : "false");
  json += ",\"tuya_version\":\"";
  json += tuya.versionText();
  json += "\",\"dp_count\":";
  json += String(tuya.dpCount());
  json += ",\"last_rx_age_s\":";
  json += String(tuya.lastRxMs() == 0
                     ? -1L
                     : static_cast<long>((millis() - tuya.lastRxMs()) / 1000));
  json += ",\"query_count\":";
  json += String(tuya.queryCount());
  json += ",\"psk_set\":";
  json += (isValidPsk(settings.loraPsk) ? "true" : "false");
  json += ",\"psk_default_zero\":";
  json += (strcmp(settings.loraPsk, "00000000000000000000000000000000") == 0
               ? "true"
               : "false");
  json += ",\"last_error\":\"";
  json += jsonEscape(tuya.lastError().c_str());
  json += "\",\"last_json\":\"";
  json += jsonEscape(tuya.lastJson().c_str());
  json += "\",\"dps\":[";
  for (uint8_t i = 0; i < tuya.dpCount(); ++i) {
    if (i > 0) json += ",";
    json += "{\"id\":\"";
    json += jsonEscape(tuya.dps()[i].id.c_str());
    json += "\",\"value\":\"";
    json += jsonEscape(tuya.dps()[i].value.c_str());
    json += "\"}";
  }
  json += "],\"lora_config\":";
  json += remoteLoraConfigStatusJson();
  json += "}";
  server->sendHeader("Cache-Control", "no-store");
  server->send(200, "application/json", json);
}

void copyArg(const char *name, char *dst, size_t dstSize, bool keepIfEmpty) {
  if (!server->hasArg(name)) return;
  String value = server->arg(name);
  if (keepIfEmpty && value.length() == 0) return;
  if (value.length() >= dstSize) value = value.substring(0, dstSize - 1);
  value.toCharArray(dst, dstSize);
}

bool validIpv4(const char *text) {
  if (!text || text[0] == '\0') return true;
  IPAddress ip;
  if (!ip.fromString(text)) return false;
  // The device is a client of this node's 192.168.4.0/24 AP.
  return ip[0] == 192 && ip[1] == 168 && ip[2] == 4 && ip[3] > 1 &&
         ip[3] < 255;
}

bool validVersion(float version) {
  return version == 0.0f || fabs(version - 3.1f) < 0.01f ||
         fabs(version - 3.3f) < 0.01f || fabs(version - 3.4f) < 0.01f ||
         fabs(version - 3.5f) < 0.01f;
}

bool validate(const RemoteSettings &s, String &error, String &tab) {
  if (s.apSsid[0] == '\0') {
    error = "AP SSID is required.";
    tab = "ap";
    return false;
  }
  if (strlen(s.apSsid) > 32) {
    error = "AP SSID must be 32 characters or fewer.";
    tab = "ap";
    return false;
  }
  if (strlen(s.apPassword) < 8 || strlen(s.apPassword) > 63) {
    error = "AP password must be 8-63 characters.";
    tab = "ap";
    return false;
  }
  if (strcmp(s.apPassword,
             "replace-with-a-strong-12-plus-character-password") == 0) {
    error = "AP password is still the example placeholder.";
    tab = "ap";
    return false;
  }
  if (s.apChannel < 1 || s.apChannel > 13) {
    error = "AP channel must be between 1 and 13.";
    tab = "ap";
    return false;
  }
  if (s.tuyaDeviceId[0] == '\0') {
    error = "Tuya device ID is required.";
    tab = "tuya";
    return false;
  }
  if (strcmp(s.tuyaDeviceId, "replace-with-device-id") == 0) {
    error = "Tuya device ID is still the example placeholder.";
    tab = "tuya";
    return false;
  }
  if (strlen(s.tuyaLocalKey) != 16) {
    error = "Tuya local key must be exactly 16 characters.";
    tab = "tuya";
    return false;
  }
  if (strcmp(s.tuyaLocalKey, "replace-16-chars") == 0) {
    error = "Tuya local key is still the example placeholder.";
    tab = "tuya";
    return false;
  }
  if (!validVersion(s.tuyaVersion)) {
    error = "Tuya protocol version must be auto, 3.1, 3.3, 3.4 or 3.5.";
    tab = "tuya";
    return false;
  }
  if (!validIpv4(s.tuyaStaticIp)) {
    error = "Static device IP must be a valid IPv4 address or empty.";
    tab = "tuya";
    return false;
  }
  if (!isValidPsk(s.loraPsk)) {
    error = "LoRa PSK must be exactly 32 hex characters.";
    tab = "lora";
    return false;
  }
  if (s.setupApSsid[0] == '\0') {
    error = "Setup AP SSID is required.";
    tab = "system";
    return false;
  }
  if (s.setupApPassword[0] == '\0' || strlen(s.setupApPassword) < 8 ||
      strlen(s.setupApPassword) > 63) {
    error = "Setup AP password must be 8-63 characters.";
    tab = "system";
    return false;
  }
  if (s.setupApChannel < 1 || s.setupApChannel > 13) {
    error = "Setup AP channel must be between 1 and 13.";
    tab = "system";
    return false;
  }
  if (s.sdFirmwareFile[0] != '/') {
    error = "SD firmware filename must start with /.";
    tab = "system";
    return false;
  }
  return true;
}

void handleSave() {
  if (remoteLoraConfigBusy()) {
    renderForm(settings, "A coordinated LoRa configuration change is in progress. Wait for it to finish before saving.", 409, "lora");
    return;
  }
  if (!server->hasArg("ap_ssid")) {
    server->send(400, "text/plain", "Bad request");
    return;
  }

  RemoteSettings next = settings;
  copyArg("ap_ssid", next.apSsid, sizeof(next.apSsid), false);
  copyArg("ap_password", next.apPassword, sizeof(next.apPassword), true);

  const int channel = server->arg("ap_channel").toInt();
  if (channel < 1 || channel > 13) {
    renderForm(next, "AP channel must be between 1 and 13.", 400, "ap");
    return;
  }
  next.apChannel = static_cast<uint8_t>(channel);

  copyArg("tuya_dev_id", next.tuyaDeviceId, sizeof(next.tuyaDeviceId), false);
  copyArg("tuya_key", next.tuyaLocalKey, sizeof(next.tuyaLocalKey), true);
  next.tuyaVersion = server->arg("tuya_version").toFloat();
  copyArg("tuya_ip", next.tuyaStaticIp, sizeof(next.tuyaStaticIp), false);
  copyArg("lora_psk", next.loraPsk, sizeof(next.loraPsk), true);

  copyArg("setup_ap_ssid", next.setupApSsid, sizeof(next.setupApSsid), false);
  copyArg("setup_ap_password", next.setupApPassword,
          sizeof(next.setupApPassword), true);
  const int setupChannel = server->arg("setup_ap_channel").toInt();
  if (setupChannel < 1 || setupChannel > 13) {
    renderForm(next, "Setup AP channel must be between 1 and 13.", 400,
               "system");
    return;
  }
  next.setupApChannel = static_cast<uint8_t>(setupChannel);

  String error;
  String tab = "system";
  if (!applyAdvancedFields(next, error, tab)) {
    renderForm(next, error, 400, tab);
    return;
  }
  if (!validate(next, error, tab)) {
    renderForm(next, error, 400, tab);
    return;
  }

  settings = next;
  if (!saveSettings()) {
    renderForm(next, "Could not write settings to NVS.", 500, tab);
    return;
  }
  bootGuardClear();

  sendResult("Saved",
             "The remote node is rebooting. If you changed the AP SSID, "
             "password or web UI port, reconnect to the new AP and open "
             "http://192.168.4.1:" + String(settings.webUiPort) + "/.");
  rebootAtMs = millis() + 1500;
}

void handleGeneratePsk() {
  char psk[33];
  do {
    for (uint8_t i = 0; i < 16; ++i) {
      snprintf(psk + i * 2, 3, "%02x", esp_random() & 0xFF);
    }
  } while (strcmp(psk, "00000000000000000000000000000000") == 0);
  String json = "{\"psk\":\"";
  json += psk;
  json += "\"}";
  server->sendHeader("Cache-Control", "no-store");
  server->send(200, "application/json", json);
}

void handleFactory() {
  bootGuardClear();
  clearSettings();
  sendResult("Factory reset",
             "All settings were cleared. The remote node is rebooting into "
             "setup mode at http://192.168.4.1:" +
                 String(settings.webUiPort) + "/.");
  rebootAtMs = millis() + 1500;
}

void handleNotFound() {
  server->sendHeader("Location", "/");
  server->send(302, "text/plain", "");
}

constexpr const char *OTA_BOARD = "remote";
bool otaRequestSeen = false;
bool otaSuccess = false;
bool otaActive = false;
String otaError;

void handleOtaPage() {
  String page = FPSTR(OTA_HTML);
  page.replace("{{NAV}}", R"NAV(<a href="/settings">Settings</a><a href="/">Status</a><a class="active" href="/ota">Firmware</a>)NAV");
  page.replace("{{TITLE}}", "Remote");
  page.replace("{{BOARD}}", "remote");
  page.replace("{{FILENAME}}", "remote-node.bin");
  page.replace("{{FW_VERSION}}", FW_VERSION);
  server->sendHeader("Cache-Control", "no-store");
  server->send(200, "text/html", page);
}

void handleOtaUpload() {
  HTTPRaw &raw = server->raw();

  if (raw.status == RAW_START) {
    otaRequestSeen = true;
    otaSuccess = false;
    otaActive = true;
    otaError = "";
    if (server->arg("board") != String(OTA_BOARD)) {
      otaError = "wrong board identifier";
      otaActive = false;
      return;
    }
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      otaError = Update.errorString();
      if (otaError.length() == 0) otaError = "could not start OTA";
    }
    return;
  }

  if (raw.status == RAW_WRITE) {
    if (!otaActive || otaError.length() != 0) return;
    const size_t written = Update.write(raw.buf, raw.currentSize);
    if (written != raw.currentSize) {
      otaError = String("short write (") + String(written) + "/" +
                 String(raw.currentSize) + ")";
    }
    return;
  }

  if (raw.status == RAW_END) {
    if (otaActive && otaError.length() == 0) {
      if (!Update.end(true)) {
        otaError = Update.errorString();
        if (otaError.length() == 0) otaError = "firmware verification failed";
      } else if (!Update.isFinished()) {
        otaError = "firmware update did not finish";
      } else {
        otaSuccess = true;
      }
    }
    otaActive = false;
    return;
  }

  if (raw.status == RAW_ABORTED) {
    if (otaActive) Update.abort();
    otaActive = false;
    otaError = "upload aborted";
  }
}

void handleOtaComplete() {
  const bool seen = otaRequestSeen;
  const bool success = otaSuccess;
  String error = otaError;
  otaRequestSeen = false;
  otaSuccess = false;
  otaActive = false;

  if (!seen) {
    server->send(400, "text/plain", "No firmware received.\n");
    return;
  }
  if (!success) {
    if (error.length() == 0) error = "unknown error";
    Serial.printf("[ota] failed: %s\n", error.c_str());
    server->send(500, "text/plain", String("OTA failed: ") + error + "\n");
    return;
  }

  Serial.println("[ota] firmware written; rebooting");
  server->send(200, "text/plain", "Firmware written. Rebooting...\n");
  rebootAtMs = millis() + 1500;
}

}  // namespace

void webUiBegin() {
  const bool forceSetup = bootGuardForceSetupAp();
  setupApActive = !apConfigured() || forceSetup;
  if (forceSetup) {
    Serial.println("[web] recovery mode: forcing setup AP");
  }

  const char *ssid = setupApActive ? settings.setupApSsid : settings.apSsid;
  const char *password =
      setupApActive ? settings.setupApPassword : settings.apPassword;
  const uint8_t channel =
      setupApActive ? settings.setupApChannel : settings.apChannel;
  uint8_t maxClients = settings.apMaxClients;
  if (maxClients < 1) maxClients = 1;
  if (maxClients > AP_MAX_CLIENTS) maxClients = AP_MAX_CLIENTS;

  WiFi.mode(WIFI_AP);
  bool ok = WiFi.softAP(ssid, password, channel, 0, maxClients);
  if (!ok && !setupApActive) {
    Serial.println("[web] configured AP failed; falling back to setup AP");
    setupApActive = true;
    ok = WiFi.softAP(settings.setupApSsid, settings.setupApPassword,
                     settings.setupApChannel, 0, maxClients);
  }
  Serial.printf("[web] AP %s -> %s (%s)\n",
                setupApActive ? settings.setupApSsid : settings.apSsid,
                WiFi.softAPIP().toString().c_str(), ok ? "ok" : "FAILED");
  if (setupApActive) {
    Serial.printf("[web] setup mode: open http://192.168.4.1:%u/\n",
                  settings.webUiPort);
  }

  server = new WebServer(settings.webUiPort);
  server->on("/", HTTP_GET, handleStatus);
  server->on("/settings", HTTP_GET, handleSettings);
  server->on("/ota", HTTP_GET, handleOtaPage);
  server->on("/api/ota", HTTP_POST, handleOtaComplete, handleOtaUpload);
  server->on("/api/status", HTTP_GET, handleApiStatus);
  server->on("/save", HTTP_POST, handleSave);
  server->on("/api/lora/psk", HTTP_POST, handleGeneratePsk);
  server->on("/factory", HTTP_POST, handleFactory);
  server->onNotFound(handleNotFound);
  server->begin();
}

void webUiLoop() {
  server->handleClient();

  if (rebootAtMs != 0 && static_cast<int32_t>(millis() - rebootAtMs) >= 0) {
    Serial.println("[web] rebooting");
    delay(100);
    ESP.restart();
  }
}

bool webUiSetupApActive() { return setupApActive; }

IPAddress webUiApIp() { return WiFi.softAPIP(); }

}  // namespace tuya_lora
