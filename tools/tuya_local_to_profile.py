#!/usr/bin/env python3
"""Convert tuya-local device YAML descriptors into Tuya LoRa Bridge profiles.

This is a best-effort converter for the compact generic schema implemented by
the base node. It intentionally skips descriptors that use unsupported entity
types, DP types, mapping features, or that exceed the firmware limits.

Usage:
    python3 tools/tuya_local_to_profile.py --source /path/to/tuya-local/custom_components/tuya_local/devices --out profiles

Requires PyYAML: pip install pyyaml
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections import Counter
from pathlib import Path
from typing import Any

try:
    import yaml
except ImportError as exc:  # pragma: no cover
    raise SystemExit("PyYAML is required: pip install pyyaml") from exc

SUPPORTED_ENTITIES = {
    "sensor",
    "binary_sensor",
    "switch",
    "number",
    "select",
    "button",
    "text",
}

PRIMARY_DP = {
    "sensor": "sensor",
    "binary_sensor": "sensor",
    "switch": "switch",
    "number": "value",
    "select": "option",
    "button": "button",
    "text": "value",
}

DP_TYPES = {
    "string": "string",
    "boolean": "boolean",
    "integer": "integer",
    "bitfield": "bitfield",
    "float": "float",
}

UNSUPPORTED_MAP_KEYS = {
    "value_redirect",
    "constraint",
    "conditions",
    "value_mirror",
    "invalid",
    "target_range",
}

RESERVED_IDS = {"auto", "fallback", "unknown"}
MAX_ENTITIES = 10
MAX_DPS = 4
MAX_MAPPINGS = 32
MAX_JSON_BYTES = 8192
MAX_ID = 23
MAX_NAME = 39
MAX_DESCRIPTION = 95
MAX_ENTITY_NAME = 31
MAX_DP_NAME = 23
MAX_FROM = 23
MAX_TO = 47
MAX_DEVICE_CLASS = 31
MAX_STATE_CLASS = 19
MAX_UNIT = 11
MAX_ICON = 35
MAX_CATEGORY = 11


class SkipProfile(Exception):
    """Raised when a descriptor cannot be represented by the compact schema."""


def byte_len(text: str) -> int:
    return len(text.encode("utf-8"))


def truncate_utf8(text: str, limit: int) -> str:
    data = text.encode("utf-8")
    if len(data) <= limit:
        return text
    return data[:limit].decode("utf-8", "ignore")


def title_case(text: str) -> str:
    text = re.sub(r"[_-]+", " ", str(text)).strip()
    return " ".join(part[:1].upper() + part[1:] for part in text.split())


def slugify(text: str, limit: int = MAX_ID) -> str:
    text = re.sub(r"[^a-z0-9]+", "_", str(text).lower()).strip("_")
    if not text:
        text = "profile"
    return truncate_utf8(text, limit).strip("_") or "profile"


def as_list(value: Any) -> list[Any]:
    return value if isinstance(value, list) else []


def as_dict(value: Any) -> dict[str, Any]:
    return value if isinstance(value, dict) else {}


def is_number(value: Any) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def mapping_bool(value: Any) -> bool | None:
    if isinstance(value, bool):
        return value
    if is_number(value):
        return float(value) != 0.0
    if isinstance(value, str):
        lowered = value.lower()
        if lowered in {"true", "1", "on", "yes"}:
            return True
        if lowered in {"false", "0", "off", "no"}:
            return False
    return None


def derive_name(entity: dict[str, Any]) -> str:
    name = entity.get("name")
    if name:
        return str(name)

    translation = entity.get("translation_key") or entity.get("translation_only_key")
    placeholders = as_dict(entity.get("translation_placeholders"))
    if translation == "status":
        return "Status"
    if translation == "voltage_x":
        suffix = placeholders.get("x", "")
        return f"Voltage {suffix}".strip()
    if translation == "current_x":
        suffix = placeholders.get("x", "")
        return f"Current {suffix}".strip()
    if translation:
        return title_case(translation)

    device_class = entity.get("class")
    if device_class:
        return title_case(device_class)

    return title_case(entity.get("entity", "Sensor").replace("_", " "))


def product_summary(products: list[Any]) -> str:
    parts: list[str] = []
    for product in products[:3]:
        if not isinstance(product, dict):
            continue
        manufacturer = product.get("manufacturer", "")
        model = product.get("model", "")
        model_id = product.get("model_id", "")
        label = " ".join(str(x) for x in (manufacturer, model) if x)
        if model_id and model_id != model:
            label = f"{label} ({model_id})".strip()
        if label:
            parts.append(label)
    return "; ".join(parts)


def display_name(yaml_name: str, products: list[Any]) -> str:
    name = str(yaml_name or "Tuya device")
    summary = product_summary(products)
    if summary:
        candidate = f"{name} ({summary})"
        if byte_len(candidate) <= MAX_NAME:
            return candidate
    return truncate_utf8(name, MAX_NAME)


def mapping_from_value(value: Any, path: str) -> Any:
    if value is None:
        return None
    if isinstance(value, (str, int, float, bool)):
        if isinstance(value, str) and byte_len(value) > MAX_FROM:
            raise SkipProfile(f"{path}: dps_val is too long")
        return value
    raise SkipProfile(f"{path}: unsupported dps_val type")


def mapping_to_value(value: Any, path: str) -> Any:
    if isinstance(value, (str, int, float, bool)):
        if isinstance(value, str) and byte_len(value) > MAX_TO:
            raise SkipProfile(f"{path}: mapping value is too long")
        return value
    raise SkipProfile(f"{path}: unsupported mapping value type")


def convert_mapping(rules: Any, path: str) -> tuple[list[dict[str, Any]], float | None]:
    if not isinstance(rules, list):
        return [], None

    converted: list[dict[str, Any]] = []
    standalone_scale: float | None = None
    for index, rule in enumerate(rules):
        if not isinstance(rule, dict):
            raise SkipProfile(f"{path}[{index}]: mapping rule is not an object")
        if UNSUPPORTED_MAP_KEYS & rule.keys():
            raise SkipProfile(f"{path}[{index}]: unsupported mapping feature")

        has_from = "dps_val" in rule
        has_value = "value" in rule
        has_scale = "scale" in rule

        if has_scale and not has_from and not has_value:
            if not is_number(rule["scale"]) or float(rule["scale"]) <= 0:
                raise SkipProfile(f"{path}[{index}]: invalid scale")
            standalone_scale = float(rule["scale"])
            continue

        if has_scale and (has_from or has_value):
            raise SkipProfile(f"{path}[{index}]: scale must be standalone")

        item: dict[str, Any] = {}
        if has_from:
            from_value = mapping_from_value(rule["dps_val"], f"{path}[{index}].dps_val")
            if from_value is not None:
                item["dps_val"] = from_value
        if has_value:
            item["value"] = mapping_to_value(rule["value"], f"{path}[{index}].value")
        if not item:
            raise SkipProfile(f"{path}[{index}]: empty mapping rule")
        if rule.get("hidden") is True:
            item["hidden"] = True
        converted.append(item)

    return converted, standalone_scale


def convert_range(value: Any, path: str) -> dict[str, Any] | None:
    if value is None:
        return None
    if not isinstance(value, dict):
        raise SkipProfile(f"{path}: range is not an object")
    minimum = value.get("min")
    maximum = value.get("max")
    if not is_number(minimum) or not is_number(maximum):
        raise SkipProfile(f"{path}: range requires numeric min and max")
    result: dict[str, Any] = {"min": minimum, "max": maximum}
    step = value.get("step")
    if step is not None:
        if not is_number(step) or float(step) <= 0:
            raise SkipProfile(f"{path}: invalid range step")
        result["step"] = step
    if float(maximum) < float(minimum):
        raise SkipProfile(f"{path}: range max is below min")
    return result


def convert_dp(dp: dict[str, Any], entity_type: str, index: int, path: str) -> dict[str, Any]:
    dp_type = DP_TYPES.get(str(dp.get("type", "")))
    if not dp_type:
        raise SkipProfile(f"{path}: unsupported DP type {dp.get('type')!r}")

    dp_id = dp.get("id")
    if not isinstance(dp_id, int) or isinstance(dp_id, bool) or not 1 <= dp_id <= 999:
        raise SkipProfile(f"{path}: invalid DP id")

    name = dp.get("name")
    if not isinstance(name, str) or not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]{0,22}", name):
        raise SkipProfile(f"{path}: invalid or too-long DP name")

    expected = PRIMARY_DP[entity_type]
    if index == 0 and name != expected:
        raise SkipProfile(f"{path}: first DP must be named {expected}")
    if index > 0 and name == expected:
        raise SkipProfile(f"{path}: extra DP reuses primary name {expected}")

    result: dict[str, Any] = {"id": dp_id, "type": dp_type, "name": name}

    unit = dp.get("unit")
    if unit is not None:
        if byte_len(str(unit)) > MAX_UNIT:
            raise SkipProfile(f"{path}: unit is too long")
        result["unit"] = str(unit)

    state_class = dp.get("class")
    if state_class is not None:
        if byte_len(str(state_class)) > MAX_STATE_CLASS:
            raise SkipProfile(f"{path}: state class is too long")
        result["class"] = str(state_class)

    precision = dp.get("precision")
    if precision is not None:
        if not isinstance(precision, int) or isinstance(precision, bool) or not 0 <= precision <= 10:
            raise SkipProfile(f"{path}: invalid precision")
        result["precision"] = precision

    scale = dp.get("scale")
    if scale is not None:
        if not is_number(scale) or float(scale) <= 0 or float(scale) > 10000:
            raise SkipProfile(f"{path}: invalid scale")
        result["scale"] = scale

    converted_range = convert_range(dp.get("range"), f"{path}.range")
    if converted_range is not None:
        result["range"] = converted_range

    for key in ("optional", "readonly", "hidden"):
        if dp.get(key) is True:
            result[key] = True

    mapping, mapping_scale = convert_mapping(dp.get("mapping"), f"{path}.mapping")
    if mapping:
        result["mapping"] = mapping
    if mapping_scale is not None:
        if "scale" in result and float(result["scale"]) != mapping_scale:
            raise SkipProfile(f"{path}: conflicting scales")
        result["scale"] = mapping_scale

    return result


def convert_entity(entity: dict[str, Any], index: int, path: str) -> dict[str, Any]:
    entity_type = entity.get("entity")
    if entity_type not in SUPPORTED_ENTITIES:
        raise SkipProfile(f"{path}: unsupported entity type {entity_type!r}")

    dps = entity.get("dps")
    if not isinstance(dps, list) or not 1 <= len(dps) <= MAX_DPS:
        raise SkipProfile(f"{path}: needs 1-{MAX_DPS} DPs")

    converted_dps: list[dict[str, Any]] = []
    names: set[str] = set()
    for dp_index, dp in enumerate(dps):
        if not isinstance(dp, dict):
            raise SkipProfile(f"{path}.dps[{dp_index}]: not an object")
        converted = convert_dp(dp, str(entity_type), dp_index, f"{path}.dps[{dp_index}]")
        if converted["name"] in names:
            raise SkipProfile(f"{path}: duplicate DP name {converted['name']}")
        names.add(converted["name"])
        converted_dps.append(converted)

    primary = converted_dps[0]
    if primary.get("hidden") is True:
        raise SkipProfile(f"{path}: primary DP is hidden")
    if entity_type in {"switch", "number", "select", "button", "text"} and primary.get("readonly") is True:
        raise SkipProfile(f"{path}: writable entity has readonly primary DP")
    if entity_type == "number" and "range" not in primary:
        raise SkipProfile(f"{path}: number entity has no range")
    if entity_type == "text" and primary["type"] != "string":
        raise SkipProfile(f"{path}: text entity needs a string DP")

    if entity_type == "binary_sensor" and primary["type"] == "string" and "mapping" not in primary:
        raise SkipProfile(f"{path}: binary sensor string DP needs a mapping")

    mapping = primary.get("mapping", [])
    visible = [item for item in mapping if not item.get("hidden")]
    has_from = [item for item in visible if "dps_val" in item]
    has_on = any(mapping_bool(item.get("value")) is True for item in visible)
    has_off = any(mapping_bool(item.get("value")) is False for item in visible)
    has_press = any(item.get("value") is True for item in visible)

    if entity_type == "select" and not has_from:
        raise SkipProfile(f"{path}: select needs at least one visible option")
    if entity_type == "switch" and mapping and (not has_on or not has_off):
        raise SkipProfile(f"{path}: switch mapping needs on and off")
    if entity_type == "button" and primary["type"] != "boolean" and not has_press:
        raise SkipProfile(f"{path}: button needs a press mapping")

    name = truncate_utf8(derive_name(entity), MAX_ENTITY_NAME)
    if not name:
        raise SkipProfile(f"{path}: empty entity name")

    result: dict[str, Any] = {
        "entity": entity_type,
        "name": name,
        "dps": converted_dps,
    }

    device_class = entity.get("class")
    if device_class is not None:
        if byte_len(str(device_class)) > MAX_DEVICE_CLASS:
            raise SkipProfile(f"{path}: device class is too long")
        result["class"] = str(device_class)

    category = entity.get("category")
    if category is not None:
        if str(category) not in {"diagnostic", "config"}:
            raise SkipProfile(f"{path}: unsupported entity category")
        result["category"] = str(category)

    icon = entity.get("icon")
    if icon is not None:
        if byte_len(str(icon)) <= MAX_ICON:
            result["icon"] = str(icon)

    if entity.get("hidden") is True:
        result["hidden"] = True

    return result


def convert_profile(source: Path, profile_id: str) -> tuple[dict[str, Any], int]:
    try:
        data = yaml.safe_load(source.read_text(encoding="utf-8"))
    except Exception as exc:
        raise SkipProfile(f"YAML parse failed: {exc}") from exc

    if not isinstance(data, dict):
        raise SkipProfile("top-level YAML is not an object")

    entities = data.get("entities")
    if not isinstance(entities, list) or not 1 <= len(entities) <= MAX_ENTITIES:
        raise SkipProfile(f"needs 1-{MAX_ENTITIES} entities")

    converted_entities: list[dict[str, Any]] = []
    total_mappings = 0
    for index, entity in enumerate(entities):
        if not isinstance(entity, dict):
            raise SkipProfile(f"entities[{index}] is not an object")
        converted = convert_entity(entity, index, f"entities[{index}]")
        for dp in converted["dps"]:
            total_mappings += len(dp.get("mapping", []))
        converted_entities.append(converted)

    if total_mappings > MAX_MAPPINGS:
        raise SkipProfile(f"uses {total_mappings} mapping rules (limit {MAX_MAPPINGS})")

    products = as_list(data.get("products"))
    summary = product_summary(products)
    description = summary or f"Generated from {source.name}"
    description = truncate_utf8(description, MAX_DESCRIPTION)

    require: list[int] = []
    # Prefer DPs that are normally present in every status snapshot. Config
    # numbers are still useful fallbacks, but requiring them can make a correct
    # profile fail to match when a device omits them from a partial snapshot.
    groups = [
        {"sensor", "binary_sensor", "switch"},
        {"select", "button", "text", "number"},
    ]
    for group in groups:
        for entity in converted_entities:
            if entity["entity"] not in group:
                continue
            primary = entity["dps"][0]
            if primary.get("optional") is True:
                continue
            if primary["id"] not in require:
                require.append(primary["id"])
            if len(require) >= 8:
                break
        if len(require) >= 8:
            break

    profile: dict[str, Any] = {
        "id": profile_id,
        "name": display_name(data.get("name", "Tuya device"), products),
        "description": description,
        "detect": {"require": require, "priority": 5},
        "entities": converted_entities,
    }

    encoded = json.dumps({"profiles": [profile]}, ensure_ascii=False, separators=(",", ":"))
    if len(encoded.encode("utf-8")) > MAX_JSON_BYTES:
        raise SkipProfile(f"profile JSON is {len(encoded.encode('utf-8'))} bytes (limit {MAX_JSON_BYTES})")

    return profile, len(encoded.encode("utf-8"))


def unique_id(base: str, used: set[str]) -> str:
    candidate = base
    suffix = 2
    while candidate in used or candidate in RESERVED_IDS:
        tail = f"_{suffix}"
        candidate = truncate_utf8(base, MAX_ID - len(tail)).rstrip("_") + tail
        suffix += 1
    used.add(candidate)
    return candidate


def write_readme(out: Path, total: int, converted: list[dict[str, Any]], skipped: Counter[str]) -> None:
    lines = [
        "# Generated tuya-local profiles",
        "",
        "These profiles were generated from the device descriptors in",
        "[`make-all/tuya-local`](https://github.com/make-all/tuya-local) using",
        "`tools/tuya_local_to_profile.py`.",
        "",
        "The base firmware supports a compact subset of the tuya-local schema:",
        "seven entity types, five DP types, value mappings, scaling, ranges,",
        "units, device/state classes and enum options. Descriptors that use",
        "unsupported entity types, DP types, mapping features or exceed the",
        "firmware limits are skipped.",
        "",
        "## How to use",
        "",
        "Each `profiles/<id>.json` file is a complete custom-profile document.",
        "Paste one into the **Profile** tab, save with `auto` selected, reboot,",
        "then select the profile and save again.",
        "",
        "The current firmware stores at most four custom profiles in NVS. The",
        "`pack.jsonl` file contains all generated profiles, one JSON object per",
        "line, for a future SD/SPIFFS profile-pack loader.",
        "",
        "## Coverage",
        "",
        f"- Source descriptors: {total}",
        f"- Generated profiles: {len(converted)}",
        f"- Skipped: {sum(skipped.values())}",
        "",
        "### Skipped by reason",
        "",
    ]
    if skipped:
        for reason, count in skipped.most_common():
            lines.append(f"- {reason}: {count}")
    else:
        lines.append("- None")
    lines.extend(
        [
            "",
            "## Attribution",
            "",
            "The source device descriptors are from tuya-local, Copyright 2018",
            "Nik Rolls, 2026 Jason Rumney, 2022-2026 other contributors, and are",
            "distributed under the MIT License. See `LICENSE.tuya-local`.",
            "",
        ]
    )
    (out / "README.md").write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path, help="tuya-local devices directory")
    parser.add_argument("--out", required=True, type=Path, help="output directory")
    args = parser.parse_args()

    sources = sorted(args.source.glob("*.yaml"))
    if not sources:
        print(f"no YAML files found under {args.source}", file=sys.stderr)
        return 1

    args.out.mkdir(parents=True, exist_ok=True)
    for old in args.out.glob("*.json"):
        old.unlink()

    used_ids: set[str] = set()
    converted: list[dict[str, Any]] = []
    index_rows: list[tuple[str, str, int, int, str]] = []
    skipped: Counter[str] = Counter()
    pack_lines: list[str] = []

    for source in sources:
        base_id = slugify(source.stem)
        profile_id = unique_id(base_id, used_ids)
        try:
            profile, encoded_size = convert_profile(source, profile_id)
        except SkipProfile as exc:
            skipped[str(exc).split(": ", 1)[-1]] += 1
            continue

        document = {"profiles": [profile]}
        pretty = json.dumps(document, indent=2, ensure_ascii=False) + "\n"
        (args.out / f"{profile_id}.json").write_text(pretty, encoding="utf-8")
        pack_lines.append(json.dumps(profile, ensure_ascii=False, separators=(",", ":")))
        converted.append(profile)
        index_rows.append(
            (
                profile_id,
                profile["name"],
                len(profile["entities"]),
                encoded_size,
                source.name,
            )
        )

    (args.out / "pack.jsonl").write_text("\n".join(pack_lines) + "\n", encoding="utf-8")

    index_lines = [
        "# Generated profile index",
        "",
        "| ID | Name | Entities | JSON size | Source |",
        "|---|---|---:|---:|---|",
    ]
    for profile_id, name, entity_count, size, source in index_rows:
        safe_name = name.replace("|", "\\|")
        index_lines.append(f"| `{profile_id}` | {safe_name} | {entity_count} | {size} | `{source}` |")
    (args.out / "INDEX.md").write_text("\n".join(index_lines) + "\n", encoding="utf-8")

    write_readme(args.out, len(sources), converted, skipped)
    print(f"converted {len(converted)} of {len(sources)} descriptors")
    print(f"wrote {args.out}")
    if skipped:
        print("top skip reasons:")
        for reason, count in skipped.most_common(10):
            print(f"  {count:4d}  {reason}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
