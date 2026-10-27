#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "profiles.h"

namespace tuya_lora {

// Generic descriptor-driven DP -> Home Assistant entity mapping.
bool genericBuildEntityState(const ProfileDef &profile,
                             const ProfileEntity &entity, JsonObject dps,
                             JsonObject &out);
bool genericBuildCommand(const ProfileDef &profile, const String &key,
                         const String &payload, String &dpsJson,
                         String &error);

void genericPublishState(const ProfileDef &profile, JsonObject dps);
void genericPublishStateFromJson(const ProfileDef &profile, const char *json);
void genericPublishDiscovery(const ProfileDef &profile);
void genericClearDiscovery();
void genericClearState();

bool genericProfileActive(const ProfileDef *profile);
String genericStateTopic(const char *key);
String genericCommandTopic(const char *key);
String genericCommandWildcard();

}  // namespace tuya_lora
