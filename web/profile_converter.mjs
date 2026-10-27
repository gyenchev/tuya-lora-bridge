// Shared tuya-local -> Tuya LoRa browser profile converter.
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
