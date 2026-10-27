#include "web_ui.h"

#include <PubSubClient.h>
#include <WebServer.h>
#include <WiFi.h>

#include <esp_system.h>
#include <string.h>

#include "boot_guard.h"
#include "browser_profile.h"
#include "config.h"
#include "config_store.h"
#include "lora_protocol.h"
#include "profiles.h"
#include "web_assets.h"

// Defined in base-node.ino (global namespace).
extern PubSubClient mqtt;
extern tuya_lora::StatusData lastStatus;
extern bool haveState;
extern bool haveSeen;
extern bool remoteOnline;
extern uint32_t lastSeenMs;
extern int lastRssi;
extern float lastSnr;
extern char dpJson[];
extern bool profileMatched;
extern char profileError[];
extern const char *profileActiveId();
extern const char *profileDetectedId();
extern uint8_t profileEntityCount();

namespace tuya_lora {

namespace {

WebServer *server = nullptr;

bool setupApActive = false;
uint32_t rebootAtMs = 0;
uint32_t lastWifiOkMs = 0;
uint32_t lastSetupApAttemptMs = 0;

const char BASE_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Tuya LoRa Bridge — Base settings</title>
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
  input, select, button, textarea { font: inherit; }
  input[type=text], input[type=password], input[type=number], select, textarea {
    width: 100%; padding: 10px 12px; border: 1px solid #8886; border-radius: 8px; background: transparent; color: inherit;
  }
  textarea { min-height: 220px; resize: vertical; font: .85rem/1.4 ui-monospace, SFMono-Regular, Menlo, monospace; }
  .row { display: flex; gap: 8px; align-items: end; }
  .row > input { flex: 1; min-width: 0; }
  .check { display: flex; gap: 8px; align-items: center; font-weight: 400; margin-top: 8px; }
  .check input { width: 18px; height: 18px; }
  .actions { display: flex; flex-wrap: wrap; gap: 10px; margin-top: 24px; }
  button { padding: 10px 16px; border: 0; border-radius: 8px; background: #2563eb; color: #fff; font-weight: 650; cursor: pointer; }
  button.secondary { background: #8883; color: inherit; }
  button.danger { background: #b91c1c; }
  .hint { font-size: .85rem; opacity: .7; margin: 6px 0 0; }
  .def { font-weight: 400; opacity: .6; font-size: .8rem; }
  hr { border: 0; border-top: 1px solid #8884; margin: 32px 0; }
</style>
</head>
<body data-active-tab="{{ACTIVE_TAB}}">
<main>
  <nav class="nav"><a class="active" href="/settings">Settings</a><a href="/">Status</a></nav>
  <h1>Tuya LoRa Bridge — Base</h1>
  <p class="muted">Firmware {{FW_VERSION}} · {{IP}}</p>

  {{ERROR}}
  <div class="status">
    <div><strong>WiFi:</strong> {{WIFI_STATUS}}</div>
    <div><strong>MQTT:</strong> {{MQTT_STATUS}}</div>
    <div><strong>LoRa PSK:</strong> {{PSK_STATUS}}</div>
  </div>

  {{PSK_WARNING}}

  <div class="tabs" role="tablist">
    <button type="button" class="tab active" data-tab="wifi" role="tab">WiFi</button>
    <button type="button" class="tab" data-tab="mqtt" role="tab">MQTT</button>
    <button type="button" class="tab" data-tab="topics" role="tab">Topics</button>
    <button type="button" class="tab" data-tab="lora" role="tab">LoRa</button>
    <button type="button" class="tab" data-tab="device" role="tab">Home Assistant</button>
    <button type="button" class="tab" data-tab="timing" role="tab">Timing</button>
    <button type="button" class="tab" data-tab="control" role="tab">Control</button>
    <button type="button" class="tab" data-tab="profile" role="tab">Profile</button>
    <button type="button" class="tab" data-tab="system" role="tab">System</button>
  </div>

  <form method="POST" action="/save" id="settings-form">
    <section class="tab-panel active" id="tab-wifi" data-tab-panel="wifi" role="tabpanel">
      <h2>Home WiFi</h2>
      <label for="wifi_ssid">SSID <span class="def">required</span></label>
      <div class="row">
        <input id="wifi_ssid" name="wifi_ssid" type="text" maxlength="32" required list="ssids" value="{{WIFI_SSID}}" autocomplete="off">
        <button class="secondary" type="button" onclick="scanWifi(this)">Scan</button>
      </div>
      <datalist id="ssids"></datalist>
      <p class="hint">Name of the 2.4 GHz WiFi network the base should join.</p>
      <label for="wifi_password">Password <span class="def">empty = open network</span></label>
      <input id="wifi_password" name="wifi_password" type="password" maxlength="64" autocomplete="new-password" placeholder="{{WIFI_PASSWORD_PLACEHOLDER}}">
      <label class="check"><input type="checkbox" name="clear_wifi_password"> Clear saved password (open network)</label>
      <p class="hint">The base joins this network. If it cannot connect, it starts the configured setup AP (default <code>TuyaLoraBase-Setup</code> at <code>192.168.4.1</code>, password <code>tuya-lora-setup</code>).</p>
    </section>

    <section class="tab-panel" id="tab-mqtt" data-tab-panel="mqtt" role="tabpanel">
      <h2>MQTT broker</h2>
      <label for="mqtt_host">Host <span class="def">required</span></label>
      <input id="mqtt_host" name="mqtt_host" type="text" maxlength="63" required value="{{MQTT_HOST}}" autocomplete="off">
      <p class="hint">Hostname or IP address of the MQTT broker.</p>
      <label for="mqtt_port">Port <span class="def">default 1883</span></label>
      <input id="mqtt_port" name="mqtt_port" type="number" min="1" max="65535" required value="{{MQTT_PORT}}">
      <p class="hint">TCP port of the broker. 1883 is plain MQTT; 8883 is usually TLS.</p>
      <label for="mqtt_user">Username <span class="def">optional</span></label>
      <input id="mqtt_user" name="mqtt_user" type="text" maxlength="32" value="{{MQTT_USER}}" autocomplete="off">
      <p class="hint">Leave empty if the broker allows anonymous connections.</p>
      <label for="mqtt_password">Password <span class="def">optional</span></label>
      <input id="mqtt_password" name="mqtt_password" type="password" maxlength="64" autocomplete="new-password" placeholder="{{MQTT_PASSWORD_PLACEHOLDER}}">
      <p class="hint">Leave empty to keep the saved password.</p>
      <label class="check"><input type="checkbox" name="clear_mqtt_password"> Clear saved password (anonymous broker)</label>
      {{FIELDS_MQTT}}
    </section>

    <section class="tab-panel" id="tab-topics" data-tab-panel="topics" role="tabpanel">
      <h2>MQTT topics</h2>
      {{FIELDS_TOPICS}}
    </section>

    <section class="tab-panel" id="tab-lora" data-tab-panel="lora" role="tabpanel">
      <h2>LoRa link</h2>
      <label for="lora_psk">Pre-shared key <span class="def">required</span></label>
      <input id="lora_psk" name="lora_psk" type="password" maxlength="32" pattern="[0-9A-Fa-f]{32}" autocomplete="new-password" placeholder="{{PSK_PLACEHOLDER}}">
      <p class="hint">32 hex characters. Must be identical on the base and remote nodes.</p>
      {{FIELDS_LORA}}
    </section>

    <section class="tab-panel" id="tab-device" data-tab-panel="device" role="tabpanel">
      <h2>Home Assistant</h2>
      <label for="ha_name">Device name <span class="def">default Tuya charger</span></label>
      <input id="ha_name" name="ha_name" type="text" maxlength="32" required value="{{HA_NAME}}">
      <p class="hint">Friendly name shown in Home Assistant.</p>
      <label for="timezone">Timezone <span class="def">default Europe/Sofia</span></label>
      <select id="timezone" name="timezone">{{TIMEZONE_OPTIONS}}</select>
      <p class="hint">Used for timestamps in Home Assistant. Select the closest city/timezone.</p>
      {{FIELDS_HA}}
    </section>

    <section class="tab-panel" id="tab-timing" data-tab-panel="timing" role="tabpanel">
      <h2>Timing</h2>
      {{FIELDS_TIMING}}
    </section>

    <section class="tab-panel" id="tab-control" data-tab-panel="control" role="tabpanel">
      <h2>Control</h2>
      {{FIELDS_CONTROL}}
    </section>

    <section class="tab-panel" id="tab-profile" data-tab-panel="profile" role="tabpanel">
      <h2>Device profile</h2>
      <div class="status">
        <div><strong>Active:</strong> {{PROFILE_ACTIVE}}</div>
        <div><strong>Detected:</strong> {{PROFILE_DETECTED}}</div>
        <div><strong>Snapshot match:</strong> {{PROFILE_MATCH}}</div>
        <div><strong>Profile entities:</strong> {{PROFILE_ENTITIES}}</div>
        <div><strong>Note:</strong> {{PROFILE_NOTE}}</div>
      </div>
      <label for="profile_id">DP profile</label>
      <select id="profile_id" name="profile_id">{{PROFILE_OPTIONS}}</select>
      <p class="hint"><a href="/profiles">Open profile catalog</a> to browse the online tuya-local catalog or the offline charger packs, convert a descriptor in the browser and save only the selected pack to the base.</p>
      <p class="hint">Auto-detect chooses a built-in or custom profile from the DP keys in the last snapshot. Selecting a profile, including a browser pack, forces its state map. Save and reboot to apply the selection.</p>
      <p class="hint">The Control tab DP IDs override outgoing command DPs for built-in profiles. Browser packs and custom profiles define their own command mappings.</p>
      <label for="profiles_json">Custom profiles (JSON) <span class="def">optional, max 8192 bytes</span></label>
      <textarea id="profiles_json" name="profiles_json" maxlength="8192" spellcheck="false" autocapitalize="off" autocomplete="off">{{PROFILES_JSON}}</textarea>
      <p class="hint">The profile catalog is the recommended path for new devices. This editor remains available for compact manual profiles using the generic <code>entities</code> schema or the legacy <code>state</code>/<code>control</code> map. Up to four custom profiles. Save the JSON and reboot once to make new profile IDs available in the dropdown, then select one and save again. Leave the box empty to remove all custom profiles. Invalid JSON is rejected and the editor keeps your text. See <code>docs/tuya-device.md</code> for the schema and examples.</p>
    </section>

    <section class="tab-panel" id="tab-system" data-tab-panel="system" role="tabpanel">
      <h2>Setup access point</h2>
      <label for="setup_ap_ssid">Setup AP SSID <span class="def">default TuyaLoraBase-Setup</span></label>
      <input id="setup_ap_ssid" name="setup_ap_ssid" type="text" maxlength="32" required value="{{SETUP_AP_SSID}}" autocomplete="off">
      <p class="hint">Name of the fallback AP used when WiFi is unavailable.</p>
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
  if (!validTabs.includes(name)) name = 'wifi';
  tabButtons.forEach(tab => tab.classList.toggle('active', tab.dataset.tab === name));
  panels.forEach(panel => panel.classList.toggle('active', panel.dataset.tabPanel === name));
  if (history.replaceState) history.replaceState(null, '', '#' + name);
}

tabButtons.forEach(tab => tab.addEventListener('click', () => activateTab(tab.dataset.tab)));
const hash = location.hash ? location.hash.slice(1) : '';
activateTab(hash || document.body.dataset.activeTab || 'wifi');

if (form) {
  form.addEventListener('invalid', event => {
    const panel = event.target.closest('.tab-panel');
    if (panel) activateTab(panel.dataset.tabPanel);
  }, true);
}

async function scanWifi(button) {
  button.disabled = true;
  button.textContent = 'Scanning...';
  try {
    const response = await fetch('/scan', {cache: 'no-store'});
    const networks = await response.json();
    const list = document.getElementById('ssids');
    list.innerHTML = '';
    for (const network of networks) {
      const option = document.createElement('option');
      option.value = network.ssid;
      option.label = network.rssi + ' dBm';
      list.appendChild(option);
    }
    button.textContent = networks.length ? 'Scan again' : 'No networks';
  } catch (error) {
    button.textContent = 'Scan failed';
  }
  button.disabled = false;
}
</script>
</body>
</html>)HTML";

const char STATUS_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Tuya LoRa Bridge — Base status</title>
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
  .cards { display: grid; grid-template-columns: repeat(auto-fit, minmax(150px, 1fr)); gap: 10px; margin: 16px 0; }
  .card { border: 1px solid #8884; border-radius: 10px; padding: 12px; }
  .card .label { display: block; font-size: .8rem; opacity: .7; }
  .card strong { font-size: 1.15rem; }
  .telemetry { width: 100%; border-collapse: collapse; }
  .telemetry th, .telemetry td { text-align: left; padding: 8px 6px; border-bottom: 1px solid #8883; }
  .telemetry th { width: 45%; font-weight: 500; opacity: .8; }
  pre { white-space: pre-wrap; word-break: break-word; background: #8881; padding: 12px; border-radius: 8px; max-height: 320px; overflow: auto; }
</style>
</head>
<body>
<main>
  <nav class="nav"><a href="/settings">Settings</a><a class="active" href="/">Status</a></nav>
  <h1>Tuya LoRa Bridge — Base status</h1>
  <p class="muted">Firmware {{FW_VERSION}} · <span id="updated">loading...</span></p>

  <div class="status">
    <div><strong>Base:</strong> <span id="base-link">--</span></div>
    <div><strong>MQTT:</strong> <span id="mqtt-link">--</span></div>
  </div>

  <div class="cards">
    <div class="card"><span class="label">Remote link</span><strong id="online">--</strong></div>
    <div class="card"><span class="label">Charger state</span><strong id="state">--</strong></div>
    <div class="card"><span class="label">Power</span><strong id="power">--</strong></div>
    <div class="card"><span class="label">Session energy</span><strong id="session-energy">--</strong></div>
    <div class="card"><span class="label">Temperature</span><strong id="temperature">--</strong></div>
    <div class="card"><span class="label">Current limit</span><strong id="current-limit">--</strong></div>
    <div class="card"><span class="label">Profile</span><strong id="profile">--</strong></div>
    <div class="card"><span class="label">Last seen</span><strong id="age">--</strong></div>
    <div class="card"><span class="label">LoRa</span><strong id="lora">--</strong></div>
  </div>

  <h2>Charger telemetry</h2>
  <p class="muted">These fixed fields are the legacy charger state model. Generic profile entities are published on per-entity MQTT topics; see <code>docs/home-assistant.md</code>.</p>
  <table class="telemetry">
    <tr><th>Status</th><td id="t-status">--</td></tr>
    <tr><th>Active profile</th><td id="t-profile">--</td></tr>
    <tr><th>Selected profile</th><td id="t-profile-selected">--</td></tr>
    <tr><th>Detected profile</th><td id="t-profile-detected">--</td></tr>
    <tr><th>Profile match</th><td id="t-profile-match">--</td></tr>
    <tr><th>Profile entities</th><td id="t-profile-entities">--</td></tr>
    <tr><th>Profile note</th><td id="t-profile-note">--</td></tr>
    <tr><th>Fault flags</th><td id="t-fault">--</td></tr>
    <tr><th>Plugged</th><td id="t-plugged">--</td></tr>
    <tr><th>Charging</th><td id="t-charging">--</td></tr>
    <tr><th>Charge session</th><td id="t-session">--</td></tr>
    <tr><th>Charge control (DP 124)</th><td id="t-control">--</td></tr>
    <tr><th>Waiting for RFID</th><td id="t-rfid-wait">--</td></tr>
    <tr><th>RFID active</th><td id="t-rfid">--</td></tr>
    <tr><th>Schedule active</th><td id="t-schedule">--</td></tr>
    <tr><th>Current limit</th><td id="t-current-limit">--</td></tr>
    <tr><th>Voltage L1</th><td id="t-v1">--</td></tr>
    <tr><th>Voltage L2</th><td id="t-v2">--</td></tr>
    <tr><th>Voltage L3</th><td id="t-v3">--</td></tr>
    <tr><th>Current L1</th><td id="t-a1">--</td></tr>
    <tr><th>Current L2</th><td id="t-a2">--</td></tr>
    <tr><th>Current L3</th><td id="t-a3">--</td></tr>
    <tr><th>Power</th><td id="t-power">--</td></tr>
    <tr><th>Total energy</th><td id="t-energy">--</td></tr>
    <tr><th>Session energy</th><td id="t-session-energy">--</td></tr>
    <tr><th>Temperature</th><td id="t-temperature">--</td></tr>
    <tr><th>Charge time</th><td id="t-charge-time">--</td></tr>
    <tr><th>LoRa RSSI</th><td id="t-rssi">--</td></tr>
    <tr><th>LoRa SNR</th><td id="t-snr">--</td></tr>
    <tr><th>Last seen</th><td id="t-age">--</td></tr>
  </table>

  <h2>Raw DP JSON</h2>
  <details><summary>Show raw JSON</summary><pre id="dp-json">--</pre></details>
</main>
<script>
const $ = id => document.getElementById(id);
function set(id, text) { const el = $(id); if (el) el.textContent = text; }
function fmt(value, digits, suffix) {
  if (value === null || value === undefined || Number.isNaN(Number(value))) return '--';
  return Number(value).toFixed(digits) + (suffix || '');
}
function yesNo(value) { return value ? 'yes' : 'no'; }
function duration(seconds) {
  if (seconds === null || seconds === undefined || seconds < 0) return '--';
  const s = Math.floor(seconds);
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  const r = s % 60;
  return (h ? h + 'h ' : '') + (m ? m + 'm ' : '') + r + 's';
}
function ago(seconds) {
  if (seconds === null || seconds === undefined || seconds < 0) return '--';
  if (seconds < 60) return Math.floor(seconds) + 's ago';
  if (seconds < 3600) return Math.floor(seconds / 60) + 'm ago';
  return Math.floor(seconds / 3600) + 'h ago';
}
async function refresh() {
  try {
    const response = await fetch('/api/status', {cache: 'no-store'});
    if (!response.ok) throw new Error('HTTP ' + response.status);
    const d = await response.json();
    set('updated', 'updated ' + new Date().toLocaleTimeString());
    set('base-link', d.wifi_connected ? 'connected to ' + d.base_ip + ' (' + d.wifi_rssi + ' dBm)' : 'WiFi not connected');
    set('mqtt-link', d.mqtt_connected ? 'connected' : 'not connected');
    set('online', d.online ? 'online' : (d.have_seen ? 'offline' : 'waiting'));
    set('state', d.have_state ? d.status : 'unknown');
    set('power', d.have_state ? fmt(d.power_kw, 2, ' kW') : '--');
    set('session-energy', d.have_state ? fmt(d.session_energy_kwh, 2, ' kWh') : '--');
    set('temperature', d.have_state ? fmt(d.temperature_c, 1, ' °C') : '--');
    set('current-limit', d.have_state ? d.current_limit + ' A' : '--');
    set('profile', d.profile || 'unknown');
    set('age', d.have_seen ? ago(d.age_s) : 'never');
    set('lora', d.have_seen ? d.rssi + ' dBm / ' + fmt(d.snr, 1) + ' dB' : '--');
    set('t-status', d.have_state ? d.status : 'unknown');
    set('t-profile', d.profile || 'unknown');
    set('t-profile-selected', d.profile_selected || 'auto');
    set('t-profile-detected', d.profile_detected || 'unknown');
    set('t-profile-match', d.have_state ? yesNo(d.profile_match) : 'waiting for snapshot');
    set('t-profile-entities', d.profile_entities || 0);
    set('t-profile-note', d.profile_error || 'none');
    set('t-fault', d.have_state ? '0x' + Number(d.fault).toString(16).padStart(4, '0') : '--');
    set('t-plugged', d.have_state ? yesNo(d.plugged) : '--');
    set('t-charging', d.have_state ? yesNo(d.charging) : '--');
    set('t-session', d.have_state ? yesNo(d.charge_session_enabled) : '--');
    set('t-control', d.have_state ? yesNo(d.charge_control) : '--');
    set('t-rfid-wait', d.have_state ? yesNo(d.waiting_for_rfid) : '--');
    set('t-rfid', d.have_state ? yesNo(d.rfid) : '--');
    set('t-schedule', d.have_state ? yesNo(d.schedule) : '--');
    set('t-current-limit', d.have_state ? d.current_limit + ' A' : '--');
    set('t-v1', d.have_state ? fmt(d.voltage_l1, 1, ' V') : '--');
    set('t-v2', d.have_state ? fmt(d.voltage_l2, 1, ' V') : '--');
    set('t-v3', d.have_state ? fmt(d.voltage_l3, 1, ' V') : '--');
    set('t-a1', d.have_state ? fmt(d.current_l1, 1, ' A') : '--');
    set('t-a2', d.have_state ? fmt(d.current_l2, 1, ' A') : '--');
    set('t-a3', d.have_state ? fmt(d.current_l3, 1, ' A') : '--');
    set('t-power', d.have_state ? fmt(d.power_kw, 2, ' kW') : '--');
    set('t-energy', d.have_state ? fmt(d.energy_kwh, 2, ' kWh') : '--');
    set('t-session-energy', d.have_state ? fmt(d.session_energy_kwh, 2, ' kWh') : '--');
    set('t-temperature', d.have_state ? fmt(d.temperature_c, 1, ' °C') : '--');
    set('t-charge-time', d.have_state ? duration(d.charge_seconds) : '--');
    set('t-rssi', d.have_seen ? d.rssi + ' dBm' : '--');
    set('t-snr', d.have_seen ? fmt(d.snr, 1, ' dB') : '--');
    set('t-age', d.have_seen ? ago(d.age_s) : 'never');
    set('dp-json', d.dp_json || '(none)');
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
<title>Tuya LoRa Bridge — Base</title>
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
      // Prevent user text from being re-expanded as a template placeholder.
      case '{': out += "&#123;"; break;
      case '}': out += "&#125;"; break;
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

const FieldSpec BASE_ADVANCED_FIELDS[] = {
    // MQTT client
    {"mqtt_keepalive_s", "Keepalive (s)", "mqtt", FIELD_U16,
     offsetof(BaseSettings, mqttKeepaliveS), 0, 5, 3600, "1", 0, "30",
     nullptr},
    {"mqtt_socket_timeout_s", "Socket timeout (s)", "mqtt", FIELD_U16,
     offsetof(BaseSettings, mqttSocketTimeoutS), 0, 1, 60, "1", 0, "5",
     nullptr},
    {"mqtt_buffer_size", "Buffer size (bytes)", "mqtt", FIELD_U16,
     offsetof(BaseSettings, mqttBufferSize), 0, 256, 8192, "64", 0, "2048",
     nullptr},

    // LoRa radio
    {"lora_frequency_mhz", "Frequency (MHz)", "lora", FIELD_FLOAT,
     offsetof(BaseSettings, loraFrequencyMhz), 0, 137, 1020, "0.1", 1, "868.1",
     "Must match the remote node."},
    {"lora_sync_word", "Sync word", "lora", FIELD_HEX8,
     offsetof(BaseSettings, loraSyncWord), 0, 0, 255, "1", 0, "0xA6",
     "Must match the remote node."},
    {"lora_tx_power", "TX power (dBm)", "lora", FIELD_I8,
     offsetof(BaseSettings, loraTxPower), 0, -1, 20, "1", 0, "14", nullptr},
    {"lora_sf", "Spreading factor", "lora", FIELD_U8,
     offsetof(BaseSettings, loraSf), 0, 6, 12, "1", 0, "7", nullptr},
    {"lora_bandwidth_hz", "Bandwidth (Hz)", "lora", FIELD_U32,
     offsetof(BaseSettings, loraBandwidthHz), 0, 7800, 500000, "100", 0,
     "125000", nullptr},
    {"lora_coding_rate", "Coding rate (4/N)", "lora", FIELD_U8,
     offsetof(BaseSettings, loraCodingRate), 0, 5, 8, "1", 0, "5", nullptr},
    {"lora_preamble", "Preamble length", "lora", FIELD_U16,
     offsetof(BaseSettings, loraPreamble), 0, 6, 65535, "1", 0, "8", nullptr},

    // MQTT topics
    {"mqtt_topic_state", "State topic", "topics", FIELD_TEXT,
     offsetof(BaseSettings, mqttTopicState), sizeof(settings.mqttTopicState), 0,
     0, "1", 0, MQTT_TOPIC_STATE, nullptr},
    {"mqtt_topic_link", "LoRa link topic", "topics", FIELD_TEXT,
     offsetof(BaseSettings, mqttTopicLink), sizeof(settings.mqttTopicLink), 0, 0,
     "1", 0, MQTT_TOPIC_LINK, nullptr},
    {"mqtt_topic_availability", "Availability topic", "topics", FIELD_TEXT,
     offsetof(BaseSettings, mqttTopicAvailability),
     sizeof(settings.mqttTopicAvailability), 0, 0, "1", 0,
     MQTT_TOPIC_AVAILABILITY, nullptr},
    {"mqtt_topic_result", "Command result topic", "topics", FIELD_TEXT,
     offsetof(BaseSettings, mqttTopicResult), sizeof(settings.mqttTopicResult),
     0, 0, "1", 0, MQTT_TOPIC_RESULT, nullptr},
    {"mqtt_topic_debug", "Debug topic", "topics", FIELD_TEXT,
     offsetof(BaseSettings, mqttTopicDebug), sizeof(settings.mqttTopicDebug), 0,
     0, "1", 0, MQTT_TOPIC_DEBUG, nullptr},
    {"mqtt_topic_dps", "Raw DP topic", "topics", FIELD_TEXT,
     offsetof(BaseSettings, mqttTopicDps), sizeof(settings.mqttTopicDps), 0, 0,
     "1", 0, MQTT_TOPIC_DPS, nullptr},
    {"mqtt_topic_cmd_charge", "Charge command topic", "topics", FIELD_TEXT,
     offsetof(BaseSettings, mqttTopicCmdCharge),
     sizeof(settings.mqttTopicCmdCharge), 0, 0, "1", 0,
     MQTT_TOPIC_CMD_CHARGE, nullptr},
    {"mqtt_topic_cmd_start", "Start command topic", "topics", FIELD_TEXT,
     offsetof(BaseSettings, mqttTopicCmdStart),
     sizeof(settings.mqttTopicCmdStart), 0, 0, "1", 0,
     MQTT_TOPIC_CMD_START, nullptr},
    {"mqtt_topic_cmd_stop", "Stop command topic", "topics", FIELD_TEXT,
     offsetof(BaseSettings, mqttTopicCmdStop),
     sizeof(settings.mqttTopicCmdStop), 0, 0, "1", 0,
     MQTT_TOPIC_CMD_STOP, nullptr},
    {"mqtt_topic_cmd_current", "Current limit topic", "topics", FIELD_TEXT,
     offsetof(BaseSettings, mqttTopicCmdCurrent),
     sizeof(settings.mqttTopicCmdCurrent), 0, 0, "1", 0,
     MQTT_TOPIC_CMD_CURRENT, nullptr},
    {"mqtt_topic_cmd_delay", "Delay command topic", "topics", FIELD_TEXT,
     offsetof(BaseSettings, mqttTopicCmdDelay),
     sizeof(settings.mqttTopicCmdDelay), 0, 0, "1", 0,
     MQTT_TOPIC_CMD_DELAY, nullptr},
    {"mqtt_topic_cmd_mode", "Mode command topic", "topics", FIELD_TEXT,
     offsetof(BaseSettings, mqttTopicCmdMode),
     sizeof(settings.mqttTopicCmdMode), 0, 0, "1", 0,
     MQTT_TOPIC_CMD_MODE, nullptr},
    {"mqtt_topic_cmd_query", "Query command topic", "topics", FIELD_TEXT,
     offsetof(BaseSettings, mqttTopicCmdQuery),
     sizeof(settings.mqttTopicCmdQuery), 0, 0, "1", 0,
     MQTT_TOPIC_CMD_QUERY, nullptr},

    // Home Assistant discovery
    {"ha_discovery_prefix", "Discovery prefix", "device", FIELD_TEXT,
     offsetof(BaseSettings, haDiscoveryPrefix),
     sizeof(settings.haDiscoveryPrefix), 0, 0, "1", 0, HA_DISCOVERY_PREFIX,
     nullptr},
    {"ha_device_id", "Device identifier", "device", FIELD_TEXT,
     offsetof(BaseSettings, haDeviceId), sizeof(settings.haDeviceId), 0, 0, "1",
     0, HA_DEVICE_ID,
     "Keep unchanged so Home Assistant updates the existing device."},

    // Timing
    {"offline_after_ms", "Offline after (ms)", "timing", FIELD_U32,
     offsetof(BaseSettings, offlineAfterMs), 0, 1000, 86400000, "1000", 0,
     "600000", nullptr},
    {"dp_query_interval_ms", "DP query interval (ms)", "timing", FIELD_U32,
     offsetof(BaseSettings, dpQueryIntervalMs), 0, 1000, 3600000, "1000", 0,
     "60000", nullptr},
    {"dp_query_timeout_ms", "DP query timeout (ms)", "timing", FIELD_U32,
     offsetof(BaseSettings, dpQueryTimeoutMs), 0, 1000, 300000, "1000", 0,
     "30000", nullptr},
    {"dp_chunk_timeout_ms", "Chunk ACK timeout (ms)", "timing", FIELD_U32,
     offsetof(BaseSettings, dpChunkTimeoutMs), 0, 100, 60000, "100", 0, "1200",
     nullptr},
    {"dp_ack_retries", "DP ACK retries", "timing", FIELD_U8,
     offsetof(BaseSettings, dpAckRetries), 0, 0, 20, "1", 0, "6", nullptr},
    {"wifi_retry_ms", "WiFi retry interval (ms)", "timing", FIELD_U32,
     offsetof(BaseSettings, wifiRetryMs), 0, 1000, 600000, "1000", 0, "10000",
     nullptr},
    {"mqtt_reconnect_ms", "MQTT reconnect interval (ms)", "timing", FIELD_U32,
     offsetof(BaseSettings, mqttReconnectMs), 0, 1000, 600000, "1000", 0,
     "5000", nullptr},
    {"display_refresh_ms", "Display refresh (ms)", "timing", FIELD_U32,
     offsetof(BaseSettings, displayRefreshMs), 0, 100, 10000, "100", 0, "500",
     nullptr},
    {"display_on_ms", "Display on time (ms)", "timing", FIELD_U32,
     offsetof(BaseSettings, displayOnMs), 0, 1000, 86400000, "1000", 0,
     "600000", nullptr},
    {"splash_ms", "Splash duration (ms)", "timing", FIELD_U32,
     offsetof(BaseSettings, splashMs), 0, 0, 60000, "500", 0, "3000", nullptr},

    // Control
    {"enable_commands", "Enable control commands", "control", FIELD_BOOL,
     offsetof(BaseSettings, enableCommands), 0, 0, 0, "1", 0, "enabled",
     nullptr},
    {"min_current", "Minimum current (A)", "control", FIELD_U8,
     offsetof(BaseSettings, minCurrent), 0, 1, 63, "1", 0, "6", nullptr},
    {"max_current", "Maximum current (A)", "control", FIELD_U8,
     offsetof(BaseSettings, maxCurrent), 0, 1, 63, "1", 0, "32", nullptr},
    {"min_delay_h", "Minimum delay (h)", "control", FIELD_U8,
     offsetof(BaseSettings, minDelayH), 0, 0, 24, "1", 0, "0", nullptr},
    {"max_delay_h", "Maximum delay (h)", "control", FIELD_U8,
     offsetof(BaseSettings, maxDelayH), 0, 0, 24, "1", 0, "15", nullptr},
    {"dp_single_charge_id", "Single-phase charge DP", "control", FIELD_U16,
     offsetof(BaseSettings, dpSingleChargeId), 0, 1, 999, "1", 0, "112",
     nullptr},
    {"dp_single_current_id", "Single-phase current DP", "control", FIELD_U16,
     offsetof(BaseSettings, dpSingleCurrentId), 0, 1, 999, "1", 0, "102",
     nullptr},
    {"dp_single_delay_id", "Single-phase delay DP", "control", FIELD_U16,
     offsetof(BaseSettings, dpSingleDelayId), 0, 1, 999, "1", 0, "104",
     nullptr},
    {"dp_single_mode_id", "Single-phase mode DP", "control", FIELD_U16,
     offsetof(BaseSettings, dpSingleModeId), 0, 1, 999, "1", 0, "14",
     nullptr},
    {"dp_three_charge_id", "Three-phase charge DP", "control", FIELD_U16,
     offsetof(BaseSettings, dpThreeChargeId), 0, 1, 999, "1", 0, "124",
     nullptr},
    {"dp_three_current_id", "Three-phase current DP", "control", FIELD_U16,
     offsetof(BaseSettings, dpThreeCurrentId), 0, 1, 999, "1", 0, "115",
     nullptr},
    {"dp_three_delay_id", "Three-phase delay DP", "control", FIELD_U16,
     offsetof(BaseSettings, dpThreeDelayId), 0, 1, 999, "1", 0, "118",
     nullptr},
    {"dp_three_mode_id", "Three-phase mode DP", "control", FIELD_U16,
     offsetof(BaseSettings, dpThreeModeId), 0, 1, 999, "1", 0, "14", nullptr},
    {"dp_obz2_charge_id", "OBZ2 charge DP", "control", FIELD_U16,
     offsetof(BaseSettings, dpObz2ChargeId), 0, 1, 999, "1", 0, "124",
     nullptr},
    {"dp_obz2_current_id", "OBZ2 current DP", "control", FIELD_U16,
     offsetof(BaseSettings, dpObz2CurrentId), 0, 1, 999, "1", 0, "115",
     nullptr},

    // System
    {"web_ui_port", "Web UI port", "system", FIELD_U16,
     offsetof(BaseSettings, webUiPort), 0, 1, 65535, "1", 0, "80", nullptr},
    {"wifi_setup_fallback_ms", "Setup AP fallback (ms)", "system", FIELD_U32,
     offsetof(BaseSettings, wifiSetupFallbackMs), 0, 1000, 3600000, "1000", 0,
     "20000", nullptr},
    {"sd_firmware_file", "SD firmware filename", "system", FIELD_TEXT,
     offsetof(BaseSettings, sdFirmwareFile), sizeof(settings.sdFirmwareFile), 0,
     0, "1", 0, SD_FIRMWARE_FILE, "Must start with /."},
    {"sd_update_wait_ms", "SD update countdown (ms)", "system", FIELD_U32,
     offsetof(BaseSettings, sdUpdateWaitMs), 0, 1000, 60000, "1000", 0,
     "10000", nullptr},
};

String fieldValue(const BaseSettings &s, const FieldSpec &f) {
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
      return String(*reinterpret_cast<const float *>(p), static_cast<unsigned int>(f.decimals));
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
  if (strcmp(name, "mqtt_keepalive_s") == 0)
    return "Seconds between MQTT keepalive pings. Raise it if the broker drops idle connections.";
  if (strcmp(name, "mqtt_socket_timeout_s") == 0)
    return "Seconds to wait for broker reads or writes before treating the link as dead.";
  if (strcmp(name, "mqtt_buffer_size") == 0)
    return "Maximum MQTT packet size. Increase it if discovery or DP messages are truncated.";
  if (strcmp(name, "lora_frequency_mhz") == 0)
    return "Radio frequency in MHz. Must match the remote node.";
  if (strcmp(name, "lora_sync_word") == 0)
    return "LoRa sync word. Must match the remote node.";
  if (strcmp(name, "lora_tx_power") == 0)
    return "Transmit power in dBm. Higher reaches farther but uses more power; -1 uses the radio default.";
  if (strcmp(name, "lora_sf") == 0)
    return "Spreading factor 6-12. Higher values increase range and airtime; lower values are faster.";
  if (strcmp(name, "lora_bandwidth_hz") == 0)
    return "LoRa bandwidth in Hz. Wider is faster but less sensitive; 125000 is the usual balance.";
  if (strcmp(name, "lora_coding_rate") == 0)
    return "Forward error correction from 4/5 to 4/8. Higher values add redundancy and airtime.";
  if (strcmp(name, "lora_preamble") == 0)
    return "Preamble symbols before each packet. Longer preambles help the receiver lock on.";
  if (strcmp(name, "mqtt_topic_state") == 0)
    return "Topic where the base publishes charger state JSON.";
  if (strcmp(name, "mqtt_topic_availability") == 0)
    return "Topic where the base publishes online and offline status.";
  if (strcmp(name, "mqtt_topic_result") == 0)
    return "Topic where command results and errors are published.";
  if (strcmp(name, "mqtt_topic_debug") == 0)
    return "Optional topic for debug messages.";
  if (strcmp(name, "mqtt_topic_dps") == 0)
    return "Topic where raw Tuya DP snapshots are published.";
  if (strcmp(name, "mqtt_topic_cmd_charge") == 0)
    return "Topic that accepts charge on/off commands.";
  if (strcmp(name, "mqtt_topic_cmd_start") == 0)
    return "Topic that starts a charging session.";
  if (strcmp(name, "mqtt_topic_cmd_stop") == 0)
    return "Topic that stops a charging session.";
  if (strcmp(name, "mqtt_topic_cmd_current") == 0)
    return "Topic that sets the charging current limit.";
  if (strcmp(name, "mqtt_topic_cmd_delay") == 0)
    return "Topic that sets the charge start delay.";
  if (strcmp(name, "mqtt_topic_cmd_mode") == 0)
    return "Topic that selects the charger mode.";
  if (strcmp(name, "mqtt_topic_cmd_query") == 0)
    return "Topic that requests an immediate DP snapshot.";
  if (strcmp(name, "ha_discovery_prefix") == 0)
    return "MQTT discovery prefix used by Home Assistant. Usually homeassistant.";
  if (strcmp(name, "ha_device_id") == 0)
    return "Stable Home Assistant device identifier. Keep it unchanged when renaming the device.";
  if (strcmp(name, "offline_after_ms") == 0)
    return "Mark the charger offline when no state arrives within this time.";
  if (strcmp(name, "dp_query_interval_ms") == 0)
    return "How often the base asks the charger for a fresh DP snapshot.";
  if (strcmp(name, "dp_query_timeout_ms") == 0)
    return "How long to wait for a complete DP snapshot before giving up.";
  if (strcmp(name, "dp_chunk_timeout_ms") == 0)
    return "How long to wait for each LoRa chunk acknowledgement.";
  if (strcmp(name, "dp_ack_retries") == 0)
    return "How many times unacknowledged DP chunks are resent.";
  if (strcmp(name, "wifi_retry_ms") == 0)
    return "How often the base retries a failed WiFi connection.";
  if (strcmp(name, "mqtt_reconnect_ms") == 0)
    return "How often the base retries a failed MQTT connection.";
  if (strcmp(name, "display_refresh_ms") == 0)
    return "How often the OLED status screen is redrawn.";
  if (strcmp(name, "display_on_ms") == 0)
    return "Time after boot before the OLED turns off. Set it high to keep the display on.";
  if (strcmp(name, "splash_ms") == 0)
    return "How long the boot splash stays visible.";
  if (strcmp(name, "enable_commands") == 0)
    return "Allow MQTT commands to change charger state. Disable for read-only monitoring.";
  if (strcmp(name, "min_current") == 0)
    return "Lowest current the UI and Home Assistant may request.";
  if (strcmp(name, "max_current") == 0)
    return "Highest current the UI and Home Assistant may request. Keep within the charger rating.";
  if (strcmp(name, "min_delay_h") == 0)
    return "Lowest charge start delay allowed.";
  if (strcmp(name, "max_delay_h") == 0)
    return "Highest charge start delay allowed.";
  if (strcmp(name, "dp_single_charge_id") == 0)
    return "Tuya DP ID for single-phase charge on/off.";
  if (strcmp(name, "dp_single_current_id") == 0)
    return "Tuya DP ID for single-phase current limit.";
  if (strcmp(name, "dp_single_delay_id") == 0)
    return "Tuya DP ID for single-phase start delay.";
  if (strcmp(name, "dp_single_mode_id") == 0)
    return "Tuya DP ID for single-phase mode.";
  if (strcmp(name, "dp_three_charge_id") == 0)
    return "Tuya DP ID for three-phase charge on/off.";
  if (strcmp(name, "dp_three_current_id") == 0)
    return "Tuya DP ID for three-phase current limit.";
  if (strcmp(name, "dp_three_delay_id") == 0)
    return "Tuya DP ID for three-phase start delay.";
  if (strcmp(name, "dp_three_mode_id") == 0)
    return "Tuya DP ID for three-phase mode.";
  if (strcmp(name, "dp_obz2_charge_id") == 0)
    return "Tuya DP ID for OBZ2 charge on/off.";
  if (strcmp(name, "dp_obz2_current_id") == 0)
    return "Tuya DP ID for OBZ2 current limit.";
  if (strcmp(name, "web_ui_port") == 0)
    return "HTTP port for this settings page. After changing it, reconnect on the new port.";
  if (strcmp(name, "wifi_setup_fallback_ms") == 0)
    return "How long to wait for WiFi before starting the setup AP.";
  if (strcmp(name, "sd_firmware_file") == 0)
    return "Path of the firmware file on the SD card. Must start with /.";
  if (strcmp(name, "sd_update_wait_ms") == 0)
    return "Countdown before an SD-card update starts. Remove the card to abort.";
  return nullptr;
}

String renderField(const BaseSettings &s, const FieldSpec &f) {
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

String renderFields(const BaseSettings &s, const char *tab) {
  String out;
  for (const auto &field : BASE_ADVANCED_FIELDS) {
    if (strcmp(field.tab, tab) == 0) out += renderField(s, field);
  }
  return out;
}

bool applyField(BaseSettings &s, const FieldSpec &f, String &error) {
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
                String(f.min, static_cast<unsigned int>(f.decimals)) + " and " +
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
                String(f.min, static_cast<unsigned int>(f.decimals)) + " and " +
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
                String(f.min, static_cast<unsigned int>(f.decimals)) + " and " +
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

bool applyAdvancedFields(BaseSettings &s, String &error, String &tab) {
  for (const auto &field : BASE_ADVANCED_FIELDS) {
    if (!applyField(s, field, error)) {
      tab = field.tab;
      return false;
    }
  }
  return true;
}

String setupApChannelOptions(const BaseSettings &s) {
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

String ipText() {
  String ip;
  if (setupApActive) {
    ip = WiFi.softAPIP().toString();
  } else if (WiFi.status() == WL_CONNECTED) {
    ip = WiFi.localIP().toString();
  } else {
    ip = "-";
  }
  return ip + ":" + String(settings.webUiPort);
}

String wifiStatusText() {
  if (setupApActive) {
    return String("setup AP <b>") + htmlEscape(settings.setupApSsid) +
           "</b> at " + WiFi.softAPIP().toString();
  }
  if (WiFi.status() == WL_CONNECTED) {
    return String("connected to <b>") + htmlEscape(settings.wifiSsid) +
           "</b> at " + WiFi.localIP().toString() + " (" +
           String(WiFi.RSSI()) + " dBm)";
  }
  if (wifiConfigured()) {
    return String("connecting to <b>") + htmlEscape(settings.wifiSsid) + "</b>";
  }
  return "not configured";
}

String mqttStatusText() {
  if (!mqttConfigured()) return "not configured";
  if (WiFi.status() != WL_CONNECTED) return "waiting for WiFi";
  const String target =
      String(htmlEscape(settings.mqttHost)) + ":" + String(settings.mqttPort);
  if (mqtt.connected()) return String("<b>connected</b> to ") + target;
  return String("connecting to ") + target;
}

String pskStatusText(const BaseSettings &s) {
  return isValidPsk(s.loraPsk) ? "set" : "not set";
}

String pskWarningText(const BaseSettings &s) {
  if (isValidPsk(s.loraPsk)) return "";
  return "<div class=\"warn\">LoRa PSK is not set. The base cannot talk to the "
         "charger until both nodes have the same 32-character hex key.</div>";
}

String pskPlaceholder(const BaseSettings &s) {
  return isValidPsk(s.loraPsk) ? "leave blank to keep current"
                               : "32 hex characters";
}

String wifiPasswordPlaceholder(const BaseSettings &s) {
  return s.wifiPassword[0] ? "leave blank to keep current"
                           : "8+ characters or empty";
}

String mqttPasswordPlaceholder(const BaseSettings &s) {
  return s.mqttPassword[0] ? "leave blank to keep current" : "optional";
}

struct TimezoneOption {
  const char *label;
  const char *value;
};

const TimezoneOption TIMEZONES[] = {
    {"UTC", "UTC0"},
    {"Europe/Sofia (EET/EEST)", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Athens (EET/EEST)", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Bucharest (EET/EEST)", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Helsinki (EET/EEST)", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Berlin (CET/CEST)", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Paris (CET/CEST)", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Madrid (CET/CEST)", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Rome (CET/CEST)", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Amsterdam (CET/CEST)", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/London (GMT/BST)", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Europe/Dublin (GMT/IST)", "GMT0IST,M3.5.0/1,M10.5.0"},
    {"Europe/Istanbul (TRT)", "TRT-3"},
    {"Europe/Moscow (MSK)", "MSK-3"},
    {"America/New_York (EST/EDT)", "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Chicago (CST/CDT)", "CST6CDT,M3.2.0,M11.1.0"},
    {"America/Denver (MST/MDT)", "MST7MDT,M3.2.0,M11.1.0"},
    {"America/Los_Angeles (PST/PDT)", "PST8PDT,M3.2.0,M11.1.0"},
    {"America/Sao_Paulo (BRT)", "<-03>3"},
    {"Asia/Dubai (GST)", "<+04>-4"},
    {"Asia/Kolkata (IST)", "IST-5:30"},
    {"Asia/Shanghai (CST)", "CST-8"},
    {"Asia/Tokyo (JST)", "JST-9"},
    {"Asia/Seoul (KST)", "KST-9"},
    {"Australia/Sydney (AEST/AEDT)", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Pacific/Auckland (NZST/NZDT)", "NZST-12NZDT,M9.5.0,M4.1.0/3"},
};

bool validTimezone(const char *tz) {
  if (!tz || !*tz) return false;
  for (const auto &option : TIMEZONES) {
    if (strcmp(tz, option.value) == 0) return true;
  }
  // Preserve an existing custom value from the old free-text field.
  return strcmp(tz, settings.timezone) == 0;
}

String timezoneOptions(const BaseSettings &s) {
  String out;
  bool found = false;
  for (const auto &option : TIMEZONES) {
    out += "<option value=\"";
    out += htmlEscape(option.value);
    out += "\"";
    if (strcmp(s.timezone, option.value) == 0) {
      out += " selected";
      found = true;
    }
    out += ">";
    out += htmlEscape(option.label);
    out += "</option>";
  }
  if (!found && s.timezone[0] != '\0') {
    out += "<option value=\"";
    out += htmlEscape(s.timezone);
    out += "\" selected>Custom: ";
    out += htmlEscape(s.timezone);
    out += "</option>";
  }
  return out;
}

void sendResult(const String &title, const String &detail) {
  String page = FPSTR(RESULT_HTML);
  page.replace("{{TITLE}}", htmlEscape(title.c_str()));
  page.replace("{{DETAIL}}", htmlEscape(detail.c_str()));
  server->sendHeader("Cache-Control", "no-store");
  server->send(200, "text/html", page);
}

void renderForm(const BaseSettings &s, const String &error, int status,
                const String &tab, const char *customJson = nullptr) {
  String page = FPSTR(BASE_HTML);
  page.replace("{{FW_VERSION}}", FW_VERSION);
  page.replace("{{IP}}", ipText());
  page.replace("{{ERROR}}", error.length() ? String("<div class=\"err\">") +
                                                htmlEscape(error.c_str()) +
                                                "</div>"
                                          : String(""));
  page.replace("{{WIFI_STATUS}}", wifiStatusText());
  page.replace("{{MQTT_STATUS}}", mqttStatusText());
  page.replace("{{PSK_STATUS}}", pskStatusText(s));
  page.replace("{{PSK_WARNING}}", pskWarningText(s));
  page.replace("{{ACTIVE_TAB}}", htmlEscape(tab.c_str()));
  page.replace("{{WIFI_SSID}}", htmlEscape(s.wifiSsid));
  page.replace("{{WIFI_PASSWORD_PLACEHOLDER}}", wifiPasswordPlaceholder(s));
  page.replace("{{MQTT_HOST}}", htmlEscape(s.mqttHost));
  page.replace("{{MQTT_PORT}}", String(s.mqttPort));
  page.replace("{{MQTT_USER}}", htmlEscape(s.mqttUser));
  page.replace("{{MQTT_PASSWORD_PLACEHOLDER}}", mqttPasswordPlaceholder(s));
  page.replace("{{PSK_PLACEHOLDER}}", pskPlaceholder(s));
  page.replace("{{HA_NAME}}", htmlEscape(s.haName));
  page.replace("{{TIMEZONE_OPTIONS}}", timezoneOptions(s));
  page.replace("{{FIELDS_MQTT}}", renderFields(s, "mqtt"));
  page.replace("{{FIELDS_TOPICS}}", renderFields(s, "topics"));
  page.replace("{{FIELDS_LORA}}", renderFields(s, "lora"));
  page.replace("{{FIELDS_HA}}", renderFields(s, "device"));
  page.replace("{{FIELDS_TIMING}}", renderFields(s, "timing"));
  page.replace("{{FIELDS_CONTROL}}", renderFields(s, "control"));
  page.replace("{{FIELDS_SYSTEM}}", renderFields(s, "system"));
  page.replace("{{SETUP_AP_SSID}}", htmlEscape(s.setupApSsid));
  page.replace("{{SETUP_AP_PASSWORD_PLACEHOLDER}}",
               "leave blank to keep current");
  page.replace("{{SETUP_AP_CHANNEL_OPTIONS}}", setupApChannelOptions(s));
  page.replace("{{PROFILE_ACTIVE}}", htmlEscape(profileActiveId()));
  page.replace("{{PROFILE_DETECTED}}", htmlEscape(profileDetectedId()));
  page.replace("{{PROFILE_MATCH}}",
               !haveState ? "waiting for snapshot"
                          : (profileMatched ? "yes" : "no"));
  if (profileError[0]) {
    page.replace("{{PROFILE_NOTE}}", htmlEscape(profileError));
  } else {
    page.replace("{{PROFILE_NOTE}}", "none");
  }
  page.replace("{{PROFILE_OPTIONS}}", profileOptions(s.profileId));
  page.replace("{{PROFILE_ENTITIES}}", String(profileEntityCount()));
  // Keep the JSON replacement last so profile text cannot be expanded as a
  // template placeholder.
  page.replace("{{PROFILES_JSON}}",
               htmlEscape(customJson ? customJson : profilesCustomJson()));
  server->sendHeader("Cache-Control", "no-store");
  server->send(status, "text/html", page);
}

void handleSettings() { renderForm(settings, "", 200, "wifi"); }

void handleStatus() {
  String page = FPSTR(STATUS_HTML);
  page.replace("{{FW_VERSION}}", FW_VERSION);
  server->sendHeader("Cache-Control", "no-store");
  server->send(200, "text/html", page);
}

void handleApiStatus() {
  String json;
  json.reserve(4096);
  const bool wifiUp = WiFi.status() == WL_CONNECTED;
  json += "{\"fw_version\":\"" FW_VERSION "\",";
  json += "\"base_ip\":\"";
  json += wifiUp ? WiFi.localIP().toString() : String("-");
  json += "\",\"wifi_connected\":";
  json += (wifiUp ? "true" : "false");
  json += ",\"wifi_rssi\":";
  json += String(wifiUp ? WiFi.RSSI() : 0);
  json += ",\"mqtt_connected\":";
  json += (mqtt.connected() ? "true" : "false");
  json += ",\"online\":";
  json += (remoteOnline ? "true" : "false");
  json += ",\"have_seen\":";
  json += (haveSeen ? "true" : "false");
  json += ",\"have_state\":";
  json += (haveState ? "true" : "false");
  json += ",\"age_s\":";
  json += String(haveSeen ? static_cast<long>((millis() - lastSeenMs) / 1000)
                          : -1L);
  json += ",\"rssi\":";
  json += String(lastRssi);
  json += ",\"snr\":";
  json += String(lastSnr, 1);
  json += ",\"status\":\"";
  json += statusName(lastStatus.status);
  json += "\",\"fault\":";
  json += String(lastStatus.faultFlags);
  json += ",\"plugged\":";
  json += (lastStatus.plugged() ? "true" : "false");
  json += ",\"charging\":";
  json += (lastStatus.charging() ? "true" : "false");
  json += ",\"charge_session_enabled\":";
  json += (lastStatus.chargeSessionEnabled() ? "true" : "false");
  json += ",\"charge_control\":";
  json += (lastStatus.chargeEnabled ? "true" : "false");
  json += ",\"waiting_for_rfid\":";
  json += (lastStatus.waitingForRfid ? "true" : "false");
  json += ",\"rfid\":";
  json += (lastStatus.rfidActive ? "true" : "false");
  json += ",\"schedule\":";
  json += (lastStatus.scheduleActive ? "true" : "false");
  json += ",\"current_limit\":";
  json += String(lastStatus.currentLimit);
  json += ",\"voltage_l1\":";
  json += String(lastStatus.voltageL1dV / 10.0f, 1);
  json += ",\"voltage_l2\":";
  json += String(lastStatus.voltageL2dV / 10.0f, 1);
  json += ",\"voltage_l3\":";
  json += String(lastStatus.voltageL3dV / 10.0f, 1);
  json += ",\"current_l1\":";
  json += String(lastStatus.currentL1dA / 10.0f, 1);
  json += ",\"current_l2\":";
  json += String(lastStatus.currentL2dA / 10.0f, 1);
  json += ",\"current_l3\":";
  json += String(lastStatus.currentL3dA / 10.0f, 1);
  json += ",\"power_kw\":";
  json += String(lastStatus.powerW / 1000.0f, 2);
  json += ",\"energy_kwh\":";
  json += String(lastStatus.energyWh / 1000.0f, 2);
  json += ",\"session_energy_kwh\":";
  json += String(lastStatus.sessionWh / 1000.0f, 2);
  json += ",\"temperature_c\":";
  json += String(lastStatus.tempDc / 10.0f, 1);
  json += ",\"charge_seconds\":";
  json += String(lastStatus.chargeSeconds);
  json += ",\"profile\":\"";
  json += jsonEscape(profileActiveId());
  json += "\",\"profile_selected\":\"";
  json += jsonEscape(settings.profileId);
  json += "\",\"profile_detected\":\"";
  json += jsonEscape(profileDetectedId());
  json += "\",\"profile_match\":";
  json += (profileMatched ? "true" : "false");
  json += ",\"profile_entities\":";
  json += String(profileEntityCount());
  json += ",\"profile_error\":\"";
  json += jsonEscape(profileError);
  json += "\",\"dp_json\":\"";
  json += jsonEscape(dpJson);
  json += "\"}";
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

bool validate(const BaseSettings &s, String &error, String &tab) {
  if (s.wifiSsid[0] == '\0') {
    error = "WiFi SSID is required.";
    tab = "wifi";
    return false;
  }
  if (strlen(s.wifiSsid) > 32) {
    error = "WiFi SSID must be 32 characters or fewer.";
    tab = "wifi";
    return false;
  }
  if (strcmp(s.wifiSsid, "your-home-ssid") == 0) {
    error = "WiFi SSID is still the example placeholder.";
    tab = "wifi";
    return false;
  }
  if (s.wifiPassword[0] != '\0' &&
      (strlen(s.wifiPassword) < 8 || strlen(s.wifiPassword) > 63)) {
    error = "WiFi password must be 8-63 characters, or empty for an open "
            "network.";
    tab = "wifi";
    return false;
  }
  if (s.mqttHost[0] == '\0') {
    error = "MQTT host is required.";
    tab = "mqtt";
    return false;
  }
  if (s.mqttPort == 0) {
    error = "MQTT port must be between 1 and 65535.";
    tab = "mqtt";
    return false;
  }
  if (strcmp(s.mqttPassword, "replace-with-a-dedicated-mqtt-password") == 0) {
    error = "MQTT password is still the example placeholder.";
    tab = "mqtt";
    return false;
  }
  if (!isValidPsk(s.loraPsk)) {
    error = "LoRa PSK must be exactly 32 hex characters.";
    tab = "lora";
    return false;
  }
  if (s.haName[0] == '\0') {
    error = "Home Assistant device name is required.";
    tab = "device";
    return false;
  }
  if (!validTimezone(s.timezone)) {
    error = "Select a timezone.";
    tab = "device";
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
  if (s.minCurrent > s.maxCurrent) {
    error = "Minimum current cannot be greater than maximum current.";
    tab = "control";
    return false;
  }
  if (s.minDelayH > s.maxDelayH) {
    error = "Minimum delay cannot be greater than maximum delay.";
    tab = "control";
    return false;
  }
  return true;
}

void handleSave() {
  if (!server->hasArg("wifi_ssid")) {
    server->send(400, "text/plain", "Bad request");
    return;
  }

  BaseSettings next = settings;
  copyArg("profile_id", next.profileId, sizeof(next.profileId), false);
  const String customJson = server->hasArg("profiles_json")
                                ? server->arg("profiles_json")
                                : String(profilesCustomJson());
  copyArg("wifi_ssid", next.wifiSsid, sizeof(next.wifiSsid), false);
  copyArg("wifi_password", next.wifiPassword, sizeof(next.wifiPassword), true);
  if (server->hasArg("clear_wifi_password")) next.wifiPassword[0] = '\0';

  copyArg("mqtt_host", next.mqttHost, sizeof(next.mqttHost), false);
  copyArg("mqtt_user", next.mqttUser, sizeof(next.mqttUser), false);
  copyArg("mqtt_password", next.mqttPassword, sizeof(next.mqttPassword), true);
  if (server->hasArg("clear_mqtt_password")) next.mqttPassword[0] = '\0';

  const long port = server->arg("mqtt_port").toInt();
  if (port < 1 || port > 65535) {
    renderForm(next, "MQTT port must be between 1 and 65535.", 400, "mqtt",
               customJson.c_str());
    return;
  }
  next.mqttPort = static_cast<uint16_t>(port);

  copyArg("lora_psk", next.loraPsk, sizeof(next.loraPsk), true);
  copyArg("ha_name", next.haName, sizeof(next.haName), false);
  copyArg("timezone", next.timezone, sizeof(next.timezone), false);

  copyArg("setup_ap_ssid", next.setupApSsid, sizeof(next.setupApSsid), false);
  copyArg("setup_ap_password", next.setupApPassword,
          sizeof(next.setupApPassword), true);
  const long apChannel = server->arg("setup_ap_channel").toInt();
  if (apChannel < 1 || apChannel > 13) {
    renderForm(next, "Setup AP channel must be between 1 and 13.", 400,
               "system", customJson.c_str());
    return;
  }
  next.setupApChannel = static_cast<uint8_t>(apChannel);

  String error;
  String tab = "system";
  if (!applyAdvancedFields(next, error, tab)) {
    renderForm(next, error, 400, tab, customJson.c_str());
    return;
  }
  if (!validate(next, error, tab)) {
    renderForm(next, error, 400, tab, customJson.c_str());
    return;
  }
  if (!profilesValidateCustomJson(customJson.c_str(), error)) {
    renderForm(next, String("Custom profiles: ") + error, 400, "profile",
               customJson.c_str());
    return;
  }
  if (!profilesSelectionValid(next.profileId, customJson.c_str(), error)) {
    renderForm(next, error, 400, "profile", customJson.c_str());
    return;
  }

  settings = next;
  if (!saveSettings()) {
    renderForm(next, "Could not write settings to NVS.", 500, tab,
               customJson.c_str());
    return;
  }
  if (!profilesSaveCustomToNvs(customJson.c_str(), error)) {
    renderForm(next, String("Could not save custom profiles: ") + error, 500,
               "profile", customJson.c_str());
    return;
  }
  bootGuardClear();

  sendResult("Saved", "The base is rebooting with the new configuration. "
                          "Open the status page at http://<base-ip>:" +
                              String(settings.webUiPort) + "/.");
  rebootAtMs = millis() + 1500;
}

void handleFactory() {
  bootGuardClear();
  clearSettings();
  profilesClearCustom();
  sendResult("Factory reset",
             "All settings were cleared. The base is rebooting into setup mode "
             "at http://192.168.4.1:" + String(settings.webUiPort) + "/.");
  rebootAtMs = millis() + 1500;
}

void handleScan() {
  const int count = WiFi.scanNetworks();
  String json = "[";
  if (count > 0) {
    for (int i = 0; i < count; ++i) {
      if (i > 0) json += ",";
      json += "{\"ssid\":\"";
      json += jsonEscape(WiFi.SSID(i).c_str());
      json += "\",\"rssi\":";
      json += String(WiFi.RSSI(i));
      json += "}";
    }
  }
  json += "]";
  WiFi.scanDelete();
  server->sendHeader("Cache-Control", "no-store");
  server->send(200, "application/json", json);
}

void addProfileCors() {
  server->sendHeader("Access-Control-Allow-Origin", "*");
  server->sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server->sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

void handleProfileBuilder() {
  server->sendHeader("Cache-Control", "no-store");
  server->send_P(200, "text/html", PROFILE_BUILDER_HTML);
}

void handleProfileConverter() {
  server->sendHeader("Cache-Control", "no-store");
  server->send_P(200, "application/javascript", PROFILE_CONVERTER_JS);
}

void handleProfileMapping() {
  server->sendHeader("Cache-Control", "no-store");
  server->send_P(200, "application/javascript", PROFILE_MAPPING_JS);
}

void handleProfileOffline() {
  addProfileCors();
  server->sendHeader("Cache-Control", "no-store");
  server->send_P(200, "application/json", OFFLINE_CHARGERS_JSON);
}

void handleProfileInfo() {
  JsonDocument doc;
  doc["ha_name"] = settings.haName;
  doc["ha_device_id"] = settings.haDeviceId;
  doc["state_topic"] = settings.mqttTopicDps;
  doc["command_topic"] = browserProfileCommandTopic();
  doc["raw_entity_id"] = browserProfileRawEntityId();
  doc["availability_topic"] = settings.mqttTopicAvailability;
  doc["selection"] = settings.profileId;
  doc["pack_loaded"] = browserProfileLoaded();
  if (browserProfileLoaded()) {
    JsonObject pack = doc["pack"].to<JsonObject>();
    pack["id"] = browserProfileId();
    pack["name"] = browserProfileName();
    pack["device_type"] = browserProfileDeviceType();
    pack["discovery"] = browserProfileDiscoveryCount();
  }
  String json;
  json.reserve(512);
  serializeJson(doc, json);
  addProfileCors();
  server->sendHeader("Cache-Control", "no-store");
  server->send(200, "application/json", json);
}

void handleProfileOptions() {
  addProfileCors();
  server->send(204, "text/plain", "");
}

bool profileStreamOk = false;
String profileStreamError;

void handleProfileRaw() {
  HTTPRaw &raw = server->raw();
  if (raw.status == RAW_START) {
    profileStreamOk = false;
    profileStreamError = "";
    browserProfileStreamBegin();
    return;
  }
  if (raw.status == RAW_WRITE) {
    if (profileStreamError.length() == 0 && raw.currentSize > 0) {
      String error;
      if (!browserProfileStreamFeed(raw.buf, raw.currentSize, error)) {
        profileStreamError = error;
      }
    }
    return;
  }
  if (raw.status == RAW_END) {
    if (profileStreamError.length() == 0) {
      String error;
      if (browserProfileStreamFinish(error)) {
        profileStreamOk = true;
      } else {
        profileStreamError = error;
      }
    }
    return;
  }
  if (raw.status == RAW_ABORTED) {
    profileStreamError = "profile upload aborted";
    browserProfileStreamAbort();
  }
}

void handleProfileSave() {
  addProfileCors();
  if (!profileStreamOk) {
    JsonDocument doc;
    doc["ok"] = false;
    doc["error"] = profileStreamError.length() ? profileStreamError
                                                : "profile upload was not received";
    String json;
    serializeJson(doc, json);
    server->send(400, "application/json", json);
    return;
  }

  bootGuardClear();
  rebootAtMs = millis() + 1500;
  JsonDocument doc;
  doc["ok"] = true;
  doc["id"] = browserProfileId();
  doc["name"] = browserProfileName();
  String json;
  serializeJson(doc, json);
  server->send(200, "application/json", json);
}

void handleNotFound() {
  server->sendHeader("Location", "/");
  server->send(302, "text/plain", "");
}

void startSetupAp() {
  if (setupApActive) return;
  if (lastSetupApAttemptMs != 0 &&
      millis() - lastSetupApAttemptMs < 10000) {
    return;
  }
  lastSetupApAttemptMs = millis();

  WiFi.mode(WIFI_AP_STA);
  const bool ok = WiFi.softAP(settings.setupApSsid, settings.setupApPassword,
                              settings.setupApChannel, 0, 4);
  setupApActive = ok;
  Serial.printf("[web] setup AP %s -> %s (%s)\n", settings.setupApSsid,
                WiFi.softAPIP().toString().c_str(), ok ? "ok" : "FAILED");
}

}  // namespace

void webUiBegin() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.setHostname("tuya-lora-base");
  WiFi.setSleep(false);
  lastWifiOkMs = millis();

  const bool forceSetup = bootGuardForceSetupAp();
  if (forceSetup) {
    Serial.println("[web] recovery mode: forcing setup AP");
  }
  if (!wifiConfigured() || forceSetup) {
    startSetupAp();
  }

  server = new WebServer(settings.webUiPort);
  server->on("/", HTTP_GET, handleStatus);
  server->on("/settings", HTTP_GET, handleSettings);
  server->on("/profiles", HTTP_GET, handleProfileBuilder);
  server->on("/profile_converter.mjs", HTTP_GET, handleProfileConverter);
  server->on("/profile_mapping.mjs", HTTP_GET, handleProfileMapping);
  server->on("/api/profile/info", HTTP_GET, handleProfileInfo);
  server->on("/api/profile/offline", HTTP_GET, handleProfileOffline);
  server->on("/api/profile/save", HTTP_POST, handleProfileSave, handleProfileRaw);
  server->on("/api/profile/save", HTTP_OPTIONS, handleProfileOptions);
  server->on("/api/status", HTTP_GET, handleApiStatus);
  server->on("/save", HTTP_POST, handleSave);
  server->on("/factory", HTTP_POST, handleFactory);
  server->on("/scan", HTTP_GET, handleScan);
  server->onNotFound(handleNotFound);
  server->begin();

  Serial.print("[web] configuration UI on http://");
  if (setupApActive) {
    Serial.print(WiFi.softAPIP());
  } else {
    Serial.print("<base-ip>");
  }
  Serial.print(":");
  Serial.print(settings.webUiPort);
  Serial.println("/");
}

void webUiLoop() {
  server->handleClient();

  if (rebootAtMs != 0 && static_cast<int32_t>(millis() - rebootAtMs) >= 0) {
    Serial.println("[web] rebooting");
    delay(100);
    ESP.restart();
  }

  const bool wifiUp = WiFi.status() == WL_CONNECTED;
  if (wifiUp) {
    lastWifiOkMs = millis();
    if (setupApActive && !bootGuardForceSetupAp()) {
      WiFi.softAPdisconnect(false);
      setupApActive = false;
      Serial.println("[web] WiFi connected, setup AP stopped");
    }
  } else if (!setupApActive &&
             millis() - lastWifiOkMs >= settings.wifiSetupFallbackMs) {
    startSetupAp();
  }
}

bool webUiSetupApActive() { return setupApActive; }

IPAddress webUiApIp() { return WiFi.softAPIP(); }

}  // namespace tuya_lora
