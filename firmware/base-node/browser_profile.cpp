#include "browser_profile.h"

#include <LittleFS.h>
#include <PubSubClient.h>
#include <ctype.h>
#include <functional>
#include <stdio.h>
#include <string.h>

#include "config_store.h"
#include "ha_discovery.h"
#include "profiles.h"

extern PubSubClient mqtt;

namespace tuya_lora {

namespace {

constexpr const char *PACK_PATH = "/profile.json";
constexpr size_t PACK_MAX_JSON = 49152;
constexpr size_t PACK_MAX_DISCOVERY = 128;

bool loaded = false;
char packId[24] = {0};
char packName[48] = {0};
char packType[24] = {0};
uint16_t packRequire[8] = {0};
uint8_t packRequireCount = 0;
uint16_t packExclude[8] = {0};
uint8_t packExcludeCount = 0;
uint8_t packPriority = 5;
size_t packDiscoveryCount = 0;

void copyText(char *dst, size_t dstSize, const char *src) {
  if (!dst || dstSize == 0) return;
  if (!src) {
    dst[0] = '\0';
    return;
  }
  strncpy(dst, src, dstSize - 1);
  dst[dstSize - 1] = '\0';
}

bool safeId(const char *text, size_t maxLen) {
  if (!text || !text[0]) return false;
  const size_t len = strlen(text);
  if (len > maxLen) return false;
  for (size_t i = 0; i < len; ++i) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    if (!isalnum(c) && c != '_' && c != '-') return false;
  }
  return true;
}

bool safeComponent(const char *text) {
  if (!text || !text[0]) return false;
  const size_t len = strlen(text);
  if (len > 31) return false;
  for (size_t i = 0; i < len; ++i) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    if (!isalnum(c) && c != '_') return false;
  }
  return true;
}

bool readPackJson(String &json) {
  File file = LittleFS.open(PACK_PATH, "r");
  if (!file) return false;
  const size_t size = file.size();
  if (size == 0 || size > PACK_MAX_JSON) {
    file.close();
    return false;
  }
  json.reserve(size + 1);
  while (file.available()) {
    json += static_cast<char>(file.read());
  }
  file.close();
  return json.length() == size;
}

bool parseDetect(JsonVariant value, uint16_t *require, uint8_t &requireCount,
                 uint16_t *exclude, uint8_t &excludeCount, uint8_t &priority,
                 String &error) {
  requireCount = 0;
  excludeCount = 0;
  priority = 5;
  if (value.isNull()) return true;
  if (!value.is<JsonObject>()) {
    error = "detect must be an object";
    return false;
  }
  JsonObject detect = value.as<JsonObject>();

  const long pri = detect["priority"] | 5;
  if (pri < 0 || pri > 255) {
    error = "detect.priority must be 0-255";
    return false;
  }
  priority = static_cast<uint8_t>(pri);

  JsonArray req = detect["require"].as<JsonArray>();
  if (!req.isNull()) {
    for (JsonVariant item : req) {
      if (requireCount >= 8) {
        error = "detect.require supports at most 8 DPs";
        return false;
      }
      const long dp = item.as<long>();
      if (dp < 1 || dp > 999) {
        error = "detect.require contains an invalid DP";
        return false;
      }
      require[requireCount++] = static_cast<uint16_t>(dp);
    }
  }

  JsonArray ex = detect["exclude"].as<JsonArray>();
  if (!ex.isNull()) {
    for (JsonVariant item : ex) {
      if (excludeCount >= 8) {
        error = "detect.exclude supports at most 8 DPs";
        return false;
      }
      const long dp = item.as<long>();
      if (dp < 1 || dp > 999) {
        error = "detect.exclude contains an invalid DP";
        return false;
      }
      exclude[excludeCount++] = static_cast<uint16_t>(dp);
    }
  }
  return true;
}

