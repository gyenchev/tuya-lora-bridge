#include "ha_discovery.h"

#include <PubSubClient.h>

#include "config.h"
#include "config_store.h"

extern PubSubClient mqtt;

namespace tuya_lora {

String haSlug(const char *text) {
  String out;
  if (!text) return out;
  bool lastUnderscore = false;
  for (const char *p = text; *p; ++p) {
    const char c = static_cast<char>(tolower(static_cast<unsigned char>(*p)));
    const bool alnum = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    if (alnum) {
      out += c;
      lastUnderscore = false;
    } else if (out.length() > 0 && !lastUnderscore) {
      out += '_';
      lastUnderscore = true;
    }
  }
  while (out.length() > 0 && out.charAt(out.length() - 1) == '_') {
    out.remove(out.length() - 1);
  }
  return out;
}

String mqttTopicPrefix() {
  String slug = haSlug(settings.haName);
  if (slug.length() == 0) slug = haSlug(settings.haDeviceId);
  if (slug.length() == 0) slug = "device";
  return String("tuya-lora/") + slug;
}

String mqttTopicLink() { return mqttTopicPrefix() + "/link"; }
String mqttTopicAvailability() { return mqttTopicPrefix() + "/availability"; }
String mqttTopicDps() { return mqttTopicPrefix() + "/dps"; }
String mqttTopicDebug() { return mqttTopicPrefix() + "/debug"; }
String mqttTopicCommand(const char *name) {
  return mqttTopicPrefix() + "/command/" + (name ? name : "");
}
String mqttTopicPackCommand() { return mqttTopicCommand("dp"); }

String haEntityPrefix() {
  String name = haSlug(settings.haName);
  if (name.length() == 0) name = "device";
  if (!name.startsWith("tuya_")) name = String("tuya_") + name;
  return name;
}

String haObjectId(const char *suffix) {
  return haEntityPrefix() + "_" + suffix;
}

String haDiscoveryTopic(const String &component, const String &objectId) {
  return String(settings.haDiscoveryPrefix) + "/" + component + "/" +
         settings.haDeviceId + "/" + objectId + "/config";
}

void addHaCommon(JsonDocument &doc) {
  doc["availability_topic"] = mqttTopicAvailability();
  doc["payload_available"] = "online";
  doc["payload_not_available"] = "offline";

  JsonObject device = doc["device"].to<JsonObject>();
  JsonArray ids = device["identifiers"].to<JsonArray>();
  ids.add(settings.haDeviceId);
  device["name"] = settings.haName;
  device["manufacturer"] = "";
  device["model"] = "Tuya device via Tuya LoRa Bridge";
  device["sw_version"] = FW_VERSION;

  JsonObject origin = doc["origin"].to<JsonObject>();
  origin["name"] = "tuya-lora-bridge";
  origin["sw_version"] = FW_VERSION;
}

bool publishHaConfig(const String &component, const String &objectId,
                     JsonDocument &doc) {
  doc["default_entity_id"] = component + "." + haObjectId(objectId.c_str());
  const String topic = haDiscoveryTopic(component, objectId);
  String payload;
  payload.reserve(768);
  serializeJson(doc, payload);

  const bool ok = mqtt.publish(topic.c_str(), payload.c_str(), true);
  if (!ok) {
    Serial.printf("[mqtt] discovery failed: %s\n", topic.c_str());
  }
  return ok;
}

void clearHaConfig(const String &component, const String &objectId) {
  const String topic = haDiscoveryTopic(component, objectId);
  mqtt.publish(topic.c_str(), "", true);
}

}  // namespace tuya_lora
