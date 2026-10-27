#include "entities.h"

#include <PubSubClient.h>
#include <Preferences.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "config.h"
#include "config_store.h"
#include "ha_discovery.h"

extern PubSubClient mqtt;

namespace tuya_lora {

namespace {

constexpr const char *NVS_NAMESPACE = "tuya-lora-base";
constexpr const char *NVS_SLOTS_KEY = "generic_slots";

enum GenericSlotType : uint8_t {
  GST_NONE = 0,
  GST_SENSOR,
  GST_BINARY_SENSOR,
  GST_SWITCH,
  GST_NUMBER,
  GST_SELECT,
  GST_BUTTON,
  GST_TEXT,
};

struct RawValue {
  String text;
  double number = 0.0;
  bool boolean = false;
  bool hasNumber = false;
  bool hasBoolean = false;
};

String dpKey(uint16_t dp) {
  char key[8];
  snprintf(key, sizeof(key), "%u", static_cast<unsigned>(dp));
  return String(key);
}

String jsonQuote(const char *text) {
  String out = "\"";
  for (const char *p = text; p && *p; ++p) {
    switch (*p) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(*p) < 0x20) {
          char escaped[7];
          snprintf(escaped, sizeof(escaped), "\\u%04x",
                   static_cast<unsigned char>(*p));
          out += escaped;
        } else {
          out += *p;
        }
        break;
    }
  }
  out += "\"";
  return out;
}

bool readRawValue(JsonObject dps, const ProfileDp &dp, RawValue &raw) {
  JsonVariant value = dps[dpKey(dp.dp).c_str()];
  if (value.isNull()) return false;

  switch (dp.type) {
    case PDT_STRING:
      if (value.is<const char *>()) {
        raw.text = value.as<const char *>();
      } else if (value.is<bool>()) {
        raw.text = value.as<bool>() ? "true" : "false";
      } else if (value.is<float>()) {
        const double number = value.as<double>();
        if (number == static_cast<double>(static_cast<long>(number))) {
          raw.text = String(static_cast<long>(number));
        } else {
          raw.text = String(number, 6);
        }
      } else {
        return false;
      }
      return true;

    case PDT_BOOL: {
      if (value.is<bool>()) {
        raw.boolean = value.as<bool>();
      } else if (value.is<float>()) {
        raw.boolean = value.as<double>() != 0.0;
      } else if (value.is<const char *>()) {
        const char *text = value.as<const char *>();
        if (strcasecmp(text, "true") == 0 || strcmp(text, "1") == 0 ||
            strcasecmp(text, "on") == 0) {
          raw.boolean = true;
        } else if (strcasecmp(text, "false") == 0 || strcmp(text, "0") == 0 ||
                   strcasecmp(text, "off") == 0) {
          raw.boolean = false;
        } else {
          return false;
        }
      } else {
        return false;
      }
      raw.hasBoolean = true;
      raw.text = raw.boolean ? "true" : "false";
      return true;
    }

    case PDT_INT:
    case PDT_BITFIELD: {
      if (value.is<const char *>()) {
        const char *text = value.as<const char *>();
        char *end = nullptr;
        const long number = strtol(text, &end, 10);
        if (!end || *end != '\0') return false;
        raw.number = static_cast<double>(number);
      } else if (value.is<bool>()) {
        raw.number = value.as<bool>() ? 1.0 : 0.0;
      } else if (value.is<float>()) {
        raw.number = value.as<double>();
      } else {
        return false;
      }
      raw.hasNumber = true;
      raw.text = String(static_cast<long>(raw.number));
      return true;
    }

    case PDT_FLOAT: {
      if (value.is<const char *>()) {
        const char *text = value.as<const char *>();
        char *end = nullptr;
        const double number = strtod(text, &end);
        if (!end || *end != '\0') return false;
        raw.number = number;
      } else if (value.is<bool>()) {
        raw.number = value.as<bool>() ? 1.0 : 0.0;
      } else if (value.is<float>()) {
        raw.number = value.as<double>();
      } else {
        return false;
      }
      raw.hasNumber = true;
      raw.text = String(raw.number, 6);
      return true;
    }

    default:
      return false;
  }
}

