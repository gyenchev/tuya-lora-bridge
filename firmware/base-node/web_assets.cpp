#include "web_assets.h"

namespace tuya_lora {

const char PROFILE_BUILDER_HTML[] PROGMEM =
R"A_HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Tuya LoRa — Profile catalog</title>
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
  .preview { border: 1px solid #8884; border-radius: 10px; padding: 12px; background: #fff; }
  .preview ul { margin: 8px 0 0 18px; padding: 0; }
  .preview li { margin: 3px 0; }
  code { font: .85em ui-monospace, SFMono-Regular, Menlo, monospace; }
  @media (prefers-color-scheme: dark) {
    body { background: #111214; color: #e8e8ea; }
    button, details, .preview { background: #1a1c20; color: inherit; }
    button.primary { background: #2f81f7; border-color: #2f81f7; }
    a { color: #6cb6ff; }
  }
</style>
</head>
<body>
<main>
  <h1>Profile catalog</h1>
  <p class="muted">Browse tuya-local device descriptors and save one precompiled profile to the base. Only the selected profile is stored on the ESP32.</p>
  <div id="base-info" class="status">Connecting to the base…</div>

  <div class="row">
    <input id="search" type="search" placeholder="Search device, brand or model…" autocomplete="off">
    <button id="reload-offline" type="button">Reload offline catalog</button>
  </div>

  <div class="row">
    <input id="catalog-url" type="url" spellcheck="false" value="https://raw.githubusercontent.com/gyenchev/tuya-lora-bridge/refs/heads/feature/dp-profiles/web/catalog.json">
    <button id="load-online" class="primary" type="button">Load online catalog</button>
  </div>
  <p class="muted">The online catalog and YAML are fetched from GitHub. The offline catalog is served by the base and works without internet.</p>

  <div id="status" class="status" hidden></div>

  <section>
    <h2>Offline charger catalog</h2>
    <div id="offline"></div>
  </section>

  <section>
    <h2>Online tuya-local catalog</h2>
    <div id="online"></div>
  </section>

  <section id="preview" hidden>
    <h2>Preview</h2>
    <div id="preview-body" class="preview"></div>
    <div class="row"><button id="save" class="primary" type="button">Save to base</button></div>
  </section>
</main>

<script type="module">
import { buildPack } from '/profile_converter.mjs';

const $ = (id) => document.getElementById(id);
let info = null;
let offlinePacks = [];
let catalog = null;
let currentPack = null;
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

async function loadBaseInfo() {
  info = await fetchJson('/api/profile/info');
  const pack = info.pack
    ? ` · active pack: ${escapeHtml(info.pack.name)} (${escapeHtml(info.pack.id)})`
    : '';
  $('base-info').innerHTML =
    `<strong>${escapeHtml(info.ha_name || 'Base')}</strong> · state: <code>${escapeHtml(info.state_topic)}</code> · command: <code>${escapeHtml(info.command_topic)}</code>${pack}`;
}

function materialize(pack) {
  if (!info) throw new Error('base info is not loaded');
  const text = JSON.stringify(pack)
    .replaceAll('__STATE_TOPIC__', info.state_topic)
    .replaceAll('__CMD_TOPIC__', info.command_topic);
  return JSON.parse(text);
}

function showPreview(pack, sourceLabel) {
  currentPack = pack;
  const size = JSON.stringify(pack).length;
  const items = pack.discovery.map((item) => {
    const name = item.config?.name || item.component;
    return `<li><code>${escapeHtml(item.component)}</code> ${escapeHtml(name)}</li>`;
  }).join('');
  $('preview-body').innerHTML =
    `<p><strong>${escapeHtml(pack.name)}</strong> <span class="badge">${escapeHtml(pack.device_type || 'Device')}</span></p>
     <p class="muted">${escapeHtml(sourceLabel || pack.source || '')}</p>
     <p class="muted">${pack.discovery.length} entities · ${size} bytes</p>
     <ul>${items}</ul>`;
  $('preview').hidden = false;
  $('preview').scrollIntoView({ behavior: 'smooth', block: 'start' });
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
  button.textContent = 'Use';
  button.disabled = Boolean(disabled);
  button.addEventListener('click', action);
  row.appendChild(button);
  return row;
}

function renderOffline() {
  const host = $('offline');
  host.innerHTML = '';
  if (!offlinePacks.length) {
    host.innerHTML = '<p class="muted">No offline charger packs were found on the base.</p>';
    return;
  }
  const details = document.createElement('details');
  details.open = true;
  details.innerHTML = `<summary>Chargers (${offlinePacks.length})</summary>`;
  for (const pack of offlinePacks) {
    details.appendChild(makeItem({
      title: pack.name,
      subtitle: `${pack.id} · ${pack.discovery.length} entities`,
      badge: { label: 'offline', ok: true },
      action: () => {
        try {
          showPreview(materialize(pack), 'Offline charger catalog');
        } catch (error) {
          setStatus(error.message, true);
        }
      },
    }));
  }
  host.appendChild(details);
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
          ? { label: `milestone 1 · ${profile.discovery} entities`, ok: true }
          : { label: 'needs later milestone', ok: false },
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
              stateTopic: info.state_topic,
              commandTopic: info.command_topic,
            });
            if (JSON.stringify(pack).length > 48000) throw new Error('Generated pack is larger than the 48 KB limit');
            setStatus('');
            showPreview(pack, `Online tuya-local catalog · ${profile.file}`);
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

async function loadOffline() {
  try {
    const data = await fetchJson('/api/profile/offline');
    offlinePacks = data.packs || [];
    renderOffline();
  } catch (error) {
    $('offline').innerHTML = `<p class="muted">Offline catalog unavailable: ${escapeHtml(error.message)}</p>`;
  }
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

async function savePack() {
  if (!currentPack) return;
  try {
    const packText = JSON.stringify(currentPack);
    if (packText.length > 48000) throw new Error('Profile pack is larger than the 48 KB limit');
    setStatus('Saving profile to the base…');
    const response = await fetch('/api/profile/save', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: new URLSearchParams({ pack: packText }),
    });
    const data = await response.json().catch(() => null);
    if (!response.ok || !data?.ok) throw new Error(data?.error || `HTTP ${response.status}`);
    setStatus(`Saved ${currentPack.name}. The base is rebooting…`);
    $('save').disabled = true;
    setTimeout(async () => {
      try {
        await loadBaseInfo();
        setStatus(`Active profile is now ${info.pack?.name || 'the saved pack'}.`);
      } catch (error) {
        setStatus(`Saved. Reconnect to the base to verify. ${error.message}`);
      }
      $('save').disabled = false;
    }, 6000);
  } catch (error) {
    setStatus(error.message, true);
  }
}

$('reload-offline').addEventListener('click', loadOffline);
$('load-online').addEventListener('click', loadOnline);
$('search').addEventListener('input', renderOnline);
$('save').addEventListener('click', savePack);

(async () => {
  try {
    await loadBaseInfo();
    await loadOffline();
  } catch (error) {
    $('base-info').textContent = `Could not reach the base: ${error.message}`;
  }
})();
</script>
</body>
</html>
)A_HTML";

const char PROFILE_CONVERTER_JS[] PROGMEM =
R"A_JS(// Shared tuya-local -> Tuya LoRa browser profile converter.
// Used by the browser builder and by the Node catalog generator.

export const SIMPLE_ENTITIES = new Set([
  "sensor",
  "binary_sensor",
  "switch",
  "number",
  "select",
  "button",
  "text",
]);

const ENTITY_TYPE = {
  sensor: "sensor",
  binary_sensor: "binary_sensor",
  switch: "switch",
  number: "number",
  select: "select",
  button: "button",
  text: "text",
};

const PRIMARY_DP = {
  sensor: "sensor",
  binary_sensor: "sensor",
  switch: "switch",
  number: "value",
  select: "option",
  button: "button",
  text: "value",
};

const DP_TYPES = new Set(["string", "boolean", "integer", "bitfield", "float"]);
const UNSUPPORTED_MAP_KEYS = new Set([
  "value_redirect",
  "constraint",
  "conditions",
  "value_mirror",
  "invalid",
  "target_range",
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

function isObject(value) {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}

function asArray(value) {
  return Array.isArray(value) ? value : [];
}

function text(value) {
  return value === null || value === undefined ? "" : String(value);
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
  if (types.has("media_player")) return "Media players";
  if (types.has("lawn_mower")) return "Lawn mowers";
  if (types.has("time") || types.has("datetime")) return "Time controls";
  if (types.has("switch") || types.has("number") || types.has("select") || types.has("button")) {
    return "Switches & controls";
  }
  return "Sensors";
}

function sortGroups(groups) {
  const present = new Set(groups);
  return GROUP_ORDER.filter((group) => present.has(group));
}

function safeNumber(value, min = -1e9, max = 1e9) {
  if (typeof value !== "number" || !Number.isFinite(value)) return null;
  if (value < min || value > max) return null;
  return value;
}

function toBoolean(value) {
  if (value === true) return true;
  if (value === false) return false;
  if (typeof value === "number" && Number.isFinite(value)) return value !== 0;
  if (typeof value === "string") {
    const lower = value.toLowerCase();
    if (["true", "1", "on", "yes", "open"].includes(lower)) return true;
    if (["false", "0", "off", "no", "close"].includes(lower)) return false;
  }
  return null;
}

function toNumber(value) {
  if (typeof value === "number" && Number.isFinite(value)) return value;
  if (typeof value === "boolean") return value ? 1 : 0;
  if (typeof value === "string" && value.trim() !== "") {
    const number = Number(value);
    if (Number.isFinite(number)) return number;
  }
  return null;
}

function jinjaLiteral(value) {
  if (value === null || value === undefined) return "none";
  if (typeof value === "string") return JSON.stringify(value);
  if (typeof value === "boolean") return value ? "true" : "false";
  if (typeof value === "number" && Number.isFinite(value)) return String(value);
  return "none";
}

function rawExpr(dp) {
  return `value_json.dps.get(${JSON.stringify(String(dp.id))})`;
}

function scaledExpr(dp, scale) {
  const raw = rawExpr(dp);
  if (!scale || scale === 1) return raw;
  return `((${raw} | default(0, true)) | float / ${scale})`;
}

function mappedExpr(dp, mapping) {
  const explicit = mapping.items.filter((item) => item.hasFrom);
  if (!explicit.length) {
    const fallback = mapping.defaultItem ? jinjaLiteral(mapping.defaultItem.to) : "none";
    return `(${rawExpr(dp)} | default(${fallback}, true))`;
  }
  const entries = explicit.map((item) => `${jinjaLiteral(item.from)}: ${jinjaLiteral(item.to)}`);
  const dict = `{${entries.join(", ")}}`;
  const fallback = mapping.defaultItem ? `, ${jinjaLiteral(mapping.defaultItem.to)}` : "";
  return `${dict}.get(${rawExpr(dp)}${fallback})`;
}

function attributesTemplate(entity) {
  const entries = [];
  for (let i = 1; i < entity.dps.length; i += 1) {
    const dp = entity.dps[i];
    if (dp.hidden) continue;
    let expr;
    if (dp.mapping) expr = mappedExpr(dp, dp.mapping);
    else if (dp.scale && dp.scale !== 1) expr = scaledExpr(dp, dp.scale);
    else expr = rawExpr(dp);
    entries.push(`${JSON.stringify(dp.name)}: ${expr}`);
  }
  if (!entries.length) return null;
  return `{{ {${entries.join(", ")}} | tojson }}`;
}

function parseMapping(dp, entityType, primary, path) {
  if (dp.mapping === undefined) return null;
  if (!Array.isArray(dp.mapping)) throw new Error(`${path}.mapping must be an array`);

  const items = [];
  let standaloneScale = null;
  for (const rule of dp.mapping) {
    if (!isObject(rule)) throw new Error(`${path}.mapping entries must be objects`);
    if ([...UNSUPPORTED_MAP_KEYS].some((key) => key in rule)) {
      throw new Error(`${path}.mapping uses an unsupported feature`);
    }
    const hasFrom = "dps_val" in rule && rule.dps_val !== null;
    const hasTo = "value" in rule;
    const hasScale = "scale" in rule;
    if (hasScale && !hasFrom && !hasTo) {
      const scale = safeNumber(rule.scale, 0.000001, 10000);
      if (scale === null) throw new Error(`${path}.mapping scale must be positive`);
      standaloneScale = scale;
      continue;
    }
    if (hasScale) throw new Error(`${path}.mapping scale must be standalone`);
    if (!hasFrom && !hasTo) throw new Error(`${path}.mapping rule is empty`);

    const item = {
      hasFrom,
      hidden: rule.hidden === true,
    };
    if (hasFrom && rule.dps_val !== null) item.from = rule.dps_val;
    if (hasTo) item.to = rule.value;
    else if (hasFrom && rule.dps_val !== null) item.to = rule.dps_val;
    else throw new Error(`${path}.mapping non-default rule needs a value`);
    items.push(item);
  }

  if (standaloneScale !== null && items.length) {
    throw new Error(`${path} combines scale and value mappings, which is not supported yet`);
  }

  if (primary) {
    for (const item of items) {
      if (!item.hasFrom) continue;
      if (entityType === "switch" || entityType === "binary_sensor" || entityType === "button") {
        const value = toBoolean(item.to);
        if (value === null) throw new Error(`${path} mapping values must be boolean for ${entityType}`);
        item.to = value;
      } else if (entityType === "number") {
        const value = toNumber(item.to);
        if (value === null) throw new Error(`${path} mapping values must be numeric for number`);
        item.to = value;
      } else if (entityType === "select") {
        item.to = text(item.to);
      }
    }
    const defaultItem = items.find((item) => !item.hasFrom);
    if (defaultItem && (entityType === "switch" || entityType === "binary_sensor" || entityType === "button")) {
      const value = toBoolean(defaultItem.to);
      if (value === null) throw new Error(`${path} default mapping must be boolean`);
      defaultItem.to = value;
    }
    if (defaultItem && entityType === "number") {
      const value = toNumber(defaultItem.to);
      if (value === null) throw new Error(`${path} default mapping must be numeric`);
      defaultItem.to = value;
    }
    if (defaultItem && entityType === "select") defaultItem.to = text(defaultItem.to);
    return {
      items,
      defaultItem,
      scale: standaloneScale ?? 1,
    };
  }

  return {
    items,
    defaultItem: items.find((item) => !item.hasFrom) || null,
    scale: standaloneScale ?? 1,
  };
}

function parseRange(range, path) {
  if (range === undefined) return null;
  if (!isObject(range)) throw new Error(`${path}.range must be an object`);
  const min = safeNumber(range.min);
  const max = safeNumber(range.max);
  if (min === null || max === null || max < min) {
    throw new Error(`${path}.range needs numeric min/max`);
  }
  const step = range.step === undefined ? 1 : safeNumber(range.step, 0.000001, 1e6);
  if (step === null) throw new Error(`${path}.range.step must be positive`);
  return { min, max, step };
}

function parseDp(dp, entityType, index, path) {
  if (!isObject(dp)) throw new Error(`${path} must be an object`);
  if (!DP_TYPES.has(dp.type)) throw new Error(`${path}.type is not supported`);
  if (!Number.isInteger(dp.id) || dp.id < 1 || dp.id > 999) {
    throw new Error(`${path}.id must be 1-999`);
  }
  const name = text(dp.name);
  if (!/^[A-Za-z_][A-Za-z0-9_]{0,22}$/.test(name)) {
    throw new Error(`${path}.name is invalid or too long`);
  }
  const primary = index === 0;
  const expected = PRIMARY_DP[entityType];
  if (primary && name !== expected) throw new Error(`${path}.name must be ${expected}`);
  if (!primary && name === expected) throw new Error(`${path}.name reuses the primary name`);

  const parsedMapping = parseMapping(dp, entityType, primary, path);
  const mapping = parsedMapping && parsedMapping.items.length ? parsedMapping : null;
  const range = parseRange(dp.range, path);
  let dpScale = 1;
  if (dp.scale !== undefined) {
    const value = safeNumber(dp.scale, 0.000001, 10000);
    if (value === null) throw new Error(`${path}.scale must be positive`);
    dpScale = value;
  }
  const scale = parsedMapping?.scale ?? dpScale;
  if (dp.scale !== undefined && mapping && mapping.items.length) {
    throw new Error(`${path} combines scale and value mappings, which is not supported yet`);
  }
  if (entityType === "number" && primary && !range) {
    throw new Error(`${path} needs a range for a number entity`);
  }
  if (entityType === "text" && primary && dp.type !== "string") {
    throw new Error(`${path} must be a string for a text entity`);
  }

  return {
    id: dp.id,
    type: dp.type,
    name,
    unit: text(dp.unit),
    stateClass: text(dp.class),
    precision: Number.isInteger(dp.precision) && dp.precision >= 0 && dp.precision <= 10
      ? dp.precision
      : null,
    optional: dp.optional === true,
    readonly: dp.readonly === true,
    hidden: dp.hidden === true,
    scale,
    range,
    mapping,
  };
}

function boolStateExpr(dp) {
  if (dp.mapping) return mappedExpr(dp, dp.mapping);
  if (dp.type === "boolean") return `(${rawExpr(dp)} | default(false, true))`;
  if (dp.type === "integer" || dp.type === "bitfield") {
    return `((${rawExpr(dp)} | default(0, true)) | int) != 0`;
  }
  throw new Error(`${dp.name} needs a mapping for a string DP`);
}

function switchCommands(dp) {
  if (dp.mapping) {
    const on = dp.mapping.items.find((item) => item.hasFrom && !item.hidden && item.to === true);
    const off = dp.mapping.items.find((item) => item.hasFrom && !item.hidden && item.to === false);
    if (on && off) return { on: on.from, off: off.from };
    if (dp.type !== "boolean" && dp.type !== "integer" && dp.type !== "bitfield") {
      throw new Error(`${dp.name} switch mapping needs on and off values`);
    }
  }
  if (dp.type === "boolean") return { on: true, off: false };
  if (dp.type === "integer" || dp.type === "bitfield") return { on: 1, off: 0 };
  throw new Error(`${dp.name} switch needs a mapping for a string DP`);
}

function buttonPressValue(dp) {
  if (dp.mapping) {
    const press = dp.mapping.items.find((item) => item.hasFrom && !item.hidden && item.to === true);
    if (press) return press.from;
    if (dp.type !== "boolean" && dp.type !== "integer" && dp.type !== "bitfield") {
      throw new Error(`${dp.name} button needs a press mapping`);
    }
  }
  if (dp.type === "boolean") return true;
  if (dp.type === "integer" || dp.type === "bitfield") return 1;
  throw new Error(`${dp.name} button needs a press mapping`);
}

function numberCommandExpr(dp) {
  if (dp.mapping) {
    const entries = dp.mapping.items
      .filter((item) => item.hasFrom && !item.hidden)
      .map((item) => `${jinjaLiteral(item.to)}: ${jinjaLiteral(item.from)}`);
    if (entries.length) {
      const fallback = dp.mapping.items.find((item) => item.hasFrom && !item.hidden);
      return `{${entries.join(", ")}}.get(value | float, ${jinjaLiteral(fallback.from)})`;
    }
    if (dp.type !== "integer" && dp.type !== "bitfield" && dp.type !== "float") {
      throw new Error(`${dp.name} number mapping is empty`);
    }
  }
  const scaled = `(value | float * ${dp.scale})`;
  if (dp.type === "integer" || dp.type === "bitfield") return `(${scaled} | round(0) | int)`;
  return `(${scaled} | round(3))`;
}

function selectCommandExpr(dp) {
  const items = dp.mapping?.items.filter((item) => item.hasFrom && !item.hidden) || [];
  if (!items.length) throw new Error(`${dp.name} select needs visible options`);
  const entries = items.map((item) => `${jinjaLiteral(text(item.to))}: ${jinjaLiteral(item.from)}`);
  return `{${entries.join(", ")}}.get(value, ${jinjaLiteral(items[0].from)})`;
}

function sensorExpression(dp) {
  if (dp.mapping) return mappedExpr(dp, dp.mapping);
  if (dp.type === "integer" || dp.type === "bitfield" || dp.type === "float") {
    return scaledExpr(dp, dp.scale);
  }
  return rawExpr(dp);
}

function selectOptions(dp) {
  return (dp.mapping?.items || [])
    .filter((item) => item.hasFrom && !item.hidden)
    .map((item) => text(item.to));
}

function enumOptions(dp) {
  const options = new Set(["unknown"]);
  for (const item of dp.mapping?.items || []) {
    if (!item.hidden) options.add(text(item.to));
  }
  return [...options];
}

function buildDiscovery(entity, index, info) {
  const dps = entity.dps;
  const primary = dps[0];
  const component = entity.type;
  const objectId = `e${index}`;
  const config = {
    name: entity.name,
  };
  if (entity.class) config.device_class = entity.class;
  if (entity.category) config.entity_category = entity.category;
  if (entity.icon) config.icon = entity.icon;
  if (entity.hidden) config.enabled_by_default = false;

  if (component === "button") {
    config.command_topic = info.commandTopic;
    config.payload_press = JSON.stringify({ [String(primary.id)]: buttonPressValue(primary) });
    if (entity.class) config.device_class = entity.class;
    return { component, object_id: objectId, config };
  }

  config.state_topic = info.stateTopic;
  const attributes = attributesTemplate(entity);

  if (component === "sensor") {
    config.value_template = `{{ ${sensorExpression(primary)} }}`;
    if (primary.unit) config.unit_of_measurement = primary.unit;
    if (primary.stateClass) config.state_class = primary.stateClass;
    if (primary.precision !== null) config.suggested_display_precision = primary.precision;
    if (entity.class === "enum") {
      if (!primary.mapping) throw new Error(`${entity.name} enum sensor needs a mapping`);
      config.options = enumOptions(primary);
    }
  } else if (component === "binary_sensor") {
    config.value_template = `{{ 'ON' if ${boolStateExpr(primary)} else 'OFF' }}`;
    config.payload_on = "ON";
    config.payload_off = "OFF";
  } else if (component === "switch") {
    config.value_template = `{{ 'ON' if ${boolStateExpr(primary)} else 'OFF' }}`;
    config.payload_on = "ON";
    config.payload_off = "OFF";
    config.command_topic = info.commandTopic;
    const { on, off } = switchCommands(primary);
    config.command_template =
      `{{ {${JSON.stringify(String(primary.id))}: (${jinjaLiteral(on)} if value == 'ON' else ${jinjaLiteral(off)})} | tojson }}`;
  } else if (component === "number") {
    config.value_template = `{{ ${sensorExpression(primary)} }}`;
    config.command_topic = info.commandTopic;
    config.command_template =
      `{{ {${JSON.stringify(String(primary.id))}: ${numberCommandExpr(primary)}} | tojson }}`;
    config.min = primary.range.min;
    config.max = primary.range.max;
    config.step = primary.range.step;
    config.mode = "box";
    if (primary.unit) config.unit_of_measurement = primary.unit;
  } else if (component === "select") {
    const options = selectOptions(primary);
    if (!options.length) throw new Error(`${entity.name} select needs visible options`);
    config.value_template = `{{ ${mappedExpr(primary, primary.mapping)} }}`;
    config.command_topic = info.commandTopic;
    config.command_template =
      `{{ {${JSON.stringify(String(primary.id))}: ${selectCommandExpr(primary)}} | tojson }}`;
    config.options = options;
  } else if (component === "text") {
    config.value_template = `{{ ${rawExpr(primary)} | default("") }}`;
    config.command_topic = info.commandTopic;
    config.command_template = `{{ {${JSON.stringify(String(primary.id))}: value} | tojson }}`;
    if (primary.range) {
      config.min = Math.max(0, Math.round(primary.range.min));
      config.max = Math.max(config.min, Math.round(primary.range.max));
    }
  } else {
    throw new Error(`unsupported entity type ${component}`);
  }

  if (attributes) {
    config.json_attributes_topic = info.stateTopic;
    config.json_attributes_template = attributes;
  }
  return { component, object_id: objectId, config };
}

function deriveDetect(entities) {
  const require = [];
  const groups = [
    new Set(["sensor", "binary_sensor", "switch"]),
    new Set(["select", "button", "text", "number"]),
  ];
  for (const group of groups) {
    for (const entity of entities) {
      if (!group.has(entity.type)) continue;
      const primary = entity.dps[0];
      if (primary.optional) continue;
      if (!require.includes(primary.id)) require.push(primary.id);
      if (require.length >= 8) return require;
    }
  }
  return require;
}

export function buildPack(yaml, meta, info) {
  if (!isObject(yaml) || !Array.isArray(yaml.entities) || !yaml.entities.length) {
    throw new Error("descriptor has no entities");
  }
  const infoSafe = {
    stateTopic: info?.stateTopic || "__STATE_TOPIC__",
    commandTopic: info?.commandTopic || "__CMD_TOPIC__",
  };
  const entities = [];
  const discovery = [];
  for (let i = 0; i < yaml.entities.length; i += 1) {
    const rawEntity = yaml.entities[i];
    if (!isObject(rawEntity)) throw new Error(`entities[${i}] is not an object`);
    const type = rawEntity.entity;
    if (!SIMPLE_ENTITIES.has(type)) throw new Error(`unsupported entity type ${type}`);
    const rawDps = asArray(rawEntity.dps);
    if (!rawDps.length) throw new Error(`entities[${i}] has no DPs`);
    const dps = rawDps.map((dp, dpIndex) => parseDp(dp, type, dpIndex, `entities[${i}].dps[${dpIndex}]`));
    const names = new Set();
    for (const dp of dps) {
      if (names.has(dp.name)) throw new Error(`entities[${i}] has duplicate DP name ${dp.name}`);
      names.add(dp.name);
    }
    const primary = dps[0];
    if (primary.hidden) throw new Error(`entities[${i}] primary DP is hidden`);
    if (["switch", "number", "select", "button", "text"].includes(type) && primary.readonly) {
      throw new Error(`entities[${i}] writable entity has readonly primary DP`);
    }
    if (type === "binary_sensor" && primary.type === "string" && !primary.mapping) {
      throw new Error(`entities[${i}] binary sensor string DP needs a mapping`);
    }
    if (type === "button" && primary.type !== "boolean" && !primary.mapping) {
      throw new Error(`entities[${i}] button needs a press mapping`);
    }
    const entity = {
      type,
      name: entityName(rawEntity).slice(0, 31),
      class: text(rawEntity.class),
      category: text(rawEntity.category),
      icon: text(rawEntity.icon),
      hidden: rawEntity.hidden === true,
      dps,
    };
    if (type === "sensor" && entity.class === "enum") {
      if (!primary.mapping || !primary.mapping.items.some((item) => item.hasFrom)) {
        throw new Error(`entities[${i}] enum sensor needs a value mapping`);
      }
      for (const item of primary.mapping.items) item.to = text(item.to);
    }
    entities.push(entity);
    discovery.push(buildDiscovery(entity, i, infoSafe));
  }

  return {
    format: 1,
    id: slugify(meta?.id || "profile", 23),
    name: text(meta?.name || displayName(yaml)).slice(0, 47),
    device_type: text(meta?.group || classifyGroup(yaml, meta?.file)),
    source: text(meta?.source || "").slice(0, 95),
    detect: { require: deriveDetect(entities), priority: 5 },
    discovery,
  };
}

export function supportInfo(yaml, meta = {}) {
  try {
    const pack = buildPack(yaml, meta, {
      stateTopic: "__STATE_TOPIC__",
      commandTopic: "__CMD_TOPIC__",
    });
    return {
      supported: true,
      group: pack.device_type,
      discovery: pack.discovery.length,
      bytes: JSON.stringify(pack).length,
      reason: "",
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

export function groupOrder(groups) {
  return sortGroups(groups);
}
)A_JS";

const char OFFLINE_CHARGERS_JSON[] PROGMEM =
R"A_JSON({"generated":"2026-10-27T19:06:43.049Z","tuya_commit":"245469649fb2df6767988213dd6ae9dcb3b3e0f6","packs":[{"format":1,"id":"adpow_gob6pro_evcharger","name":"EV charger (ADPOW GO-B6 Pro 32A)","device_type":"Chargers","source":"tuya-local 245469649fb2/adpow_gob6pro_evcharger.yaml","detect":{"require":[18,24,4,17],"priority":5},"discovery":[{"component":"sensor","object_id":"e0","config":{"name":"Energy","device_class":"energy","state_topic":"__STATE_TOPIC__","value_template":"{{ ((value_json.dps.get(\"1\") | default(0, true)) | float / 100) }}","unit_of_measurement":"kWh","state_class":"total_increasing"}},{"component":"number","object_id":"e1","config":{"name":"Charge current","device_class":"current","entity_category":"config","state_topic":"__STATE_TOPIC__","value_template":"{{ value_json.dps.get(\"4\") }}","command_topic":"__CMD_TOPIC__","command_template":"{{ {\"4\": ((value | float * 1) | round(0) | int)} | tojson }}","min":13,"max":32,"step":1,"mode":"box","unit_of_measurement":"A"}},{"component":"sensor","object_id":"e2","config":{"name":"Power","device_class":"power","entity_category":"diagnostic","state_topic":"__STATE_TOPIC__","value_template":"{{ ((value_json.dps.get(\"9\") | default(0, true)) | float / 1000) }}","unit_of_measurement":"kW","state_class":"measurement"}},{"component":"sensor","object_id":"e3","config":{"name":"Connection state","device_class":"enum","entity_category":"diagnostic","icon":"mdi:ev-plug-type2","state_topic":"__STATE_TOPIC__","value_template":"{{ {\"controlpi_12v\": \"standby\", \"controlpi_12v_pwm\": \"Communication initialising\", \"controlpi_9v\": \"Vehicle detected\", \"controlpi_9v_pwm\": \"Vehicle connected\", \"controlpi_6v\": \"Ready to charge\", \"controlpi_6v_pwm\": \"charging\", \"controlpi_error\": \"fault\"}.get(value_json.dps.get(\"13\")) }}","options":["unknown","standby","Communication initialising","Vehicle detected","Vehicle connected","Ready to charge","charging","fault"],"json_attributes_topic":"__STATE_TOPIC__","json_attributes_template":"{{ {\"unknown_3\": value_json.dps.get(\"3\"), \"unknown_10\": value_json.dps.get(\"10\"), \"unknown_103\": value_json.dps.get(\"103\"), \"unknown_104\": value_json.dps.get(\"104\"), \"unknown_105\": value_json.dps.get(\"105\"), \"unknown_106\": value_json.dps.get(\"106\"), \"unknown_107\": value_json.dps.get(\"107\"), \"unknown_108\": value_json.dps.get(\"108\"), \"unknown_109\": value_json.dps.get(\"109\"), \"unknown_112\": value_json.dps.get(\"112\"), \"unknown_114\": value_json.dps.get(\"114\"), \"unknown_117\": value_json.dps.get(\"117\")} | tojson }}"}},{"component":"number","object_id":"e4","config":{"name":"Charge energy","device_class":"energy_storage","entity_category":"config","state_topic":"__STATE_TOPIC__","value_template":"{{ ((value_json.dps.get(\"17\") | default(0, true)) | float / 100) }}","command_topic":"__CMD_TOPIC__","command_template":"{{ {\"17\": ((value | float * 100) | round(0) | int)} | tojson }}","min":0,"max":2000000,"step":1,"mode":"box","unit_of_measurement":"kWh"}},{"component":"switch","object_id":"e5","config":{"name":"Switch","icon":"mdi:power","state_topic":"__STATE_TOPIC__","value_template":"{{ 'ON' if (value_json.dps.get(\"18\") | default(false, true)) else 'OFF' }}","payload_on":"ON","payload_off":"OFF","command_topic":"__CMD_TOPIC__","command_template":"{{ {\"18\": (true if value == 'ON' else false)} | tojson }}"}},{"component":"sensor","object_id":"e6","config":{"name":"Temperature","device_class":"temperature","entity_category":"diagnostic","state_topic":"__STATE_TOPIC__","value_template":"{{ value_json.dps.get(\"24\") }}","unit_of_measurement":"C","state_class":"measurement"}},{"component":"sensor","object_id":"e7","config":{"name":"Last charge","entity_category":"diagnostic","state_topic":"__STATE_TOPIC__","value_template":"{{ ((value_json.dps.get(\"25\") | default(0, true)) | float / 100) }}","unit_of_measurement":"kWh","state_class":"measurement"}}]},{"format":1,"id":"feyree_ev_charger","name":"EV charger","device_type":"Chargers","source":"tuya-local 245469649fb2/feyree_ev_charger.yaml","detect":{"require":[101,10,106,107,108,109,110,112],"priority":5},"discovery":[{"component":"sensor","object_id":"e0","config":{"name":"Status","device_class":"enum","icon":"mdi:ev-station","state_topic":"__STATE_TOPIC__","value_template":"{{ {\"no_connect\": \"available\", \"connect\": \"plugged_in\", \"charing\": \"charging\", \"wait_rfid\": \"waiting_for_authorization\", \"finish\": \"charged\", \"wait_charing\": \"waiting\", \"error\": \"fault\"}.get(value_json.dps.get(\"101\")) }}","options":["unknown","available","plugged_in","charging","waiting_for_authorization","charged","waiting","fault"],"json_attributes_topic":"__STATE_TOPIC__","json_attributes_template":"{{ {\"charger_status\": value_json.dps.get(\"3\"), \"alarm_set_1\": value_json.dps.get(\"11\"), \"alarm_set_2\": value_json.dps.get(\"12\"), \"mode\": {\"charge_now\": \"charge_now\", \"charge_pct\": \"charge_pct\", \"charge_energy\": \"charge_energy\", \"charge_schedule\": \"charge_schedule\"}.get(value_json.dps.get(\"14\")), \"balance_energy\": value_json.dps.get(\"15\"), \"switch\": value_json.dps.get(\"18\"), \"system_version\": value_json.dps.get(\"23\"), \"charge_energy_once\": value_json.dps.get(\"25\"), \"online_state\": value_json.dps.get(\"27\"), \"require_authorization\": value_json.dps.get(\"105\")} | tojson }}"}},{"component":"sensor","object_id":"e1","config":{"name":"Alarm event","device_class":"enum","icon":"mdi:alert-outline","state_topic":"__STATE_TOPIC__","value_template":"{{ {0: \"Ready\", 1: \"Under voltage protection\", 2: \"Over voltage protection\", 4: \"Over current protection\", 8: \"Protective earth error\", 16: \"Temperature is to high\", 32: \"Control pilot line error\", 64: \"Leak\", 128: \"Relay socket\", 256: \"Protective earth error 2\", 512: \"Plug temperature is to high.\", 1024: \"PCB temperature is to high.\", 2048: \"Core temperature is to high.\", 4096: \"ESB\", 8192: \"Socket protective earth error\", 16384: \"Socket leakage\", 32768: \"Socket temperature is to high.\"}.get(value_json.dps.get(\"10\")) }}","options":["unknown","Ready","Under voltage protection","Over voltage protection","Over current protection","Protective earth error","Temperature is to high","Control pilot line error","Leak","Relay socket","Protective earth error 2","Plug temperature is to high.","PCB temperature is to high.","Core temperature is to high.","ESB","Socket protective earth error","Socket leakage","Socket temperature is to high."]}},{"component":"button","object_id":"e2","config":{"name":"Clear energy","device_class":"restart","entity_category":"config","command_topic":"__CMD_TOPIC__","payload_press":"{\"16\":true}"}},{"component":"button","object_id":"e3","config":{"name":"Clear event","device_class":"restart","entity_category":"config","command_topic":"__CMD_TOPIC__","payload_press":"{\"20\":true}"}},{"component":"number","object_id":"e4","config":{"name":"Set charge current","icon":"mdi:ev-plug-type2","state_topic":"__STATE_TOPIC__","value_template":"{{ value_json.dps.get(\"102\") }}","command_topic":"__CMD_TOPIC__","command_template":"{{ {\"102\": ((value | float * 1) | round(0) | int)} | tojson }}","min":6,"max":32,"step":1,"mode":"box","unit_of_measurement":"A"}},{"component":"switch","object_id":"e5","config":{"name":"Leak enable","entity_category":"config","state_topic":"__STATE_TOPIC__","value_template":"{{ 'ON' if {false: false, true: true}.get(value_json.dps.get(\"103\")) else 'OFF' }}","payload_on":"ON","payload_off":"OFF","command_topic":"__CMD_TOPIC__","command_template":"{{ {\"103\": (true if value == 'ON' else false)} | tojson }}"}},{"component":"number","object_id":"e6","config":{"name":"Charge delay","entity_category":"config","icon":"mdi:car-clock","state_topic":"__STATE_TOPIC__","value_template":"{{ value_json.dps.get(\"104\") }}","command_topic":"__CMD_TOPIC__","command_template":"{{ {\"104\": ((value | float * 1) | round(0) | int)} | tojson }}","min":0,"max":15,"step":1,"mode":"box","unit_of_measurement":"h"}},{"component":"switch","object_id":"e7","config":{"name":"Power switch","entity_category":"config","icon":"mdi:power","state_topic":"__STATE_TOPIC__","value_template":"{{ 'ON' if (value_json.dps.get(\"105\") | default(false, true)) else 'OFF' }}","payload_on":"ON","payload_off":"OFF","command_topic":"__CMD_TOPIC__","command_template":"{{ {\"105\": (true if value == 'ON' else false)} | tojson }}"}},{"component":"sensor","object_id":"e8","config":{"name":"Energy","device_class":"energy","state_topic":"__STATE_TOPIC__","value_template":"{{ ((value_json.dps.get(\"106\") | default(0, true)) | float / 10) }}","unit_of_measurement":"kWh","state_class":"total_increasing"}},{"component":"sensor","object_id":"e9","config":{"name":"Voltage","device_class":"voltage","entity_category":"diagnostic","state_topic":"__STATE_TOPIC__","value_template":"{{ value_json.dps.get(\"107\") }}","unit_of_measurement":"V","state_class":"measurement"}},{"component":"sensor","object_id":"e10","config":{"name":"Current","device_class":"current","state_topic":"__STATE_TOPIC__","value_template":"{{ ((value_json.dps.get(\"108\") | default(0, true)) | float / 10) }}","unit_of_measurement":"A","state_class":"measurement"}},{"component":"sensor","object_id":"e11","config":{"name":"Power","device_class":"power","state_topic":"__STATE_TOPIC__","value_template":"{{ ((value_json.dps.get(\"109\") | default(0, true)) | float / 10) }}","unit_of_measurement":"kW","state_class":"measurement"}},{"component":"sensor","object_id":"e12","config":{"name":"Temperature","device_class":"temperature","entity_category":"diagnostic","state_topic":"__STATE_TOPIC__","value_template":"{{ ((value_json.dps.get(\"110\") | default(0, true)) | float / 10) }}","unit_of_measurement":"C","state_class":"measurement"}},{"component":"number","object_id":"e13","config":{"name":"Leakage calibration","entity_category":"config","state_topic":"__STATE_TOPIC__","value_template":"{{ {20: 20}.get(value_json.dps.get(\"111\")) }}","command_topic":"__CMD_TOPIC__","command_template":"{{ {\"111\": {20: 20}.get(value | float, 20)} | tojson }}","min":20,"max":250,"step":1,"mode":"box"}},{"component":"switch","object_id":"e14","config":{"name":"Charging enable","icon":"mdi:ev-station","state_topic":"__STATE_TOPIC__","value_template":"{{ 'ON' if (value_json.dps.get(\"112\") | default(false, true)) else 'OFF' }}","payload_on":"ON","payload_off":"OFF","command_topic":"__CMD_TOPIC__","command_template":"{{ {\"112\": (true if value == 'ON' else false)} | tojson }}"}},{"component":"sensor","object_id":"e15","config":{"name":"Charging time","device_class":"duration","entity_category":"diagnostic","state_topic":"__STATE_TOPIC__","value_template":"{{ ((value_json.dps.get(\"113\") | default(0, true)) | float / 10) }}","unit_of_measurement":"h","state_class":"measurement"}},{"component":"number","object_id":"e16","config":{"name":"Charging duration","entity_category":"config","icon":"mdi:car-clock","state_topic":"__STATE_TOPIC__","value_template":"{{ value_json.dps.get(\"114\") }}","command_topic":"__CMD_TOPIC__","command_template":"{{ {\"114\": ((value | float * 1) | round(0) | int)} | tojson }}","min":0,"max":15,"step":1,"mode":"box","unit_of_measurement":"h"}}]},{"format":1,"id":"makeskyblue_mppt_charge","name":"Solar charging controller","device_type":"Chargers","source":"tuya-local 245469649fb2/makeskyblue_mppt_charger.yaml","detect":{"require":[101,102,103,104,105,107,108,111],"priority":5},"discovery":[{"component":"sensor","object_id":"e0","config":{"name":"Charge mode","device_class":"enum","entity_category":"diagnostic","state_topic":"__STATE_TOPIC__","value_template":"{{ {\"mode_1\": \"standby\", \"mode_2\": \"Constant current\", \"mode_3\": \"Constant voltage\", \"mode_4\": \"MPPT\", \"mode_5\": \"Floating charge\"}.get(value_json.dps.get(\"101\")) }}","options":["unknown","standby","Constant current","Constant voltage","MPPT","Floating charge"]}},{"component":"sensor","object_id":"e1","config":{"name":"Solar voltage","device_class":"voltage","entity_category":"diagnostic","state_topic":"__STATE_TOPIC__","value_template":"{{ ((value_json.dps.get(\"102\") | default(0, true)) | float / 10) }}","unit_of_measurement":"V","state_class":"measurement"}},{"component":"sensor","object_id":"e2","config":{"name":"Battery voltage","device_class":"voltage","entity_category":"diagnostic","state_topic":"__STATE_TOPIC__","value_template":"{{ ((value_json.dps.get(\"103\") | default(0, true)) | float / 10) }}","unit_of_measurement":"V","state_class":"measurement"}},{"component":"sensor","object_id":"e3","config":{"name":"Current","device_class":"current","entity_category":"diagnostic","state_topic":"__STATE_TOPIC__","value_template":"{{ ((value_json.dps.get(\"104\") | default(0, true)) | float / 10) }}","unit_of_measurement":"A","state_class":"measurement"}},{"component":"sensor","object_id":"e4","config":{"name":"Power","device_class":"power","entity_category":"diagnostic","state_topic":"__STATE_TOPIC__","value_template":"{{ ((value_json.dps.get(\"105\") | default(0, true)) | float / 10) }}","unit_of_measurement":"W","state_class":"measurement"}},{"component":"switch","object_id":"e5","config":{"name":"Charging","entity_category":"config","enabled_by_default":false,"state_topic":"__STATE_TOPIC__","value_template":"{{ 'ON' if (value_json.dps.get(\"106\") | default(false, true)) else 'OFF' }}","payload_on":"ON","payload_off":"OFF","command_topic":"__CMD_TOPIC__","command_template":"{{ {\"106\": (true if value == 'ON' else false)} | tojson }}"}},{"component":"binary_sensor","object_id":"e6","config":{"name":"Problem","device_class":"problem","entity_category":"diagnostic","state_topic":"__STATE_TOPIC__","value_template":"{{ 'ON' if {0: false}.get(value_json.dps.get(\"107\"), true) else 'OFF' }}","payload_on":"ON","payload_off":"OFF","json_attributes_topic":"__STATE_TOPIC__","json_attributes_template":"{{ {\"fault_code\": value_json.dps.get(\"107\"), \"description\": {0: \"ok\", 1: \"Battery undervoltage\", 2: \"Battery overvoltage\"}.get(value_json.dps.get(\"107\"))} | tojson }}"}},{"component":"sensor","object_id":"e7","config":{"name":"Energy","device_class":"energy","entity_category":"diagnostic","state_topic":"__STATE_TOPIC__","value_template":"{{ ((value_json.dps.get(\"108\") | default(0, true)) | float / 10) }}","unit_of_measurement":"kWh","state_class":"total_increasing"}},{"component":"number","object_id":"e8","config":{"name":"Undervoltage recovery level","device_class":"voltage","entity_category":"config","state_topic":"__STATE_TOPIC__","value_template":"{{ ((value_json.dps.get(\"109\") | default(0, true)) | float / 10) }}","command_topic":"__CMD_TOPIC__","command_template":"{{ {\"109\": ((value | float * 10) | round(0) | int)} | tojson }}","min":0,"max":1000,"step":1,"mode":"box","unit_of_measurement":"V"}},{"component":"number","object_id":"e9","config":{"name":"Undervoltage level","device_class":"voltage","entity_category":"config","state_topic":"__STATE_TOPIC__","value_template":"{{ ((value_json.dps.get(\"110\") | default(0, true)) | float / 10) }}","command_topic":"__CMD_TOPIC__","command_template":"{{ {\"110\": ((value | float * 10) | round(0) | int)} | tojson }}","min":0,"max":1000,"step":1,"mode":"box","unit_of_measurement":"V"}},{"component":"sensor","object_id":"e10","config":{"name":"Battery","device_class":"battery","state_topic":"__STATE_TOPIC__","value_template":"{{ value_json.dps.get(\"111\") }}","unit_of_measurement":"%"}},{"component":"select","object_id":"e11","config":{"name":"Battery type","entity_category":"config","state_topic":"__STATE_TOPIC__","value_template":"{{ {\"lead_acid\": \"Lead acid\", \"lithium\": \"Lithium\"}.get(value_json.dps.get(\"112\")) }}","command_topic":"__CMD_TOPIC__","command_template":"{{ {\"112\": {\"Lead acid\": \"lead_acid\", \"Lithium\": \"lithium\"}.get(value, \"lead_acid\")} | tojson }}","options":["Lead acid","Lithium"]}},{"component":"sensor","object_id":"e12","config":{"name":"Temperature","device_class":"temperature","state_topic":"__STATE_TOPIC__","value_template":"{{ ((value_json.dps.get(\"113\") | default(0, true)) | float / 10) }}","unit_of_measurement":"C","state_class":"measurement"}},{"component":"sensor","object_id":"e13","config":{"name":"Battery cells","entity_category":"diagnostic","state_topic":"__STATE_TOPIC__","value_template":"{{ value_json.dps.get(\"114\") }}"}},{"component":"number","object_id":"e14","config":{"name":"Equalization voltage","device_class":"voltage","entity_category":"config","state_topic":"__STATE_TOPIC__","value_template":"{{ ((value_json.dps.get(\"115\") | default(0, true)) | float / 10) }}","command_topic":"__CMD_TOPIC__","command_template":"{{ {\"115\": ((value | float * 10) | round(0) | int)} | tojson }}","min":0,"max":1000,"step":1,"mode":"box","unit_of_measurement":"V"}},{"component":"number","object_id":"e15","config":{"name":"Float charge voltage","device_class":"voltage","entity_category":"config","state_topic":"__STATE_TOPIC__","value_template":"{{ ((value_json.dps.get(\"116\") | default(0, true)) | float / 10) }}","command_topic":"__CMD_TOPIC__","command_template":"{{ {\"116\": ((value | float * 10) | round(0) | int)} | tojson }}","min":0,"max":1000,"step":1,"mode":"box","unit_of_measurement":"V"}},{"component":"number","object_id":"e16","config":{"name":"Constant current","device_class":"current","entity_category":"config","state_topic":"__STATE_TOPIC__","value_template":"{{ value_json.dps.get(\"117\") }}","command_topic":"__CMD_TOPIC__","command_template":"{{ {\"117\": ((value | float * 1) | round(0) | int)} | tojson }}","min":0,"max":150,"step":1,"mode":"box","unit_of_measurement":"A"}},{"component":"number","object_id":"e17","config":{"name":"Timer","device_class":"duration","entity_category":"config","state_topic":"__STATE_TOPIC__","value_template":"{{ value_json.dps.get(\"118\") }}","command_topic":"__CMD_TOPIC__","command_template":"{{ {\"118\": ((value | float * 1) | round(0) | int)} | tojson }}","min":0,"max":24,"step":1,"mode":"box","unit_of_measurement":"h"}}]},{"format":1,"id":"parkside_plgs2012a1_sma","name":"Battery charger (Parkside PLGS2012A1)","device_type":"Chargers","source":"tuya-local 245469649fb2/parkside_plgs2012a1_smart_charger.yaml","detect":{"require":[8,9,5,6,10,101,102,103],"priority":5},"discovery":[{"component":"switch","object_id":"e0","config":{"name":"Switch","icon":"mdi:ev-station","state_topic":"__STATE_TOPIC__","value_template":"{{ 'ON' if (value_json.dps.get(\"1\") | default(false, true)) else 'OFF' }}","payload_on":"ON","payload_off":"OFF","command_topic":"__CMD_TOPIC__","command_template":"{{ {\"1\": (true if value == 'ON' else false)} | tojson }}","json_attributes_topic":"__STATE_TOPIC__","json_attributes_template":"{{ {\"model\": value_json.dps.get(\"2\")} | tojson }}"}},{"component":"number","object_id":"e1","config":{"name":"Charge current","device_class":"current","entity_category":"config","icon":"mdi:current-dc","state_topic":"__STATE_TOPIC__","value_template":"{{ ((value_json.dps.get(\"3\") | default(0, true)) | float / 1000) }}","command_topic":"__CMD_TOPIC__","command_template":"{{ {\"3\": ((value | float * 1000) | round(0) | int)} | tojson }}","min":0,"max":30000,"step":1,"mode":"box","unit_of_measurement":"A"}},{"component":"number","object_id":"e2","config":{"name":"Charge voltage","device_class":"voltage","entity_category":"config","icon":"mdi:battery-charging","state_topic":"__STATE_TOPIC__","value_template":"{{ ((value_json.dps.get(\"4\") | default(0, true)) | float / 1000) }}","command_topic":"__CMD_TOPIC__","command_template":"{{ {\"4\": ((value | float * 1000) | round(0) | int)} | tojson }}","min":0,"max":25000,"step":1,"mode":"box","unit_of_measurement":"V"}},{"component":"switch","object_id":"e3","config":{"name":"Storage","entity_category":"config","state_topic":"__STATE_TOPIC__","value_template":"{{ 'ON' if {true: true, false: false}.get(value_json.dps.get(\"8\")) else 'OFF' }}","payload_on":"ON","payload_off":"OFF","command_topic":"__CMD_TOPIC__","command_template":"{{ {\"8\": (true if value == 'ON' else false)} | tojson }}"}},{"component":"switch","object_id":"e4","config":{"name":"Temperature limiter","entity_category":"config","icon":"mdi:thermometer-alert","state_topic":"__STATE_TOPIC__","value_template":"{{ 'ON' if (value_json.dps.get(\"9\") | default(false, true)) else 'OFF' }}","payload_on":"ON","payload_off":"OFF","command_topic":"__CMD_TOPIC__","command_template":"{{ {\"9\": (true if value == 'ON' else false)} | tojson }}"}},{"component":"sensor","object_id":"e5","config":{"name":"Battery","device_class":"battery","state_topic":"__STATE_TOPIC__","value_template":"{{ value_json.dps.get(\"5\") }}","unit_of_measurement":"%"}},{"component":"sensor","object_id":"e6","config":{"name":"Temperature","device_class":"temperature","entity_category":"diagnostic","state_topic":"__STATE_TOPIC__","value_template":"{{ value_json.dps.get(\"6\") }}","unit_of_measurement":"C","state_class":"measurement"}},{"component":"sensor","object_id":"e7","config":{"name":"Max temperature count","entity_category":"diagnostic","icon":"mdi:thermometer-plus","state_topic":"__STATE_TOPIC__","value_template":"{{ value_json.dps.get(\"10\") }}"}},{"component":"select","object_id":"e8","config":{"name":"Charge type","entity_category":"config","icon":"mdi:battery-unknown","state_topic":"__STATE_TOPIC__","value_template":"{{ {\"ECO\": \"Eco\", \"quick\": \"Performance\", \"standard\": \"Balanced\", \"individual\": \"Expert\"}.get(value_json.dps.get(\"7\")) }}","command_topic":"__CMD_TOPIC__","command_template":"{{ {\"7\": {\"Eco\": \"ECO\", \"Performance\": \"quick\", \"Balanced\": \"standard\", \"Expert\": \"individual\"}.get(value, \"ECO\")} | tojson }}","options":["Eco","Performance","Balanced","Expert"]}},{"component":"sensor","object_id":"e9","config":{"name":"Max current","device_class":"current","entity_category":"diagnostic","state_topic":"__STATE_TOPIC__","value_template":"{{ ((value_json.dps.get(\"101\") | default(0, true)) | float / 1000) }}","unit_of_measurement":"A","state_class":"measurement"}},{"component":"sensor","object_id":"e10","config":{"name":"Time Remaining","device_class":"duration","state_topic":"__STATE_TOPIC__","value_template":"{{ value_json.dps.get(\"102\") }}","unit_of_measurement":"min"}},{"component":"binary_sensor","object_id":"e11","config":{"name":"Almost charged","entity_category":"diagnostic","icon":"mdi:battery-charging-90","state_topic":"__STATE_TOPIC__","value_template":"{{ 'ON' if (value_json.dps.get(\"103\") | default(false, true)) else 'OFF' }}","payload_on":"ON","payload_off":"OFF"}},{"component":"binary_sensor","object_id":"e12","config":{"name":"Fully charged","entity_category":"diagnostic","icon":"mdi:battery-charging-100","state_topic":"__STATE_TOPIC__","value_template":"{{ 'ON' if (value_json.dps.get(\"104\") | default(false, true)) else 'OFF' }}","payload_on":"ON","payload_off":"OFF"}},{"component":"binary_sensor","object_id":"e13","config":{"name":"Problem","device_class":"problem","entity_category":"diagnostic","state_topic":"__STATE_TOPIC__","value_template":"{{ 'ON' if {0: false}.get(value_json.dps.get(\"11\"), true) else 'OFF' }}","payload_on":"ON","payload_off":"OFF","json_attributes_topic":"__STATE_TOPIC__","json_attributes_template":"{{ {\"fault_code\": value_json.dps.get(\"11\")} | tojson }}"}}]},{"format":1,"id":"tary_taac380_evcharger","name":"EV charger (TARY TA-AC380/22)","device_type":"Chargers","source":"tuya-local 245469649fb2/tary_taac380_evcharger.yaml","detect":{"require":[109,103,104,105,106,107,108,131],"priority":5},"discovery":[{"component":"switch","object_id":"e0","config":{"name":"Switch","icon":"mdi:ev-station","state_topic":"__STATE_TOPIC__","value_template":"{{ 'ON' if (value_json.dps.get(\"109\") | default(false, true)) else 'OFF' }}","payload_on":"ON","payload_off":"OFF","command_topic":"__CMD_TOPIC__","command_template":"{{ {\"109\": (true if value == 'ON' else false)} | tojson }}","json_attributes_topic":"__STATE_TOPIC__","json_attributes_template":"{{ {\"firmware_version\": value_json.dps.get(\"127\"), \"device_number\": value_json.dps.get(\"152\"), \"station_code\": value_json.dps.get(\"113\")} | tojson }}"}},{"component":"sensor","object_id":"e1","config":{"name":"Voltage A","device_class":"voltage","state_topic":"__STATE_TOPIC__","value_template":"{{ ((value_json.dps.get(\"103\") | default(0, true)) | float / 100) }}","unit_of_measurement":"V","state_class":"measurement"}},{"component":"sensor","object_id":"e2","config":{"name":"Voltage B","device_class":"voltage","state_topic":"__STATE_TOPIC__","value_template":"{{ ((value_json.dps.get(\"104\") | default(0, true)) | float / 100) }}","unit_of_measurement":"V","state_class":"measurement"}},{"component":"sensor","object_id":"e3","config":{"name":"Voltage C","device_class":"voltage","state_topic":"__STATE_TOPIC__","value_template":"{{ ((value_json.dps.get(\"105\") | default(0, true)) | float / 100) }}","unit_of_measurement":"V","state_class":"measurement"}},{"component":"sensor","object_id":"e4","config":{"name":"Current A","device_class":"current","state_topic":"__STATE_TOPIC__","value_template":"{{ ((value_json.dps.get(\"106\") | default(0, true)) | float / 100) }}","unit_of_measurement":"A","state_class":"measurement"}},{"component":"sensor","object_id":"e5","config":{"name":"Current B","device_class":"current","state_topic":"__STATE_TOPIC__","value_template":"{{ ((value_json.dps.get(\"107\") | default(0, true)) | float / 100) }}","unit_of_measurement":"A","state_class":"measurement"}},{"component":"sensor","object_id":"e6","config":{"name":"Current C","device_class":"current","state_topic":"__STATE_TOPIC__","value_template":"{{ ((value_json.dps.get(\"108\") | default(0, true)) | float / 100) }}","unit_of_measurement":"A","state_class":"measurement"}},{"component":"sensor","object_id":"e7","config":{"name":"Current","device_class":"current","state_topic":"__STATE_TOPIC__","value_template":"{{ ((value_json.dps.get(\"131\") | default(0, true)) | float / 100) }}","unit_of_measurement":"A","state_class":"measurement"}},{"component":"sensor","object_id":"e8","config":{"name":"Charged energy","device_class":"energy","state_topic":"__STATE_TOPIC__","value_template":"{{ ((value_json.dps.get(\"110\") | default(0, true)) | float / 100) }}","unit_of_measurement":"kWh","state_class":"total_increasing"}},{"component":"sensor","object_id":"e9","config":{"name":"Power X","device_class":"power","state_topic":"__STATE_TOPIC__","value_template":"{{ ((value_json.dps.get(\"150\") | default(0, true)) | float / 100) }}","unit_of_measurement":"kW","state_class":"measurement"}},{"component":"sensor","object_id":"e10","config":{"name":"Power X","device_class":"power","state_topic":"__STATE_TOPIC__","value_template":"{{ ((value_json.dps.get(\"151\") | default(0, true)) | float / 100) }}","unit_of_measurement":"kW","state_class":"measurement"}},{"component":"sensor","object_id":"e11","config":{"name":"Transaction energy","device_class":"energy","state_topic":"__STATE_TOPIC__","value_template":"{{ ((value_json.dps.get(\"153\") | default(0, true)) | float / 100) }}","unit_of_measurement":"kWh","state_class":"total_increasing"}},{"component":"sensor","object_id":"e12","config":{"name":"Transaction time","device_class":"duration","state_topic":"__STATE_TOPIC__","value_template":"{{ value_json.dps.get(\"154\") }}","unit_of_measurement":"s","state_class":"measurement"}},{"component":"sensor","object_id":"e13","config":{"name":"Transaction amount","enabled_by_default":false,"state_topic":"__STATE_TOPIC__","value_template":"{{ ((value_json.dps.get(\"155\") | default(0, true)) | float / 100) }}","state_class":"total_increasing"}},{"component":"binary_sensor","object_id":"e14","config":{"name":"Transaction","device_class":"running","state_topic":"__STATE_TOPIC__","value_template":"{{ 'ON' if (value_json.dps.get(\"156\") | default(false, true)) else 'OFF' }}","payload_on":"ON","payload_off":"OFF"}},{"component":"number","object_id":"e15","config":{"name":"Rated current","device_class":"current","entity_category":"config","icon":"mdi:current-ac","state_topic":"__STATE_TOPIC__","value_template":"{{ ((value_json.dps.get(\"115\") | default(0, true)) | float / 100) }}","command_topic":"__CMD_TOPIC__","command_template":"{{ {\"115\": ((value | float * 100) | round(0) | int)} | tojson }}","min":900,"max":3200,"step":1,"mode":"box","unit_of_measurement":"A"}},{"component":"number","object_id":"e16","config":{"name":"Load balancing current","device_class":"current","entity_category":"config","icon":"mdi:current-ac","state_topic":"__STATE_TOPIC__","value_template":"{{ ((value_json.dps.get(\"117\") | default(0, true)) | float / 100) }}","command_topic":"__CMD_TOPIC__","command_template":"{{ {\"117\": ((value | float * 100) | round(0) | int)} | tojson }}","min":100,"max":10000,"step":1,"mode":"box","unit_of_measurement":"A"}},{"component":"binary_sensor","object_id":"e17","config":{"name":"Load balancing","device_class":"safety","state_topic":"__STATE_TOPIC__","value_template":"{{ 'ON' if (value_json.dps.get(\"116\") | default(false, true)) else 'OFF' }}","payload_on":"ON","payload_off":"OFF"}},{"component":"select","object_id":"e18","config":{"name":"Charge pattern","entity_category":"config","icon":"mdi:ev-station","state_topic":"__STATE_TOPIC__","value_template":"{{ {\"netversion\": \"Network version\", \"standalone\": \"Standalone normal\", \"standalone_reserved\": \"Standalone reserved\", \"plug_and_charge\": \"Plug and charge\"}.get(value_json.dps.get(\"123\")) }}","command_topic":"__CMD_TOPIC__","command_template":"{{ {\"123\": {\"Network version\": \"netversion\", \"Standalone normal\": \"standalone\", \"Standalone reserved\": \"standalone_reserved\", \"Plug and charge\": \"plug_and_charge\"}.get(value, \"netversion\")} | tojson }}","options":["Network version","Standalone normal","Standalone reserved","Plug and charge"]}},{"component":"select","object_id":"e19","config":{"name":"Measurement","entity_category":"config","state_topic":"__STATE_TOPIC__","value_template":"{{ {\"internal_meter\": \"Internal meter\", \"external_meter\": \"External meter\"}.get(value_json.dps.get(\"126\")) }}","command_topic":"__CMD_TOPIC__","command_template":"{{ {\"126\": {\"Internal meter\": \"internal_meter\", \"External meter\": \"external_meter\"}.get(value, \"internal_meter\")} | tojson }}","options":["Internal meter","External meter"]}},{"component":"switch","object_id":"e20","config":{"name":"Earth test","entity_category":"config","state_topic":"__STATE_TOPIC__","value_template":"{{ 'ON' if {\"enabled_energy\": true, \"forbidden_energy\": false}.get(value_json.dps.get(\"128\")) else 'OFF' }}","payload_on":"ON","payload_off":"OFF","command_topic":"__CMD_TOPIC__","command_template":"{{ {\"128\": (\"enabled_energy\" if value == 'ON' else \"forbidden_energy\")} | tojson }}"}},{"component":"switch","object_id":"e21","config":{"name":"Pen protect","entity_category":"config","state_topic":"__STATE_TOPIC__","value_template":"{{ 'ON' if {\"enabled_energy\": true, \"forbidden_energy\": false}.get(value_json.dps.get(\"129\")) else 'OFF' }}","payload_on":"ON","payload_off":"OFF","command_topic":"__CMD_TOPIC__","command_template":"{{ {\"129\": (\"enabled_energy\" if value == 'ON' else \"forbidden_energy\")} | tojson }}"}},{"component":"select","object_id":"e22","config":{"name":"Network model","entity_category":"config","state_topic":"__STATE_TOPIC__","value_template":"{{ {\"LAN\": \"LAN\", \"4G\": \"4G\"}.get(value_json.dps.get(\"140\")) }}","command_topic":"__CMD_TOPIC__","command_template":"{{ {\"140\": {\"LAN\": \"LAN\", \"4G\": \"4G\"}.get(value, \"LAN\")} | tojson }}","options":["LAN","4G"]}},{"component":"binary_sensor","object_id":"e23","config":{"name":"Tamper","device_class":"tamper","state_topic":"__STATE_TOPIC__","value_template":"{{ 'ON' if (value_json.dps.get(\"142\") | default(false, true)) else 'OFF' }}","payload_on":"ON","payload_off":"OFF"}},{"component":"switch","object_id":"e24","config":{"name":"OCCP TLS encryption","entity_category":"config","enabled_by_default":false,"state_topic":"__STATE_TOPIC__","value_template":"{{ 'ON' if (value_json.dps.get(\"143\") | default(false, true)) else 'OFF' }}","payload_on":"ON","payload_off":"OFF","command_topic":"__CMD_TOPIC__","command_template":"{{ {\"143\": (true if value == 'ON' else false)} | tojson }}"}},{"component":"sensor","object_id":"e25","config":{"name":"Accumulated balance","enabled_by_default":false,"state_topic":"__STATE_TOPIC__","value_template":"{{ ((value_json.dps.get(\"111\") | default(0, true)) | float / 100) }}","state_class":"total_increasing"}},{"component":"sensor","object_id":"e26","config":{"name":"Card balance","enabled_by_default":false,"state_topic":"__STATE_TOPIC__","value_template":"{{ ((value_json.dps.get(\"112\") | default(0, true)) | float / 100) }}","state_class":"total_increasing"}},{"component":"binary_sensor","object_id":"e27","config":{"name":"Connectivity","device_class":"connectivity","state_topic":"__STATE_TOPIC__","value_template":"{{ 'ON' if (value_json.dps.get(\"102\") | default(false, true)) else 'OFF' }}","payload_on":"ON","payload_off":"OFF"}},{"component":"sensor","object_id":"e28","config":{"name":"Voltage CP","device_class":"voltage","state_topic":"__STATE_TOPIC__","value_template":"{{ ((value_json.dps.get(\"114\") | default(0, true)) | float / 100) }}","unit_of_measurement":"V","state_class":"measurement"}},{"component":"sensor","object_id":"e29","config":{"name":"Equipment time","entity_category":"diagnostic","enabled_by_default":false,"state_topic":"__STATE_TOPIC__","value_template":"{{ value_json.dps.get(\"124\") }}"}},{"component":"text","object_id":"e30","config":{"name":"OCCP URL","entity_category":"config","enabled_by_default":false,"state_topic":"__STATE_TOPIC__","value_template":"{{ value_json.dps.get(\"125\") | default(\"\") }}","command_topic":"__CMD_TOPIC__","command_template":"{{ {\"125\": value} | tojson }}"}},{"component":"text","object_id":"e31","config":{"name":"APN","entity_category":"config","enabled_by_default":false,"state_topic":"__STATE_TOPIC__","value_template":"{{ value_json.dps.get(\"132\") | default(\"\") }}","command_topic":"__CMD_TOPIC__","command_template":"{{ {\"132\": value} | tojson }}"}},{"component":"text","object_id":"e32","config":{"name":"APN user","entity_category":"config","enabled_by_default":false,"state_topic":"__STATE_TOPIC__","value_template":"{{ value_json.dps.get(\"133\") | default(\"\") }}","command_topic":"__CMD_TOPIC__","command_template":"{{ {\"133\": value} | tojson }}"}},{"component":"text","object_id":"e33","config":{"name":"APN password","entity_category":"config","enabled_by_default":false,"state_topic":"__STATE_TOPIC__","value_template":"{{ value_json.dps.get(\"134\") | default(\"\") }}","command_topic":"__CMD_TOPIC__","command_template":"{{ {\"134\": value} | tojson }}"}},{"component":"text","object_id":"e34","config":{"name":"Authorized card 1","entity_category":"config","enabled_by_default":false,"state_topic":"__STATE_TOPIC__","value_template":"{{ value_json.dps.get(\"135\") | default(\"\") }}","command_topic":"__CMD_TOPIC__","command_template":"{{ {\"135\": value} | tojson }}"}},{"component":"text","object_id":"e35","config":{"name":"Authorized card 2","entity_category":"config","enabled_by_default":false,"state_topic":"__STATE_TOPIC__","value_template":"{{ value_json.dps.get(\"136\") | default(\"\") }}","command_topic":"__CMD_TOPIC__","command_template":"{{ {\"136\": value} | tojson }}"}},{"component":"text","object_id":"e36","config":{"name":"QR code prefix","entity_category":"config","enabled_by_default":false,"state_topic":"__STATE_TOPIC__","value_template":"{{ value_json.dps.get(\"141\") | default(\"\") }}","command_topic":"__CMD_TOPIC__","command_template":"{{ {\"141\": value} | tojson }}"}}]},{"format":1,"id":"zencar_ev_charger","name":"EV charger (Zencar Wpro 32A 3P)","device_type":"Chargers","source":"tuya-local 245469649fb2/zencar_ev_charger.yaml","detect":{"require":[3,9,4],"priority":5},"discovery":[{"component":"sensor","object_id":"e0","config":{"name":"Status","device_class":"enum","icon":"mdi:ev-station","state_topic":"__STATE_TOPIC__","value_template":"{{ {\"charger_free\": \"available\", \"charger_insert\": \"plugged_in\", \"charger_free_fault\": \"fault_unplugged\", \"charger_charging\": \"charging\", \"charger_stop_wait\": \"waiting\", \"charger_fault\": \"fault\"}.get(value_json.dps.get(\"3\")) }}","options":["unknown","available","plugged_in","fault_unplugged","charging","waiting","fault"],"json_attributes_topic":"__STATE_TOPIC__","json_attributes_template":"{{ {\"system_version\": value_json.dps.get(\"23\"), \"serial_number\": value_json.dps.get(\"101\")} | tojson }}"}},{"component":"sensor","object_id":"e1","config":{"name":"Energy","device_class":"energy","state_topic":"__STATE_TOPIC__","value_template":"{{ ((value_json.dps.get(\"1\") | default(0, true)) | float / 100) }}","unit_of_measurement":"kWh","state_class":"total_increasing"}},{"component":"number","object_id":"e2","config":{"name":"Set current","device_class":"current","entity_category":"config","icon":"mdi:ev-plug-type2","state_topic":"__STATE_TOPIC__","value_template":"{{ value_json.dps.get(\"4\") }}","command_topic":"__CMD_TOPIC__","command_template":"{{ {\"4\": ((value | float * 1) | round(0) | int)} | tojson }}","min":6,"max":32,"step":1,"mode":"box","unit_of_measurement":"A"}},{"component":"sensor","object_id":"e3","config":{"name":"Power","device_class":"power","entity_category":"diagnostic","state_topic":"__STATE_TOPIC__","value_template":"{{ ((value_json.dps.get(\"9\") | default(0, true)) | float / 1000) }}","unit_of_measurement":"kW","state_class":"measurement"}},{"component":"select","object_id":"e4","config":{"name":"Charging Mode","icon":"mdi:ev-station","state_topic":"__STATE_TOPIC__","value_template":"{{ {\"charge_now\": \"immediate\", \"charge_pct\": \"charge_to_percent\", \"charge_energy\": \"fixed_charge\", \"charge_schedule\": \"scheduled_charge\"}.get(value_json.dps.get(\"14\")) }}","command_topic":"__CMD_TOPIC__","command_template":"{{ {\"14\": {\"immediate\": \"charge_now\", \"charge_to_percent\": \"charge_pct\", \"fixed_charge\": \"charge_energy\", \"scheduled_charge\": \"charge_schedule\"}.get(value, \"charge_now\")} | tojson }}","options":["immediate","charge_to_percent","fixed_charge","scheduled_charge"]}},{"component":"switch","object_id":"e5","config":{"name":"Switch","icon":"mdi:ev-station","state_topic":"__STATE_TOPIC__","value_template":"{{ 'ON' if (value_json.dps.get(\"18\") | default(false, true)) else 'OFF' }}","payload_on":"ON","payload_off":"OFF","command_topic":"__CMD_TOPIC__","command_template":"{{ {\"18\": (true if value == 'ON' else false)} | tojson }}"}},{"component":"sensor","object_id":"e6","config":{"name":"Temperature","device_class":"temperature","entity_category":"diagnostic","state_topic":"__STATE_TOPIC__","value_template":"{{ value_json.dps.get(\"24\") }}","unit_of_measurement":"C","state_class":"measurement"}},{"component":"sensor","object_id":"e7","config":{"name":"Last charge","device_class":"energy_storage","state_topic":"__STATE_TOPIC__","value_template":"{{ ((value_json.dps.get(\"25\") | default(0, true)) | float / 100) }}","unit_of_measurement":"kWh","state_class":"measurement"}}]}]})A_JSON";

}  // namespace tuya_lora
