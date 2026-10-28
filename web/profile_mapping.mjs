// Portions derived from tuya-local (MIT). See THIRD_PARTY_NOTICES.md.
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