bool mappingFromMatches(const ProfileMapping &mapping, const ProfileDp &dp,
                        const RawValue &raw) {
  if (!mapping.hasFrom) return false;
  if (raw.text == mapping.from) return true;
  if (dp.type == PDT_BOOL) {
    if (strcmp(mapping.from, "1") == 0) return raw.boolean;
    if (strcmp(mapping.from, "0") == 0) return !raw.boolean;
  }
  if ((dp.type == PDT_INT || dp.type == PDT_BITFIELD ||
       dp.type == PDT_FLOAT) &&
      raw.hasNumber) {
    char *end = nullptr;
    const double value = strtod(mapping.from, &end);
    if (end && *end == '\0' && fabs(value - raw.number) < 0.000001) {
      return true;
    }
  }
  return false;
}

const ProfileMapping *findMapping(const ProfileDef &profile,
                                  const ProfileDp &dp, const RawValue &raw,
                                  bool includeDefault, bool includeHidden) {
  const ProfileMapping *fallback = nullptr;
  for (uint8_t i = 0; i < dp.mappingCount; ++i) {
    const ProfileMapping &mapping =
        profile.mappings[dp.mappingStart + i];
    if (mapping.hidden && !includeHidden) continue;
    if (mappingFromMatches(mapping, dp, raw)) return &mapping;
    if (!mapping.hasFrom && includeDefault && !fallback) fallback = &mapping;
  }
  return fallback;
}

void setMappedValue(JsonObject &out, const char *name, uint8_t type,
                    const char *text) {
  switch (type) {
    case PVT_BOOL:
      out[name] = strcmp(text, "true") == 0;
      break;
    case PVT_INT:
      out[name] = strtol(text, nullptr, 10);
      break;
    case PVT_FLOAT:
      out[name] = strtod(text, nullptr);
      break;
    case PVT_STRING:
    default:
      out[name] = text;
      break;
  }
}

void setRawValue(JsonObject &out, const ProfileDp &dp, const RawValue &raw) {
  switch (dp.type) {
    case PDT_STRING:
      out[dp.name] = raw.text;
      break;
    case PDT_BOOL:
      out[dp.name] = raw.boolean;
      break;
    case PDT_INT:
    case PDT_BITFIELD:
      if (dp.scale != 1.0f && dp.scale > 0.0f) {
        out[dp.name] = raw.number / dp.scale;
      } else {
        out[dp.name] = static_cast<long>(raw.number);
      }
      break;
    case PDT_FLOAT:
      out[dp.name] = raw.number / (dp.scale > 0.0f ? dp.scale : 1.0f);
      break;
    default:
      break;
  }
}

uint8_t componentSlotType(const ProfileEntity &entity) {
  if (!settings.enableCommands) {
    switch (entity.type) {
      case PET_SWITCH:
      case PET_NUMBER:
      case PET_SELECT:
      case PET_TEXT:
        return GST_SENSOR;
      case PET_BUTTON:
        return GST_NONE;
      default:
        break;
    }
  }

  switch (entity.type) {
    case PET_BINARY_SENSOR: return GST_BINARY_SENSOR;
    case PET_SWITCH: return GST_SWITCH;
    case PET_NUMBER: return GST_NUMBER;
    case PET_SELECT: return GST_SELECT;
    case PET_BUTTON: return GST_BUTTON;
    case PET_TEXT: return GST_TEXT;
    case PET_SENSOR:
    default: return GST_SENSOR;
  }
}

const char *componentName(uint8_t slotType) {
  switch (slotType) {
    case GST_BINARY_SENSOR: return "binary_sensor";
    case GST_SWITCH: return "switch";
    case GST_NUMBER: return "number";
    case GST_SELECT: return "select";
    case GST_BUTTON: return "button";
    case GST_TEXT: return "text";
    case GST_SENSOR:
    default: return "sensor";
  }
}