bool applyMetadata(const String &json, String &error) {
  if (json.length() == 0 || json.length() > PACK_MAX_JSON) {
    error = "profile pack is empty or too large";
    return false;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, json);
  if (err) {
    error = String("invalid JSON: ") + err.c_str();
    return false;
  }
  if (!doc.is<JsonObject>()) {
    error = "profile pack must be an object";
    return false;
  }
  JsonObject root = doc.as<JsonObject>();

  if (!root["format"].is<int>() || root["format"].as<int>() != 1) {
    error = "unsupported profile pack format";
    return false;
  }

  const char *id = root["id"] | "";
  const char *name = root["name"] | "";
  const char *type = root["device_type"] | "";
  if (!safeId(id, 23)) {
    error = "profile id must be 1-23 safe characters";
    return false;
  }
  if (strcmp(id, "auto") == 0 || strcmp(id, "fallback") == 0 ||
      strcmp(id, "unknown") == 0) {
    error = "profile id is reserved";
    return false;
  }
  if (!name[0] || strlen(name) >= sizeof(packName)) {
    error = "profile name must be 1-47 characters";
    return false;
  }
  if (strlen(type) >= sizeof(packType)) {
    error = "device_type is too long";
    return false;
  }
  if (profileFind(id)) {
    error = "profile id is already used by a built-in or custom profile";
    return false;
  }

  JsonArray discovery = root["discovery"].as<JsonArray>();
  if (discovery.isNull() || discovery.size() == 0 ||
      discovery.size() > PACK_MAX_DISCOVERY) {
    error = "discovery must contain 1-128 entries";
    return false;
  }
  for (JsonVariant item : discovery) {
    if (!item.is<JsonObject>()) {
      error = "each discovery entry must be an object";
      return false;
    }
    JsonObject entry = item.as<JsonObject>();
    const char *component = entry["component"] | "";
    const char *objectId = entry["object_id"] | "";
    if (!safeComponent(component)) {
      error = "discovery component is invalid";
      return false;
    }
    if (!safeId(objectId, 31)) {
      error = "discovery object_id is invalid";
      return false;
    }
    if (!entry["config"].is<JsonObject>()) {
      error = "discovery config must be an object";
      return false;
    }
  }

  uint16_t require[8] = {0};
  uint16_t exclude[8] = {0};
  uint8_t requireCount = 0;
  uint8_t excludeCount = 0;
  uint8_t priority = 5;
  if (!parseDetect(root["detect"], require, requireCount, exclude, excludeCount,
                   priority, error)) {
    return false;
  }

  copyText(packId, sizeof(packId), id);
  copyText(packName, sizeof(packName), name);
  copyText(packType, sizeof(packType), type);
  memcpy(packRequire, require, sizeof(packRequire));
  memcpy(packExclude, exclude, sizeof(packExclude));
  packRequireCount = requireCount;
  packExcludeCount = excludeCount;
  packPriority = priority;
  packDiscoveryCount = discovery.size();
  return true;
}

bool loadMetadataFromFile() {
  loaded = false;
  String json;
  if (!readPackJson(json)) return false;
  String error;
  if (!applyMetadata(json, error)) {
    Serial.printf("[pack] invalid %s: %s\n", PACK_PATH, error.c_str());
    return false;
  }
  loaded = true;
  Serial.printf("[pack] loaded '%s' (%u discovery entries, %u bytes)\n", packId,
                static_cast<unsigned>(packDiscoveryCount),
                static_cast<unsigned>(json.length()));
  return true;
}

bool keyPresent(JsonObject dps, uint16_t dp) {
  char key[8];
  snprintf(key, sizeof(key), "%u", static_cast<unsigned>(dp));
  return !dps[key].isNull();
}

bool packMatchesDps(JsonObject dps) {
  if (packRequireCount == 0) return true;
  for (uint8_t i = 0; i < packRequireCount; ++i) {
    if (!keyPresent(dps, packRequire[i])) return false;
  }
  for (uint8_t i = 0; i < packExcludeCount; ++i) {
    if (keyPresent(dps, packExclude[i])) return false;
  }
  return true;
}

