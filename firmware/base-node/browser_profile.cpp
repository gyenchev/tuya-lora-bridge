#include "browser_profile.h"

#include <LittleFS.h>
#include <PubSubClient.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "config_store.h"
#include "ha_discovery.h"

extern PubSubClient mqtt;

namespace tuya_lora {

namespace {

constexpr const char *META_PATH = "/profile.meta.json";
constexpr const char *META_TMP = "/profile.meta.tmp";
constexpr const char *LEGACY_PATH = "/profile.json";
constexpr size_t MAX_META_BYTES = 20000;
constexpr size_t MAX_ITEM_BYTES = 24000;
constexpr size_t MAX_TOTAL_BYTES = 250000;
constexpr size_t MAX_ITEMS = 256;

bool loaded = false;
char packId[24] = {0};
char packName[48] = {0};
char packType[24] = {0};
size_t packDiscoveryCount = 0;
uint32_t packGeneration = 0;

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

String itemPath(uint32_t generation, size_t index) {
  return String("/p") + generation + "_" + index + ".json";
}


struct Meta {
  char id[24] = {0};
  char name[48] = {0};
  char type[24] = {0};
  size_t count = 0;
  uint32_t generation = 0;
};

bool parseMeta(JsonObject root, Meta &meta, bool incoming, String &error) {
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
  if (!name[0] || strlen(name) >= sizeof(meta.name)) {
    error = "profile name must be 1-47 characters";
    return false;
  }
  if (strlen(type) >= sizeof(meta.type)) {
    error = "device_type is too long";
    return false;
  }
  const long count = root["discovery_count"] | 0;
  if (count < 1 || count > static_cast<long>(MAX_ITEMS)) {
    error = "discovery_count must be 1-256";
    return false;
  }
  const long gen = root["generation"] | 0;
  if (!incoming && (gen < 1 || gen > 0x7fffffffL)) {
    error = "profile generation is invalid";
    return false;
  }
  copyText(meta.id, sizeof(meta.id), id);
  copyText(meta.name, sizeof(meta.name), name);
  copyText(meta.type, sizeof(meta.type), type);
  meta.count = static_cast<size_t>(count);
  meta.generation = incoming ? 0 : static_cast<uint32_t>(gen);
  return true;
}

void applyMeta(const Meta &meta) {
  copyText(packId, sizeof(packId), meta.id);
  copyText(packName, sizeof(packName), meta.name);
  copyText(packType, sizeof(packType), meta.type);
  packDiscoveryCount = meta.count;
  packGeneration = meta.generation;
}

bool readMetaFile(String &json) {
  File file = LittleFS.open(META_PATH, "r");
  if (!file) return false;
  const size_t size = file.size();
  if (size == 0 || size > MAX_META_BYTES) {
    file.close();
    return false;
  }
  json.reserve(size + 1);
  while (file.available()) json += static_cast<char>(file.read());
  file.close();
  return json.length() == size;
}

bool loadMetaFromFile() {
  loaded = false;
  String json;
  if (!readMetaFile(json)) return false;
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, json);
  if (err || !doc.is<JsonObject>()) return false;
  Meta meta;
  String error;
  if (!parseMeta(doc.as<JsonObject>(), meta, false, error)) {
    Serial.printf("[pack] invalid metadata: %s\n", error.c_str());
    return false;
  }
  if (!LittleFS.exists(itemPath(meta.generation, 0))) {
    Serial.println("[pack] metadata references missing discovery files");
    return false;
  }
  applyMeta(meta);
  loaded = true;
  Serial.printf("[pack] loaded '%s' (%u entries, generation %u)\n", packId,
                static_cast<unsigned>(packDiscoveryCount),
                static_cast<unsigned>(packGeneration));
  return true;
}

bool readItemFile(size_t index, String &json) {
  File file = LittleFS.open(itemPath(packGeneration, index), "r");
  if (!file) return false;
  const size_t size = file.size();
  if (size == 0 || size > MAX_ITEM_BYTES) {
    file.close();
    return false;
  }
  json.reserve(size + 1);
  while (file.available()) json += static_cast<char>(file.read());
  file.close();
  return json.length() == size;
}


String prefixedObjectId(const char *objectId) {
  return String("pack_") + objectId;
}

void removeOldPack() {
  browserProfileClearDiscovery();
  if (loaded) {
    for (size_t i = 0; i < packDiscoveryCount; ++i) {
      LittleFS.remove(itemPath(packGeneration, i));
    }
  }
  LittleFS.remove(META_PATH);
  LittleFS.remove(META_TMP);
  LittleFS.remove(LEGACY_PATH);
  loaded = false;
}

bool streamActive = false;
bool streamMetaParsed = false;
bool streamFailed = false;
String streamLine;
String streamMetaRaw;
String streamError;
Meta streamMeta;
uint32_t streamGeneration = 0;
size_t streamItemIndex = 0;
size_t streamTotal = 0;

bool processStreamLine(const String &line, bool &metaParsed, size_t &itemIndex,
                       Meta &meta, uint32_t &generation, String &error) {
  if (line.length() == 0) return true;
  if (!metaParsed) {
    if (line.length() > MAX_META_BYTES) {
      error = "profile metadata line is too long";
      return false;
    }
    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, line);
    if (err || !doc.is<JsonObject>()) {
      error = "invalid profile metadata JSON";
      return false;
    }
    Meta incoming;
    if (!parseMeta(doc.as<JsonObject>(), incoming, true, error)) return false;
    generation = packGeneration + 1;
    removeOldPack();
    incoming.generation = generation;
    meta = incoming;
    streamMetaRaw = line;
    metaParsed = true;
    itemIndex = 0;
    Serial.printf("[pack] upload '%s' count=%u generation=%u\n", meta.id,
                  static_cast<unsigned>(meta.count),
                  static_cast<unsigned>(meta.generation));
    return true;
  }
  if (itemIndex >= meta.count) return true;
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, line);
  if (err || !doc.is<JsonObject>()) {
    error = "invalid discovery JSON";
    return false;
  }
  JsonObject entry = doc.as<JsonObject>();
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
  const String path = itemPath(generation, itemIndex);
  File file = LittleFS.open(path, "w");
  if (!file) {
    error = "could not write discovery file";
    return false;
  }
  const size_t written = file.print(line);
  file.close();
  if (written != line.length()) {
    error = "short write while saving discovery file";
    return false;
  }
  itemIndex += 1;
  if (itemIndex % 10 == 0) {
    Serial.printf("[pack] saved %u/%u discovery entries\n",
                  static_cast<unsigned>(itemIndex),
                  static_cast<unsigned>(meta.count));
  }
  return true;
}