void loadSlots(uint8_t *slots) {
  memset(slots, 0, PROFILE_MAX_ENTITIES);
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, true)) return;
  const size_t length = prefs.getBytesLength(NVS_SLOTS_KEY);
  if (length == PROFILE_MAX_ENTITIES) {
    prefs.getBytes(NVS_SLOTS_KEY, slots, length);
  }
  prefs.end();
}

void saveSlots(const uint8_t *slots) {
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, false)) return;
  prefs.putBytes(NVS_SLOTS_KEY, slots, PROFILE_MAX_ENTITIES);
  prefs.end();
}

String commandBase() {
  String base = mqttTopicCommand("charge");
  const int slash = base.lastIndexOf('/');
  if (slash > 0) base.remove(slash);
  return base;
}

void addEntityOptions(JsonDocument &doc, const ProfileDef &profile,
                      const ProfileEntity &entity, bool writable) {
  JsonArray options = doc["options"].to<JsonArray>();
  if (!writable) options.add("unknown");
  const ProfileDp &primary = entity.dps[0];
  for (uint8_t i = 0; i < primary.mappingCount; ++i) {
    const ProfileMapping &mapping =
        profile.mappings[primary.mappingStart + i];
    if (mapping.hidden) continue;
    if (writable && !mapping.hasFrom) continue;
    bool duplicate = false;
    for (JsonVariant existing : options) {
      if (strcmp(existing.as<const char *>(), mapping.to) == 0) {
        duplicate = true;
        break;
      }
    }
    if (!duplicate) options.add(mapping.to);
  }
}

String entityValueTemplate(const ProfileEntity &entity) {
  const char *primary = entity.dps[0].name;
  if (entity.type == PET_BINARY_SENSOR || entity.type == PET_SWITCH) {
    return String("{{ 'ON' if value_json[\"") + primary +
           "\"] else 'OFF' }}";
  }
  return String("{{ value_json[\"") + primary + "\"] }}";
}

void addEntityCategory(JsonDocument &doc, const ProfileEntity &entity) {
  if (entity.category[0]) doc["entity_category"] = entity.category;
  if (entity.hidden()) doc["enabled_by_default"] = false;
}

void addEntityIcon(JsonDocument &doc, const ProfileEntity &entity) {
  if (entity.icon[0]) doc["icon"] = entity.icon;
}

}  // namespace

bool genericProfileActive(const ProfileDef *profile) {
  return profile && profile->hasGenericEntities();
}

String genericStateTopic(const char *key) {
  return String(mqttTopicState()) + "/entities/" + key;
}

String genericCommandTopic(const char *key) {
  return commandBase() + "/generic/" + key;
}

String genericCommandWildcard() { return commandBase() + "/generic/+"; }

bool genericBuildEntityState(const ProfileDef &profile,
                             const ProfileEntity &entity, JsonObject dps,
                             JsonObject &out) {
  bool primary = false;
  for (uint8_t i = 0; i < entity.dpCount; ++i) {
    const ProfileDp &field = entity.dps[i];
    if (field.hidden()) continue;

    RawValue raw;
    if (!readRawValue(dps, field, raw)) continue;

    if (field.mappingCount > 0) {
      const ProfileMapping *mapping =
          findMapping(profile, field, raw, true, true);
      if (!mapping) continue;
      setMappedValue(out, field.name, mapping->toType, mapping->to);
    } else {
      setRawValue(out, field, raw);
    }
    if (i == 0) primary = true;
  }
  return primary;
}

void genericPublishState(const ProfileDef &profile, JsonObject dps) {
  if (!mqtt.connected() || !genericProfileActive(&profile)) return;

  for (size_t i = 0; i < profile.genericEntityCount; ++i) {
    const ProfileEntity &entity = profile.entities[i];
    if (entity.type == PET_BUTTON) continue;

    JsonDocument doc;
    JsonObject state = doc.to<JsonObject>();
    genericBuildEntityState(profile, entity, dps, state);
    String payload;
    payload.reserve(256);
    serializeJson(doc, payload);
    mqtt.publish(genericStateTopic(entity.key).c_str(), payload.c_str(), true);
  }
}

