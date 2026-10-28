#include "web_assets.h"

namespace tuya_lora {

const char PROFILE_BUILDER_HTML[] PROGMEM =
R"A_HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Tuya LoRa — Profile editor</title>
<style>
  :root { color-scheme: light dark; }
  * { box-sizing: border-box; }
  body { margin: 0; padding: 24px 16px; font: 16px/1.45 system-ui, -apple-system, Segoe UI, sans-serif; background: #f7f7f8; color: #17181b; }
  main { max-width: 980px; margin: 0 auto; }
  h1 { font-size: 1.5rem; margin: 0 0 4px; }
  h2 { font-size: 1.15rem; margin: 24px 0 8px; }
  p { margin: 6px 0; }
  a { color: #0969da; }
  .muted { opacity: .7; font-size: .92rem; }
  .row { display: flex; gap: 8px; align-items: center; flex-wrap: wrap; margin: 10px 0; }
  input[type=search], input[type=url] { flex: 1; min-width: 220px; padding: 9px 11px; border: 1px solid #8886; border-radius: 8px; background: transparent; color: inherit; font: inherit; }
  button { padding: 9px 13px; border: 1px solid #8886; border-radius: 8px; background: #fff; color: #17181b; font: inherit; cursor: pointer; }
  button.primary { background: #0969da; border-color: #0969da; color: #fff; }
  button:disabled { opacity: .45; cursor: not-allowed; }
  textarea { width: 100%; min-height: 420px; padding: 12px; border: 1px solid #8886; border-radius: 8px; background: #fff; color: inherit; font: .82rem/1.4 ui-monospace, SFMono-Regular, Menlo, monospace; resize: vertical; }
  details { border: 1px solid #8884; border-radius: 10px; margin: 8px 0; background: #fff; }
  summary { cursor: pointer; padding: 10px 12px; font-weight: 650; }
  .item { display: flex; gap: 12px; align-items: center; justify-content: space-between; padding: 9px 12px; border-top: 1px solid #8882; }
  .item:first-of-type { border-top: 0; }
  .item .meta { min-width: 0; }
  .item .name { font-weight: 600; }
  .item .sub { font-size: .82rem; opacity: .65; overflow-wrap: anywhere; }
  .badge { font-size: .72rem; padding: 2px 6px; border-radius: 999px; background: #8882; }
  .badge.ok { background: #1f883d22; color: #1a7f37; }
  .badge.no { background: #cf222e22; color: #cf222e; }
  .status { margin: 10px 0; padding: 10px 12px; border-radius: 8px; background: #8881; white-space: pre-wrap; }
  .status.err { background: #cf222e18; color: #cf222e; }
  code { font: .85em ui-monospace, SFMono-Regular, Menlo, monospace; }
  @media (prefers-color-scheme: dark) {
    body { background: #111214; color: #e8e8ea; }
    button, details, textarea { background: #1a1c20; color: inherit; }
    button.primary { background: #2f81f7; border-color: #2f81f7; }
    a { color: #6cb6ff; }
  }
</style>
</head>
<body>
<main>
  <h1>Profile editor</h1>
  <p class="muted">One active profile. Pick a device from the online tuya-local catalog, then fine-tune the generated JSON before saving it to the base.</p>
  <div id="base-info" class="status">Connecting to the base…</div>

  <section>
    <h2>Active profile JSON</h2>
    <div class="status" id="profile-summary">No profile loaded.</div>
    <div class="row">
      <button id="reload-active" type="button">Reload active profile</button>
      <button id="format-json" type="button">Format JSON</button>
      <button id="save" class="primary" type="button">Save to base</button>
    </div>
    <textarea id="profile-json" spellcheck="false" autocapitalize="off" autocomplete="off" placeholder="Select a catalog device or paste a profile JSON object here."></textarea>
    <p class="hint muted">The profile is stored as metadata plus one discovery entry per Home Assistant entity. `discovery` is the editable entity array; saving adds its count automatically. Topic tokens (`__STATE_TOPIC__`, `__CMD_TOPIC__`, `__RAW_ENTITY__`) are resolved by the base at publish time, so the profile survives MQTT topic/device-name changes. The base reboots after a successful save.</p>
  </section>

  <div id="status" class="status" hidden></div>

  <section>
    <h2>Online tuya-local catalog</h2>
    <div class="row">
      <input id="catalog-url" type="url" spellcheck="false" value="https://raw.githubusercontent.com/gyenchev/tuya-lora-bridge/refs/heads/main/web/catalog.json">
      <button id="load-online" class="primary" type="button">Load online catalog</button>
    </div>
    <div class="row">
      <input id="search" type="search" placeholder="Search device, brand or model…" autocomplete="off">
    </div>
    <div id="online"></div>
  </section>
</main>

<script type="module">
import { buildPack } from '/profile_converter.mjs';

const $ = (id) => document.getElementById(id);
let info = null;
let catalog = null;
let yamlLib = null;

function setStatus(message, isError = false) {
  const box = $('status');
  box.hidden = !message;
  box.textContent = message;
  box.classList.toggle('err', Boolean(isError));
}

function escapeHtml(value) {
  return String(value ?? '')
    .replaceAll('&', '&amp;')
    .replaceAll('<', '&lt;')
    .replaceAll('>', '&gt;')
    .replaceAll('"', '&quot;')
    .replaceAll("'", '&#39;');
}

async function fetchJson(url, options) {
  const response = await fetch(url, { cache: 'no-store', ...options });
  const text = await response.text();
  let data = null;
  try { data = text ? JSON.parse(text) : null; } catch {}
  if (!response.ok) throw new Error(data?.error || text || `HTTP ${response.status}`);
  return data;
}

function profileSize(profile) {
  return JSON.stringify(profile).length;
}

function tokenizeProfile(profile) {
  let text = JSON.stringify(profile);
  const pairs = [
    [info?.state_topic, '__STATE_TOPIC__'],
    [info?.command_topic, '__CMD_TOPIC__'],
    [info?.raw_entity_id, '__RAW_ENTITY__'],
    ['tuya-lora/device/dps', '__STATE_TOPIC__'],
    ['tuya-lora/device/command/dp', '__CMD_TOPIC__'],
  ];
  for (const [from, to] of pairs) {
    if (from) text = text.split(from).join(to);
  }
  text = text.replace(/sensor\.[a-z0-9_]+_pack_raw/g, '__RAW_ENTITY__');
  return JSON.parse(text);
}

function setProfile(profile, sourceLabel) {
  const text = JSON.stringify(profile, null, 2);
  $('profile-json').value = text;
  const entities = Array.isArray(profile.discovery) ? profile.discovery.length : 0;
  $('profile-summary').textContent =
    `${profile.name || profile.id || 'Unnamed profile'} · ${entities} entities · ${text.length} bytes${sourceLabel ? ` · ${sourceLabel}` : ''}`;
}

function parseProfile() {
  let profile;
  try {
    profile = JSON.parse($('profile-json').value);
  } catch (error) {
    throw new Error(`Invalid JSON: ${error.message}`);
  }
  if (!profile || typeof profile !== 'object' || Array.isArray(profile)) {
    throw new Error('Profile JSON must be an object');
  }
  if (!Array.isArray(profile.discovery) || !profile.discovery.length) {
    throw new Error('Profile JSON must contain a non-empty discovery array');
  }
  for (const [index, item] of profile.discovery.entries()) {
    if (!item || typeof item !== 'object' || Array.isArray(item)) {
      throw new Error(`discovery[${index}] must be an object`);
    }
    if (!item.component || !item.object_id || !item.config || typeof item.config !== 'object') {
      throw new Error(`discovery[${index}] needs component, object_id and config`);
    }
  }
  return profile;
}

async function loadBaseInfo() {
  info = await fetchJson('/api/profile/info');
  const pack = info.pack
    ? ` · active: ${escapeHtml(info.pack.name)} (${escapeHtml(info.pack.id)})`
    : ' · no active profile';
  $('base-info').innerHTML =
    `<strong>${escapeHtml(info.ha_name || 'Base')}</strong> · state: <code>${escapeHtml(info.state_topic)}</code> · command: <code>${escapeHtml(info.command_topic)}</code>${pack}`;
}

async function loadActiveProfile() {
  try {
    setStatus('Loading active profile from the base…');
    const response = await fetch('/api/profile/export', { cache: 'no-store' });
    if (response.status === 204) {
      $('profile-json').value = '';
      $('profile-summary').textContent = 'No profile loaded. Select a catalog device or paste JSON.';
      setStatus('No active profile on the base.');
      return;
    }
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const text = await response.text();
    const lines = text.split('\n').map((line) => line.trim()).filter(Boolean);
    if (!lines.length) throw new Error('Base returned an empty profile');
    const meta = JSON.parse(lines.shift());
    delete meta.generation;
    delete meta.discovery_count;
    delete meta.detect;
    meta.discovery = lines.map((line) => JSON.parse(line));
    setProfile(tokenizeProfile(meta), 'active profile');
    setStatus('');
  } catch (error) {
    setStatus(`Could not load the active profile: ${error.message}`, true);
  }
}

async function saveProfile() {
  try {
    const profile = parseProfile();
    const { discovery, ...meta } = profile;
    delete meta.generation;
    delete meta.detect;
    meta.discovery_count = discovery.length;
    const body = [
      JSON.stringify(meta),
      ...discovery.map((item) => JSON.stringify(item)),
    ].join('\n') + '\n';
    if (body.length > 250000) throw new Error('Profile upload is larger than the 250 KB limit');
    setStatus('Saving profile to the base…');
    $('save').disabled = true;
    const response = await fetch('/api/profile/save', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-ndjson' },
      body,
    });
    const data = await response.json().catch(() => null);
    if (!response.ok || !data?.ok) throw new Error(data?.error || `HTTP ${response.status}`);
    setStatus(`Saved ${data.name || meta.name || 'profile'}. The base is rebooting…`);
    setTimeout(async () => {
      try {
        await loadBaseInfo();
        await loadActiveProfile();
        setStatus(`Active profile is now ${info.pack?.name || 'the saved profile'}.`);
      } catch (error) {
        setStatus(`Saved. Reconnect to the base to verify. ${error.message}`);
      }
      $('save').disabled = false;
    }, 6000);
  } catch (error) {
    setStatus(error.message, true);
    $('save').disabled = false;
  }
}

function makeItem({ title, subtitle, badge, disabled, action }) {
  const row = document.createElement('div');
  row.className = 'item';
  row.innerHTML = `<div class="meta"><div class="name">${escapeHtml(title)}</div><div class="sub">${escapeHtml(subtitle || '')}</div></div>`;
  if (badge) {
    const span = document.createElement('span');
    span.className = `badge ${badge.ok ? 'ok' : 'no'}`;
    span.textContent = badge.label;
    row.appendChild(span);
  }
  const button = document.createElement('button');
  button.type = 'button';
  button.textContent = 'Edit';
  button.disabled = Boolean(disabled);
  button.addEventListener('click', action);
  row.appendChild(button);
  return row;
}

function renderOnline() {
  const host = $('online');
  host.innerHTML = '';
  if (!catalog) {
    host.innerHTML = '<p class="muted">Load the online catalog to browse tuya-local devices.</p>';
    return;
  }
  const query = $('search').value.trim().toLowerCase();
  const groups = new Map();
  for (const profile of catalog.profiles) {
    if (query && !`${profile.name} ${profile.id} ${profile.file} ${profile.group}`.toLowerCase().includes(query)) continue;
    if (!groups.has(profile.group)) groups.set(profile.group, []);
    groups.get(profile.group).push(profile);
  }
  if (!groups.size) {
    host.innerHTML = '<p class="muted">No profiles matched the search.</p>';
    return;
  }
  for (const [group, profiles] of groups) {
    const details = document.createElement('details');
    details.open = Boolean(query);
    details.innerHTML = `<summary>${escapeHtml(group)} (${profiles.length})</summary>`;
    for (const profile of profiles) {
      details.appendChild(makeItem({
        title: profile.name,
        subtitle: `${profile.id} · ${profile.file}`,
        badge: profile.supported
          ? { label: `${profile.discovery} entities`, ok: true }
          : { label: 'conversion error', ok: false },
        disabled: !profile.supported,
        action: async () => {
          try {
            setStatus(`Fetching ${profile.file}…`);
            await ensureYaml();
            const response = await fetch(catalog.yaml_base_url + profile.file, { cache: 'no-store' });
            if (!response.ok) throw new Error(`YAML fetch failed: HTTP ${response.status}`);
            const source = await response.text();
            const yaml = yamlLib.load(source, { schema: yamlLib.DEFAULT_SCHEMA });
            const pack = buildPack(yaml, {
              id: profile.id,
              name: profile.name,
              group: profile.group,
              source: `tuya-local ${catalog.tuya_commit.slice(0, 12)}/${profile.file}`,
            }, {
              stateTopic: '__STATE_TOPIC__',
              commandTopic: '__CMD_TOPIC__',
              rawEntity: '__RAW_ENTITY__',
            });
            if (profileSize(pack) > 200000) throw new Error('Generated profile is larger than the 200 KB limit');
            setProfile(pack, `tuya-local catalog · ${profile.file}`);
            setStatus('Profile loaded into the editor. Review it, then save to the base.');
            $('profile-json').scrollIntoView({ behavior: 'smooth', block: 'start' });
          } catch (error) {
            setStatus(error.message, true);
          }
        },
      }));
    }
    host.appendChild(details);
  }
}

async function ensureYaml() {
  if (yamlLib) return yamlLib;
  setStatus('Loading YAML parser…');
  await new Promise((resolve, reject) => {
    const script = document.createElement('script');
    script.src = 'https://cdn.jsdelivr.net/npm/js-yaml@4/dist/js-yaml.min.js';
    script.onload = resolve;
    script.onerror = () => reject(new Error('Could not load js-yaml from the CDN'));
    document.head.appendChild(script);
  });
  yamlLib = window.jsyaml;
  if (!yamlLib) throw new Error('js-yaml did not initialize');
  return yamlLib;
}

async function loadOnline() {
  try {
    setStatus('Fetching online catalog…');
    catalog = await fetchJson($('catalog-url').value.trim());
    setStatus(`Loaded ${catalog.profiles.length} profiles from tuya-local.`);
    renderOnline();
  } catch (error) {
    setStatus(`Online catalog unavailable: ${error.message}`, true);
  }
}

$('reload-active').addEventListener('click', loadActiveProfile);
$('format-json').addEventListener('click', () => {
  try {
    const profile = parseProfile();
    setProfile(profile, 'formatted');
    setStatus('');
  } catch (error) {
    setStatus(error.message, true);
  }
});
$('save').addEventListener('click', saveProfile);
$('load-online').addEventListener('click', loadOnline);
$('search').addEventListener('input', renderOnline);

(async () => {
  try {
    await loadBaseInfo();
    await loadActiveProfile();
  } catch (error) {
    $('base-info').textContent = `Could not reach the base: ${error.message}`;
  }
})();
</script>
</body>
</html>
)A_HTML";

const char PROFILE_CONVERTER_JS[] PROGMEM =
R"A_JS(// Portions derived from tuya-local (MIT). See THIRD_PARTY_NOTICES.md.
// Shared tuya-local -> Tuya LoRa browser profile converter.
// Used by the browser builder and by the Node catalog generator.
//
// The converter emits precompiled HA MQTT discovery configs. The ESP32 only
// publishes those configs and relays raw `dps` command JSON; all state and
// command mapping runs in Home Assistant templates.

import {
  Ctx,
  boolValue,
  commandTemplate,
  emitWriteTo,
  formatFieldsTemplate,
  formatHaToField,
  formatPackTemplate,
  formatStruct,
  lit,
  mappingRules,
  numberValue,
  stateTemplate,
  text,
  writeAppend,
} from "./profile_mapping.mjs";

export const SIMPLE_ENTITIES = new Set([
  "sensor",
  "binary_sensor",
  "switch",
  "number",
  "select",
  "button",
  "text",
]);

const NATIVE_ENTITIES = new Set([
  ...SIMPLE_ENTITIES,
  "light",
  "fan",
  "cover",
  "climate",
  "water_heater",
  "humidifier",
  "lock",
  "vacuum",
  "alarm_control_panel",
  "siren",
  "valve",
  "event",
  "time",
  "datetime",
  "lawn_mower",
  "camera",
  "media_player",
  "remote",
  "infrared",
]);

const GROUP_ORDER = [
  "Chargers",
  "Lights",
  "Climate",
  "Covers",
  "Fans",
  "Locks",
  "Vacuums",
  "Water heaters",
  "Alarms",
  "Humidifiers",
  "Sirens",
  "Valves",
  "Media players",
  "Lawn mowers",
  "Time controls",
  "Switches & controls",
  "Sensors",
  "Other",
];

const DP_TYPES = new Set([
  "string",
  "boolean",
  "integer",
  "bitfield",
  "float",
  "json",
  "base64",
  "utf16b64",
  "hex",
  "unixtime",
]);

function isObject(value) {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}

function asArray(value) {
  return Array.isArray(value) ? value : [];
}

function titleCase(value) {
  return text(value)
    .replace(/[_-]+/g, " ")
    .trim()
    .replace(/\b\w/g, (c) => c.toUpperCase());
}

export function slugify(value, limit = 23) {
  const slug = text(value)
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, "_")
    .replace(/^_+|_+$/g, "");
  return (slug || "profile").slice(0, limit).replace(/_+$/g, "") || "profile";
}

export function productSummary(products) {
  const parts = [];
  for (const product of asArray(products).slice(0, 3)) {
    if (!isObject(product)) continue;
    const label = [product.manufacturer, product.model]
      .filter(Boolean)
      .map(text)
      .join(" ");
    const modelId = text(product.model_id);
    if (label) parts.push(modelId && modelId !== product.model ? `${label} (${modelId})` : label);
  }
  return parts.join("; ");
}

export function displayName(yaml, fallback = "Tuya device") {
  const name = text(yaml?.name || fallback);
  const summary = productSummary(yaml?.products);
  if (summary && `${name} (${summary})`.length <= 47) return `${name} (${summary})`;
  return name.slice(0, 47);
}

function entityName(entity) {
  if (entity?.name) return text(entity.name);
  const translation = entity?.translation_key || entity?.translation_only_key;
  const placeholders = isObject(entity?.translation_placeholders)
    ? entity.translation_placeholders
    : {};
  if (translation === "status") return "Status";
  if (translation === "voltage_x") return `Voltage ${text(placeholders.x)}`.trim();
  if (translation === "current_x") return `Current ${text(placeholders.x)}`.trim();
  if (translation === "power_x") return `Power ${text(placeholders.x)}`.trim();
  if (translation === "switch") return "Switch";
  if (translation === "brightness") return "Brightness";
  if (translation === "color_temp") return "Color temperature";
  if (translation) return titleCase(translation);
  if (entity?.class) return titleCase(entity.class);
  return titleCase(text(entity?.entity || "Sensor").replace(/_/g, " "));
}

export function classifyGroup(yaml, filename = "") {
  const name = `${filename} ${text(yaml?.name)}`.toLowerCase();
  if (/(charger|wallbox|evse|ev-charger|ev_charger)/.test(name)) return "Chargers";

  const types = new Set(
    asArray(yaml?.entities)
      .map((entity) => entity?.entity)
      .filter(Boolean),
  );
  if (types.has("light")) return "Lights";
  if (types.has("climate")) return "Climate";
  if (types.has("cover")) return "Covers";
  if (types.has("fan")) return "Fans";
  if (types.has("lock")) return "Locks";
  if (types.has("vacuum")) return "Vacuums";
  if (types.has("water_heater")) return "Water heaters";
  if (types.has("alarm_control_panel")) return "Alarms";
  if (types.has("humidifier")) return "Humidifiers";
  if (types.has("siren")) return "Sirens";
  if (types.has("valve")) return "Valves";
  if (types.has("media_player") || types.has("remote") || types.has("infrared")) return "Media players";
  if (types.has("lawn_mower")) return "Lawn mowers";
  if (types.has("time") || types.has("datetime")) return "Time controls";
  if (types.has("switch") || types.has("number") || types.has("select") || types.has("button")) {
    return "Switches & controls";
  }
  return "Sensors";
}

export function groupOrder(groups) {
  const present = new Set(groups);
  return GROUP_ORDER.filter((group) => present.has(group));
}

function safeUnit(unit) {
  const value = text(unit);
  if (value === "C") return "°C";
  if (value === "F") return "°F";
  if (value === "ugm3") return "µg/m³";
  if (value === "m2") return "m²";
  return value;
}

function parseDp(raw, entity, index) {
  const dp = {
    ...raw,
    id: String(raw.id),
    rawtype: DP_TYPES.has(raw.type) ? raw.type : "string",
    index,
    name: text(raw.name),
    mapping: Array.isArray(raw.mapping) ? raw.mapping.filter((r) => r && typeof r === "object") : [],
    range: isObject(raw.range) ? raw.range : null,
    format: Array.isArray(raw.format) ? raw.format : null,
    mask: raw.mask ? String(raw.mask) : null,
    mask_signed: raw.mask_signed === true,
    endianness: raw.endianness === "little" ? "little" : "big",
    readonly: raw.readonly === true,
    optional: raw.optional === true,
    hidden: raw.hidden === true,
    unit: safeUnit(raw.unit),
    class: text(raw.class),
    precision: Number.isInteger(raw.precision) ? raw.precision : null,
    entity,
  };
  return dp;
}

function parseEntity(raw, index) {
  const dps = asArray(raw.dps).map((dp, dpIndex) => parseDp(dp, raw, dpIndex));
  const byName = new Map();
  for (const dp of dps) byName.set(dp.name, dp);
  const entity = {
    raw,
    index,
    type: raw.entity,
    name: entityName(raw).slice(0, 31),
    class: text(raw.class),
    category: text(raw.category),
    icon: text(raw.icon),
    hidden: raw.hidden === true,
    deprecated: raw.deprecated === true,
    dps,
    byName,
    consumed: new Set(),
  };
  return entity;
}

function use(entity, roleName) {
  const dp = entity.byName.get(roleName);
  if (dp) entity.consumed.add(dp.name);
  return dp || null;
}

function useDp(entity, dp) {
  if (dp) entity.consumed.add(dp.name);
  return dp;
}

function normalizeRuleValues(rule, fn) {
  const out = { ...rule };
  if (Object.prototype.hasOwnProperty.call(out, "value")) {
    const normalized = fn(out.value);
    if (normalized !== null) out.value = normalized;
  }
  if (Array.isArray(out.conditions)) {
    out.conditions = out.conditions.map((cond) => {
      const c = { ...cond };
      if (Object.prototype.hasOwnProperty.call(c, "value")) {
        const normalized = fn(c.value);
        if (normalized !== null) c.value = normalized;
      }
      if (Array.isArray(c.mapping)) {
        c.mapping = c.mapping.map((m) => {
          const n = { ...m };
          if (Object.prototype.hasOwnProperty.call(n, "value")) {
            const normalizedValue = fn(n.value);
            if (normalizedValue !== null) n.value = normalizedValue;
          }
          return n;
        });
      }
      return c;
    });
  }
  return out;
}

function normalizeDp(dp, fn) {
  return { ...dp, mapping: dp.mapping.map((r) => normalizeRuleValues(r, fn)) };
}

function stateFor(entity, dp, options = {}) {
  const { code, var: v } = stateTemplate(entity, dp, options);
  return { code, var: v };
}

function stateTemplateFull(entity, dp, options = {}) {
  const { code, var: v } = stateFor(entity, dp, options);
  const dpsVar = options.dpsVar || "dps";
  const prelude = options.prelude === false ? "" : `{% set ${dpsVar} = value_json.dps %}\n`;
  const output = options.transform ? options.transform(v) : `{{ ${v} }}`;
  return `${prelude}${code}${output}`;
}

function boolStateTemplate(entity, dp) {
  const normalized = normalizeDp(dp, boolValue);
  const { code, var: v } = stateFor(entity, normalized);
  return `{% set dps = value_json.dps %}\n${code}{{ 'ON' if (${v} is number and ${v} != 0) or (${v} | string | lower in ['true','on','1','yes','open','enabled','detected']) else 'OFF' }}`;
}

function numberStateTemplate(entity, dp) {
  const normalized = normalizeDp(dp, numberValue);
  return stateTemplateFull(entity, normalized);
}

function textStateTemplate(entity, dp) {
  return stateTemplateFull(entity, dp, { transform: (v) => `{{ ${v} | default('') }}` });
}

function commandFor(entity, dp, targetExpr, info) {
  return commandTemplate(entity, dp, targetExpr, { rawEntity: info.rawEntity }).code;
}

function commandForMany(entity, writes, info) {
  const ctx = new Ctx();
  const rawEntity = info.rawEntity || "__RAW_ENTITY__";
  let code = `{% set dps = state_attr(${lit(rawEntity)}, "dps") or {} %}\n{% set writes = namespace(out="") %}\n`;
  for (const write of writes) {
    if (!write || !write.dp) continue;
    code += emitWriteTo(entity, write.dp, write.target, ctx, "writes");
  }
  code += "{{ '{' ~ writes.out ~ '}' }}";
  return code;
}

function attributesTemplate(entity) {
  const extras = entity.dps.filter((dp) => !entity.consumed.has(dp.name) && !dp.hidden);
  if (!extras.length) return null;
  const ctx = new Ctx("a");
  const entries = [];
  let code = "";
  for (const dp of extras.slice(0, 24)) {
    const { code: dpCode, var: v } = stateFor(entity, dp, { ctx });
    code += dpCode;
    if (dp.rawtype === "json") {
      code += `{% if ${v} is not none %}{% set ${v} = ${v} | from_json %}{% endif %}\n`;
    }
    let key = dp.name || `dp_${dp.id}`;
    if (entries.some((e) => e.key === key)) key = `${key}_${dp.id}`;
    entries.push({ key, var: v });
  }
  const dict = entries.map((e) => `${JSON.stringify(e.key)}: ${e.var}`).join(", ");
  return `{% set dps = value_json.dps %}\n${code}{{ {${dict}} | tojson }}`;
}

function addAttributes(config, entity, info) {
  // The ESP32 publishes a hidden raw-DP sensor with every DP as an attribute.
  // That keeps every DP visible without duplicating large attribute templates
  // in every entity, which matters for the 48 KB pack budget.
  return;
}

function commonEntityConfig(entity, config) {
  config.name = entity.name;
  if (entity.category) config.entity_category = entity.category;
  if (entity.icon) config.icon = entity.icon;
  if (entity.hidden || entity.deprecated) config.enabled_by_default = false;
}

let objectCounter = 0;

function addItem(items, component, objectId, config) {
  // Prefer a semantic object ID derived from the entity name. The builders use
  // positional placeholders (e0, e1, ...) plus optional suffixes; those are not
  // stable or readable in Home Assistant. Keep explicit IDs for any future
  // non-positional component.
  const positional = /^e\d+(?:_|$)/.test(String(objectId));
  const baseId = positional && config?.name
    ? (slugify(config.name, 31) || `e${objectCounter++}`)
    : String(objectId).replace(/[^A-Za-z0-9_-]/g, "_").slice(0, 31) || `e${objectCounter++}`;
  let unique = baseId;
  let suffix = 2;
  while (items.some((item) => item.component === component && item.object_id === unique)) {
    const tail = `_${suffix}`;
    unique = `${baseId.slice(0, 31 - tail.length)}${tail}`;
    suffix += 1;
  }
  items.push({ component, object_id: unique, config });
}

function boolOptions(dp) {
  return mappingRules(dp).filter((r) => !r.hidden && Object.prototype.hasOwnProperty.call(r, "value")).length > 0;
}

function visibleValues(dp, { normalize = (v) => v, includeHidden = false } = {}) {
  const values = [];
  for (const rule of mappingRules(dp)) {
    if (rule.hidden && !includeHidden) continue;
    if (!Object.prototype.hasOwnProperty.call(rule, "value")) continue;
    const value = normalize(rule.value);
    if (value !== null && !values.some((v) => String(v) === String(value))) values.push(value);
    for (const cond of asArray(rule.conditions)) {
      if (cond.hidden || !Object.prototype.hasOwnProperty.call(cond, "value")) continue;
      const cvalue = normalize(cond.value);
      if (cvalue !== null && !values.some((v) => String(v) === String(cvalue))) values.push(cvalue);
    }
  }
  return values;
}

function rawForLogical(entity, dp, logicalValue, depth = 0) {
  if (!dp || depth > 4) return null;
  if (dp.mask || dp.format) return null;
  if (dp.value_redirect) return null;
  const rules = mappingRules(dp);
  let selected = null;
  for (const rule of rules) {
    if (!Object.prototype.hasOwnProperty.call(rule, "value") || rule.hidden) continue;
    if (String(rule.value) === String(logicalValue)) {
      selected = rule;
      break;
    }
    for (const cond of asArray(rule.conditions)) {
      if (!cond.hidden && Object.prototype.hasOwnProperty.call(cond, "value") &&
          String(cond.value) === String(logicalValue)) {
        selected = { ...rule, ...cond };
        break;
      }
    }
    if (selected) break;
  }
  let raw = selected && Object.prototype.hasOwnProperty.call(selected, "dps_val")
    ? selected.dps_val
    : logicalValue;
  if (selected?.value_mirror) return null;
  if (selected?.value_redirect) {
    const target = entity.byName.get(selected.value_redirect);
    return rawForLogical(entity, target, logicalValue, depth + 1);
  }
  const scale = selected?.scale === undefined ? 1 : Number(selected.scale);
  if (Number.isFinite(scale) && scale !== 0 && scale !== 1 && typeof raw === "number") raw *= scale;
  const targetRange = selected?.target_range;
  if (targetRange && dp.range && targetRange.min !== undefined && targetRange.max !== undefined && typeof raw === "number") {
    const fmin = Number(targetRange.min);
    const fmax = Number(targetRange.max);
    const tmin = Number(dp.range.min);
    const tmax = Number(dp.range.max);
    if (fmax !== fmin) raw = tmin + ((raw - fmin) * (tmax - tmin)) / (fmax - fmin);
  }
  if (selected?.invert && dp.range && typeof raw === "number") {
    raw = -raw + Number(dp.range.min) + Number(dp.range.max);
  }
  if (selected?.step && typeof raw === "number") raw = Number(selected.step) * Math.round(raw / Number(selected.step));
  if (dp.rawtype === "utf16b64" && typeof raw === "string") return utf16beBase64(raw);
  return raw;
}

function utf16beBase64(value) {
  const bytes = [];
  for (const ch of String(value)) {
    const code = ch.codePointAt(0);
    if (code <= 0xffff) {
      bytes.push((code >> 8) & 0xff, code & 0xff);
    } else {
      const v = code - 0x10000;
      const hi = 0xd800 + (v >> 10);
      const lo = 0xdc00 + (v & 0x3ff);
      bytes.push(hi >> 8, hi & 0xff, lo >> 8, lo & 0xff);
    }
  }
  if (typeof Buffer !== "undefined") return Buffer.from(bytes).toString("base64");
  let binary = "";
  for (const b of bytes) binary += String.fromCharCode(b);
  return btoa(binary);
}

function staticFragment(entity, dp, logicalValue) {
  if (!dp) return null;
  const raw = rawForLogical(entity, dp, logicalValue);
  if (raw === null || raw === undefined) return null;
  return JSON.stringify({ [dp.id]: raw });
}

function staticFragments(entity, writes) {
  const out = {};
  for (const write of writes) {
    if (!write?.dp) continue;
    const raw = rawForLogical(entity, write.dp, write.value);
    if (raw !== null && raw !== undefined) out[write.dp.id] = raw;
  }
  return Object.keys(out).length ? JSON.stringify(out) : null;
}

function addSimpleAttributes(config, entity, info) {
  addAttributes(config, entity, info);
}

function buildSensor(entity, info) {
  const primary = use(entity, "sensor") || useDp(entity, entity.dps[0]);
  if (!primary) return [];
  const dp = entity.class === "enum" ? normalizeDp(primary, text) : primary;
  const config = {
    state_topic: info.stateTopic,
    value_template: stateTemplateFull(entity, dp),
  };
  commonEntityConfig(entity, config);
  if (entity.class) config.device_class = entity.class;
  if (primary.unit) config.unit_of_measurement = primary.unit;
  if (primary.class) config.state_class = primary.class;
  if (primary.precision !== null) config.suggested_display_precision = primary.precision;
  if (entity.class === "enum") {
    const options = visibleValues(primary, { normalize: text });
    config.options = options.length ? options : ["unknown"];
  }
  addSimpleAttributes(config, entity, info);
  return [{ component: "sensor", object_id: `e${entity.index}`, config }];
}

function buildBinarySensor(entity, info) {
  const primary = use(entity, "sensor") || useDp(entity, entity.dps[0]);
  if (!primary) return [];
  const config = {
    state_topic: info.stateTopic,
    value_template: boolStateTemplate(entity, primary),
    payload_on: "ON",
    payload_off: "OFF",
  };
  commonEntityConfig(entity, config);
  if (entity.class) config.device_class = entity.class;
  addSimpleAttributes(config, entity, info);
  return [{ component: "binary_sensor", object_id: `e${entity.index}`, config }];
}

function buildSwitch(entity, info) {
  const primary = use(entity, "switch") || useDp(entity, entity.dps[0]);
  if (!primary) return [];
  const normalized = normalizeDp(primary, boolValue);
  const config = {
    state_topic: info.stateTopic,
    value_template: boolStateTemplate(entity, primary),
    command_topic: info.commandTopic,
    command_template: commandFor(entity, normalized, "(true if value == 'ON' else false)", info),
    payload_on: "ON",
    payload_off: "OFF",
  };
  commonEntityConfig(entity, config);
  if (entity.class) config.device_class = entity.class;
  addSimpleAttributes(config, entity, info);
  return [{ component: "switch", object_id: `e${entity.index}`, config }];
}

function buildNumber(entity, info) {
  const primary = use(entity, "value") || useDp(entity, entity.dps[0]);
  if (!primary) return [];
  const normalized = normalizeDp(primary, numberValue);
  const config = {
    state_topic: info.stateTopic,
    value_template: numberStateTemplate(entity, primary),
    command_topic: info.commandTopic,
    command_template: commandFor(entity, normalized, "(value | float)", info),
    min: primary.range?.min ?? 0,
    max: primary.range?.max ?? 100,
    step: primary.range?.step ?? 1,
    mode: "box",
  };
  commonEntityConfig(entity, config);
  if (entity.class) config.device_class = entity.class;
  if (primary.unit) config.unit_of_measurement = primary.unit;
  addSimpleAttributes(config, entity, info);
  return [{ component: "number", object_id: `e${entity.index}`, config }];
}

function buildSelect(entity, info) {
  const primary = use(entity, "option") || useDp(entity, entity.dps[0]);
  if (!primary) return [];
  const options = visibleValues(primary, { normalize: text });
  if (!options.length) return [];
  const normalized = normalizeDp(primary, text);
  const config = {
    state_topic: info.stateTopic,
    value_template: stateTemplateFull(entity, normalized),
    command_topic: info.commandTopic,
    command_template: commandFor(entity, normalized, "value", info),
    options,
  };
  commonEntityConfig(entity, config);
  if (entity.class) config.device_class = entity.class;
  addSimpleAttributes(config, entity, info);
  return [{ component: "select", object_id: `e${entity.index}`, config }];
}

function buildText(entity, info) {
  const primary = use(entity, "value") || useDp(entity, entity.dps[0]);
  if (!primary) return [];
  const config = {
    state_topic: info.stateTopic,
    value_template: textStateTemplate(entity, primary),
    command_topic: info.commandTopic,
    command_template: commandFor(entity, primary, "value", info),
  };
  commonEntityConfig(entity, config);
  if (primary.range?.min !== undefined) config.min = Math.max(0, Math.round(primary.range.min));
  if (primary.range?.max !== undefined) config.max = Math.max(config.min || 0, Math.round(primary.range.max));
  addSimpleAttributes(config, entity, info);
  return [{ component: "text", object_id: `e${entity.index}`, config }];
}

function buildButton(entity, info) {
  const primary = use(entity, "button") || useDp(entity, entity.dps[0]);
  if (!primary) return [];
  let press = true;
  const values = visibleValues(primary);
  if (values.length) {
    press = values.find((v) => boolValue(v) === true) ?? values[0];
  } else if (primary.rawtype === "integer" || primary.rawtype === "bitfield") {
    press = 1;
  } else if (primary.rawtype === "string" && !primary.mapping.length) {
    press = "";
  }
  const raw = rawForLogical(entity, primary, press);
  if (raw === null || raw === undefined) return [];
  const config = {
    command_topic: info.commandTopic,
    payload_press: JSON.stringify({ [primary.id]: raw }),
  };
  commonEntityConfig(entity, config);
  if (entity.class) config.device_class = entity.class;
  return [{ component: "button", object_id: `e${entity.index}`, config }];
}

// ---------------------------------------------------------------------------
// Light
// ---------------------------------------------------------------------------

function emitHsvToRgb(ctx, hVar, sVar, vVar, out) {
  const h = ctx.var("h");
  const s = ctx.var("s");
  const v = ctx.var("v");
  const c = ctx.var("c");
  const x = ctx.var("x");
  const m = ctx.var("m");
  const r = ctx.var("r");
  const g = ctx.var("g");
  const b = ctx.var("b");
  let code = "";
  code += `{% set ${h} = ((${hVar} | float) % 360) / 60 %}\n`;
  code += `{% set ${s} = (${sVar} | float) / 100 %}\n`;
  code += `{% set ${v} = (${vVar} | float) / 255 %}\n`;
  code += `{% set ${c} = ${v} * ${s} %}\n`;
  code += `{% set ${x} = ${c} * (1 - ((((${h} % 2) - 1) | abs))) %}\n`;
  code += `{% set ${m} = ${v} - ${c} %}\n`;
  code += `{% if ${h} < 1 %}{% set ${r} = ${c} %}{% set ${g} = ${x} %}{% set ${b} = 0 %}\n`;
  code += `{% elif ${h} < 2 %}{% set ${r} = ${x} %}{% set ${g} = ${c} %}{% set ${b} = 0 %}\n`;
  code += `{% elif ${h} < 3 %}{% set ${r} = 0 %}{% set ${g} = ${c} %}{% set ${b} = ${x} %}\n`;
  code += `{% elif ${h} < 4 %}{% set ${r} = 0 %}{% set ${g} = ${x} %}{% set ${b} = ${c} %}\n`;
  code += `{% elif ${h} < 5 %}{% set ${r} = ${x} %}{% set ${g} = 0 %}{% set ${b} = ${c} %}\n`;
  code += `{% else %}{% set ${r} = ${c} %}{% set ${g} = 0 %}{% set ${b} = ${x} %}{% endif %}\n`;
  code += `{% set ${out}r = (((${r} + ${m}) * 255) | round(0) | int) %}\n`;
  code += `{% set ${out}g = (((${g} + ${m}) * 255) | round(0) | int) %}\n`;
  code += `{% set ${out}b = (((${b} + ${m}) * 255) | round(0) | int) %}\n`;
  return code;
}

function emitRgbToHsv(ctx, rVar, gVar, bVar, out) {
  const r = ctx.var("rn");
  const g = ctx.var("gn");
  const b = ctx.var("bn");
  const mx = ctx.var("mx");
  const mn = ctx.var("mn");
  const d = ctx.var("d");
  const h = ctx.var("hh");
  const s = ctx.var("ss");
  const v = ctx.var("vv");
  let code = "";
  code += `{% set ${r} = (${rVar} | float) / 255 %}\n`;
  code += `{% set ${g} = (${gVar} | float) / 255 %}\n`;
  code += `{% set ${b} = (${bVar} | float) / 255 %}\n`;
  code += `{% set ${mx} = [${r}, ${g}, ${b}] | max %}\n`;
  code += `{% set ${mn} = [${r}, ${g}, ${b}] | min %}\n`;
  code += `{% set ${d} = ${mx} - ${mn} %}\n`;
  code += `{% set ${v} = ${mx} %}\n`;
  code += `{% set ${s} = 0 if ${mx} == 0 else (${d} / ${mx}) * 100 %}\n`;
  code += `{% if ${d} == 0 %}{% set ${h} = 0 %}\n`;
  code += `{% elif ${mx} == ${r} %}{% set ${h} = (60 * (((${g} - ${b}) / ${d}) % 6)) %}\n`;
  code += `{% elif ${mx} == ${g} %}{% set ${h} = 60 * (((${b} - ${r}) / ${d}) + 2) %}\n`;
  code += `{% else %}{% set ${h} = 60 * (((${r} - ${g}) / ${d}) + 4) %}{% endif %}\n`;
  code += `{% set ${out}h = ${h} %}\n{% set ${out}s = ${s} %}\n{% set ${out}v = ${v} * 255 %}\n`;
  return code;
}

function lightFormatFields(entity, dp, source, ctx, prelude) {
  const spec = formatStruct(dp);
  if (!spec) return null;
  const result = formatFieldsTemplate(entity, dp, { ctx, source, dpsPrelude: prelude });
  const fields = new Map();
  for (const field of result.fields) fields.set(field.name, field);
  return { ...result, fields };
}

function lightStateColorTemplate(entity, rgb, fieldName) {
  const ctx = new Ctx("l");
  const fields = lightFormatFields(entity, rgb, `value_json.dps.get(${lit(String(rgb.id))})`, ctx, "{% set dps = value_json.dps %}\n");
  if (!fields) return null;
  let code = fields.code;
  const get = (name) => fields.fields.get(name)?.var;
  const h = get("h");
  const s = get("s");
  const v = get("v");
  const r = get("r");
  const g = get("g");
  const b = get("b");
  if (r && g && b) {
    const scale = (name, field) => {
      const max = field.max || 255;
      return `{% set ${name} = ((${name} | float) * ${255 / max}) | round(0) | int %}\n`;
    };
    code += scale(r, fields.fields.get("r"));
    code += scale(g, fields.fields.get("g"));
    code += scale(b, fields.fields.get("b"));
    const outVar = `${fieldName}Out`;
    code += `{% set ${outVar} = ${r} if ${lit(fieldName)} == 'r' else (${g} if ${lit(fieldName)} == 'g' else ${b}) %}\n`;
    return `${code}{{ ${outVar} }}`;
  }
  if (h && s && v) {
    const rgbOut = ctx.var("rgb");
    code += emitHsvToRgb(ctx, h, s, v, rgbOut);
    code += `{% set out = ${rgbOut}${fieldName} %}\n{{ out }}`;
    return code;
  }
  return null;
}

function buildLight(entity, info) {
  const switchDp = use(entity, "switch");
  const brightnessDp = use(entity, "brightness");
  const colorModeDp = use(entity, "color_mode");
  const colorTempDp = use(entity, "color_temp");
  const rgb = use(entity, "rgbhsv");
  const namedColor = use(entity, "named_color");
  const effectDp = use(entity, "effect");
  if (!switchDp && !brightnessDp && !rgb && !namedColor && !effectDp && !colorModeDp) return [];

  const config = {
    schema: "template",
    name: entity.name,
    command_topic: info.commandTopic,
    state_topic: info.stateTopic,
  };
  if (entity.category) config.entity_category = entity.category;
  if (entity.icon) config.icon = entity.icon;
  if (entity.hidden || entity.deprecated) config.enabled_by_default = false;

  if (switchDp) {
    config.state_template = boolStateTemplate(entity, switchDp);
  } else if (brightnessDp) {
    const { code, var: v } = stateFor(entity, brightnessDp);
    config.state_template = `{% set dps = value_json.dps %}\n${code}{{ 'ON' if (${v} is number and ${v} > 0) else 'OFF' }}`;
  } else if (effectDp) {
    const { code, var: v } = stateFor(entity, effectDp);
    config.state_template = `{% set dps = value_json.dps %}\n${code}{{ 'OFF' if ${v} | string == 'off' else 'ON' }}`;
  } else {
    config.state_template = "{{ 'ON' }}";
  }

  if (brightnessDp) {
    const { code, var: v } = stateFor(entity, brightnessDp);
    const range = brightnessDp.range || { min: 0, max: 100 };
    const scaled = `(((${v} | float) - ${Number(range.min)}) * ${255 / (Number(range.max) - Number(range.min) || 1)})`;
    config.brightness_template = `{% set dps = value_json.dps %}\n${code}{{ ${scaled} | round(0) | int }}`;
  } else if (rgb) {
    const spec = formatStruct(rgb);
    const vField = spec?.fields.find((f) => f.name === "v");
    if (vField) {
      const fields = lightFormatFields(entity, rgb, `value_json.dps.get(${lit(String(rgb.id))})`, new Ctx("b"), "{% set dps = value_json.dps %}\n");
      const actualV = fields?.fields.get("v");
      if (fields && actualV) {
        config.brightness_template = `${fields.code}{{ ((${actualV.var} | float) * ${255 / (actualV.max || 255)}) | round(0) | int }}`;
      }
    }
  }

  if (colorTempDp) {
    config.color_temp_kelvin = true;
    if (colorTempDp.range) {
      config.min_kelvin = Math.round(Number(colorTempDp.range.min));
      config.max_kelvin = Math.round(Number(colorTempDp.range.max));
    }
    config.color_temp_template = stateTemplateFull(entity, colorTempDp);
  }

  if (rgb) {
    const spec = formatStruct(rgb);
    const names = new Set(spec?.fields.map((f) => f.name));
    if (names.has("r") && names.has("g") && names.has("b")) {
      for (const name of ["r", "g", "b"]) {
        const template = lightStateColorTemplate(entity, rgb, name);
        if (template) config[`${name === "r" ? "red" : name === "g" ? "green" : "blue"}_template`] = template;
      }
    } else if (names.has("h") && names.has("s") && names.has("v")) {
      for (const name of ["r", "g", "b"]) {
        const template = lightStateColorTemplate(entity, rgb, name);
        if (template) config[`${name === "r" ? "red" : name === "g" ? "green" : "blue"}_template`] = template;
      }
    }
  }

  let effects = [];
  if (effectDp) {
    effects = visibleValues(effectDp, { normalize: text });
    const { code, var: v } = stateFor(entity, effectDp);
    config.effect_template = `{% set dps = value_json.dps %}\n${code}{{ ${v} | default('') }}`;
  } else if (colorModeDp) {
    effects = visibleValues(colorModeDp, { normalize: text }).filter((v) => {
      const lower = String(v).toLowerCase();
      return !["onoff", "brightness", "color_temp", "hs", "xy", "rgb", "rgbw", "rgbww", "white"].includes(lower);
    });
    if (effects.length) effects.push("off");
    const { code, var: v } = stateFor(entity, colorModeDp);
    config.effect_template = `{% set dps = value_json.dps %}\n${code}{{ ${v} if ${v} | string | lower not in ['onoff','brightness','color_temp','hs','xy','rgb','rgbw','rgbww','white'] else 'off' }}`;
  }
  if (effects.length) config.effect_list = effects;

  // Commands.
  const onWrites = [];
  if (switchDp) onWrites.push({ dp: switchDp, target: "true" });

  let commandCode = `{% set dps = state_attr(${lit(info.rawEntity)}, "dps") or {} %}\n{% set writes = namespace(out="") %}\n`;
  const ctx = new Ctx("c");
  if (brightnessDp) {
    const range = brightnessDp.range || { min: 0, max: 100 };
    const expr = `(((${range.min} + (brightness / 255) * ${(Number(range.max) - Number(range.min)) || 1}) | round(0) | int) if brightness is defined and brightness is not none else none)`;
    commandCode += `{% if brightness is defined and brightness is not none %}\n${emitWriteTo(entity, brightnessDp, expr, ctx, "writes")}{% endif %}\n`;
  }
  if (colorTempDp) {
    commandCode += `{% if color_temp is defined and color_temp is not none %}\n${emitWriteTo(entity, colorTempDp, "color_temp", ctx, "writes")}{% endif %}\n`;
    if (colorModeDp) {
      const rawMode = rawForLogical(entity, colorModeDp, "color_temp");
      if (rawMode !== null) commandCode += `{% if color_temp is defined and color_temp is not none %}${writeAppend("writes", lit(String(colorModeDp.id)), lit(rawMode))}{% endif %}\n`;
    }
  }
  if (rgb) {
    const spec = formatStruct(rgb);
    const fields = spec?.fields || [];
    const fieldMap = new Map(fields.map((f) => [f.name, f]));
    const fieldsState = lightFormatFields(entity, rgb, `dps.get(${lit(String(rgb.id))})`, ctx, "");
    if (fieldsState) {
      // Current field values from the current raw snapshot.
      commandCode += fieldsState.code;
      const get = (name) => fieldsState.fields.get(name)?.var;
      const h = get("h");
      const s = get("s");
      const v = get("v");
      const r = get("r");
      const g = get("g");
      const b = get("b");
      let hsv = null;
      if (h && s && v) {
        hsv = { h, s, v };
      } else if (r && g && b) {
        const out = ctx.var("hsv");
        commandCode += emitRgbToHsv(ctx, r, g, b, out);
        hsv = { h: `${out}h`, s: `${out}s`, v: `${out}v` };
      }
      const fieldExprs = [];
      for (const field of fields) {
        if (["h", "s", "v"].includes(field.name) && hsv) {
          const varName = field.name;
          if (field.name === "h") {
            commandCode += `{% set ${varName}_cmd = hue if hue is defined and hue is not none else ${hsv.h} %}\n`;
          } else if (field.name === "s") {
            commandCode += `{% set ${varName}_cmd = sat if sat is defined and sat is not none else ${hsv.s} %}\n`;
          } else {
            commandCode += `{% set ${varName}_cmd = (brightness if brightness is defined and brightness is not none else ${hsv.v}) %}\n`;
          }
          fieldExprs.push(formatHaToField(field, `${varName}_cmd`, field.name));
        } else if (["r", "g", "b"].includes(field.name)) {
          const varName = field.name;
          const haVar = field.name === "r" ? "red" : field.name === "g" ? "green" : "blue";
          commandCode += `{% set ${varName}_cmd = ${haVar} if ${haVar} is defined and ${haVar} is not none else ${fieldsState.fields.get(field.name)?.var || 0} %}\n`;
          fieldExprs.push(formatHaToField(field, `${varName}_cmd`, field.name));
        } else {
          // Preserve other packed fields (switch/white channels) from the
          // current snapshot.
          const current = fieldsState.fields.get(field.name)?.var || "0";
          fieldExprs.push(`(${current} if ${current} is not none else 0)`);
        }
      }
      const packed = formatPackTemplate(entity, rgb, fieldExprs, { ctx });
      commandCode += packed.code;
      commandCode += `${writeAppend("writes", lit(String(rgb.id)), packed.var)}\n`;
      if (colorModeDp) {
        const mode = rawForLogical(entity, colorModeDp, hsv ? "hs" : "rgb");
        if (mode !== null) commandCode += `{% if (hue is defined and hue is not none) or (red is defined and red is not none) or (green is defined and green is not none) or (blue is defined and blue is not none) %}${writeAppend("writes", lit(String(colorModeDp.id)), lit(mode))}{% endif %}\n`;
      }
    }
  }
  if (effectDp) {
    commandCode += `{% if effect is defined and effect is not none and effect != 'off' %}\n${emitWriteTo(entity, effectDp, "effect", ctx, "writes")}{% endif %}\n`;
  }
  if (!switchDp && !brightnessDp && effectDp) {
    commandCode += `{% if brightness is none and color_temp is none and hue is none and red is none and effect is none %}\n`;
    const raw = rawForLogical(entity, effectDp, visibleValues(effectDp)[0] ?? "on");
    if (raw !== null) commandCode += `${writeAppend("writes", lit(String(effectDp.id)), lit(raw))}\n`;
    commandCode += `{% endif %}\n`;
  }
  commandCode += "{{ '{' ~ writes.out ~ '}' }}";
  config.command_on_template = commandCode;

  let offCode = `{% set dps = state_attr(${lit(info.rawEntity)}, "dps") or {} %}\n{% set writes = namespace(out="") %}\n`;
  if (switchDp) {
    const raw = rawForLogical(entity, switchDp, false);
    if (raw !== null) offCode += `${writeAppend("writes", lit(String(switchDp.id)), lit(raw))}\n`;
  } else if (brightnessDp) {
    const raw = rawForLogical(entity, brightnessDp, 0);
    if (raw !== null) offCode += `${writeAppend("writes", lit(String(brightnessDp.id)), lit(raw))}\n`;
  } else if (effectDp) {
    const raw = rawForLogical(entity, effectDp, "off");
    if (raw !== null) offCode += `${writeAppend("writes", lit(String(effectDp.id)), lit(raw))}\n`;
  }
  offCode += "{{ '{' ~ writes.out ~ '}' }}";
  config.command_off_template = offCode;

  addAttributes(config, entity, info);
  return [{ component: "light", object_id: `e${entity.index}`, config }];
}

// ---------------------------------------------------------------------------
// Fan
// ---------------------------------------------------------------------------

function buildFan(entity, info) {
  const switchDp = use(entity, "switch");
  const speedDp = use(entity, "speed");
  const presetDp = use(entity, "preset_mode");
  const oscillateDp = use(entity, "oscillate");
  const directionDp = use(entity, "direction");
  if (!switchDp && !speedDp && !presetDp && !oscillateDp && !directionDp) return [];

  const config = { name: entity.name, state_topic: info.stateTopic, command_topic: info.commandTopic };
  if (entity.category) config.entity_category = entity.category;
  if (entity.icon) config.icon = entity.icon;
  if (entity.hidden || entity.deprecated) config.enabled_by_default = false;

  if (switchDp) {
    config.state_value_template = boolStateTemplate(entity, switchDp);
    const on = staticFragment(entity, switchDp, true);
    const off = staticFragment(entity, switchDp, false);
    if (on) config.payload_on = on;
    if (off) config.payload_off = off;
  } else {
    config.state_value_template = "{{ 'ON' }}";
    config.payload_on = "{}";
    config.payload_off = "{}";
  }

  if (speedDp) {
    const range = speedDp.range || { min: 0, max: 100, step: 1 };
    const min = Number(range.min);
    const max = Number(range.max);
    const effectiveMin = min === 0 ? 1 : min;
    config.speed_range_min = effectiveMin;
    config.speed_range_max = max;
    const { code, var: v } = stateFor(entity, speedDp);
    const percentage = `(((${v} | float) - ${effectiveMin}) * ${100 / (max - effectiveMin || 1)})`;
    config.percentage_state_topic = info.stateTopic;
    config.percentage_value_template = `{% set dps = value_json.dps %}\n${code}{{ ${percentage} | round(0) | int }}`;
    config.percentage_command_topic = info.commandTopic;
    const target = `(${effectiveMin} + (value / 100) * ${(max - effectiveMin) || 1})`;
    const pctWrites = [{ dp: speedDp, target: `(${target} | round(0) | int)` }];
    if (switchDp) pctWrites.unshift({ dp: switchDp, target: "true" });
    config.percentage_command_template = commandForMany(entity, pctWrites, info);
    config.percentage_step = (Number(range.step || 1) / (max - min || 1)) * 100;
  }

  if (presetDp) {
    const presets = visibleValues(presetDp, { normalize: text });
    if (presets.length) {
      config.preset_modes = presets;
      config.preset_mode_state_topic = info.stateTopic;
      config.preset_mode_value_template = stateTemplateFull(entity, normalizeDp(presetDp, text));
      config.preset_mode_command_topic = info.commandTopic;
      config.preset_mode_command_template = commandFor(entity, normalizeDp(presetDp, text), "value", info);
    }
  }
  if (oscillateDp) {
    config.oscillation_state_topic = info.stateTopic;
    config.oscillation_value_template = boolStateTemplate(entity, oscillateDp);
    config.oscillation_command_topic = info.commandTopic;
    config.oscillation_command_template = commandFor(entity, normalizeDp(oscillateDp, boolValue), "(true if value == 'ON' else false)", info);
  }
  if (directionDp) {
    const directions = visibleValues(directionDp, { normalize: text });
    if (directions.length) config.direction_list = directions;
    config.direction_state_topic = info.stateTopic;
    config.direction_value_template = stateTemplateFull(entity, normalizeDp(directionDp, text));
    config.direction_command_topic = info.commandTopic;
    config.direction_command_template = commandFor(entity, normalizeDp(directionDp, text), "value", info);
  }

  addAttributes(config, entity, info);
  return [{ component: "fan", object_id: `e${entity.index}`, config }];
}

// ---------------------------------------------------------------------------
// Cover
// ---------------------------------------------------------------------------

function buildCover(entity, info) {
  const positionDp = use(entity, "position");
  const currentDp = use(entity, "current_position");
  const tiltDp = use(entity, "tilt_position");
  const controlDp = use(entity, "control");
  const actionDp = use(entity, "action");
  const openDp = use(entity, "open");
  if (!positionDp && !currentDp && !tiltDp && !controlDp && !actionDp && !openDp) return [];

  const config = { name: entity.name, state_topic: info.stateTopic, command_topic: info.commandTopic };
  if (entity.category) config.entity_category = entity.category;
  if (entity.icon) config.icon = entity.icon;
  if (entity.hidden || entity.deprecated) config.enabled_by_default = false;
  if (entity.class) config.device_class = entity.class;

  let stateCode = "";
  let stateExpr = "'open'";
  if (actionDp) {
    const { code, var: v } = stateFor(entity, normalizeDp(actionDp, text));
    stateCode += code;
    stateExpr = `(${v} | string | lower)`;
  } else if (currentDp) {
    const { code, var: v } = stateFor(entity, currentDp);
    stateCode += code;
    stateExpr = `('closed' if (${v} | float) < 5 else ('open' if (${v} | float) > 95 else 'open'))`;
  } else if (openDp) {
    const { code, var: v } = stateFor(entity, normalizeDp(openDp, boolValue));
    stateCode += code;
    stateExpr = `('open' if ${v} else 'closed')`;
  } else if (controlDp) {
    const { code, var: v } = stateFor(entity, normalizeDp(controlDp, text));
    stateCode += code;
    stateExpr = `('opening' if ${v} | string | lower == 'open' else ('closing' if ${v} | string | lower == 'close' else 'closed'))`;
  }
  config.value_template = `{% set dps = value_json.dps %}\n${stateCode}{{ ${stateExpr} }}`;

  if (positionDp) {
    config.position_topic = info.stateTopic;
    config.position_template = stateTemplateFull(entity, positionDp);
    config.set_position_topic = info.commandTopic;
    config.set_position_template = commandFor(entity, positionDp, "(position | float)", info);
  } else if (openDp) {
    config.position_topic = info.stateTopic;
    config.position_template = stateTemplateFull(entity, normalizeDp(openDp, boolValue), {
      transform: (v) => `{{ 100 if ${v} else 0 }}`,
    });
  }
  if (tiltDp) {
    config.tilt_status_topic = info.stateTopic;
    config.tilt_status_template = stateTemplateFull(entity, tiltDp);
    config.tilt_command_topic = info.commandTopic;
    config.tilt_command_template = commandFor(entity, tiltDp, "(tilt_position | float)", info);
  }

  if (controlDp) {
    for (const [key, logical] of [["payload_open", "open"], ["payload_close", "close"], ["payload_stop", "stop"]]) {
      const raw = rawForLogical(entity, controlDp, logical);
      if (raw !== null) config[key] = JSON.stringify({ [controlDp.id]: raw });
    }
  } else if (positionDp) {
    const openRaw = rawForLogical(entity, positionDp, 100);
    const closeRaw = rawForLogical(entity, positionDp, 0);
    if (openRaw !== null) config.payload_open = JSON.stringify({ [positionDp.id]: openRaw });
    if (closeRaw !== null) config.payload_close = JSON.stringify({ [positionDp.id]: closeRaw });
  }

  addAttributes(config, entity, info);
  return [{ component: "cover", object_id: `e${entity.index}`, config }];
}

// ---------------------------------------------------------------------------
// Climate / water heater / humidifier
// ---------------------------------------------------------------------------

function buildClimate(entity, info) {
  const roles = [
    "current_temperature", "current_humidity", "temperature", "target_temp_high", "target_temp_low",
    "hvac_mode", "hvac_action", "fan_mode", "swing_mode", "swing_horizontal_mode", "preset_mode",
    "humidity", "temperature_unit", "min_temperature", "max_temperature",
  ];
  const dp = {};
  for (const role of roles) dp[role] = use(entity, role);
  if (!dp.hvac_mode && !dp.temperature && !dp.target_temp_high && !dp.target_temp_low &&
      !dp.fan_mode && !dp.swing_mode && !dp.preset_mode && !dp.humidity) {
    return [];
  }

  const config = { name: entity.name };
  if (entity.category) config.entity_category = entity.category;
  if (entity.icon) config.icon = entity.icon;
  if (entity.hidden || entity.deprecated) config.enabled_by_default = false;

  if (dp.current_temperature) {
    config.current_temperature_topic = info.stateTopic;
    config.current_temperature_template = stateTemplateFull(entity, dp.current_temperature);
  }
  if (dp.current_humidity) {
    config.current_humidity_topic = info.stateTopic;
    config.current_humidity_template = stateTemplateFull(entity, dp.current_humidity);
  }
  if (dp.temperature) {
    config.temperature_state_topic = info.stateTopic;
    config.temperature_state_template = stateTemplateFull(entity, dp.temperature);
    config.temperature_command_topic = info.commandTopic;
    config.temperature_command_template = commandFor(entity, dp.temperature, "(value | float)", info);
    if (dp.temperature.range) {
      config.min_temp = Number(dp.temperature.range.min);
      config.max_temp = Number(dp.temperature.range.max);
      config.temp_step = Number(dp.temperature.range.step || 1);
    }
  }
  for (const [role, low] of [["target_temp_low", true], ["target_temp_high", false]]) {
    const d = dp[role];
    if (!d) continue;
    const prefix = low ? "temperature_low" : "temperature_high";
    config[`${prefix}_state_topic`] = info.stateTopic;
    config[`${prefix}_state_template`] = stateTemplateFull(entity, d);
    config[`${prefix}_command_topic`] = info.commandTopic;
    config[`${prefix}_command_template`] = commandFor(entity, d, "(value | float)", info);
    if (d.range) {
      if (low) config.min_temp = Number(d.range.min);
      else config.max_temp = Number(d.range.max);
    }
  }
  if (dp.hvac_mode) {
    let modes = visibleValues(dp.hvac_mode, { normalize: text });
    if (dp.hvac_mode.rawtype === "boolean") modes = ["off", "heat"];
    if (modes.length) {
      config.modes = modes;
      config.mode_state_topic = info.stateTopic;
      config.mode_state_template = dp.hvac_mode.rawtype === "boolean"
        ? stateTemplateFull(entity, normalizeDp(dp.hvac_mode, (v) => (boolValue(v) ? "heat" : "off")))
        : stateTemplateFull(entity, normalizeDp(dp.hvac_mode, text));
      config.mode_command_topic = info.commandTopic;
      config.mode_command_template = dp.hvac_mode.rawtype === "boolean"
        ? commandFor(entity, normalizeDp(dp.hvac_mode, boolValue), "(value != 'off')", info)
        : commandFor(entity, normalizeDp(dp.hvac_mode, text), "value", info);
    }
  }
  if (dp.hvac_action) {
    config.action_topic = info.stateTopic;
    config.action_template = stateTemplateFull(entity, normalizeDp(dp.hvac_action, text));
  }
  for (const [role, prefix] of [["fan_mode", "fan"], ["swing_mode", "swing"], ["swing_horizontal_mode", "swing_horizontal"]]) {
    const d = dp[role];
    if (!d) continue;
    const values = visibleValues(d, { normalize: text });
    if (values.length) config[`${prefix}_modes`] = values;
    config[`${prefix}_mode_state_topic`] = info.stateTopic;
    config[`${prefix}_mode_state_template`] = stateTemplateFull(entity, normalizeDp(d, text));
    config[`${prefix}_mode_command_topic`] = info.commandTopic;
    config[`${prefix}_mode_command_template`] = commandFor(entity, normalizeDp(d, text), "value", info);
  }
  if (dp.preset_mode) {
    const values = visibleValues(dp.preset_mode, { normalize: text });
    if (values.length) config.preset_modes = values;
    config.preset_mode_state_topic = info.stateTopic;
    config.preset_mode_state_template = stateTemplateFull(entity, normalizeDp(dp.preset_mode, text));
    config.preset_mode_command_topic = info.commandTopic;
    config.preset_mode_command_template = commandFor(entity, normalizeDp(dp.preset_mode, text), "value", info);
  }
  if (dp.humidity) {
    config.target_humidity_state_topic = info.stateTopic;
    config.target_humidity_state_template = stateTemplateFull(entity, dp.humidity);
    config.target_humidity_command_topic = info.commandTopic;
    config.target_humidity_command_template = commandFor(entity, dp.humidity, "(value | float)", info);
    if (dp.humidity.range) {
      config.min_humidity = Number(dp.humidity.range.min);
      config.max_humidity = Number(dp.humidity.range.max);
    }
  }

  addAttributes(config, entity, info);
  return [{ component: "climate", object_id: `e${entity.index}`, config }];
}

function buildWaterHeater(entity, info) {
  const temperature = use(entity, "temperature");
  const current = use(entity, "current_temperature");
  const mode = use(entity, "operation_mode");
  const away = use(entity, "away_mode");
  if (!temperature && !mode && !away) return [];
  const config = { name: entity.name, mode_state_topic: info.stateTopic, temperature_state_topic: info.stateTopic, current_temperature_topic: info.stateTopic };
  if (entity.category) config.entity_category = entity.category;
  if (entity.icon) config.icon = entity.icon;
  if (entity.hidden || entity.deprecated) config.enabled_by_default = false;
  if (current) config.current_temperature_template = stateTemplateFull(entity, current);
  if (temperature) {
    config.temperature_state_template = stateTemplateFull(entity, temperature);
    config.temperature_command_topic = info.commandTopic;
    config.temperature_command_template = commandFor(entity, temperature, "(value | float)", info);
    if (temperature.range) {
      config.min_temp = Number(temperature.range.min);
      config.max_temp = Number(temperature.range.max);
    }
  }
  if (mode) {
    let modes = visibleValues(mode, { normalize: text });
    if (mode.rawtype === "boolean") modes = ["off", "heat"];
    if (modes.length) {
      config.modes = modes;
      config.mode_state_template = mode.rawtype === "boolean"
        ? stateTemplateFull(entity, normalizeDp(mode, (v) => (boolValue(v) ? "heat" : "off")))
        : stateTemplateFull(entity, normalizeDp(mode, text));
      config.mode_command_topic = info.commandTopic;
      config.mode_command_template = mode.rawtype === "boolean"
        ? commandFor(entity, normalizeDp(mode, boolValue), "(value != 'off')", info)
        : commandFor(entity, normalizeDp(mode, text), "value", info);
    }
  }
  addAttributes(config, entity, info);
  const items = [{ component: "water_heater", object_id: `e${entity.index}`, config }];
  if (away && away.rawtype === "boolean") {
    const awayConfig = {
      name: `${entity.name} away`,
      state_topic: info.stateTopic,
      value_template: boolStateTemplate(entity, away),
      command_topic: info.commandTopic,
      command_template: commandFor(entity, normalizeDp(away, boolValue), "(true if value == 'ON' else false)", info),
      payload_on: "ON",
      payload_off: "OFF",
      entity_category: "config",
    };
    items.push({ component: "switch", object_id: `e${entity.index}_away`, config: awayConfig });
  }
  return items;
}

function buildHumidifier(entity, info) {
  const switchDp = use(entity, "switch");
  const humidity = use(entity, "humidity");
  const current = use(entity, "current_humidity");
  const mode = use(entity, "mode");
  const action = use(entity, "action");
  if (!switchDp && !humidity && !mode) return [];
  const config = {
    name: entity.name,
    state_topic: info.stateTopic,
    command_topic: info.commandTopic,
    target_humidity_command_topic: info.commandTopic,
  };
  if (entity.category) config.entity_category = entity.category;
  if (entity.icon) config.icon = entity.icon;
  if (entity.hidden || entity.deprecated) config.enabled_by_default = false;
  if (entity.class === "dehumidifier") config.device_class = "dehumidifier";
  else if (entity.class) config.device_class = entity.class;

  if (switchDp) {
    config.state_value_template = boolStateTemplate(entity, switchDp);
    config.command_template = commandFor(entity, normalizeDp(switchDp, boolValue), "(true if value == 'ON' else false)", info);
    config.payload_on = "ON";
    config.payload_off = "OFF";
  } else {
    config.state_value_template = "{{ 'ON' }}";
    config.command_template = "{{ {} | tojson }}";
    config.payload_on = "ON";
    config.payload_off = "OFF";
  }
  if (humidity) {
    config.target_humidity_state_topic = info.stateTopic;
    config.target_humidity_state_template = stateTemplateFull(entity, humidity);
    config.target_humidity_command_template = commandFor(entity, humidity, "(value | float)", info);
    if (humidity.range) {
      config.min_humidity = Number(humidity.range.min);
      config.max_humidity = Number(humidity.range.max);
    }
  } else {
    config.target_humidity_command_template = "{{ {} | tojson }}";
  }
  if (current) {
    config.current_humidity_topic = info.stateTopic;
    config.current_humidity_template = stateTemplateFull(entity, current);
  }
  if (mode) {
    const values = visibleValues(mode, { normalize: text });
    if (values.length) config.modes = values;
    config.mode_state_topic = info.stateTopic;
    config.mode_state_template = stateTemplateFull(entity, normalizeDp(mode, text));
    config.mode_command_topic = info.commandTopic;
    config.mode_command_template = commandFor(entity, normalizeDp(mode, text), "value", info);
  }
  if (action) {
    config.action_topic = info.stateTopic;
    config.action_template = stateTemplateFull(entity, normalizeDp(action, text));
  }
  addAttributes(config, entity, info);
  return [{ component: "humidifier", object_id: `e${entity.index}`, config }];
}

// ---------------------------------------------------------------------------
// Lock / alarm / siren / valve
// ---------------------------------------------------------------------------

function buildLock(entity, info) {
  const lock = use(entity, "lock");
  const lockState = use(entity, "lock_state");
  const open = use(entity, "open");
  const jammed = use(entity, "jammed");
  if (!lock && !lockState && !open && !jammed) return [];
  const config = { name: entity.name, state_topic: info.stateTopic, command_topic: info.commandTopic };
  if (entity.category) config.entity_category = entity.category;
  if (entity.icon) config.icon = entity.icon;
  if (entity.hidden || entity.deprecated) config.enabled_by_default = false;

  let code = "";
  let expr = "none";
  if (jammed) {
    const j = stateFor(entity, normalizeDp(jammed, boolValue));
    code += j.code;
    expr = `('JAMMED' if ${j.var} else ${expr})`;
  }
  if (open) {
    const o = stateFor(entity, normalizeDp(open, boolValue));
    code += o.code;
    expr = `('OPEN' if ${o.var} else ${expr})`;
  }
  if (lockState) {
    const s = stateFor(entity, normalizeDp(lockState, boolValue));
    code += s.code;
    expr = `('LOCKED' if ${s.var} else 'UNLOCKED')`;
  } else if (lock) {
    const s = stateFor(entity, normalizeDp(lock, boolValue));
    code += s.code;
    expr = `('LOCKED' if ${s.var} else 'UNLOCKED')`;
  }
  config.value_template = `{% set dps = value_json.dps %}\n${code}{{ ${expr} }}`;
  if (lock) {
    const lockRaw = rawForLogical(entity, lock, true);
    const unlockRaw = rawForLogical(entity, lock, false);
    if (lockRaw !== null) config.payload_lock = JSON.stringify({ [lock.id]: lockRaw });
    if (unlockRaw !== null) config.payload_unlock = JSON.stringify({ [lock.id]: unlockRaw });
  }
  if (open) {
    const openRaw = rawForLogical(entity, open, true);
    if (openRaw !== null) config.payload_open = JSON.stringify({ [open.id]: openRaw });
  }
  addAttributes(config, entity, info);
  return [{ component: "lock", object_id: `e${entity.index}`, config }];
}

function buildAlarm(entity, info) {
  const state = use(entity, "alarm_state");
  const trigger = use(entity, "trigger");
  if (!state && !trigger) return [];
  const config = { name: entity.name, state_topic: info.stateTopic, command_topic: info.commandTopic };
  if (entity.category) config.entity_category = entity.category;
  if (entity.icon) config.icon = entity.icon;
  if (entity.hidden || entity.deprecated) config.enabled_by_default = false;
  config.code_arm_required = false;
  config.code_disarm_required = false;
  config.code_trigger_required = false;

  let code = "";
  let expr = "none";
  if (trigger) {
    const t = stateFor(entity, normalizeDp(trigger, boolValue));
    code += t.code;
    expr = `('triggered' if ${t.var} else ${expr})`;
  }
  if (state) {
    const s = stateFor(entity, normalizeDp(state, text));
    code += s.code;
    expr = `(${s.var} if ${s.var} is not none else ${expr})`;
  }
  config.value_template = `{% set dps = value_json.dps %}\n${code}{{ ${expr} }}`;

  const commands = {
    payload_disarm: "disarmed",
    payload_arm_home: "armed_home",
    payload_arm_away: "armed_away",
    payload_arm_night: "armed_night",
    payload_arm_vacation: "armed_vacation",
    payload_arm_custom_bypass: "armed_custom_bypass",
  };
  if (state) {
    for (const [key, logical] of Object.entries(commands)) {
      const raw = rawForLogical(entity, state, logical);
      if (raw !== null) config[key] = JSON.stringify({ [state.id]: raw });
    }
  }
  if (trigger) {
    const raw = rawForLogical(entity, trigger, true);
    if (raw !== null) config.payload_trigger = JSON.stringify({ [trigger.id]: raw });
  }
  addAttributes(config, entity, info);
  return [{ component: "alarm_control_panel", object_id: `e${entity.index}`, config }];
}

function buildSiren(entity, info) {
  const switchDp = use(entity, "switch");
  const tone = use(entity, "tone");
  const volume = use(entity, "volume_level");
  const duration = use(entity, "duration");
  if (!switchDp && !tone) return [];
  const config = { name: entity.name, state_topic: info.stateTopic, command_topic: info.commandTopic };
  if (entity.category) config.entity_category = entity.category;
  if (entity.icon) config.icon = entity.icon;
  if (entity.hidden || entity.deprecated) config.enabled_by_default = false;

  let code = "";
  let expr = "none";
  if (switchDp) {
    const s = stateFor(entity, normalizeDp(switchDp, boolValue));
    code += s.code;
    expr = `('ON' if ${s.var} else 'OFF')`;
  } else if (tone) {
    const t = stateFor(entity, tone);
    code += t.code;
    expr = `('OFF' if ${t.var} | string == 'off' else 'ON')`;
  }
  config.state_value_template = `{% set dps = value_json.dps %}\n${code}{{ ${expr} }}`;
  config.command_template = commandForMany(entity, [
    switchDp ? { dp: switchDp, target: "true" } : null,
    tone ? { dp: tone, target: "tone" } : null,
    duration ? { dp: duration, target: "duration" } : null,
    volume ? { dp: volume, target: "volume_level" } : null,
  ].filter(Boolean), info);
  if (switchDp) {
    const off = staticFragment(entity, switchDp, false);
    if (off) config.command_off_template = off;
    else config.command_off_template = "{{ {} | tojson }}";
  } else if (tone) {
    const off = staticFragment(entity, tone, "off");
    if (off) config.command_off_template = off;
  }
  if (tone) {
    const tones = visibleValues(tone, { normalize: text }).filter((v) => String(v).toLowerCase() !== "off");
    if (tones.length) config.available_tones = tones;
    config.support_duration = Boolean(duration);
    config.support_volume_set = Boolean(volume);
  }
  addAttributes(config, entity, info);
  return [{ component: "siren", object_id: `e${entity.index}`, config }];
}

function buildValve(entity, info) {
  const valve = use(entity, "valve");
  const switchDp = use(entity, "switch");
  const current = use(entity, "current_position");
  if (!valve && !switchDp) return [];
  const config = { name: entity.name, state_topic: info.stateTopic, command_topic: info.commandTopic };
  if (entity.category) config.entity_category = entity.category;
  if (entity.icon) config.icon = entity.icon;
  if (entity.hidden || entity.deprecated) config.enabled_by_default = false;
  if (entity.class) config.device_class = entity.class;

  const reportsPosition = valve && (valve.rawtype === "integer" || visibleValues(valve).some((v) => typeof v === "number"));
  config.reports_position = Boolean(reportsPosition);
  let code = "";
  let expr = "'closed'";
  if (current) {
    const c = stateFor(entity, current);
    code += c.code;
    expr = `('closed' if (${c.var} | float) <= 0 else 'open')`;
  } else if (switchDp) {
    const s = stateFor(entity, normalizeDp(switchDp, boolValue));
    code += s.code;
    expr = `('open' if ${s.var} else 'closed')`;
  } else if (valve) {
    const v = stateFor(entity, valve);
    code += v.code;
    expr = `('open' if ${v.var} else 'closed')`;
  }
  config.value_template = `{% set dps = value_json.dps %}\n${code}{{ ${expr} }}`;
  if (reportsPosition) {
    config.set_position_topic = info.commandTopic;
    config.set_position_template = commandFor(entity, valve, "(position | float)", info);
  }
  if (switchDp) {
    const on = staticFragment(entity, switchDp, true);
    const off = staticFragment(entity, switchDp, false);
    if (on) config.payload_open = on;
    if (off) config.payload_close = off;
  } else if (reportsPosition) {
    const openRaw = rawForLogical(entity, valve, 100);
    const closeRaw = rawForLogical(entity, valve, 0);
    if (openRaw !== null) config.payload_open = JSON.stringify({ [valve.id]: openRaw });
    if (closeRaw !== null) config.payload_close = JSON.stringify({ [valve.id]: closeRaw });
  }
  addAttributes(config, entity, info);
  return [{ component: "valve", object_id: `e${entity.index}`, config }];
}

// ---------------------------------------------------------------------------
// Event / time / datetime / lawn mower / camera
// ---------------------------------------------------------------------------

function buildEvent(entity, info) {
  const event = use(entity, "event");
  if (!event) return [];
  const types = visibleValues(event, { normalize: text });
  const config = {
    name: entity.name,
    state_topic: info.stateTopic,
    event_types: types.length ? types : ["unknown"],
    value_template: stateTemplateFull(entity, normalizeDp(event, text), {
      transform: (v) => `{{ {'event_type': ${v}} | tojson }}`,
    }),
  };
  if (entity.category) config.entity_category = entity.category;
  if (entity.icon) config.icon = entity.icon;
  if (entity.hidden || entity.deprecated) config.enabled_by_default = false;
  addAttributes(config, entity, info);
  return [{ component: "event", object_id: `e${entity.index}`, config }];
}

function buildTime(entity, info) {
  const hour = use(entity, "hour");
  const minute = use(entity, "minute");
  const second = use(entity, "second");
  const hms = use(entity, "hms");
  if (!hour && !minute && !second && !hms) return [];
  const config = { name: entity.name, state_topic: info.stateTopic, command_topic: info.commandTopic };
  if (entity.category) config.entity_category = entity.category;
  if (entity.icon) config.icon = entity.icon;
  if (entity.hidden || entity.deprecated) config.enabled_by_default = false;

  if (hms) {
    config.value_template = stateTemplateFull(entity, hms, { transform: (v) => `{{ ${v} | default('00:00:00') }}` });
    config.command_template = "{{ {} | tojson }}";
  } else {
    let code = "{% set dps = value_json.dps %}\n";
    const vars = {};
    for (const [name, dp] of [["h", hour], ["m", minute], ["s", second]]) {
      if (!dp) continue;
      const st = stateFor(entity, dp);
      code += st.code;
      vars[name] = st.var;
    }
    code += `{{ "%02d:%02d:%02d" | format(${vars.h || 0}, ${vars.m || 0}, ${vars.s || 0}) }}`;
    config.value_template = code;
    const writes = [];
    if (hour) writes.push({ dp: hour, target: "(value.hour)" });
    if (minute) writes.push({ dp: minute, target: "(value.minute)" });
    else if (hour) writes.length = 0;
    if (second) writes.push({ dp: second, target: "(value.second)" });
    if (minute && hour) {
      config.command_template = commandForMany(entity, writes, info);
    } else if (hour) {
      config.command_template = commandForMany(entity, [{ dp: hour, target: "(value.hour * 60 + value.minute)" }], info);
    } else if (minute) {
      config.command_template = commandForMany(entity, [{ dp: minute, target: "(value.minute)" }], info);
    } else {
      config.command_template = "{{ {} | tojson }}";
    }
  }
  addAttributes(config, entity, info);
  return [{ component: "time", object_id: `e${entity.index}`, config }];
}

function buildDatetime(entity, info) {
  const roles = ["year", "month", "day", "hour", "minute", "second"];
  const dp = {};
  for (const role of roles) dp[role] = use(entity, role);
  if (!roles.some((role) => dp[role])) return [];
  const config = { name: entity.name, state_topic: info.stateTopic, command_topic: info.commandTopic };
  if (entity.category) config.entity_category = entity.category;
  if (entity.icon) config.icon = entity.icon;
  if (entity.hidden || entity.deprecated) config.enabled_by_default = false;
  const epoch = dp.second && dp.second.rawtype === "unixtime" && !dp.year && !dp.month && !dp.day && !dp.hour && !dp.minute;
  if (epoch) {
    config.value_template = stateTemplateFull(entity, dp.second, {
      transform: (v) => `{{ ${v} | timestamp_custom('%Y-%m-%d %H:%M:%S') }}`,
    });
    config.command_template = commandFor(entity, dp.second, "(value | as_timestamp | int)", info);
  } else {
    let code = "{% set dps = value_json.dps %}\n";
    const vars = {};
    for (const role of roles) {
      if (!dp[role]) continue;
      const st = stateFor(entity, dp[role]);
      code += st.code;
      vars[role] = st.var;
    }
    code += `{{ "%04d-%02d-%02d %02d:%02d:%02d" | format(${vars.year || 1970}, ${vars.month || 1}, ${vars.day || 1}, ${vars.hour || 0}, ${vars.minute || 0}, ${vars.second || 0}) }}`;
    config.value_template = code;
    const writes = [];
    if (dp.year) writes.push({ dp: dp.year, target: "(value.year)" });
    if (dp.month) writes.push({ dp: dp.month, target: "(value.month)" });
    if (dp.day) writes.push({ dp: dp.day, target: "(value.day)" });
    if (dp.hour) writes.push({ dp: dp.hour, target: "(value.hour)" });
    if (dp.minute) writes.push({ dp: dp.minute, target: "(value.minute)" });
    if (dp.second) writes.push({ dp: dp.second, target: "(value.second)" });
    config.command_template = writes.length ? commandForMany(entity, writes, info) : "{{ {} | tojson }}";
  }
  addAttributes(config, entity, info);
  return [{ component: "datetime", object_id: `e${entity.index}`, config }];
}

function buildLawnMower(entity, info) {
  const activity = use(entity, "activity");
  const command = use(entity, "command");
  if (!activity && !command) return [];
  const config = { name: entity.name, activity_state_topic: info.stateTopic, command_topic: info.commandTopic };
  if (entity.category) config.entity_category = entity.category;
  if (entity.icon) config.icon = entity.icon;
  if (entity.hidden || entity.deprecated) config.enabled_by_default = false;
  if (activity) config.activity_value_template = stateTemplateFull(entity, normalizeDp(activity, text));
  const values = command ? visibleValues(command, { normalize: text }) : [];
  for (const [key, value] of [["start_mowing", "start_mowing"], ["pause", "pause"], ["dock", "dock"]]) {
    if (!command || !values.includes(value)) continue;
    const raw = rawForLogical(entity, command, value);
    if (raw === null) continue;
    config[`${key}_command_topic`] = info.commandTopic;
    config[`${key}_command_template`] = JSON.stringify({ [command.id]: raw });
  }
  addAttributes(config, entity, info);
  return [{ component: "lawn_mower", object_id: `e${entity.index}`, config }];
}

function buildCamera(entity, info) {
  const snapshot = use(entity, "snapshot");
  const switchDp = use(entity, "switch");
  const motion = use(entity, "motion_enable");
  const record = use(entity, "record");
  const items = [];
  if (snapshot && (snapshot.rawtype === "base64" || snapshot.rawtype === "utf16b64")) {
    const config = {
      name: entity.name,
      topic: `${info.stateTopic}/${snapshot.id}`,
      image_encoding: "b64",
    };
    if (entity.icon) config.icon = entity.icon;
    if (entity.hidden || entity.deprecated) config.enabled_by_default = false;
    items.push({ component: "camera", object_id: `e${entity.index}`, config });
  }
  if (switchDp) {
    const cfg = {
      name: `${entity.name} power`,
      state_topic: info.stateTopic,
      value_template: boolStateTemplate(entity, switchDp),
      command_topic: info.commandTopic,
      command_template: commandFor(entity, normalizeDp(switchDp, boolValue), "(true if value == 'ON' else false)", info),
      payload_on: "ON",
      payload_off: "OFF",
      entity_category: "config",
    };
    items.push({ component: "switch", object_id: `e${entity.index}_power`, config: cfg });
  }
  if (motion) {
    const cfg = {
      name: `${entity.name} motion detection`,
      state_topic: info.stateTopic,
      value_template: boolStateTemplate(entity, motion),
      command_topic: info.commandTopic,
      command_template: commandFor(entity, normalizeDp(motion, boolValue), "(true if value == 'ON' else false)", info),
      payload_on: "ON",
      payload_off: "OFF",
      entity_category: "config",
    };
    items.push({ component: "switch", object_id: `e${entity.index}_motion`, config: cfg });
  }
  if (record) {
    const cfg = {
      name: `${entity.name} recording`,
      state_topic: info.stateTopic,
      value_template: boolStateTemplate(entity, record),
      entity_category: "diagnostic",
    };
    items.push({ component: "binary_sensor", object_id: `e${entity.index}_record`, config: cfg });
  }
  if (!items.length) return [];
  addAttributes({ ...items[0].config }, entity, info);
  return items;
}

// ---------------------------------------------------------------------------
// Decomposed platforms
// ---------------------------------------------------------------------------

function buildGenericWritable(entity, dp, info, name, componentHint, category = "config") {
  if (!dp) return null;
  const base = {
    name,
    state_topic: info.stateTopic,
    entity_category: category,
  };
  if (entity.icon) base.icon = entity.icon;
  if (entity.hidden || entity.deprecated) base.enabled_by_default = false;
  if (componentHint === "switch" || dp.rawtype === "boolean") {
    return {
      component: "switch",
      object_id: `e${entity.index}_${slugify(dp.name, 18)}`,
      config: {
        ...base,
        value_template: boolStateTemplate(entity, dp),
        command_topic: info.commandTopic,
        command_template: commandFor(entity, normalizeDp(dp, boolValue), "(true if value == 'ON' else false)", info),
        payload_on: "ON",
        payload_off: "OFF",
      },
    };
  }
  if (componentHint === "number" || dp.rawtype === "integer" || dp.rawtype === "float") {
    return {
      component: "number",
      object_id: `e${entity.index}_${slugify(dp.name, 18)}`,
      config: {
        ...base,
        value_template: numberStateTemplate(entity, dp),
        command_topic: info.commandTopic,
        command_template: commandFor(entity, normalizeDp(dp, numberValue), "(value | float)", info),
        min: dp.range?.min ?? 0,
        max: dp.range?.max ?? 100,
        step: dp.range?.step ?? 1,
        mode: "box",
      },
    };
  }
  if (componentHint === "select" || (dp.mapping.length && visibleValues(dp).length)) {
    const options = visibleValues(dp, { normalize: text });
    if (options.length) {
      return {
        component: "select",
        object_id: `e${entity.index}_${slugify(dp.name, 18)}`,
        config: {
          ...base,
          value_template: stateTemplateFull(entity, normalizeDp(dp, text)),
          command_topic: info.commandTopic,
          command_template: commandFor(entity, normalizeDp(dp, text), "value", info),
          options,
        },
      };
    }
  }
  return {
    component: "text",
    object_id: `e${entity.index}_${slugify(dp.name, 18)}`,
    config: {
      ...base,
      value_template: textStateTemplate(entity, dp),
      command_topic: info.commandTopic,
      command_template: commandFor(entity, dp, "value", info),
    },
  };
}

function buildMediaPlayer(entity, info) {
  const items = [];
  const state = use(entity, "playback_state");
  const power = use(entity, "switch");
  const volume = use(entity, "volume");
  const mute = use(entity, "mute");
  const source = use(entity, "source");
  const play = use(entity, "play");
  const pause = use(entity, "pause");
  const stop = use(entity, "stop");
  const prev = use(entity, "prev");
  const next = use(entity, "next");
  const shuffle = use(entity, "shuffle");
  const repeat = use(entity, "repeat");
  const seek = use(entity, "seek_position");
  if (state) {
    items.push({
      component: "sensor",
      object_id: `e${entity.index}_state`,
      config: { name: `${entity.name} state`, state_topic: info.stateTopic, value_template: stateTemplateFull(entity, normalizeDp(state, text)), icon: "mdi:play-circle" },
    });
  }
  for (const [dp, name, hint] of [
    [power, "Power", "switch"], [volume, "Volume", "number"], [mute, "Mute", "switch"],
    [source, "Source", "select"], [shuffle, "Shuffle", "switch"], [repeat, "Repeat", "select"],
    [seek, "Seek", "number"],
  ]) {
    if (!dp) continue;
    const item = buildGenericWritable(entity, dp, info, `${entity.name} ${name}`, hint);
    if (item) items.push(item);
  }
  for (const [dp, name] of [[play, "Play"], [pause, "Pause"], [stop, "Stop"], [prev, "Previous"], [next, "Next"]]) {
    if (!dp) continue;
    const raw = rawForLogical(entity, dp, true);
    if (raw === null) continue;
    items.push({
      component: "button",
      object_id: `e${entity.index}_${slugify(name, 18)}`,
      config: { name: `${entity.name} ${name}`, command_topic: info.commandTopic, payload_press: JSON.stringify({ [dp.id]: raw }), entity_category: "config" },
    });
  }
  return items;
}

function buildVacuum(entity, info) {
  const status = use(entity, "status");
  const command = use(entity, "command");
  const power = use(entity, "power");
  const activate = use(entity, "activate");
  const locate = use(entity, "locate");
  const error = use(entity, "error");
  const fanSpeed = use(entity, "fan_speed");
  const battery = use(entity, "battery") || use(entity, "battery_level");
  if (!status && !command && !power && !activate) return [];
  const items = [];
  if (status) {
    items.push({
      component: "sensor",
      object_id: `e${entity.index}_status`,
      config: {
        name: `${entity.name} status`,
        state_topic: info.stateTopic,
        value_template: stateTemplateFull(entity, normalizeDp(status, text)),
        icon: "mdi:robot-vacuum",
      },
    });
  }
  if (error) {
    const cfg = {
      name: `${entity.name} error`,
      state_topic: info.stateTopic,
      value_template: boolStateTemplate(entity, error),
      entity_category: "diagnostic",
    };
    items.push({ component: "binary_sensor", object_id: `e${entity.index}_error`, config: cfg });
  }
  if (battery) {
    const cfg = {
      name: `${entity.name} battery`,
      state_topic: info.stateTopic,
      value_template: stateTemplateFull(entity, battery),
      device_class: "battery",
      unit_of_measurement: "%",
      entity_category: "diagnostic",
    };
    items.push({ component: "sensor", object_id: `e${entity.index}_battery`, config: cfg });
  }
  if (power) {
    const cfg = {
      name: `${entity.name} power`,
      state_topic: info.stateTopic,
      value_template: boolStateTemplate(entity, power),
      command_topic: info.commandTopic,
      command_template: commandFor(entity, normalizeDp(power, boolValue), "(true if value == 'ON' else false)", info),
      payload_on: "ON",
      payload_off: "OFF",
      entity_category: "config",
    };
    items.push({ component: "switch", object_id: `e${entity.index}_power`, config: cfg });
  }
  if (fanSpeed) {
    const options = visibleValues(fanSpeed, { normalize: text });
    if (options.length) {
      const cfg = {
        name: `${entity.name} fan speed`,
        state_topic: info.stateTopic,
        value_template: stateTemplateFull(entity, normalizeDp(fanSpeed, text)),
        command_topic: info.commandTopic,
        command_template: commandFor(entity, normalizeDp(fanSpeed, text), "value", info),
        options,
        entity_category: "config",
      };
      items.push({ component: "select", object_id: `e${entity.index}_fan`, config: cfg });
    }
  }
  const addStaticButton = (suffix, label, dp, value) => {
    if (!dp) return;
    const raw = rawForLogical(entity, dp, value);
    if (raw === null || raw === undefined) return;
    items.push({
      component: "button",
      object_id: `e${entity.index}_${suffix}`,
      config: {
        name: `${entity.name} ${label}`,
        command_topic: info.commandTopic,
        payload_press: JSON.stringify({ [dp.id]: raw }),
        entity_category: "config",
      },
    });
  };
  if (activate) {
    addStaticButton("start", "start", activate, true);
    addStaticButton("pause", "pause", activate, false);
  }
  const commandSink = command || status;
  if (commandSink) {
    addStaticButton("start", "start", commandSink, "start");
    addStaticButton("pause", "pause", commandSink, "pause");
    addStaticButton("stop", "stop", commandSink, "stop");
    addStaticButton("dock", "dock", commandSink, "return_to_base");
    addStaticButton("spot", "spot clean", commandSink, "clean_spot");
  }
  if (locate) addStaticButton("locate", "locate", locate, true);
  return items;
}

function buildRemote(entity, info) {
  const items = [];
  const send = use(entity, "send");
  const receive = use(entity, "receive");
  const control = use(entity, "control");
  if (send) {
    const item = buildGenericWritable(entity, send, info, `${entity.name} send`, "text");
    if (item) items.push(item);
  }
  if (receive) {
    items.push({
      component: "sensor",
      object_id: `e${entity.index}_receive`,
      config: { name: `${entity.name} received code`, state_topic: info.stateTopic, value_template: textStateTemplate(entity, receive), entity_category: "diagnostic" },
    });
  }
  if (control) {
    const item = buildGenericWritable(entity, control, info, `${entity.name} control`, "select");
    if (item) items.push(item);
  }
  return items;
}

function buildInfrared(entity, info) {
  const send = use(entity, "send");
  const control = use(entity, "control");
  const items = [];
  if (send) {
    // Emit a text entity carrying the Tuya base64 code. For split-DP devices
    // the user also gets a control selector below; the code is still exposed.
    const item = buildGenericWritable(entity, send, info, `${entity.name} code`, "text");
    if (item) items.push(item);
  }
  if (control) {
    const item = buildGenericWritable(entity, control, info, `${entity.name} control`, "select");
    if (item) items.push(item);
  }
  return items;
}

// ---------------------------------------------------------------------------
// Generic entity / DP fallback
// ---------------------------------------------------------------------------

function genericDpName(entity, dp, used) {
  let base = dp.name && !["sensor", "value", "switch", "button"].includes(dp.name)
    ? `${entity.name} ${titleCase(dp.name)}`
    : entity.name;
  base = base.slice(0, 31);
  let name = base;
  let suffix = 2;
  while (used.has(name)) {
    name = `${base.slice(0, 28)} ${suffix}`;
    suffix += 1;
  }
  used.add(name);
  return name;
}

function buildGenericEntity(entity, info) {
  const items = [];
  const usedNames = new Set();
  for (const dp of entity.dps) {
    if (entity.consumed.has(dp.name)) continue;
    const name = genericDpName(entity, dp, usedNames);
    const objectId = `e${entity.index}_d${dp.id}`;
    const config = {
      name,
      state_topic: info.stateTopic,
      entity_category: entity.category || "diagnostic",
    };
    if (entity.hidden || entity.deprecated) config.enabled_by_default = false;
    if (dp.rawtype === "boolean") {
      config.value_template = boolStateTemplate(entity, dp);
      config.payload_on = "ON";
      config.payload_off = "OFF";
      items.push({ component: "binary_sensor", object_id: objectId, config });
    } else {
      const template = stateTemplateFull(entity, dp, { transform: (v) => `{{ ${v} }}` });
      if (dp.mapping.length) {
        const options = visibleValues(dp, { normalize: text });
        if (options.length && options.every((v) => typeof v === "string")) {
          config.device_class = "enum";
          config.options = options;
        }
      }
      if (dp.unit) config.unit_of_measurement = dp.unit;
      if (dp.class && ["measurement", "total", "total_increasing"].includes(dp.class)) config.state_class = dp.class;
      config.value_template = template;
      items.push({ component: "sensor", object_id: objectId, config });
    }
  }
  return items;
}

function buildSimpleEntity(entity, info) {
  switch (entity.type) {
    case "sensor": return buildSensor(entity, info);
    case "binary_sensor": return buildBinarySensor(entity, info);
    case "switch": return buildSwitch(entity, info);
    case "number": return buildNumber(entity, info);
    case "select": return buildSelect(entity, info);
    case "text": return buildText(entity, info);
    case "button": return buildButton(entity, info);
    default: return [];
  }
}

function buildNative(entity, info) {
  if (SIMPLE_ENTITIES.has(entity.type)) return buildSimpleEntity(entity, info);
  switch (entity.type) {
    case "light": return buildLight(entity, info);
    case "fan": return buildFan(entity, info);
    case "cover": return buildCover(entity, info);
    case "climate": return buildClimate(entity, info);
    case "water_heater": return buildWaterHeater(entity, info);
    case "humidifier": return buildHumidifier(entity, info);
    case "lock": return buildLock(entity, info);
    case "alarm_control_panel": return buildAlarm(entity, info);
    case "siren": return buildSiren(entity, info);
    case "valve": return buildValve(entity, info);
    case "event": return buildEvent(entity, info);
    case "time": return buildTime(entity, info);
    case "datetime": return buildDatetime(entity, info);
    case "lawn_mower": return buildLawnMower(entity, info);
    case "camera": return buildCamera(entity, info);
    case "vacuum": return buildVacuum(entity, info);
    case "media_player": return buildMediaPlayer(entity, info);
    case "remote": return buildRemote(entity, info);
    case "infrared": return buildInfrared(entity, info);
    default: return [];
  }
}

export function buildPack(yaml, meta, info) {
  if (!isObject(yaml) || !Array.isArray(yaml.entities) || !yaml.entities.length) {
    throw new Error("descriptor has no entities");
  }
  const infoSafe = {
    stateTopic: info?.stateTopic || "__STATE_TOPIC__",
    commandTopic: info?.commandTopic || "__CMD_TOPIC__",
    rawEntity: info?.rawEntity || "__RAW_ENTITY__",
  };
  const items = [];
  const warnings = [];
  for (let index = 0; index < yaml.entities.length; index += 1) {
    const raw = yaml.entities[index];
    if (!isObject(raw)) continue;
    const entity = parseEntity(raw, index);
    let native = [];
    try {
      native = NATIVE_ENTITIES.has(entity.type) ? buildNative(entity, infoSafe) : [];
    } catch (error) {
      warnings.push(`${entity.type}: ${error.message}`);
      native = [];
    }
    if (native.length) {
      for (const item of native) addItem(items, item.component, item.object_id, item.config);
    } else {
      const fallback = buildGenericEntity(entity, infoSafe);
      for (const item of fallback) addItem(items, item.component, item.object_id, item.config);
    }
  }
  if (!items.length) {
    // Last-resort fallback: expose every DP as a sensor.
    for (const raw of yaml.entities) {
      if (!isObject(raw)) continue;
      const entity = parseEntity(raw, yaml.entities.indexOf(raw));
      for (const item of buildGenericEntity(entity, infoSafe)) {
        addItem(items, item.component, item.object_id, item.config);
      }
    }
  }
  const telemetry = buildTelemetry(yaml);
  const pack = {
    format: 1,
    id: slugify(meta?.id || "profile", 23),
    name: text(meta?.name || displayName(yaml)).slice(0, 47),
    device_type: text(meta?.group || classifyGroup(yaml, meta?.file)).slice(0, 23),
    source: text(meta?.source || "").slice(0, 95),
    telemetry,
    discovery: items,
    warnings,
  };
  compactPack(pack, 200000);
  return pack;
}

function itemPriority(item) {
  const component = item.component;
  let priority = 5;
  if (["light", "fan", "cover", "climate", "water_heater", "humidifier", "lock", "vacuum",
    "alarm_control_panel", "siren", "valve", "lawn_mower", "camera"].includes(component)) {
    priority = 10;
  } else if (["event", "time", "datetime"].includes(component)) {
    priority = 8;
  } else if (["switch", "number", "select", "button", "text"].includes(component)) {
    priority = 7;
  } else if (component === "binary_sensor" || component === "sensor") {
    priority = 5;
  }
  if (item.config?.entity_category === "diagnostic") priority -= 3;
  if (item.config?.enabled_by_default === false) priority -= 2;
  return priority;
}

function compactPack(pack, maxBytes = 56000) {
  if (JSON.stringify(pack).length <= maxBytes) return;
  const original = pack.discovery.length;
  const candidates = pack.discovery
    .map((item, index) => ({ item, index, priority: itemPriority(item) }))
    .sort((a, b) => a.priority - b.priority || b.index - a.index);
  for (const candidate of candidates) {
    if (pack.discovery.length <= 1) break;
    pack.discovery = pack.discovery.filter((item) => item !== candidate.item);
    if (JSON.stringify(pack).length <= maxBytes) break;
  }
  const removed = original - pack.discovery.length;
  if (removed > 0) {
    pack.warnings = [
      ...(pack.warnings || []),
      `compacted ${removed} lower-priority entities to fit the base pack limit; all DPs remain available in the raw-DP sensor`,
    ];
  }
}

function telemetryKind(entity, dp, dpIndex) {
  if (dpIndex === 0 && (entity.type === "binary_sensor" || entity.type === "switch" || dp.rawtype === "boolean")) {
    return "bool";
  }
  if (dp.rawtype === "boolean") return "bool";
  if (entity.type === "select" || entity.class === "enum") return "enum";
  const rules = mappingRules(dp);
  if (rules.some((r) => Object.prototype.hasOwnProperty.call(r, "value"))) {
    return "enum";
  }
  if (entity.type === "number" || ["integer", "float", "bitfield", "unixtime"].includes(dp.rawtype)) {
    return "number";
  }
  return "text";
}

function telemetryLabel(entity, dp, dpIndex, count) {
  const base = entityName(entity);
  if (count > 1 && dpIndex > 0) {
    return `${base} ${titleCase(dp.name || `DP ${dp.id}`)}`.trim();
  }
  return base;
}

function telemetryMap(dp) {
  const map = {};
  let size = 0;
  for (const rule of mappingRules(dp)) {
    if (!Object.prototype.hasOwnProperty.call(rule, "dps_val") || rule.dps_val === null) continue;
    const key = String(rule.dps_val);
    if (Object.prototype.hasOwnProperty.call(map, key)) continue;
    const value = Object.prototype.hasOwnProperty.call(rule, "value") ? rule.value : rule.dps_val;
    if (value === null || value === undefined) continue;
    map[key] = String(value);
    size += 1;
    if (size >= 24) break;
  }
  return map;
}

function telemetryTrueValues(dp) {
  const values = [];
  for (const rule of mappingRules(dp)) {
    if (!Object.prototype.hasOwnProperty.call(rule, "dps_val") || rule.dps_val === null) continue;
    if (!Object.prototype.hasOwnProperty.call(rule, "value")) continue;
    if (boolValue(rule.value) === true) values.push(String(rule.dps_val));
  }
  return values;
}

function semanticTelemetryKey(label, dp, entity, usedKeys) {
  const lower = `${label} ${dp.name} ${dp.unit}`.toLowerCase();
  const has = (key) => usedKeys.has(key);
  if (!has("status") && (lower.includes("status") || entity.raw?.translation_key === "status")) {
    return "status";
  }
  if (!has("fault") && (lower.includes("fault") || lower.includes("alarm") || entity.class === "problem")) {
    return "fault";
  }
  if (!has("current_limit") && (entity.type === "number" || entity.type === "select") && lower.includes("current")) {
    return "current_limit";
  }
  if (!has("power") && (lower.includes("power") || dp.unit === "kW" || dp.unit === "W")) {
    return "power";
  }
  if (!has("temperature") && (lower.includes("temperature") || dp.unit === "°C" || dp.unit === "°F")) {
    return "temperature";
  }
  if (dp.unit === "kWh") {
    if (!has("session_energy") && (lower.includes("session") || lower.includes("last"))) return "session_energy";
    if (!has("energy")) return "energy";
  }
  const phaseValue = (match) => {
    if (!match) return 0;
    if (match[1] === "a") return 1;
    if (match[1] === "b") return 2;
    if (match[1] === "c") return 3;
    return Number(match[1]);
  };
  const phaseVoltage = phaseValue(lower.match(/voltage\s+([abc])\b/) || dp.name.toLowerCase().match(/voltage_?l?([123])\b/));
  if (phaseVoltage >= 1 && phaseVoltage <= 3 && !has(`voltage_l${phaseVoltage}`)) {
    return `voltage_l${phaseVoltage}`;
  }
  const phaseCurrent = phaseValue(lower.match(/current\s+([abc])\b/) || dp.name.toLowerCase().match(/current_?l?([123])\b/));
  if (phaseCurrent >= 1 && phaseCurrent <= 3 && !has(`current_l${phaseCurrent}`)) {
    return `current_l${phaseCurrent}`;
  }
  return null;
}

export function buildTelemetry(yaml) {
  const fields = [];
  const usedKeys = new Set();
  const usedLabels = new Set();
  const entities = asArray(yaml.entities);
  for (let entityIndex = 0; entityIndex < entities.length; entityIndex += 1) {
    const raw = entities[entityIndex];
    if (!isObject(raw)) continue;
    if (["button", "camera", "remote", "infrared"].includes(raw.entity)) continue;
    const dps = asArray(raw.dps).map((dp, index) => parseDp(dp, raw, index));
    const entity = { type: raw.entity, class: text(raw.class), raw, index: entityIndex, name: entityName(raw) };
    const denseTypes = ["sensor", "binary_sensor", "climate", "water_heater", "humidifier", "cover", "light", "fan", "lock", "vacuum", "siren", "valve", "event", "time", "datetime", "lawn_mower", "media_player", "remote", "infrared"];
    for (let dpIndex = 0; dpIndex < dps.length; dpIndex += 1) {
      const dp = dps[dpIndex];
      if (!dp.name || dp.format || dp.mask) continue;
      if (dpIndex > 0 && !denseTypes.includes(raw.entity)) continue;
      const kind = telemetryKind(entity, dp, dpIndex);
      if (kind === "text" && ["time", "datetime"].includes(entity.type) === false &&
          !dp.unit && !dp.class && !mappingRules(dp).length) {
        // Plain unlabelled strings are usually commands or configuration;
        // keep the table focused on measurable telemetry.
        if (entity.type !== "sensor") continue;
      }
      let label = telemetryLabel(raw, dp, dpIndex, dps.length);
      if (!label || label.toLowerCase() === "sensor") {
        label = titleCase(dp.name || `DP ${dp.id}`);
      }
      if (usedLabels.has(label)) label = `${label} (DP ${dp.id})`;
      usedLabels.add(label);
      let key = semanticTelemetryKey(label, dp, entity, usedKeys) ||
        slugify(label, 18) || `dp_${dp.id}`;
      let suffix = 2;
      while (usedKeys.has(key)) {
        key = `${slugify(label, 16)}_${suffix}`;
        suffix += 1;
      }
      usedKeys.add(key);
      const field = {
        key,
        label,
        dp: dp.id,
        kind,
      };
      if (dpIndex === 0 && ["number", "select"].includes(raw.entity)) {
        const redirects = [];
        for (const rule of mappingRules(dp)) {
          for (const cond of asArray(rule.conditions)) {
            if (!cond || cond.hidden || !cond.value_redirect ||
                !Object.prototype.hasOwnProperty.call(cond, "dps_val") ||
                cond.dps_val === null) continue;
            const constraintName = cond.constraint || rule.constraint || dp.name;
            const constraintDp = asArray(raw.dps).find((candidate) => candidate?.name === constraintName);
            const targetDp = asArray(raw.dps).find((candidate) => candidate?.name === cond.value_redirect);
            if (!constraintDp || !targetDp) continue;
            redirects.push({
              constraint: String(constraintDp.id),
              value: String(cond.dps_val),
              dp: String(targetDp.id),
            });
          }
        }
        if (redirects.length) field.redirects = redirects.slice(0, 4);
        const primaryType = asArray(raw.dps)[0]?.type;
        const fallbacks = asArray(raw.dps).slice(1)
          .filter((candidate) => candidate && candidate.type === primaryType &&
            !/minimum|maximum/i.test(text(candidate.name)))
          .map((candidate) => String(candidate.id))
          .filter((candidate) => candidate !== dp.id);
        const unique = [...new Set(fallbacks)].slice(0, 4);
        if (unique.length) field.fallbacks = unique;
      }
      if (kind === "bool") {
        const values = telemetryTrueValues(dp);
        if (values.length) field.values = values;
      } else if (kind === "enum") {
        field.map = telemetryMap(dp);
        if (!Object.keys(field.map).length) field.kind = "text";
      } else if (kind === "number") {
        const standalone = mappingRules(dp).find((r) => r.scale !== undefined &&
          !Object.prototype.hasOwnProperty.call(r, "dps_val") &&
          !Object.prototype.hasOwnProperty.call(r, "value"));
        const scale = Number(standalone?.scale ?? dp.scale ?? 1);
        field.scale = Number.isFinite(scale) && scale > 0 ? scale : 1;
        if (field.scale > 1) {
          field.precision = Math.max(0, Math.ceil(Math.log10(field.scale)));
        } else {
          field.precision = dp.precision ?? 0;
        }
        if (dp.unit) field.unit = dp.unit;
      }
      fields.push(field);
      if (fields.length >= 24) break;
    }
    if (fields.length >= 24) break;
  }

  const status = fields.find((f) => f.key === "status" && f.kind === "enum" && f.map);
  if (status) {
    const mapEntries = Object.entries(status.map);
    const plugged = mapEntries
      .filter(([, value]) => ["plugged_in", "charging", "charged", "waiting", "waiting_for_authorization"].includes(String(value).toLowerCase()))
      .map(([raw]) => raw);
    const charging = mapEntries
      .filter(([, value]) => String(value).toLowerCase() === "charging")
      .map(([raw]) => raw);
    const fault = mapEntries
      .filter(([, value]) => String(value).toLowerCase().includes("fault"))
      .map(([raw]) => raw);
    let insertAt = fields.indexOf(status) + 1;
    const addDerived = (key, label, values) => {
      if (!values.length || fields.some((f) => f.key === key)) return;
      fields.splice(insertAt++, 0, {
        key, label, dp: status.dp, kind: "bool", values,
      });
    };
    addDerived("plugged", "Plugged", plugged);
    addDerived("charging", "Charging", charging);
    addDerived("fault", "Fault", fault);
  }
  if (JSON.stringify(fields).length > 10000) {
    while (fields.length > 1 && JSON.stringify(fields).length > 10000) fields.pop();
  }
  return fields;
}


export function supportInfo(yaml, meta = {}) {
  try {
    const pack = buildPack(yaml, meta, {
      stateTopic: "__STATE_TOPIC__",
      commandTopic: "__CMD_TOPIC__",
      rawEntity: "__RAW_ENTITY__",
    });
    const bytes = JSON.stringify(pack).length;
    return {
      supported: true,
      group: pack.device_type,
      discovery: pack.discovery.length,
      bytes,
      reason: pack.warnings.length ? pack.warnings.slice(0, 2).join("; ") : "",
    };
  } catch (error) {
    return {
      supported: false,
      group: classifyGroup(yaml, meta.file),
      discovery: 0,
      bytes: 0,
      reason: error.message,
    };
  }
}
)A_JS";

const char PROFILE_MAPPING_JS[] PROGMEM =
R"A_MAP(// Portions derived from tuya-local (MIT). See THIRD_PARTY_NOTICES.md.
// Jinja code generation for tuya-local DP mappings.
//
// The generated templates run in Home Assistant. State templates read the raw
// DP snapshot (`value_json.dps`). Command templates read the current snapshot
// through the hidden raw-DP sensor (`state_attr(raw_entity, "dps")`) so that
// conditional mappings, masks and packed values can be handled in HA.

export class Ctx {
  constructor(prefix = "v") {
    this.prefix = prefix;
    this.n = 0;
  }
  var(name = "v") {
    return `${name}${this.n++}`;
  }
}

export function lit(value) {
  if (value === null || value === undefined) return "none";
  if (typeof value === "string") return JSON.stringify(value);
  if (typeof value === "boolean") return value ? "true" : "false";
  if (typeof value === "number" && Number.isFinite(value)) return String(value);
  if (Array.isArray(value)) return `[${value.map(lit).join(", ")}]`;
  if (typeof value === "object") {
    return `{${Object.entries(value).map(([k, v]) => `${lit(k)}: ${lit(v)}`).join(", ")}}`;
  }
  return "none";
}

export function text(value) {
  return value === null || value === undefined ? "" : String(value);
}

export function boolValue(value) {
  if (value === true) return true;
  if (value === false) return false;
  if (typeof value === "number") return value !== 0;
  if (typeof value === "string") {
    const lower = value.toLowerCase();
    if (["true", "1", "on", "yes", "open", "enabled", "enable"].includes(lower)) return true;
    if (["false", "0", "off", "no", "close", "disabled", "disable"].includes(lower)) return false;
  }
  return null;
}

export function numberValue(value) {
  if (typeof value === "number" && Number.isFinite(value)) return value;
  if (typeof value === "boolean") return value ? 1 : 0;
  if (typeof value === "string" && value.trim() !== "") {
    const n = Number(value);
    if (Number.isFinite(n)) return n;
  }
  return null;
}

export function slug(value, limit = 23) {
  const out = text(value)
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, "_")
    .replace(/^_+|_+$/g, "");
  return (out || "profile").slice(0, limit).replace(/_+$/g, "") || "profile";
}

export function mappingRules(dp) {
  return Array.isArray(dp.mapping) ? dp.mapping.filter((r) => r && typeof r === "object") : [];
}

export function hasFrom(rule) {
  return Object.prototype.hasOwnProperty.call(rule, "dps_val") && rule.dps_val !== null;
}

export function ruleValue(rule) {
  return Object.prototype.hasOwnProperty.call(rule, "value") ? rule.value : undefined;
}

function maskInfo(dp) {
  if (!dp.mask) return null;
  const raw = String(dp.mask).replace(/^0x/i, "");
  const hex = raw.padStart(Math.ceil(raw.length / 2) * 2, "0");
  const mask = BigInt(`0x${hex}`);
  if (mask === 0n) return null;
  let lsb = 1n;
  while ((mask & lsb) === 0n) lsb <<= 1n;
  const positions = [];
  for (let i = 0n; i < BigInt(hex.length * 4); i += 1n) {
    if ((mask >> i) & 1n) positions.push(Number(i));
  }
  return {
    hex,
    mask,
    lsb,
    lsbPos: positions.length ? positions[0] : 0,
    bits: positions.length,
    positions,
    byteLen: hex.length / 2,
    signed: Boolean(dp.mask_signed),
    little: dp.endianness === "little",
  };
}

function pow2(index) {
  return (1n << BigInt(index)).toString();
}

function maskPositions(mask) {
  let value;
  try {
    value = BigInt(mask);
  } catch {
    return [];
  }
  if (value <= 0n) return [];
  const positions = [];
  for (let i = 0n; i < 128n; i += 1n) {
    if ((value >> i) & 1n) positions.push(Number(i));
  }
  return positions;
}

function maskValueExpr(valueExpr, info) {
  const terms = info.positions.map((position) => {
    const shift = position - info.lsbPos;
    return `(((${valueExpr}) // ${pow2(position)}) % 2) * ${pow2(shift)}`;
  });
  return terms.length ? `(${terms.join(" + ")})` : "0";
}

function bitTestExpr(valueExpr, mask) {
  const positions = maskPositions(mask);
  if (!positions.length) return "false";
  const terms = positions.map((position) => `(((${valueExpr}) // ${pow2(position)}) % 2)`);
  return `(${terms.join(" + ")}) > 0`;
}

function bitAllExpr(valueExpr, mask) {
  const positions = maskPositions(mask);
  if (!positions.length) return "true";
  const terms = positions.map((position) => `(((${valueExpr}) // ${pow2(position)}) % 2) == 1`);
  return `(${terms.join(" and ")})`;
}

function decodeBytesExpr(dp, expr) {
  if (dp.rawtype === "base64") return `${expr} | base64_decode`;
  if (dp.rawtype === "hex") return `${expr} | from_hex`;
  return null;
}

function emitBytesToInt(ctx, bytesVar, intVar, little) {
  const ns = ctx.var("ns");
  const byte = ctx.var("b");
  let code = `{% set ${ns} = namespace(n=0) %}\n`;
  code += `{% for ${byte} in ${bytesVar}${little ? " | reverse" : ""} %}{% set ${ns}.n = ${ns}.n * 256 + ${byte} %}{% endfor %}\n`;
  code += `{% set ${intVar} = ${ns}.n %}\n`;
  return code;
}

function emitIntToHex(ctx, intVar, hexVar, byteLen, little) {
  const ns = ctx.var("h");
  const i = ctx.var("i");
  const start = little ? 0 : byteLen - 1;
  const end = little ? byteLen : -1;
  const step = little ? 1 : -1;
  let code = `{% set ${ns} = namespace(h="") %}\n`;
  code += `{% for ${i} in range(${start}, ${end}, ${step}) %}{% set ${ns}.h = ${ns}.h ~ ("%02X" % ((${intVar} // (256 ** ${i})) % 256)) %}{% endfor %}\n`;
  code += `{% set ${hexVar} = ${ns}.h %}\n`;
  return code;
}

function signedAdjust(ctx, varName, bits) {
  if (!bits || bits >= 63) return "";
  return `{% if ${varName} >= ${2 ** (bits - 1)} %}{% set ${varName} = ${varName} - ${2 ** bits} %}{% endif %}\n`;
}

function emitDecodeMask(entity, dp, ctx, rawVar) {
  const info = maskInfo(dp);
  if (!info) return "";
  let code = "";
  if (dp.rawtype === "base64" || dp.rawtype === "hex") {
    const b = ctx.var("bytes");
    const n = ctx.var("n");
    code += `{% if ${rawVar} is not none %}\n`;
    code += `{% set ${b} = ${decodeBytesExpr(dp, rawVar)} %}\n`;
    code += emitBytesToInt(ctx, b, n, info.little);
    code += `{% set ${rawVar} = ${maskValueExpr(n, info)} %}\n`;
    code += signedAdjust(ctx, rawVar, info.bits);
    code += `{% endif %}\n`;
  } else {
    code += `{% if ${rawVar} is not none %}\n`;
    code += `{% set ${rawVar} = ${maskValueExpr(`(${rawVar} | int)`, info)} %}\n`;
    code += signedAdjust(ctx, rawVar, info.bits);
    code += `{% endif %}\n`;
  }
  return code;
}

function dpNameToDp(entity, name) {
  if (!name) return null;
  return (entity.dps || []).find((d) => d.name === name || String(d.id) === String(name)) || null;
}

function stateValueForDp(entity, target, ctx, dpsVar, depth) {
  const out = ctx.var("r");
  if (!target || depth > 4) {
    return { code: `{% set ${out} = none %}\n`, var: out };
  }
  return { code: emitState(entity, target, ctx, dpsVar, out, depth + 1), var: out };
}

function emitTransform(entity, dp, ctx, varName, rule, depth) {
  let code = "";
  let terminal = false;

  if (rule.value_redirect) {
    const target = dpNameToDp(entity, rule.value_redirect);
    if (target) {
      const value = stateValueForDp(entity, target, ctx, "dps", depth);
      code += value.code;
      code += `{% if ${value.var} is not none %}{% set ${varName} = ${value.var} %}{% endif %}\n`;
    }
  } else if (rule.value_mirror) {
    const target = dpNameToDp(entity, rule.value_mirror);
    if (target) {
      const value = stateValueForDp(entity, target, ctx, "dps", depth);
      code += value.code;
      code += `{% set ${varName} = ${value.var} %}\n`;
      terminal = true;
    }
  }

  if (!terminal) {
    if (rule.invert && dp.range) {
      const min = Number(dp.range.min);
      const max = Number(dp.range.max);
      code += `{% if ${varName} is number %}{% set ${varName} = -1 * ${varName} + ${min} + ${max} %}{% endif %}\n`;
    }
    if (rule.target_range && dp.range && rule.target_range.min !== undefined && rule.target_range.max !== undefined) {
      const fmin = Number(dp.range.min);
      const fmax = Number(dp.range.max);
      const tmin = Number(rule.target_range.min);
      const tmax = Number(rule.target_range.max);
      if (fmax !== fmin) {
        code += `{% if ${varName} is number %}{% set ${varName} = ${tmin} + ((${varName} - ${fmin}) * ${(tmax - tmin)} / ${(fmax - fmin)}) %}{% endif %}\n`;
      }
    }
    const scale = rule.scale === undefined ? 1 : Number(rule.scale);
    if (Number.isFinite(scale) && scale !== 0 && scale !== 1) {
      code += `{% if ${varName} is number %}{% set ${varName} = ${varName} / ${scale} %}{% endif %}\n`;
    }
  }
  return { code, terminal };
}

function emitConditionMatch(entity, dp, rule, cond, dpsVar, ctx) {
  const constraintName = cond.constraint || rule.constraint || dp.name;
  const constraint = dpNameToDp(entity, constraintName) || dp;
  const cid = String(constraint.id);
  const cval = ctx.var("c");
  let code = `{% set ${cval} = ${dpsVar}.get(${lit(cid)}) %}\n`;
  let conds = [];

  if (Object.prototype.hasOwnProperty.call(cond, "dps_val")) {
    const expected = cond.dps_val;
    if (expected === null) {
      conds.push(`${cval} is none`);
    } else if (Array.isArray(expected)) {
      conds.push(`${cval} | string in ${lit(expected.map((v) => String(v)))}`);
    } else if (constraint.rawtype === "bitfield") {
      conds.push(bitAllExpr(`(${cval} | int)`, expected));
    } else {
      conds.push(`${cval} | string == ${lit(String(expected))}`);
    }
  } else {
    // A condition without dps_val is only an override for command values,
    // not a state condition.
    conds.push("false");
  }

  if (cond.available) {
    const avail = dpNameToDp(entity, cond.available);
    if (avail) conds.push(`${dpsVar}.get(${lit(String(avail.id))})`);
  }

  const match = conds.map((c) => `(${c})`).join(" and ");
  code += `{% if ${match} %}\n`;
  return { code, close: "{% endif %}\n" };
}

function emitNestedMapping(ctx, varName, mapping) {
  let code = "";
  if (!Array.isArray(mapping)) return code;
  for (const nested of mapping) {
    if (!nested || typeof nested !== "object" || !Object.prototype.hasOwnProperty.call(nested, "value")) continue;
    if (nested.hidden) continue;
    let match;
    if (Object.prototype.hasOwnProperty.call(nested, "dps_val") && nested.dps_val !== null) {
      match = `${varName} | string == ${lit(String(nested.dps_val))}`;
    } else {
      match = "true";
    }
    code += `{% if ${match} %}{% set ${varName} = ${lit(nested.value)} %}{% endif %}\n`;
  }
  return code;
}

function emitAssignRule(entity, dp, ctx, rawVar, outVar, rule, depth, isDefault = false) {
  let code = "";
  if (Object.prototype.hasOwnProperty.call(rule, "value")) {
    code += `{% set ${outVar} = ${lit(rule.value)} %}\n`;
  } else {
    code += `{% set ${outVar} = ${rawVar} %}\n`;
  }

  // Conditional overrides. Multiple matching conditions are applied in order,
  // the last one wins, matching tuya-local's _active_condition behaviour.
  if (Array.isArray(rule.conditions)) {
    for (const cond of rule.conditions) {
      if (!cond || typeof cond !== "object") continue;
      const match = emitConditionMatch(entity, dp, rule, cond, "dps", ctx);
      code += match.code;
      if (cond.invalid) {
        code += `{% set ${outVar} = none %}\n`;
      } else {
        if (Object.prototype.hasOwnProperty.call(cond, "value")) {
          code += `{% set ${outVar} = ${lit(cond.value)} %}\n`;
        }
        if (cond.mapping) {
          code += emitNestedMapping(ctx, outVar, cond.mapping);
        }
        const transform = emitTransform(entity, dp, ctx, outVar, cond, depth);
        code += transform.code;
      }
      code += match.close;
    }
  }

  const transform = emitTransform(entity, dp, ctx, outVar, rule, depth);
  code += transform.code;
  return code;
}

function isNullDpsRule(rule) {
  return Object.prototype.hasOwnProperty.call(rule, "dps_val") && rule.dps_val === null;
}

export function emitState(entity, dp, ctx, dpsVar, outVar, depth = 0) {
  const rawVar = ctx.var("r");
  let code = `{% set ${rawVar} = ${dpsVar}.get(${lit(String(dp.id))}) %}\n`;

  if (dp.rawtype === "utf16b64") {
    code += `{% if ${rawVar} is not none %}{% set ${rawVar} = (${rawVar} | base64_decode).decode("utf-16-be") %}{% endif %}\n`;
  }
  code += emitDecodeMask(entity, dp, ctx, rawVar);

  if (dp.format) {
    // Format DPs are handled by format-aware platform builders. For generic
    // use, expose the raw/decoded value.
    code += `{% set ${outVar} = ${rawVar} %}\n`;
    return code;
  }

  const rules = mappingRules(dp);
  if (!rules.length) {
    code += `{% set ${outVar} = ${rawVar} %}\n`;
    return code;
  }

  const withFrom = rules.filter((r) => hasFrom(r) || isNullDpsRule(r));
  const defaults = rules.filter((r) => !hasFrom(r) && !isNullDpsRule(r));
  const defaultRule = defaults.length ? defaults[defaults.length - 1] : null;

  if (withFrom.length) {
    withFrom.forEach((rule, index) => {
      let match;
      if (isNullDpsRule(rule)) {
        match = `${rawVar} is none`;
      } else if (Array.isArray(rule.dps_val)) {
        match = `${rawVar} | string in ${lit(rule.dps_val.map((v) => String(v)))}`;
      } else if (dp.rawtype === "bitfield" && Number(rule.dps_val) !== 0) {
        match = bitTestExpr(`(${rawVar} | int)`, rule.dps_val);
      } else {
        match = `${rawVar} | string == ${lit(String(rule.dps_val))}`;
      }
      if (rule.available) {
        const avail = dpNameToDp(entity, rule.available);
        if (avail) match = `(${match}) and (${dpsVar}.get(${lit(String(avail.id))}))`;
      }
      code += `{% ${index === 0 ? "if" : "elif"} ${match} %}\n`;
      code += emitAssignRule(entity, dp, ctx, rawVar, outVar, rule, depth);
    });
    code += `{% else %}\n`;
    if (defaultRule) {
      code += emitAssignRule(entity, dp, ctx, rawVar, outVar, defaultRule, depth, true);
    } else {
      code += `{% set ${outVar} = ${rawVar} %}\n`;
    }
    code += `{% endif %}\n`;
  } else if (defaultRule) {
    code += emitAssignRule(entity, dp, ctx, rawVar, outVar, defaultRule, depth, true);
  } else {
    code += `{% set ${outVar} = ${rawVar} %}\n`;
  }
  return code;
}

export function stateTemplate(entity, dp, options = {}) {
  const ctx = options.ctx || new Ctx();
  const dpsVar = options.dpsVar || "dps";
  const outVar = options.outVar || ctx.var("v");
  const code = emitState(entity, dp, ctx, dpsVar, outVar, options.depth || 0);
  return { code, var: outVar, ctx };
}

function emitRangeClamp(ctx, varName, range) {
  if (!range || range.min === undefined || range.max === undefined) return "";
  let code = "";
  code += `{% if ${varName} is number and ${varName} < ${Number(range.min)} %}{% set ${varName} = ${Number(range.min)} %}{% endif %}\n`;
  code += `{% if ${varName} is number and ${varName} > ${Number(range.max)} %}{% set ${varName} = ${Number(range.max)} %}{% endif %}\n`;
  return code;
}

function emitCommandTransform(entity, dp, ctx, varName, rule) {
  let code = "";
  if (rule.scale !== undefined && Number(rule.scale) !== 0 && Number(rule.scale) !== 1) {
    code += `{% if ${varName} is number %}{% set ${varName} = ${varName} * ${Number(rule.scale)} %}{% endif %}\n`;
  }
  if (rule.target_range && dp.range && rule.target_range.min !== undefined && rule.target_range.max !== undefined) {
    const fmin = Number(rule.target_range.min);
    const fmax = Number(rule.target_range.max);
    const tmin = Number(dp.range.min);
    const tmax = Number(dp.range.max);
    if (fmax !== fmin) {
      code += `{% if ${varName} is number %}{% set ${varName} = ${tmin} + ((${varName} - ${fmin}) * ${(tmax - tmin)} / ${(fmax - fmin)}) %}{% endif %}\n`;
    }
  }
  if (rule.invert && dp.range) {
    const min = Number(dp.range.min);
    const max = Number(dp.range.max);
    code += `{% if ${varName} is number %}{% set ${varName} = -1 * ${varName} + ${min} + ${max} %}{% endif %}\n`;
  }
  const step = rule.step === undefined ? null : Number(rule.step);
  if (step && step > 0) {
    code += `{% if ${varName} is number %}{% set ${varName} = ${step} * (${varName} / ${step}) | round %}{% endif %}\n`;
  }
  return code;
}

function emitEncode(entity, dp, ctx, rawVar, outVar, options) {
  const info = maskInfo(dp);
  let code = "";

  if (info) {
    const cur = ctx.var("cur");
    const curInt = ctx.var("curInt");
    code += `{% set ${cur} = dps.get(${lit(String(dp.id))}) %}\n`;
    code += `{% set ${curInt} = 0 %}\n`;
    if (dp.rawtype === "base64" || dp.rawtype === "hex") {
      const bytes = ctx.var("cb");
      code += `{% if ${cur} is not none %}\n`;
      code += `{% set ${bytes} = ${decodeBytesExpr(dp, cur)} %}\n`;
      code += emitBytesToInt(ctx, bytes, curInt, info.little);
      code += `{% endif %}\n`;
    } else {
      code += `{% if ${cur} is not none %}{% set ${curInt} = ${cur} | int %}{% endif %}\n`;
    }
    const currentMasked = ctx.var("curMasked");
    code += `{% set ${currentMasked} = ${maskValueExpr(curInt, info)} %}\n`;
    code += `{% set ${rawVar} = ${curInt} - (${currentMasked} * ${info.lsb}) + (((${rawVar} | int) % ${pow2(info.bits)}) * ${info.lsb}) %}\n`;
    if (dp.rawtype === "base64" || dp.rawtype === "hex") {
      const hex = ctx.var("hex");
      code += emitIntToHex(ctx, rawVar, hex, info.byteLen, info.little);
      if (dp.rawtype === "base64") {
        code += `{% set ${outVar} = ${hex} | from_hex | base64_encode %}\n`;
      } else {
        code += `{% set ${outVar} = ${hex} %}\n`;
      }
    } else {
      code += `{% set ${outVar} = ${rawVar} | int %}\n`;
    }
    return code;
  }

  if (dp.rawtype === "utf16b64") {
    code += `{% set ${outVar} = (${rawVar} | string).encode("utf-16-be") | base64_encode %}\n`;
  } else if (dp.rawtype === "unixtime" && options?.dateTarget) {
    code += `{% set ${outVar} = ${rawVar} | as_timestamp | int %}\n`;
  } else if (dp.rawtype === "json" && options?.jsonTarget) {
    code += `{% set ${outVar} = ${rawVar} | tojson %}\n`;
  } else {
    code += `{% set ${outVar} = ${rawVar} %}\n`;
  }
  return code;
}

function emitRedirectWrite(entity, dp, ctx, targetVar, writesVar, rule, depth) {
  const targetDp = dpNameToDp(entity, rule.value_redirect);
  if (!targetDp || depth > 4) return false;
  let code = `{% set ${targetVar} = ${targetVar} %}\n`; // no-op, keeps the expression explicit
  code += emitWrite(entity, targetDp, ctx, targetVar, writesVar, depth + 1);
  return code;
}

function emitWrite(entity, dp, ctx, targetVar, writesVar, depth = 0) {
  const rules = mappingRules(dp);
  const defaults = rules.filter((r) => !Object.prototype.hasOwnProperty.call(r, "value") && !r.hidden && !isNullDpsRule(r));
  const defaultRule = defaults.length ? defaults[defaults.length - 1] : null;
  if (defaultRule && !Object.prototype.hasOwnProperty.call(defaultRule, "value") &&
      !Object.prototype.hasOwnProperty.call(defaultRule, "dps_val") &&
      Array.isArray(defaultRule.conditions)) {
    const branches = [];
    for (const cond of defaultRule.conditions) {
      if (!cond || cond.hidden || !cond.value_redirect || !Object.prototype.hasOwnProperty.call(cond, "dps_val") ||
          cond.dps_val === null || Array.isArray(cond.dps_val)) continue;
      const target = dpNameToDp(entity, cond.value_redirect);
      const constraintName = cond.constraint || defaultRule.constraint || dp.name;
      const constraint = dpNameToDp(entity, constraintName);
      if (!target || !constraint) continue;
      const current = `dps.get(${lit(String(constraint.id))})`;
      let match;
      if (constraint.rawtype === "bitfield" && Number(cond.dps_val) !== 0) {
        match = bitAllExpr(`(${current} | int)`, cond.dps_val);
      } else {
        match = `${current} | string == ${lit(String(cond.dps_val))}`;
      }
      branches.push({ match, target });
    }
    if (branches.length) {
      let code = "";
      branches.forEach((branch, index) => {
        code += `{% ${index === 0 ? "if" : "elif"} ${branch.match} %}\n`;
        code += emitWrite(entity, branch.target, ctx, targetVar, writesVar, depth + 1);
      });
      code += `{% else %}\n`;
      code += emitWriteNormal(entity, dp, ctx, targetVar, writesVar, depth);
      code += `{% endif %}\n`;
      return code;
    }
  }
  return emitWriteNormal(entity, dp, ctx, targetVar, writesVar, depth);
}

function emitWriteNormal(entity, dp, ctx, targetVar, writesVar, depth = 0) {
  const rules = mappingRules(dp);
  const withValue = rules.filter((r) => Object.prototype.hasOwnProperty.call(r, "value") && !r.hidden && !isNullDpsRule(r));
  const defaults = rules.filter((r) => !Object.prototype.hasOwnProperty.call(r, "value") && !r.hidden && !isNullDpsRule(r));
  const defaultRule = defaults.length ? defaults[defaults.length - 1] : null;

  const rawVar = ctx.var("raw");
  const idVar = ctx.var("id");
  let code = `{% set ${idVar} = ${lit(String(dp.id))} %}\n`;
  code += `{% set ${rawVar} = ${targetVar} %}\n`;

  const emitBranch = (rule) => {
    let branch = "";
    if (Object.prototype.hasOwnProperty.call(rule, "dps_val") && rule.dps_val !== null) {
      branch += `{% set ${rawVar} = ${lit(rule.dps_val)} %}\n`;
    } else {
      branch += `{% set ${rawVar} = ${targetVar} %}\n`;
    }
    if (rule.value_mirror) {
      const mirror = dpNameToDp(entity, rule.value_mirror);
      if (mirror) branch += `{% set ${rawVar} = dps.get(${lit(String(mirror.id))}) %}\n`;
    }
    if (rule.value_redirect) {
      // Delegate to the redirect DP by writing the same HA value there.
      const redirect = dpNameToDp(entity, rule.value_redirect);
      if (redirect && depth <= 4) {
        branch += emitWrite(entity, redirect, ctx, targetVar, writesVar, depth + 1);
        return { code: branch, terminal: true };
      }
    }
    branch += emitCommandTransform(entity, dp, ctx, rawVar, rule);
    branch += emitRangeClamp(ctx, rawVar, dp.range);

    // Conditional mappings can also co-write the constraint DP and override
    // the raw value based on the target value.
    if (Array.isArray(rule.conditions)) {
      for (const cond of rule.conditions) {
        if (!cond || typeof cond !== "object" || cond.hidden) continue;
        if (!Object.prototype.hasOwnProperty.call(cond, "value")) continue;
        const match = `${targetVar} | string == ${lit(String(cond.value))}`;
        branch += `{% if ${match} %}\n`;
        if (Object.prototype.hasOwnProperty.call(cond, "dps_val") && cond.dps_val !== null) {
          branch += `{% set ${rawVar} = ${lit(cond.dps_val)} %}\n`;
        }
        if (cond.mapping) {
          for (const nested of cond.mapping) {
            if (!nested || nested.hidden) continue;
            if (Object.prototype.hasOwnProperty.call(nested, "value") &&
                Object.prototype.hasOwnProperty.call(nested, "dps_val") &&
                nested.dps_val !== null &&
                String(nested.value) === String(cond.value)) {
              branch += `{% set ${rawVar} = ${lit(nested.dps_val)} %}\n`;
            }
          }
        }
        const constraintName = cond.constraint || rule.constraint || dp.name;
        const constraint = dpNameToDp(entity, constraintName);
        if (constraint && String(constraint.id) !== String(dp.id) &&
            !constraint.readonly &&
            Object.prototype.hasOwnProperty.call(cond, "dps_val") &&
            cond.dps_val !== null && !Array.isArray(cond.dps_val)) {
          branch += writeAppend(writesVar, lit(String(constraint.id)), lit(cond.dps_val));
        }
        branch += emitCommandTransform(entity, dp, ctx, rawVar, cond);
        branch += emitRangeClamp(ctx, rawVar, dp.range);
        branch += `{% endif %}\n`;
      }
    }

    const encoded = ctx.var("enc");
    branch += emitEncode(entity, dp, ctx, rawVar, encoded);
    branch += writeAppend(writesVar, idVar, encoded);
    return { code: branch, terminal: false };
  };

  if (withValue.length) {
    withValue.forEach((rule, index) => {
      let match;
      if (typeof rule.value === "number") {
        match = `(${targetVar} | float) == ${rule.value}`;
      } else if (typeof rule.value === "boolean") {
        match = `(${targetVar} | string) == ${lit(String(rule.value))}`;
      } else {
        match = `(${targetVar} | string) == ${lit(String(rule.value))}`;
      }
      if (rule.available) {
        const avail = dpNameToDp(entity, rule.available);
        if (avail) match = `(${match}) and (dps.get(${lit(String(avail.id))}))`;
      }
      code += `{% ${index === 0 ? "if" : "elif"} ${match} %}\n`;
      code += emitBranch(rule).code;
    });
    code += `{% else %}\n`;
    code += emitBranch(defaultRule || {}).code;
    code += `{% endif %}\n`;
  } else {
    code += emitBranch(defaultRule || {}).code;
  }
  return code;
}

export function writeAppend(writesVar, idExpr, valueExpr) {
  return `{% set ${writesVar}.out = ${writesVar}.out ~ (', ' if ${writesVar}.out else '') ~ ('"' ~ ${idExpr} ~ '": ' ~ (${valueExpr} | tojson)) %}\n`;
}

export function commandTemplate(entity, dp, targetExpr, options = {}) {
  const ctx = options.ctx || new Ctx();
  const rawEntity = options.rawEntity || "__RAW_ENTITY__";
  const writesVar = options.writesVar || "writes";
  const targetVar = ctx.var("target");
  let code = `{% set dps = state_attr(${lit(rawEntity)}, "dps") or {} %}\n`;
  code += `{% set ${targetVar} = ${targetExpr} %}\n`;
  code += `{% set ${writesVar} = namespace(out="") %}\n`;
  code += emitWrite(entity, dp, ctx, targetVar, writesVar, 0);
  code += `{{ '{' ~ ${writesVar}.out ~ '}' }}`;
  return { code, writesVar, ctx };
}

export function emitWriteTo(entity, dp, targetExpr, ctx, writesVar) {
  const targetVar = ctx.var("target");
  let code = `{% set ${targetVar} = ${targetExpr} %}\n`;
  code += emitWrite(entity, dp, ctx, targetVar, writesVar, 0);
  return code;
}

export function commandWrites(entity, dp, targetExpr, options = {}) {
  const ctx = options.ctx || new Ctx();
  const writesVar = options.writesVar || ctx.var("writes");
  const targetVar = ctx.var("target");
  let code = "";
  if (options.includePrelude !== false) {
    const rawEntity = options.rawEntity || "__RAW_ENTITY__";
    code += `{% set dps = state_attr(${lit(rawEntity)}, "dps") or {} %}\n`;
  }
  code += `{% set ${targetVar} = ${targetExpr} %}\n`;
  code += `{% set ${writesVar} = namespace(out="") %}\n`;
  code += emitWrite(entity, dp, ctx, targetVar, writesVar, 0);
  return { code, writesVar, ctx };
}

export function formatStruct(dp) {
  if (!Array.isArray(dp.format)) return null;
  let fmt = ">";
  const fields = [];
  for (const field of dp.format) {
    const bytes = Number(field.bytes || 1);
    const min = field.range && field.range.min !== undefined ? Number(field.range.min) : 0;
    const max = field.range && field.range.max !== undefined ? Number(field.range.max) : 256 ** bytes - 1;
    let ch;
    if (bytes === 1) ch = min < 0 ? "b" : "B";
    else if (bytes === 2) ch = min < 0 ? "h" : "H";
    else if (bytes === 4) ch = min < 0 ? "i" : "I";
    else if (bytes === 8) ch = min < 0 ? "q" : "Q";
    else ch = min < 0 ? "i" : "I";
    fmt += ch;
    fields.push({ name: field.name, bytes, min, max, signed: min < 0 });
  }
  return { fmt, fields };
}

export function formatFieldsTemplate(entity, dp, options = {}) {
  const ctx = options.ctx || new Ctx();
  const rawEntity = options.rawEntity || "__RAW_ENTITY__";
  const source = options.source || `dps.get(${lit(String(dp.id))})`;
  const dpsPrelude = options.dpsPrelude || "";
  const spec = formatStruct(dp);
  if (!spec) return { code: "", fields: [], ctx };
  const bytesVar = ctx.var("fb");
  const valsVar = ctx.var("fv");
  let code = dpsPrelude;
  code += `{% set ${bytesVar} = ${source} %}\n`;
  code += `{% if ${bytesVar} is not none %}\n`;
  if (dp.rawtype === "string") {
    code += `{% set ${bytesVar} = (${bytesVar} | string).encode("latin-1") %}\n`;
  } else {
    const decode = decodeBytesExpr(dp, bytesVar);
    code += `{% set ${bytesVar} = ${decode || bytesVar} %}\n`;
  }
  code += `{% set ${valsVar} = ${bytesVar} | unpack(${lit(spec.fmt)}) %}\n`;
  code += `{% endif %}\n`;
  const fields = [];
  for (let i = 0; i < spec.fields.length; i += 1) {
    const field = spec.fields[i];
    const varName = ctx.var(`f_${field.name || i}`);
    const value = spec.fields.length === 1 ? valsVar : `${valsVar}[${i}]`;
    code += `{% set ${varName} = ${value} if ${bytesVar} is not none else none %}\n`;
    fields.push({ ...field, var: varName });
  }
  return { code, fields, ctx };
}

export function formatPackTemplate(entity, dp, fieldExprs, options = {}) {
  const ctx = options.ctx || new Ctx();
  const spec = formatStruct(dp);
  if (!spec) return { code: "", var: "none", ctx };
  const hexVar = ctx.var("hex");
  const parts = [];
  for (let i = 0; i < spec.fields.length; i += 1) {
    const field = spec.fields[i];
    const value = fieldExprs[i] || "0";
    const width = field.bytes * 2;
    const mask = 2 ** (field.bytes * 8) - 1;
    parts.push(`("%0${width}X" % ((${value} | int) % ${256 ** field.bytes}))`);
  }
  let code = `{% set ${hexVar} = ${parts.join(" ~ ")} %}\n`;
  const outVar = ctx.var("enc");
  if (dp.rawtype === "base64") {
    code += `{% set ${outVar} = ${hexVar} | from_hex | base64_encode %}\n`;
  } else if (dp.rawtype === "hex") {
    code += `{% set ${outVar} = ${hexVar} %}\n`;
  } else {
    code += `{% set ${outVar} = ${hexVar} %}\n`;
  }
  return { code, var: outVar, ctx };
}

export function formatFieldToHa(field, varName, haName) {
  // Convert a raw packed field to the HA range used by light entities.
  const max = field.max || 1;
  if (haName === "h") return `{% set ${varName} = ((${varName} | float) * ${360 / max}) | round %}\n`;
  if (haName === "s") return `{% set ${varName} = ((${varName} | float) * ${100 / max}) | round %}\n`;
  if (["v", "r", "g", "b"].includes(haName)) return `{% set ${varName} = ((${varName} | float) * ${255 / max}) | round %}\n`;
  return "";
}

export function formatHaToField(field, expr, haName) {
  const max = field.max || 1;
  let scale = 1;
  if (haName === "h") scale = max / 360;
  else if (haName === "s") scale = max / 100;
  else if (["v", "r", "g", "b"].includes(haName)) scale = max / 255;
  return `(((${expr} | float) * ${scale}) | round(0) | int)`;
}

export { dpNameToDp };
)A_MAP";

}  // namespace tuya_lora