String prefixedObjectId(const char *objectId) {
  return String("pack_") + objectId;
}

bool forEachDiscovery(
    const String &json,
    const std::function<bool(const char *, const char *, JsonObject)> &callback,
    String &error) {
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, json);
  if (err || !doc.is<JsonObject>()) {
    error = "profile pack is invalid";
    return false;
  }
  JsonArray discovery = doc["discovery"].as<JsonArray>();
  if (discovery.isNull()) {
    error = "profile pack has no discovery entries";
    return false;
  }
  for (JsonVariant item : discovery) {
    JsonObject entry = item.as<JsonObject>();
    const char *component = entry["component"] | "";
    const char *objectId = entry["object_id"] | "";
    JsonObject config = entry["config"].as<JsonObject>();
    if (!callback(component, objectId, config)) return false;
  }
  return true;
}

}  // namespace

bool browserProfileInit() {
  if (!LittleFS.begin(true)) {
    Serial.println("[pack] LittleFS mount failed");
    loaded = false;
    return false;
  }
  return loadMetadataFromFile();
}

bool browserProfileLoaded() { return loaded; }

const char *browserProfileId() { return loaded ? packId : ""; }

const char *browserProfileName() { return loaded ? packName : ""; }

const char *browserProfileDeviceType() { return loaded ? packType : ""; }

size_t browserProfileDiscoveryCount() {
  return loaded ? packDiscoveryCount : 0;
}

bool browserProfileSelected() {
  return loaded && settings.profileId[0] &&
         strcmp(settings.profileId, packId) == 0;
}

bool browserProfileMatches(JsonObject dps) {
  return loaded && packMatchesDps(dps);
}

bool browserProfileValidateAndSave(const String &json, String &error) {
  if (!applyMetadata(json, error)) return false;

  // Clear the currently published pack before replacing it.
  browserProfileClearDiscovery();

  File file = LittleFS.open(PACK_PATH, "w");
  if (!file) {
    error = "could not open profile storage";
    return false;
  }
  const size_t written = file.print(json);
  file.close();
  if (written != json.length()) {
    LittleFS.remove(PACK_PATH);
    error = "could not write profile pack";
    return false;
  }

  if (!loadMetadataFromFile()) {
    LittleFS.remove(PACK_PATH);
    error = "saved profile pack failed validation";
    return false;
  }

  copyText(settings.profileId, sizeof(settings.profileId), packId);
  if (!saveSettings()) {
    error = "could not save profile selection";
    return false;
  }
  return true;
}

void browserProfileClearDiscovery() {
  if (!loaded) return;
  String json;
  if (!readPackJson(json)) return;
  String error;
  forEachDiscovery(
      json,
      [](const char *component, const char *objectId, JsonObject) {
        clearHaConfig(component, prefixedObjectId(objectId));
        return true;
      },
      error);
}

void browserProfilePublishDiscovery() {
  if (!loaded || !mqtt.connected()) return;
  String json;
  if (!readPackJson(json)) {
    Serial.println("[pack] could not read profile pack for discovery");
    return;
  }

  String error;
  const bool ok = forEachDiscovery(
      json,
      [](const char *component, const char *objectId, JsonObject config) {
        JsonDocument doc;
        for (JsonPair kv : config) {
          doc[kv.key()] = kv.value();
        }
        addHaCommon(doc);
        const String prefixed = prefixedObjectId(objectId);
        const String unique = String(settings.haDeviceId) + "_" + prefixed +
                              "_" + component;
        doc["object_id"] = haObjectId(prefixed.c_str());
        doc["unique_id"] = unique;
        return publishHaConfig(component, prefixed, doc);
      },
      error);
  if (!ok) Serial.printf("[pack] discovery failed: %s\n", error.c_str());
}

String browserProfileCommandTopic() {
  String base = settings.mqttTopicCmdCharge;
  const int slash = base.lastIndexOf('/');
  if (slash > 0) base.remove(slash);
  return base + "/dp";
}

}  // namespace tuya_lora