bool commitStreamMeta(const Meta &meta, String &error) {
  JsonDocument out;
  bool preserved = false;
  if (streamMetaRaw.length() > 0 && !deserializeJson(out, streamMetaRaw) &&
      out.is<JsonObject>()) {
    out["generation"] = meta.generation;
    out["discovery_count"] = meta.count;
    preserved = true;
  }
  if (!preserved) {
    out["format"] = 1;
    out["id"] = meta.id;
    out["name"] = meta.name;
    out["device_type"] = meta.type;
    out["discovery_count"] = meta.count;
    out["generation"] = meta.generation;
  }

  String json;
  serializeJson(out, json);
  File file = LittleFS.open(META_TMP, "w");
  if (!file) {
    error = "could not write profile metadata";
    return false;
  }
  const size_t written = file.print(json);
  file.close();
  if (written != json.length() || !LittleFS.rename(META_TMP, META_PATH)) {
    error = "could not commit profile metadata";
    return false;
  }
  return true;
}

void cleanupOrphanFiles() {
  const String keep = loaded ? String("/p") + packGeneration + "_" : String();
  File root = LittleFS.open("/");
  if (!root) return;
  File file = root.openNextFile();
  while (file) {
    String name = file.name();
    if (!name.startsWith("/")) name = String("/") + name;
    const bool currentItem = loaded && name.startsWith(keep) && name.endsWith(".json");
    const bool meta = name == META_PATH;
    if (!currentItem && !meta &&
        (name.startsWith("/p") || name == LEGACY_PATH || name == META_TMP)) {
      LittleFS.remove(name);
    }
    file = root.openNextFile();
  }
  root.close();
}

void publishRawSensor() {
  JsonDocument doc;
  doc["name"] = "Raw DPs";
  doc["state_topic"] = mqttTopicDps();
  doc["value_template"] = "{{ value_json.dps | count }}";
  doc["json_attributes_topic"] = mqttTopicDps();
  doc["json_attributes_template"] = "{{ {\"dps\": value_json.dps} | tojson }}";
  doc["entity_category"] = "diagnostic";
  doc["icon"] = "mdi:code-json";
  addHaCommon(doc);
  doc["object_id"] = haObjectId("pack_raw");
  doc["unique_id"] = haObjectId("pack_raw");
  publishHaConfig("sensor", "pack_raw", doc);
}

}  // namespace

bool browserProfileInit() {
  if (!LittleFS.begin(true)) {
    Serial.println("[pack] LittleFS mount failed");
    loaded = false;
    return false;
  }
  const bool ok = loadMetaFromFile();
  cleanupOrphanFiles();
  return ok;
}

bool browserProfileLoaded() { return loaded; }

const char *browserProfileId() { return loaded ? packId : ""; }

const char *browserProfileName() { return loaded ? packName : ""; }

const char *browserProfileDeviceType() { return loaded ? packType : ""; }

size_t browserProfileDiscoveryCount() {
  return loaded ? packDiscoveryCount : 0;
}

bool browserProfileSelected() { return loaded; }

void browserProfileStreamBegin() {
  streamActive = true;
  streamMetaParsed = false;
  streamFailed = false;
  streamLine = "";
  streamMetaRaw = "";
  streamError = "";
  streamItemIndex = 0;
  streamTotal = 0;
  streamGeneration = packGeneration + 1;
  memset(&streamMeta, 0, sizeof(streamMeta));
}

