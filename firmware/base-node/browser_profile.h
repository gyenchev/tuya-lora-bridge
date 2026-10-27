#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

namespace tuya_lora {

bool browserProfileInit();
bool browserProfileLoaded();
const char *browserProfileId();
const char *browserProfileName();
const char *browserProfileDeviceType();
size_t browserProfileDiscoveryCount();
bool browserProfileSelected();
bool browserProfileMatches(JsonObject dps);
void browserProfileStreamBegin();
bool browserProfileStreamFeed(const uint8_t *data, size_t len, String &error);
bool browserProfileStreamFinish(String &error);
void browserProfileStreamAbort();
void browserProfileClearDiscovery();
void browserProfilePublishDiscovery();
String browserProfileCommandTopic();
String browserProfileRawEntityId();
String browserProfileTelemetryJson();

}  // namespace tuya_lora
