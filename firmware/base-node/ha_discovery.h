#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

namespace tuya_lora {

String haSlug(const char *text);
String mqttTopicPrefix();
String mqttTopicLink();
String mqttTopicAvailability();
String mqttTopicDps();
String mqttTopicDebug();
String mqttTopicCommand(const char *name);
String mqttTopicPackCommand();
String haEntityPrefix();
String haObjectId(const char *suffix);
String haDiscoveryTopic(const String &component, const String &objectId);
void addHaCommon(JsonDocument &doc);
bool publishHaConfig(const String &component, const String &objectId,
                     JsonDocument &doc);
void clearHaConfig(const String &component, const String &objectId);

}  // namespace tuya_lora