void genericPublishStateFromJson(const ProfileDef &profile, const char *json) {
  if (!json || !*json || !genericProfileActive(&profile) ||
      !mqtt.connected()) {
    return;
  }

  JsonDocument doc;
  if (deserializeJson(doc, json)) return;

  JsonObject dps;
  if (doc["dps"].is<JsonObject>()) {
    dps = doc["dps"].as<JsonObject>();
  } else if (doc["data"]["dps"].is<JsonObject>()) {
    dps = doc["data"]["dps"].as<JsonObject>();
  }
  if (!dps.isNull()) genericPublishState(profile, dps);
}

bool genericBuildCommand(const ProfileDef &profile, const String &key,
                         const String &payload, String &dpsJson,
                         String &error) {
  const ProfileEntity *found = nullptr;
  for (size_t i = 0; i < profile.genericEntityCount; ++i) {
    if (key == profile.entities[i].key) {
      found = &profile.entities[i];
      break;
    }
  }
  if (!found) {
    error = "unknown generic entity";
    return false;
  }
  if (found->type == PET_SENSOR || found->type == PET_BINARY_SENSOR) {
    error = "entity is read-only";
    return false;
  }

  const ProfileDp &primary = found->dps[0];
  String value;

  String command = payload;
  command.trim();
  String lower = command;
  lower.toLowerCase();

  switch (found->type) {
    case PET_SWITCH: {
      bool desired = false;
      if (lower == "on" || lower == "1" || lower == "true" ||
          lower == "start") {
        desired = true;
      } else if (lower == "off" || lower == "0" || lower == "false" ||
                 lower == "stop") {
        desired = false;
      } else {
        error = "use ON or OFF";
        return false;
      }

      if (primary.mappingCount > 0) {
        bool mapped = false;
        for (uint8_t i = 0; i < primary.mappingCount; ++i) {
          const ProfileMapping &mapping =
              profile.mappings[primary.mappingStart + i];
          if (mapping.hidden || !mapping.hasFrom ||
              mapping.toType != PVT_BOOL) {
            continue;
          }
          if ((strcmp(mapping.to, "true") == 0) == desired) {
            value = mapping.from;
            mapped = true;
            break;
          }
        }
        if (!mapped) {
          error = "switch mapping has no matching value";
          return false;
        }
      } else if (primary.type == PDT_BOOL) {
        value = desired ? "true" : "false";
      } else if (primary.type == PDT_INT || primary.type == PDT_BITFIELD) {
        value = desired ? "1" : "0";
      } else {
        error = "switch DP needs a mapping";
        return false;
      }
      break;
    }

    case PET_NUMBER: {
      if (command.length() == 0) {
        error = "value must be a number";
        return false;
      }
      char *end = nullptr;
      const double requested = strtod(command.c_str(), &end);
      if (!end || *end != '\0' || !isfinite(requested)) {
        error = "value must be a number";
        return false;
      }
      double display = requested;
      if (primary.hasRange) {
        if (display < primary.min) display = primary.min;
        if (display > primary.max) display = primary.max;
      }

      if (primary.mappingCount > 0) {
        bool mapped = false;
        for (uint8_t i = 0; i < primary.mappingCount; ++i) {
          const ProfileMapping &mapping =
              profile.mappings[primary.mappingStart + i];
          if (mapping.hidden || !mapping.hasFrom) continue;
          if (fabs(atof(mapping.to) - display) < 0.000001) {
            value = mapping.from;
            mapped = true;
            break;
          }
        }
        if (!mapped) {
          error = "number mapping has no matching value";
          return false;
        }
      } else {
        const double scale = primary.scale > 0.0f ? primary.scale : 1.0f;
        const double raw = display * scale;
        if (primary.type == PDT_INT || primary.type == PDT_BITFIELD) {
          value = String(static_cast<long>(lround(raw)));
        } else {
          value = String(raw, 6);
        }
      }
      break;
    }

    case PET_SELECT: {
      bool mapped = false;
      for (uint8_t i = 0; i < primary.mappingCount; ++i) {
        const ProfileMapping &mapping =
            profile.mappings[primary.mappingStart + i];
        if (mapping.hidden || !mapping.hasFrom) continue;
        if (strcmp(mapping.to, command.c_str()) == 0 ||
            strcasecmp(mapping.to, command.c_str()) == 0) {
          value = mapping.from;
          mapped = true;
          break;
        }
      }
      if (!mapped) {
        error = "unknown option";
        return false;
      }
      break;
    }

    case PET_BUTTON: {
      bool mapped = false;
      for (uint8_t i = 0; i < primary.mappingCount; ++i) {
        const ProfileMapping &mapping =
            profile.mappings[primary.mappingStart + i];
        if (mapping.hidden || !mapping.hasFrom ||
            mapping.toType != PVT_BOOL || strcmp(mapping.to, "true") != 0) {
          continue;
        }
        value = mapping.from;
        mapped = true;
        break;
      }
      if (!mapped) {
        if (primary.type == PDT_BOOL) {
          value = "true";
        } else {
          error = "button DP needs a press mapping";
          return false;
        }
      }
      break;
    }

    case PET_TEXT:
      if (primary.type != PDT_STRING) {
        error = "text entity requires a string DP";
        return false;
      }
      value = payload;
      break;

    default:
      error = "entity is read-only";
      return false;
  }

  switch (primary.type) {
    case PDT_STRING:
      dpsJson = String("{\"") + String(primary.dp) + "\":" +
                jsonQuote(value.c_str()) + "}";
      break;
    case PDT_BOOL:
      dpsJson = String("{\"") + String(primary.dp) + "\":" +
                (strcmp(value.c_str(), "true") == 0 ? "true" : "false") + "}";
      break;
    case PDT_INT:
    case PDT_BITFIELD: {
      char *end = nullptr;
      strtol(value.c_str(), &end, 10);
      if (!end || *end != '\0') {
        error = "mapped value is not an integer";
        return false;
      }
      dpsJson = String("{\"") + String(primary.dp) + "\":" + value + "}";
      break;
    }
    case PDT_FLOAT: {
      char *end = nullptr;
      strtod(value.c_str(), &end);
      if (!end || *end != '\0') {
        error = "mapped value is not numeric";
        return false;
      }
      dpsJson = String("{\"") + String(primary.dp) + "\":" + value + "}";
      break;
    }
    default:
      error = "unsupported DP type";
      return false;
  }

  return true;
}

