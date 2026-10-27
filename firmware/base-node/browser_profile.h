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
bool browserProfileValidateAndSave(const String &json, String &error);
void browserProfileClearDiscovery();
void browserProfilePublishDiscovery();
String browserProfileCommandTopic();

}  // namespace tuya_lora