bool browserProfileStreamFeed(const uint8_t *data, size_t len, String &error) {
  if (!streamActive || streamFailed) {
    error = streamError.length() ? streamError : "profile upload is not active";
    return false;
  }
  streamTotal += len;
  if (streamTotal > MAX_TOTAL_BYTES) {
    streamFailed = true;
    streamError = "profile upload is too large";
    error = streamError;
    return false;
  }
  for (size_t i = 0; i < len; ++i) {
    const char c = static_cast<char>(data[i]);
    if (c == '\r') continue;
    if (c == '\n') {
      if (!processStreamLine(streamLine, streamMetaParsed, streamItemIndex,
                             streamMeta, streamGeneration, streamError)) {
        streamFailed = true;
        error = streamError;
        return false;
      }
      streamLine = "";
      continue;
    }
    if (streamLine.length() >= MAX_ITEM_BYTES) {
      streamFailed = true;
      streamError = "profile line is too long";
      error = streamError;
      return false;
    }
    streamLine += c;
  }
  return true;
}

bool browserProfileStreamFinish(String &error) {
  if (!streamActive || streamFailed) {
    error = streamError.length() ? streamError : "profile upload is not active";
    return false;
  }
  if (streamLine.length() > 0) {
    if (!processStreamLine(streamLine, streamMetaParsed, streamItemIndex,
                           streamMeta, streamGeneration, streamError)) {
      streamFailed = true;
      error = streamError;
      return false;
    }
    streamLine = "";
  }
  if (!streamMetaParsed) {
    error = "profile metadata line is missing";
    return false;
  }
  if (streamItemIndex != streamMeta.count) {
    error = "profile discovery count does not match metadata";
    return false;
  }
  if (!commitStreamMeta(streamMeta, error)) {
    return false;
  }
  applyMeta(streamMeta);
  loaded = true;
  streamActive = false;
  Serial.printf("[pack] upload complete: '%s' (%u entries, generation %u)\n",
                packId, static_cast<unsigned>(packDiscoveryCount),
                static_cast<unsigned>(packGeneration));
  return true;
}

void browserProfileStreamAbort() {
  streamActive = false;
  streamFailed = true;
  streamError = "profile upload aborted";
}

bool browserProfileMetaJson(String &json) {
  return loaded && readMetaFile(json);
}

bool browserProfileItemJson(size_t index, String &json) {
  return loaded && index < packDiscoveryCount && readItemFile(index, json);
}

String browserProfileTelemetryJson() {
  String json;
  if (!readMetaFile(json)) return "[]";
  JsonDocument doc;
  if (deserializeJson(doc, json) || !doc.is<JsonObject>()) return "[]";
  JsonArray telemetry = doc["telemetry"].as<JsonArray>();
  if (telemetry.isNull()) return "[]";
  String out;
  serializeJson(telemetry, out);
  return out.length() ? out : "[]";
}

void browserProfileClearDiscovery() {
  if (!loaded) return;
  clearHaConfig("sensor", "pack_raw");
  for (size_t i = 0; i < packDiscoveryCount; ++i) {
    String json;
    if (!readItemFile(i, json)) continue;
    JsonDocument doc;
    if (deserializeJson(doc, json)) continue;
    JsonObject entry = doc.as<JsonObject>();
    const char *component = entry["component"] | "";
    const char *objectId = entry["object_id"] | "";
    if (component[0] && objectId[0]) {
      clearHaConfig(component, prefixedObjectId(objectId));
    }
  }
}

void browserProfilePublishDiscovery() {
  if (!loaded || !mqtt.connected()) return;
  for (size_t i = 0; i < packDiscoveryCount; ++i) {
    String json;
    if (!readItemFile(i, json)) continue;
    JsonDocument itemDoc;
    if (deserializeJson(itemDoc, json)) continue;
    JsonObject entry = itemDoc.as<JsonObject>();
    const char *component = entry["component"] | "";
    const char *objectId = entry["object_id"] | "";
    JsonObject config = entry["config"].as<JsonObject>();
    if (!component[0] || !objectId[0] || config.isNull()) continue;

    String configJson;
    serializeJson(config, configJson);
    configJson.replace("__STATE_TOPIC__", mqttTopicDps());
    configJson.replace("__CMD_TOPIC__", mqttTopicPackCommand());
    configJson.replace("__RAW_ENTITY__", browserProfileRawEntityId());
    configJson.replace("tuya-lora/charger", mqttTopicPrefix());
    JsonDocument doc;
    if (deserializeJson(doc, configJson)) continue;
    addHaCommon(doc);
    const String prefixed = prefixedObjectId(objectId);
    const String unique = haObjectId(prefixed.c_str()) + "_" + component;
    doc["object_id"] = haObjectId(prefixed.c_str());
    doc["unique_id"] = unique;
    publishHaConfig(component, prefixed, doc);
  }
  publishRawSensor();
}

String browserProfileCommandTopic() { return mqttTopicPackCommand(); }

String browserProfileRawEntityId() {
  return String("sensor.") + haObjectId("pack_raw");
}

}  // namespace tuya_lora