void genericPublishDiscovery(const ProfileDef &profile) {
  if (!genericProfileActive(&profile) || !mqtt.connected()) return;

  uint8_t oldSlots[PROFILE_MAX_ENTITIES] = {0};
  uint8_t newSlots[PROFILE_MAX_ENTITIES] = {0};
  loadSlots(oldSlots);

  for (size_t i = 0; i < PROFILE_MAX_ENTITIES; ++i) {
    uint8_t slotType = GST_NONE;
    if (i < profile.genericEntityCount) {
      slotType = componentSlotType(profile.entities[i]);
    }
    newSlots[i] = slotType;

    if (oldSlots[i] != GST_NONE && oldSlots[i] != slotType) {
      char key[6];
      snprintf(key, sizeof(key), "e%u", static_cast<unsigned>(i));
      String objectId = String("profile_") + key;
      clearHaConfig(componentName(oldSlots[i]), objectId);
    }
  }

  for (size_t i = 0; i < profile.genericEntityCount; ++i) {
    const ProfileEntity &entity = profile.entities[i];
    const uint8_t slotType = newSlots[i];
    if (slotType == GST_NONE) continue;

    const char *component = componentName(slotType);
    const String objectId = String("profile_") + entity.key;

    JsonDocument doc;
    addHaCommon(doc);
    doc["name"] = entity.name;
    doc["object_id"] = haObjectId(objectId.c_str());
    doc["unique_id"] =
        String(settings.haDeviceId) + "_profile_" + component + "_" +
        entity.key;
    addEntityCategory(doc, entity);
    addEntityIcon(doc, entity);

    const bool hasState = slotType != GST_BUTTON;
    if (hasState) {
      doc["state_topic"] = genericStateTopic(entity.key);
      doc["value_template"] = entityValueTemplate(entity);
      if (entity.dpCount > 1) {
        doc["json_attributes_topic"] = genericStateTopic(entity.key);
        doc["json_attributes_template"] = "{{ value_json | tojson }}";
      }
    }

    if (slotType == GST_SENSOR) {
      if (entity.unit[0]) doc["unit_of_measurement"] = entity.unit;
      if ((entity.type == PET_SENSOR || entity.type == PET_NUMBER) &&
          entity.deviceClass[0]) {
        doc["device_class"] = entity.deviceClass;
      }
      if (entity.stateClass[0]) doc["state_class"] = entity.stateClass;
      if (entity.precision >= 0) {
        doc["suggested_display_precision"] = entity.precision;
      }
      if (entity.type == PET_SENSOR &&
          strcmp(entity.deviceClass, "enum") == 0) {
        addEntityOptions(doc, profile, entity, false);
      }
    } else if (slotType == GST_BINARY_SENSOR) {
      doc["payload_on"] = "ON";
      doc["payload_off"] = "OFF";
      if (entity.deviceClass[0]) doc["device_class"] = entity.deviceClass;
    } else if (slotType == GST_SWITCH) {
      doc["payload_on"] = "ON";
      doc["payload_off"] = "OFF";
      doc["command_topic"] = genericCommandTopic(entity.key);
      if (entity.deviceClass[0]) doc["device_class"] = entity.deviceClass;
    } else if (slotType == GST_NUMBER) {
      const ProfileDp &primary = entity.dps[0];
      doc["command_topic"] = genericCommandTopic(entity.key);
      doc["min"] = primary.min;
      doc["max"] = primary.max;
      doc["step"] = primary.step;
      doc["mode"] = "box";
      if (entity.unit[0]) doc["unit_of_measurement"] = entity.unit;
      if (entity.deviceClass[0]) doc["device_class"] = entity.deviceClass;
    } else if (slotType == GST_SELECT) {
      doc["command_topic"] = genericCommandTopic(entity.key);
      addEntityOptions(doc, profile, entity, true);
    } else if (slotType == GST_BUTTON) {
      doc["command_topic"] = genericCommandTopic(entity.key);
      doc["payload_press"] = "PRESS";
      if (entity.deviceClass[0]) doc["device_class"] = entity.deviceClass;
    } else if (slotType == GST_TEXT) {
      doc["command_topic"] = genericCommandTopic(entity.key);
      if (entity.dps[0].hasRange) {
        doc["min"] = static_cast<int>(entity.dps[0].min);
        doc["max"] = static_cast<int>(entity.dps[0].max);
      }
    }

    publishHaConfig(component, objectId, doc);
  }

  if (memcmp(oldSlots, newSlots, sizeof(newSlots)) != 0) {
    saveSlots(newSlots);
  }
}

void genericClearDiscovery() {
  uint8_t oldSlots[PROFILE_MAX_ENTITIES] = {0};
  loadSlots(oldSlots);
  bool changed = false;
  for (size_t i = 0; i < PROFILE_MAX_ENTITIES; ++i) {
    if (oldSlots[i] == GST_NONE) continue;
    changed = true;
    char key[6];
    snprintf(key, sizeof(key), "e%u", static_cast<unsigned>(i));
    clearHaConfig(componentName(oldSlots[i]),
                  String("profile_") + key);
  }
  if (changed) {
    uint8_t empty[PROFILE_MAX_ENTITIES] = {0};
    saveSlots(empty);
  }
}

void genericClearState() {
  if (!mqtt.connected()) return;
  for (size_t i = 0; i < PROFILE_MAX_ENTITIES; ++i) {
    char key[6];
    snprintf(key, sizeof(key), "e%u", static_cast<unsigned>(i));
    mqtt.publish(genericStateTopic(key).c_str(), "", true);
  }
}

}  // namespace tuya_lora
