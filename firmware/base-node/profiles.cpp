#include "profiles.h"

#include <Preferences.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "config.h"
#include "browser_profile.h"

namespace tuya_lora {

namespace {

constexpr const char *NVS_NAMESPACE = "tuya-lora-base";
constexpr const char *NVS_PROFILES_KEY = "profiles_json";

ProfileDef builtinProfiles[3];
ProfileDef fallbackProfile;
ProfileDef *customProfiles = nullptr;
ProfileDef parseScratch;
constexpr size_t CUSTOM_PROFILES_BYTES =
    sizeof(ProfileDef) * PROFILE_MAX_CUSTOM;
size_t customCount = 0;
char customJson[PROFILE_JSON_MAX + 1] = {0};
bool initialized = false;

void copyText(char *dst, size_t dstSize, const char *src) {
  if (!dst || dstSize == 0) return;
  if (!src) {
    dst[0] = '\0';
    return;
  }
  strncpy(dst, src, dstSize - 1);
  dst[dstSize - 1] = '\0';
}

void setField(ProfileField &field, uint16_t dp, uint8_t type = PF_INT,
              float scale = 1.0f, const char *match = nullptr) {
  field.dp = dp;
  field.type = dp ? type : PF_NONE;
  field.scale = scale;
  field.match[0] = '\0';
  if (match) copyText(field.match, sizeof(field.match), match);
}

void setDetect(ProfileDetect &detect, uint8_t priority,
               const uint16_t *require, uint8_t requireCount,
               const uint16_t *exclude = nullptr, uint8_t excludeCount = 0) {
  detect.priority = priority;
  detect.requireCount = requireCount > 8 ? 8 : requireCount;
  detect.excludeCount = excludeCount > 8 ? 8 : excludeCount;
  for (uint8_t i = 0; i < detect.requireCount; ++i) {
    detect.require[i] = require[i];
  }
  for (uint8_t i = 0; i < detect.excludeCount; ++i) {
    detect.exclude[i] = exclude[i];
  }
}

void setControl(ProfileControl &control, uint16_t chargeDp, uint8_t chargeType,
                const char *chargeOn, const char *chargeOff,
                uint16_t currentDp, uint16_t delayDp, bool delaySetsMode,
                uint16_t modeDp, const char *modeNow,
                const char *modeSchedule) {
  control.chargeDp = chargeDp;
  control.chargeType = chargeDp ? chargeType : PCT_NONE;
  copyText(control.chargeOn, sizeof(control.chargeOn), chargeOn);
  copyText(control.chargeOff, sizeof(control.chargeOff), chargeOff);
  control.currentDp = currentDp;
  control.delayDp = delayDp;
  control.delaySetsMode = delaySetsMode;
  control.modeDp = modeDp;
  copyText(control.modeNow, sizeof(control.modeNow), modeNow);
  copyText(control.modeSchedule, sizeof(control.modeSchedule), modeSchedule);
}

const ProfileDef *findBuiltin(const char *id) {
  if (!id || !*id) return nullptr;
  for (auto &profile : builtinProfiles) {
    if (strcmp(profile.id, id) == 0) return &profile;
  }
  return nullptr;
}

void clearCustom() {
  customCount = 0;
  customJson[0] = '\0';
  if (customProfiles) {
    memset(customProfiles, 0, CUSTOM_PROFILES_BYTES);
  }
}

void initBuiltins() {
  memset(builtinProfiles, 0, sizeof(builtinProfiles));
  memset(&fallbackProfile, 0, sizeof(fallbackProfile));

  // Feyree / Goodcell three-phase profile.
  {
    ProfileDef &p = builtinProfiles[0];
    copyText(p.id, sizeof(p.id), "3-phase");
    copyText(p.name, sizeof(p.name), "Feyree 3-phase");
    copyText(p.description, sizeof(p.description),
             "Three-phase local TCP profile with L1/L2/L3 telemetry");
    p.phases = 3;
    const uint16_t require[] = {103, 104, 105, 106, 107};
    setDetect(p.detect, 30, require, 5);

    setField(p.state.status, 3, PF_TEXT);
    setField(p.state.statusFallback, 101, PF_TEXT);
    setField(p.state.statusAuth, 101, PF_TEXT);
    setField(p.state.fault, 10);
    setField(p.state.voltageL1, 102, PF_INT, 0.1f);
    setField(p.state.voltageL2, 103, PF_INT, 0.1f);
    setField(p.state.voltageL3, 104, PF_INT, 0.1f);
    setField(p.state.currentL1, 105, PF_INT, 0.1f);
    setField(p.state.currentL2, 106, PF_INT, 0.1f);
    setField(p.state.currentL3, 107, PF_INT, 0.1f);
    setField(p.state.power, 109, PF_INT, 100.0f);
    setField(p.state.energy, 112, PF_INT, 100.0f);
    setField(p.state.sessionEnergy, 25, PF_INT, 1000.0f);
    setField(p.state.temperature, 110, PF_INT, 0.1f);
    setField(p.state.currentLimit, 115);
    setField(p.state.currentLimitAlt, 114);
    setField(p.state.chargeSeconds, 120, PF_DURATION_HMS);
    setField(p.state.chargeEnable, 124, PF_TEXT, 1.0f,
             "opencharging,charge,on");
    setField(p.state.rfid, 123, PF_BOOL);
    setField(p.state.schedule, 14, PF_TEXT_CONTAINS, 1.0f, "schedule");
    setField(p.state.delay, 118);

    setControl(p.control, DP_THREE_CHARGE_ID, PCT_STRING, "OpenCharging",
               "CloseCharging", DP_THREE_CURRENT_ID, DP_THREE_DELAY_ID, false,
               DP_THREE_MODE_ID, "charge_now", "charge_schedule");
  }

  // Feyree single-phase profile.
  {
    ProfileDef &p = builtinProfiles[1];
    copyText(p.id, sizeof(p.id), "single-phase");
    copyText(p.name, sizeof(p.name), "Feyree single-phase");
    copyText(p.description, sizeof(p.description),
             "Single-phase local TCP profile with auto-scaled voltage");
    p.phases = 1;
    const uint16_t require[] = {106, 107, 108};
    setDetect(p.detect, 20, require, 3);

    setField(p.state.status, 101, PF_TEXT);
    setField(p.state.statusFallback, 3, PF_TEXT);
    setField(p.state.statusAuth, 101, PF_TEXT);
    setField(p.state.fault, 10);
    setField(p.state.voltageL1, 107, PF_VOLTAGE_AUTO);
    setField(p.state.currentL1, 108, PF_INT, 0.1f);
    setField(p.state.power, 109, PF_INT, 100.0f);
    setField(p.state.energy, 106, PF_INT, 100.0f);
    setField(p.state.sessionEnergy, 25, PF_INT, 1000.0f);
    setField(p.state.temperature, 110, PF_INT, 0.1f);
    setField(p.state.currentLimit, 102);
    setField(p.state.chargeSeconds, 114, PF_DURATION_HOURS);
    setField(p.state.chargeSecondsAlt, 113, PF_DURATION_TENTH_HOURS);
    setField(p.state.chargeEnable, 112, PF_BOOL);
    setField(p.state.rfid, 123, PF_BOOL);
    setField(p.state.schedule, 14, PF_TEXT_CONTAINS, 1.0f, "schedule");
    setField(p.state.delay, 104);

    setControl(p.control, DP_SINGLE_CHARGE_ID, PCT_BOOL, "true", "false",
               DP_SINGLE_CURRENT_ID, DP_SINGLE_DELAY_ID, true,
               DP_SINGLE_MODE_ID, "charge_now", "charge_schedule");
  }

  // F-OBZ2 / TuyaMCU-style single-phase profile.
  {
    ProfileDef &p = builtinProfiles[2];
    copyText(p.id, sizeof(p.id), "F-OBZ2");
    copyText(p.name, sizeof(p.name), "Feyree F-OBZ2");
    copyText(p.description, sizeof(p.description),
             "Numeric-state single-phase profile with DP 124 charge control");
    p.phases = 1;
    const uint16_t require[] = {102, 105, 109};
    setDetect(p.detect, 10, require, 3);

    setField(p.state.status, 101, PF_TEXT);
    setField(p.state.statusFallback, 3, PF_TEXT);
    setField(p.state.statusAuth, 101, PF_TEXT);
    setField(p.state.fault, 10);
    setField(p.state.voltageL1, 102, PF_INT, 0.1f);
    setField(p.state.currentL1, 105, PF_INT, 0.1f);
    setField(p.state.power, 109, PF_INT, 100.0f);
    setField(p.state.sessionEnergy, 112, PF_INT, 10.0f);
    setField(p.state.temperature, 110, PF_INT, 0.1f);
    setField(p.state.currentLimit, 115);
    setField(p.state.chargeSeconds, 120, PF_DURATION_HMS);
    setField(p.state.chargeEnable, 124, PF_INT, 1.0f, "0");
    setField(p.state.schedule, 14, PF_TEXT_CONTAINS, 1.0f, "schedule");

    setControl(p.control, DP_OBZ2_CHARGE_ID, PCT_INT, "0", "1",
               DP_OBZ2_CURRENT_ID, 0, false, 0, "", "");
  }

  // Generic read-only fallback. It maps only the fields shared by the known
  // families; control is intentionally disabled.
  {
    ProfileDef &p = fallbackProfile;
    copyText(p.id, sizeof(p.id), "fallback");
    copyText(p.name, sizeof(p.name), "Generic fallback");
    copyText(p.description, sizeof(p.description),
             "Read-only common fields for an unrecognized snapshot");
    p.phases = 0;
    setDetect(p.detect, 0, nullptr, 0);
    setField(p.state.status, 101, PF_TEXT);
    setField(p.state.statusFallback, 3, PF_TEXT);
    setField(p.state.statusAuth, 101, PF_TEXT);
    setField(p.state.fault, 10);
    setField(p.state.power, 109, PF_INT, 100.0f);
    setField(p.state.temperature, 110, PF_INT, 0.1f);
    setField(p.state.sessionEnergy, 25, PF_INT, 1000.0f);
    setField(p.state.schedule, 14, PF_TEXT_CONTAINS, 1.0f, "schedule");
  }
}

void ensureInit() {
  if (!initialized) profilesInit();
}

bool parseDpId(JsonVariant value, uint16_t &out) {
  if (value.isNull() || value.is<bool>()) return false;

  long number = -1;
  if (value.is<const char *>()) {
    const char *text = value.as<const char *>();
    if (!text || !text[0]) return false;
    char *end = nullptr;
    number = strtol(text, &end, 10);
    if (!end || *end != '\0') return false;
  } else if (value.is<float>()) {
    const float f = value.as<float>();
    number = static_cast<long>(f);
    if (static_cast<float>(number) != f) return false;
  } else {
    number = value.as<long>();
  }

  if (number < 1 || number > 999) return false;
  out = static_cast<uint16_t>(number);
  return true;
}

bool parseIntegerValue(JsonVariant value, long min, long max, long &out) {
  if (value.isNull() || value.is<bool>() || value.is<const char *>()) {
    return false;
  }
  if (!(value.is<int>() || value.is<long>() || value.is<unsigned int>() ||
        value.is<unsigned long>() || value.is<float>())) {
    return false;
  }

  const double number = value.as<double>();
  if (number < static_cast<double>(min) || number > static_cast<double>(max)) {
    return false;
  }
  const long result = static_cast<long>(number);
  if (static_cast<double>(result) != number) return false;
  out = result;
  return true;
}

bool parseFieldType(const char *text, uint8_t &type) {
  if (!text || !*text || strcmp(text, "int") == 0) {
    type = PF_INT;
    return true;
  }
  if (strcmp(text, "bool") == 0) {
    type = PF_BOOL;
    return true;
  }
  if (strcmp(text, "text") == 0) {
    type = PF_TEXT;
    return true;
  }
  if (strcmp(text, "text_contains") == 0) {
    type = PF_TEXT_CONTAINS;
    return true;
  }
  if (strcmp(text, "duration_hms") == 0) {
    type = PF_DURATION_HMS;
    return true;
  }
  if (strcmp(text, "duration_hours") == 0) {
    type = PF_DURATION_HOURS;
    return true;
  }
  if (strcmp(text, "duration_tenth_hours") == 0) {
    type = PF_DURATION_TENTH_HOURS;
    return true;
  }
  if (strcmp(text, "voltage_auto") == 0) {
    type = PF_VOLTAGE_AUTO;
    return true;
  }
  return false;
}

bool parseJsonText(JsonVariant value, char *dst, size_t dstSize, bool required,
                   String &error, const String &path) {
  if (value.isNull()) {
    if (required) {
      error = path + " is required";
      return false;
    }
    if (dst && dstSize) dst[0] = '\0';
    return true;
  }

  String text;
  if (value.is<const char *>()) {
    text = value.as<const char *>();
  } else if (value.is<bool>()) {
    text = value.as<bool>() ? "true" : "false";
  } else if (value.is<int>() || value.is<long>() || value.is<unsigned int>() ||
             value.is<unsigned long>()) {
    text = String(value.as<long>());
  } else if (value.is<float>()) {
    text = String(value.as<float>(), 3);
  } else {
    error = path + " must be a string, number or boolean";
    return false;
  }

  if (text.length() >= dstSize) {
    error = path + " is too long";
    return false;
  }
  text.toCharArray(dst, dstSize);
  return true;
}

bool parseFieldObject(JsonVariant value, ProfileField &field, uint8_t defaultType,
                      bool allowMatch, String &error, const String &path) {
  if (value.isNull()) return true;
  if (!value.is<JsonObject>()) {
    error = path + " must be an object";
    return false;
  }

  JsonObject object = value.as<JsonObject>();
  uint16_t dp = 0;
  if (!parseDpId(object["dp"], dp)) {
    error = path + ".dp must be 1-999";
    return false;
  }

  uint8_t type = defaultType;
  if (!object["type"].isNull()) {
    if (!object["type"].is<const char *>()) {
      error = path + ".type must be a string";
      return false;
    }
    if (!parseFieldType(object["type"].as<const char *>(), type)) {
      error = path + ".type is unknown";
      return false;
    }
  }

  field.dp = dp;
  field.type = dp ? type : PF_NONE;
  field.scale = 1.0f;
  field.match[0] = '\0';

  if (!object["scale"].isNull()) {
    if (!object["scale"].is<float>() && !object["scale"].is<int>() &&
        !object["scale"].is<long>()) {
      error = path + ".scale must be a number";
      return false;
    }
    const float scale = object["scale"].as<float>();
    if (!(scale > 0.0f) || scale > 10000.0f) {
      error = path + ".scale must be greater than 0 and at most 10000";
      return false;
    }
    field.scale = scale;
  }

  if (allowMatch) {
    JsonVariant match = object["match"];
    if (match.isNull()) match = object["on"];
    if (!match.isNull()) {
      if (!parseJsonText(match, field.match, sizeof(field.match), false, error,
                         path + ".match")) {
        return false;
      }
    }
  }

  return true;
}

bool stateHasAnyField(const ProfileState &state) {
  return state.status.present() || state.statusFallback.present() ||
         state.statusAuth.present() || state.fault.present() ||
         state.voltageL1.present() || state.voltageL2.present() ||
         state.voltageL3.present() || state.currentL1.present() ||
         state.currentL2.present() || state.currentL3.present() ||
         state.power.present() || state.energy.present() ||
         state.sessionEnergy.present() || state.temperature.present() ||
         state.currentLimit.present() || state.currentLimitAlt.present() ||
         state.chargeSeconds.present() || state.chargeSecondsAlt.present() ||
         state.chargeEnable.present() || state.rfid.present() ||
         state.schedule.present() || state.delay.present();
}

bool parseState(JsonVariant value, ProfileState &state, String &error,
                const String &path) {
  if (value.isNull()) return true;
  if (!value.is<JsonObject>()) {
    error = path + " must be an object";
    return false;
  }
  JsonObject object = value.as<JsonObject>();

  if (!parseFieldObject(object["status"], state.status, PF_TEXT, false, error,
                        path + ".status")) {
    return false;
  }
  if (!object["status"].isNull() && object["status"].is<JsonObject>()) {
    JsonObject status = object["status"].as<JsonObject>();
    if (!status["fallback"].isNull()) {
      if (!parseDpId(status["fallback"], state.statusFallback.dp)) {
        error = path + ".status.fallback must be 1-999";
        return false;
      }
      state.statusFallback.type = PF_TEXT;
      state.statusFallback.scale = 1.0f;
    }
  }

  if (!parseFieldObject(object["auth"], state.statusAuth, PF_TEXT, false, error,
                        path + ".auth")) {
    return false;
  }
  if (!parseFieldObject(object["fault"], state.fault, PF_INT, false, error,
                        path + ".fault")) {
    return false;
  }
  if (!parseFieldObject(object["voltage_l1"], state.voltageL1, PF_INT, false,
                        error, path + ".voltage_l1")) {
    return false;
  }
  if (!parseFieldObject(object["voltage_l2"], state.voltageL2, PF_INT, false,
                        error, path + ".voltage_l2")) {
    return false;
  }
  if (!parseFieldObject(object["voltage_l3"], state.voltageL3, PF_INT, false,
                        error, path + ".voltage_l3")) {
    return false;
  }
  if (!parseFieldObject(object["current_l1"], state.currentL1, PF_INT, false,
                        error, path + ".current_l1")) {
    return false;
  }
  if (!parseFieldObject(object["current_l2"], state.currentL2, PF_INT, false,
                        error, path + ".current_l2")) {
    return false;
  }
  if (!parseFieldObject(object["current_l3"], state.currentL3, PF_INT, false,
                        error, path + ".current_l3")) {
    return false;
  }
  if (!parseFieldObject(object["power"], state.power, PF_INT, false, error,
                        path + ".power")) {
    return false;
  }
  if (!parseFieldObject(object["energy"], state.energy, PF_INT, false, error,
                        path + ".energy")) {
    return false;
  }
  if (!parseFieldObject(object["session_energy"], state.sessionEnergy, PF_INT,
                        false, error, path + ".session_energy")) {
    return false;
  }
  if (!parseFieldObject(object["temperature"], state.temperature, PF_INT, false,
                        error, path + ".temperature")) {
    return false;
  }
  if (!parseFieldObject(object["current_limit"], state.currentLimit, PF_INT,
                        false, error, path + ".current_limit")) {
    return false;
  }
  if (!parseFieldObject(object["current_limit_alt"], state.currentLimitAlt,
                        PF_INT, false, error, path + ".current_limit_alt")) {
    return false;
  }
  if (!parseFieldObject(object["charge_seconds"], state.chargeSeconds,
                        PF_DURATION_HMS, false, error, path + ".charge_seconds")) {
    return false;
  }
  if (!parseFieldObject(object["charge_seconds_alt"], state.chargeSecondsAlt,
                        PF_DURATION_HMS, false, error,
                        path + ".charge_seconds_alt")) {
    return false;
  }
  if (!parseFieldObject(object["charge_enable"], state.chargeEnable, PF_TEXT,
                        true, error, path + ".charge_enable")) {
    return false;
  }
  if (state.chargeEnable.type == PF_TEXT && state.chargeEnable.match[0] == '\0') {
    copyText(state.chargeEnable.match, sizeof(state.chargeEnable.match),
             "on,true,1,opencharging,charge");
  }
  if (state.chargeEnable.type == PF_INT && state.chargeEnable.match[0]) {
    char *end = nullptr;
    strtol(state.chargeEnable.match, &end, 10);
    if (!end || *end != '\0') {
      error = path + ".charge_enable.on/match must be an integer for type int";
      return false;
    }
  }
  if (!parseFieldObject(object["rfid"], state.rfid, PF_BOOL, false, error,
                        path + ".rfid")) {
    return false;
  }
  if (!parseFieldObject(object["schedule"], state.schedule, PF_TEXT_CONTAINS,
                        true, error, path + ".schedule")) {
    return false;
  }
  if (state.schedule.type == PF_TEXT_CONTAINS &&
      state.schedule.match[0] == '\0') {
    copyText(state.schedule.match, sizeof(state.schedule.match), "schedule");
  }
  if (!parseFieldObject(object["delay"], state.delay, PF_INT, false, error,
                        path + ".delay")) {
    return false;
  }

  return true;
}

bool parseDetect(JsonVariant value, ProfileDetect &detect, String &error,
                 const String &path) {
  detect.priority = 5;
  if (value.isNull()) return true;
  if (!value.is<JsonObject>()) {
    error = path + " must be an object";
    return false;
  }

  JsonObject object = value.as<JsonObject>();
  if (!object["priority"].isNull()) {
    long priority = 0;
    if (!parseIntegerValue(object["priority"], 0, 255, priority)) {
      error = path + ".priority must be an integer between 0 and 255";
      return false;
    }
    detect.priority = static_cast<uint8_t>(priority);
  }

  JsonVariant requireValue = object["require"];
  if (!requireValue.isNull()) {
    if (!requireValue.is<JsonArray>()) {
      error = path + ".require must be an array";
      return false;
    }
    JsonArray require = requireValue.as<JsonArray>();
    if (require.size() > 8) {
      error = path + ".require supports at most 8 DPs";
      return false;
    }
    detect.requireCount = 0;
    for (JsonVariant item : require) {
      uint16_t dp = 0;
      if (!parseDpId(item, dp)) {
        error = path + ".require contains an invalid DP";
        return false;
      }
      detect.require[detect.requireCount++] = dp;
    }
  }

  JsonVariant excludeValue = object["exclude"];
  if (!excludeValue.isNull()) {
    if (!excludeValue.is<JsonArray>()) {
      error = path + ".exclude must be an array";
      return false;
    }
    JsonArray exclude = excludeValue.as<JsonArray>();
    if (exclude.size() > 8) {
      error = path + ".exclude supports at most 8 DPs";
      return false;
    }
    detect.excludeCount = 0;
    for (JsonVariant item : exclude) {
      uint16_t dp = 0;
      if (!parseDpId(item, dp)) {
        error = path + ".exclude contains an invalid DP";
        return false;
      }
      detect.exclude[detect.excludeCount++] = dp;
    }
  }

  return true;
}

bool parseControl(JsonVariant value, ProfileControl &control, String &error,
                  const String &path) {
  if (value.isNull()) return true;
  if (!value.is<JsonObject>()) {
    error = path + " must be an object";
    return false;
  }

  JsonObject object = value.as<JsonObject>();

  if (!object["charge"].isNull() && !object["charge"].is<JsonObject>()) {
    error = path + ".charge must be an object";
    return false;
  }
  JsonObject charge = object["charge"].as<JsonObject>();
  if (!charge.isNull()) {
    uint16_t dp = 0;
    if (!parseDpId(charge["dp"], dp)) {
      error = path + ".charge.dp must be 1-999";
      return false;
    }

    String type = "string";
    if (!charge["type"].isNull()) {
      if (!charge["type"].is<const char *>()) {
        error = path + ".charge.type must be a string";
        return false;
      }
      type = charge["type"].as<const char *>();
    }

    control.chargeDp = dp;
    if (type == "bool") {
      control.chargeType = PCT_BOOL;
      if (!parseJsonText(charge["on"], control.chargeOn,
                         sizeof(control.chargeOn), false, error,
                         path + ".charge.on")) {
        return false;
      }
      if (!parseJsonText(charge["off"], control.chargeOff,
                         sizeof(control.chargeOff), false, error,
                         path + ".charge.off")) {
        return false;
      }
      if (control.chargeOn[0] == '\0') {
        copyText(control.chargeOn, sizeof(control.chargeOn), "true");
      }
      if (control.chargeOff[0] == '\0') {
        copyText(control.chargeOff, sizeof(control.chargeOff), "false");
      }
      String on = control.chargeOn;
      on.toLowerCase();
      String off = control.chargeOff;
      off.toLowerCase();
      if (on != "true" && on != "1") {
        error = path + ".charge.on must be true/false or 1/0 for type bool";
        return false;
      }
      if (off != "false" && off != "0") {
        error = path + ".charge.off must be true/false or 1/0 for type bool";
        return false;
      }
      copyText(control.chargeOn, sizeof(control.chargeOn), "true");
      copyText(control.chargeOff, sizeof(control.chargeOff), "false");
    } else if (type == "int") {
      control.chargeType = PCT_INT;
      if (!parseJsonText(charge["on"], control.chargeOn,
                         sizeof(control.chargeOn), true, error,
                         path + ".charge.on")) {
        return false;
      }
      if (!parseJsonText(charge["off"], control.chargeOff,
                         sizeof(control.chargeOff), true, error,
                         path + ".charge.off")) {
        return false;
      }
      char *end = nullptr;
      const long onValue = strtol(control.chargeOn, &end, 10);
      if (!control.chargeOn[0] || !end || *end != '\0') {
        error = path + ".charge.on must be an integer";
        return false;
      }
      const long offValue = strtol(control.chargeOff, &end, 10);
      if (!control.chargeOff[0] || !end || *end != '\0') {
        error = path + ".charge.off must be an integer";
        return false;
      }
      snprintf(control.chargeOn, sizeof(control.chargeOn), "%ld", onValue);
      snprintf(control.chargeOff, sizeof(control.chargeOff), "%ld", offValue);
    } else if (type == "string") {
      control.chargeType = PCT_STRING;
      if (!parseJsonText(charge["on"], control.chargeOn,
                         sizeof(control.chargeOn), true, error,
                         path + ".charge.on")) {
        return false;
      }
      if (!parseJsonText(charge["off"], control.chargeOff,
                         sizeof(control.chargeOff), true, error,
                         path + ".charge.off")) {
        return false;
      }
      if (control.chargeOn[0] == '\0' || control.chargeOff[0] == '\0') {
        error = path + ".charge.on and .charge.off must not be empty";
        return false;
      }
    } else {
      error = path + ".charge.type must be bool, int or string";
      return false;
    }
  }

  if (!object["current"].isNull() && !object["current"].is<JsonObject>()) {
    error = path + ".current must be an object";
    return false;
  }
  JsonObject current = object["current"].as<JsonObject>();
  if (!current.isNull()) {
    if (!parseDpId(current["dp"], control.currentDp)) {
      error = path + ".current.dp must be 1-999";
      return false;
    }
  }

  if (!object["delay"].isNull() && !object["delay"].is<JsonObject>()) {
    error = path + ".delay must be an object";
    return false;
  }
  JsonObject delay = object["delay"].as<JsonObject>();
  if (!delay.isNull()) {
    if (!parseDpId(delay["dp"], control.delayDp)) {
      error = path + ".delay.dp must be 1-999";
      return false;
    }
    control.delaySetsMode = delay["sets_mode"] | false;
  }

  if (!object["mode"].isNull() && !object["mode"].is<JsonObject>()) {
    error = path + ".mode must be an object";
    return false;
  }
  JsonObject mode = object["mode"].as<JsonObject>();
  if (!mode.isNull()) {
    if (!parseDpId(mode["dp"], control.modeDp)) {
      error = path + ".mode.dp must be 1-999";
      return false;
    }
    if (!parseJsonText(mode["now"], control.modeNow, sizeof(control.modeNow),
                       false, error, path + ".mode.now")) {
      return false;
    }
    if (!parseJsonText(mode["schedule"], control.modeSchedule,
                       sizeof(control.modeSchedule), false, error,
                       path + ".mode.schedule")) {
      return false;
    }
    if (control.modeNow[0] == '\0') {
      copyText(control.modeNow, sizeof(control.modeNow), "charge_now");
    }
    if (control.modeSchedule[0] == '\0') {
      copyText(control.modeSchedule, sizeof(control.modeSchedule),
               "charge_schedule");
    }
  }

  if (control.delaySetsMode && control.modeDp == 0) {
    error = path + ".delay.sets_mode requires a mode object";
    return false;
  }

  return true;
}

bool parseFloatValue(JsonVariant value, float min, float max, float &out) {
  if (value.isNull() || value.is<bool>() || value.is<const char *>()) {
    return false;
  }
  if (!(value.is<int>() || value.is<long>() || value.is<unsigned int>() ||
        value.is<unsigned long>() || value.is<float>())) {
    return false;
  }
  const double number = value.as<double>();
  if (number < static_cast<double>(min) || number > static_cast<double>(max)) {
    return false;
  }
  out = static_cast<float>(number);
  return true;
}

bool parseCanonicalValue(JsonVariant value, char *dst, size_t dstSize,
                         uint8_t &type, String &error, const String &path) {
  if (value.isNull()) {
    error = path + " must not be null";
    return false;
  }

  if (value.is<const char *>()) {
    const char *text = value.as<const char *>();
    if (!text || strlen(text) >= dstSize) {
      error = path + " is too long";
      return false;
    }
    type = PVT_STRING;
    copyText(dst, dstSize, text);
    return true;
  }

  if (value.is<bool>()) {
    type = PVT_BOOL;
    copyText(dst, dstSize, value.as<bool>() ? "true" : "false");
    return true;
  }

  if (value.is<int>() || value.is<long>() || value.is<unsigned int>() ||
      value.is<unsigned long>()) {
    type = PVT_INT;
    snprintf(dst, dstSize, "%ld", value.as<long>());
    return true;
  }

  if (value.is<float>()) {
    type = PVT_FLOAT;
    snprintf(dst, dstSize, "%.6g", value.as<double>());
    return true;
  }

  error = path + " must be a string, number or boolean";
  return false;
}

uint8_t canonicalTypeForDp(uint8_t dpType) {
  switch (dpType) {
    case PDT_STRING: return PVT_STRING;
    case PDT_BOOL: return PVT_BOOL;
    case PDT_FLOAT: return PVT_FLOAT;
    default: return PVT_INT;
  }
}

bool coerceBoolean(const char *text, bool &value) {
  if (!text) return false;
  if (strcasecmp(text, "true") == 0 || strcmp(text, "1") == 0 ||
      strcasecmp(text, "on") == 0 || strcasecmp(text, "yes") == 0) {
    value = true;
    return true;
  }
  if (strcasecmp(text, "false") == 0 || strcmp(text, "0") == 0 ||
      strcasecmp(text, "off") == 0 || strcasecmp(text, "no") == 0) {
    value = false;
    return true;
  }
  return false;
}

bool coerceNumber(const char *text, float &value) {
  if (!text || !text[0]) return false;
  char *end = nullptr;
  const double number = strtod(text, &end);
  if (!end || *end != '\0') return false;
  value = static_cast<float>(number);
  return true;
}

void normalizeMappingValue(ProfileMapping &mapping, uint8_t entityType,
                           bool primary, String &error, const String &path) {
  if (!primary) return;

  if (entityType == PET_BINARY_SENSOR || entityType == PET_SWITCH) {
    if (mapping.toType == PVT_BOOL) return;
    if (mapping.toType == PVT_INT || mapping.toType == PVT_FLOAT) {
      mapping.toType = PVT_BOOL;
      copyText(mapping.to, sizeof(mapping.to),
               atof(mapping.to) != 0.0 ? "true" : "false");
      return;
    }
    bool value = false;
    if (!coerceBoolean(mapping.to, value)) {
      error = path + ".mapping value must map to a boolean for this entity type";
      return;
    }
    mapping.toType = PVT_BOOL;
    copyText(mapping.to, sizeof(mapping.to), value ? "true" : "false");
    return;
  }

  if (entityType == PET_NUMBER) {
    float value = 0.0f;
    if (!coerceNumber(mapping.to, value)) {
      error = path + ".mapping value must be numeric for a number entity";
      return;
    }
    mapping.toType = PVT_FLOAT;
    snprintf(mapping.to, sizeof(mapping.to), "%.6g", value);
  }
}

bool parseMappings(JsonVariant value, uint8_t entityType, bool primary,
                   ProfileDef &profile, ProfileDp &dp, String &error,
                   const String &path) {
  if (value.isNull()) return true;
  if (!value.is<JsonArray>()) {
    error = path + " must be an array";
    return false;
  }

  JsonArray rules = value.as<JsonArray>();
  for (JsonVariant item : rules) {
    if (!item.is<JsonObject>()) {
      error = path + " must contain objects";
      return false;
    }
    JsonObject rule = item.as<JsonObject>();

    const bool hasFrom = !rule["dps_val"].isNull();
    const bool hasTo = !rule["value"].isNull();
    if (!hasFrom && !hasTo) {
      if (rule["scale"].isNull()) {
        error = path + " rule requires value, dps_val or scale";
        return false;
      }
      float scale = 1.0f;
      if (!parseFloatValue(rule["scale"], 0.000001f, 10000.0f, scale)) {
        error = path + ".scale must be a number greater than 0";
        return false;
      }
      dp.scale = scale;
      continue;
    }

    if (!rule["scale"].isNull()) {
      error = path + " scale must be a standalone mapping rule";
      return false;
    }

    if (profile.mappingCount >= PROFILE_MAX_MAPPINGS) {
      error = path + " exceeds the profile mapping limit";
      return false;
    }

    ProfileMapping &mapping = profile.mappings[profile.mappingCount];
    memset(&mapping, 0, sizeof(mapping));
    mapping.hidden = rule["hidden"] | false;

    if (hasFrom) {
      if (!parseCanonicalValue(rule["dps_val"], mapping.from,
                               sizeof(mapping.from), mapping.toType, error,
                               path + ".dps_val")) {
        return false;
      }
      mapping.hasFrom = true;
    }

    if (hasTo) {
      uint8_t toType = PVT_STRING;
      if (!parseCanonicalValue(rule["value"], mapping.to, sizeof(mapping.to),
                               toType, error, path + ".value")) {
        return false;
      }
      mapping.toType = toType;
    } else {
      copyText(mapping.to, sizeof(mapping.to), mapping.from);
      mapping.toType = canonicalTypeForDp(dp.type);
    }

    normalizeMappingValue(mapping, entityType, primary, error, path);
    if (error.length()) return false;

    if (dp.mappingCount == 0) dp.mappingStart = profile.mappingCount;
    ++dp.mappingCount;
    ++profile.mappingCount;
  }

  return true;
}

bool parseRange(JsonVariant value, ProfileDp &dp, String &error,
                const String &path) {
  if (value.isNull()) return true;
  if (!value.is<JsonObject>()) {
    error = path + " must be an object";
    return false;
  }

  JsonObject range = value.as<JsonObject>();
  float min = 0.0f;
  float max = 0.0f;
  float step = 1.0f;
  if (!parseFloatValue(range["min"], -1000000.0f, 1000000.0f, min)) {
    error = path + ".min must be a number";
    return false;
  }
  if (!parseFloatValue(range["max"], -1000000.0f, 1000000.0f, max)) {
    error = path + ".max must be a number";
    return false;
  }
  if (max < min) {
    error = path + ".max must be greater than or equal to min";
    return false;
  }
  if (!range["step"].isNull() &&
      !parseFloatValue(range["step"], 0.000001f, 1000000.0f, step)) {
    error = path + ".step must be a number greater than 0";
    return false;
  }

  dp.min = min;
  dp.max = max;
  dp.step = step;
  dp.hasRange = true;
  return true;
}

const char *primaryDpName(uint8_t entityType) {
  switch (entityType) {
    case PET_BINARY_SENSOR: return "sensor";
    case PET_SWITCH: return "switch";
    case PET_NUMBER: return "value";
    case PET_SELECT: return "option";
    case PET_BUTTON: return "button";
    case PET_TEXT: return "value";
    case PET_SENSOR:
    default: return "sensor";
  }
}

bool parseEntityType(const char *text, uint8_t &type) {
  if (!text) return false;
  if (strcmp(text, "sensor") == 0) {
    type = PET_SENSOR;
  } else if (strcmp(text, "binary_sensor") == 0) {
    type = PET_BINARY_SENSOR;
  } else if (strcmp(text, "switch") == 0) {
    type = PET_SWITCH;
  } else if (strcmp(text, "number") == 0) {
    type = PET_NUMBER;
  } else if (strcmp(text, "select") == 0) {
    type = PET_SELECT;
  } else if (strcmp(text, "button") == 0) {
    type = PET_BUTTON;
  } else if (strcmp(text, "text") == 0) {
    type = PET_TEXT;
  } else {
    return false;
  }
  return true;
}

bool parseDpType(const char *text, uint8_t &type) {
  if (!text) return false;
  if (strcmp(text, "string") == 0) {
    type = PDT_STRING;
  } else if (strcmp(text, "boolean") == 0 || strcmp(text, "bool") == 0) {
    type = PDT_BOOL;
  } else if (strcmp(text, "integer") == 0 || strcmp(text, "int") == 0) {
    type = PDT_INT;
  } else if (strcmp(text, "bitfield") == 0) {
    type = PDT_BITFIELD;
  } else if (strcmp(text, "float") == 0) {
    type = PDT_FLOAT;
  } else {
    return false;
  }
  return true;
}

void titleCase(const char *text, char *dst, size_t dstSize) {
  if (!dst || dstSize == 0) return;
  dst[0] = '\0';
  if (!text) return;
  size_t out = 0;
  bool wordStart = true;
  for (const char *p = text; *p && out + 1 < dstSize; ++p) {
    unsigned char c = static_cast<unsigned char>(*p);
    if (c == '_' || c == '-' || c == ' ') {
      if (out > 0 && dst[out - 1] != ' ') dst[out++] = ' ';
      wordStart = true;
      continue;
    }
    if (wordStart) {
      c = static_cast<unsigned char>(toupper(c));
      wordStart = false;
    }
    dst[out++] = static_cast<char>(c);
  }
  dst[out] = '\0';
}

void deriveEntityName(JsonObject object, uint8_t entityType, char *dst,
                      size_t dstSize) {
  const char *name = object["name"] | "";
  if (name[0]) {
    copyText(dst, dstSize, name);
    return;
  }

  const char *translation = object["translation_key"] | "";
  const char *placeholder = "";
  if (object["translation_placeholders"].is<JsonObject>()) {
    placeholder = object["translation_placeholders"]["x"] | "";
  }

  if (strcmp(translation, "status") == 0) {
    copyText(dst, dstSize, "Status");
    return;
  }
  if (strcmp(translation, "voltage_x") == 0) {
    String value = String("Voltage ") + (placeholder[0] ? placeholder : "");
    copyText(dst, dstSize, value.c_str());
    return;
  }
  if (strcmp(translation, "current_x") == 0) {
    String value = String("Current ") + (placeholder[0] ? placeholder : "");
    copyText(dst, dstSize, value.c_str());
    return;
  }
  if (translation[0]) {
    titleCase(translation, dst, dstSize);
    return;
  }

  const char *deviceClass = object["class"] | "";
  if (deviceClass[0]) {
    titleCase(deviceClass, dst, dstSize);
    return;
  }

  switch (entityType) {
    case PET_BINARY_SENSOR: copyText(dst, dstSize, "Binary sensor"); break;
    case PET_SWITCH: copyText(dst, dstSize, "Switch"); break;
    case PET_NUMBER: copyText(dst, dstSize, "Number"); break;
    case PET_SELECT: copyText(dst, dstSize, "Select"); break;
    case PET_BUTTON: copyText(dst, dstSize, "Button"); break;
    case PET_TEXT: copyText(dst, dstSize, "Text"); break;
    case PET_SENSOR:
    default: copyText(dst, dstSize, "Sensor"); break;
  }
}

bool parseGenericDp(JsonVariant value, uint8_t entityType, bool primary,
                    ProfileDef &profile, ProfileDp &dp, String &error,
                    const String &path) {
  if (!value.is<JsonObject>()) {
    error = path + " must be an object";
    return false;
  }
  JsonObject object = value.as<JsonObject>();
  memset(&dp, 0, sizeof(dp));

  if (!parseDpId(object["id"], dp.dp)) {
    error = path + ".id must be 1-999";
    return false;
  }
  if (object["type"].isNull() || !object["type"].is<const char *>()) {
    error = path + ".type is required";
    return false;
  }
  if (!parseDpType(object["type"].as<const char *>(), dp.type)) {
    error = path + ".type must be string, boolean, integer, bitfield or float";
    return false;
  }
  if (object["name"].isNull() || !object["name"].is<const char *>()) {
    error = path + ".name is required";
    return false;
  }
  const char *name = object["name"].as<const char *>();
  if (!name || !name[0] || strlen(name) >= sizeof(dp.name)) {
    error = path + ".name must be 1-23 characters";
    return false;
  }
  const char *expected = primaryDpName(entityType);
  if (primary && strcmp(name, expected) != 0) {
    error = path + ".name must be '" + String(expected) + "' for the first DP";
    return false;
  }
  if (!primary && strcmp(name, expected) == 0) {
    error = path + ".name is reserved for the primary DP";
    return false;
  }
  for (const char *p = name; *p; ++p) {
    const unsigned char c = static_cast<unsigned char>(*p);
    if (p == name && !isalpha(c) && c != '_') {
      error = path + ".name must start with a letter or '_'";
      return false;
    }
    if (!isalnum(c) && c != '_') {
      error = path + ".name may contain only letters, digits and '_'";
      return false;
    }
  }
  copyText(dp.name, sizeof(dp.name), name);

  dp.flags = 0;
  if (object["optional"] | false) dp.flags |= PDF_OPTIONAL;
  if (object["readonly"] | false) dp.flags |= PDF_READONLY;
  if (object["hidden"] | false) dp.flags |= PDF_HIDDEN;
  dp.scale = 1.0f;

  if (!object["scale"].isNull() &&
      !parseFloatValue(object["scale"], 0.000001f, 10000.0f, dp.scale)) {
    error = path + ".scale must be a number greater than 0";
    return false;
  }

  if (!parseRange(object["range"], dp, error, path + ".range")) {
    return false;
  }
  if (!parseMappings(object["mapping"], entityType, primary, profile, dp,
                     error, path + ".mapping")) {
    return false;
  }

  return true;
}

bool parseGenericEntity(JsonVariant value, size_t index, ProfileDef &profile,
                        ProfileEntity &entity, String &error,
                        const String &path) {
  if (!value.is<JsonObject>()) {
    error = path + " must be an object";
    return false;
  }
  JsonObject object = value.as<JsonObject>();
  memset(&entity, 0, sizeof(entity));
  entity.precision = -1;

  if (object["entity"].isNull() || !object["entity"].is<const char *>()) {
    error = path + ".entity is required";
    return false;
  }
  if (!parseEntityType(object["entity"].as<const char *>(), entity.type)) {
    error = path + ".entity must be sensor, binary_sensor, switch, number, "
                  "select, button or text";
    return false;
  }

  if (!object["name"].isNull() && !object["name"].is<const char *>()) {
    error = path + ".name must be a string";
    return false;
  }
  if (!object["name"].isNull() &&
      strlen(object["name"].as<const char *>()) >= sizeof(entity.name)) {
    error = path + ".name must be 1-31 characters";
    return false;
  }
  deriveEntityName(object, entity.type, entity.name, sizeof(entity.name));
  if (!entity.name[0]) {
    error = path + ".name is empty";
    return false;
  }

  const char *deviceClass = object["class"] | "";
  if (strlen(deviceClass) >= sizeof(entity.deviceClass)) {
    error = path + ".class is too long";
    return false;
  }
  copyText(entity.deviceClass, sizeof(entity.deviceClass), deviceClass);

  const char *category = object["category"] | "";
  if (category[0]) {
    if (strcmp(category, "diagnostic") != 0 && strcmp(category, "config") != 0) {
      error = path + ".category must be diagnostic or config";
      return false;
    }
    copyText(entity.category, sizeof(entity.category), category);
  }

  const char *icon = object["icon"] | "";
  if (strlen(icon) >= sizeof(entity.icon)) {
    error = path + ".icon is too long";
    return false;
  }
  copyText(entity.icon, sizeof(entity.icon), icon);
  if (object["hidden"] | false) entity.flags |= PEF_HIDDEN;

  JsonVariant dpsValue = object["dps"];
  if (dpsValue.isNull() || !dpsValue.is<JsonArray>()) {
    error = path + ".dps must be an array";
    return false;
  }
  JsonArray dps = dpsValue.as<JsonArray>();
  if (dps.size() == 0 || dps.size() > PROFILE_MAX_DP_PER_ENTITY) {
    error = path + ".dps supports 1-4 entries";
    return false;
  }

  uint8_t count = 0;
  for (JsonVariant item : dps) {
    if (!parseGenericDp(item, entity.type, count == 0, profile,
                        entity.dps[count], error,
                        path + ".dps[" + String(count) + "]")) {
      return false;
    }
    ++count;
  }
  entity.dpCount = count;

  for (uint8_t i = 0; i < entity.dpCount; ++i) {
    for (uint8_t j = i + 1; j < entity.dpCount; ++j) {
      if (strcmp(entity.dps[i].name, entity.dps[j].name) == 0) {
        error = path + ".dps names must be unique";
        return false;
      }
    }
  }

  ProfileDp &primary = entity.dps[0];
  if (primary.hidden()) {
    error = path + " primary DP cannot be hidden";
    return false;
  }
  if (entity.type == PET_SENSOR || entity.type == PET_BINARY_SENSOR) {
    primary.flags |= PDF_READONLY;
  }
  if (entity.type == PET_BINARY_SENSOR && primary.type == PDT_STRING &&
      primary.mappingCount == 0) {
    error = path + " binary sensors need a boolean, integer or mapped DP";
    return false;
  }
  if ((entity.type == PET_SWITCH || entity.type == PET_NUMBER ||
       entity.type == PET_SELECT || entity.type == PET_BUTTON ||
       entity.type == PET_TEXT) &&
      primary.readonly()) {
    error = path + " primary DP cannot be readonly for this entity type";
    return false;
  }
  if (entity.type == PET_NUMBER && !primary.hasRange) {
    error = path + " number entities require a range";
    return false;
  }
  if (entity.type == PET_NUMBER && primary.mappingCount == 0 &&
      primary.type != PDT_INT && primary.type != PDT_BITFIELD &&
      primary.type != PDT_FLOAT) {
    error = path + " number entities need a numeric DP or a mapping";
    return false;
  }
  if (entity.type == PET_TEXT && primary.type != PDT_STRING) {
    error = path + " text entities require a string DP";
    return false;
  }

  if (entity.type == PET_SELECT ||
      (entity.type == PET_SENSOR &&
       strcmp(entity.deviceClass, "enum") == 0)) {
    for (uint8_t i = 0; i < primary.mappingCount; ++i) {
      profile.mappings[primary.mappingStart + i].toType = PVT_STRING;
    }
  }

  bool hasOn = false;
  bool hasOff = false;
  bool hasPress = false;
  bool hasOption = false;
  for (uint8_t i = 0; i < primary.mappingCount; ++i) {
    const ProfileMapping &mapping =
        profile.mappings[primary.mappingStart + i];
    if (mapping.hidden || !mapping.hasFrom) continue;
    if (mapping.toType == PVT_BOOL) {
      if (strcmp(mapping.to, "true") == 0) {
        hasOn = true;
        hasPress = true;
      }
      if (strcmp(mapping.to, "false") == 0) hasOff = true;
    }
    hasOption = true;
  }

  if (entity.type == PET_SELECT && !hasOption) {
    error = path + " select entities require at least one visible option";
    return false;
  }
  if (entity.type == PET_SWITCH && primary.mappingCount > 0 &&
      (!hasOn || !hasOff)) {
    error = path + " switch mappings must include on and off values";
    return false;
  }
  if (entity.type == PET_BUTTON && primary.type != PDT_BOOL && !hasPress) {
    error = path + " button entities need a boolean DP or a press mapping";
    return false;
  }
  if (entity.type == PET_SENSOR && strcmp(entity.deviceClass, "enum") == 0 &&
      primary.mappingCount == 0) {
    error = path + " enum sensors require a mapping";
    return false;
  }

  JsonVariant primaryValue = object["dps"][0];
  JsonObject primaryObject = primaryValue.as<JsonObject>();

  const char *stateClass = primaryObject["class"] | "";
  if (strlen(stateClass) >= sizeof(entity.stateClass)) {
    error = path + ".dps[0].class is too long";
    return false;
  }
  copyText(entity.stateClass, sizeof(entity.stateClass), stateClass);

  const char *unit = primaryObject["unit"] | "";
  if (strlen(unit) >= sizeof(entity.unit)) {
    error = path + ".dps[0].unit is too long";
    return false;
  }
  copyText(entity.unit, sizeof(entity.unit), unit);

  if (!primaryObject["precision"].isNull()) {
    long precision = 0;
    if (!parseIntegerValue(primaryObject["precision"], 0, 10, precision)) {
      error = path + ".dps[0].precision must be 0-10";
      return false;
    }
    entity.precision = static_cast<int8_t>(precision);
  }

  snprintf(entity.key, sizeof(entity.key), "e%u", static_cast<unsigned>(index));
  return true;
}

bool parseEntities(JsonVariant value, ProfileDef &profile, String &error,
                   const String &path) {
  if (value.isNull()) return true;
  if (!value.is<JsonArray>()) {
    error = path + " must be an array";
    return false;
  }
  JsonArray array = value.as<JsonArray>();
  if (array.size() > PROFILE_MAX_ENTITIES) {
    error = path + " supports at most " + String(PROFILE_MAX_ENTITIES) +
            " entities";
    return false;
  }

  for (JsonVariant item : array) {
    if (profile.genericEntityCount >= PROFILE_MAX_ENTITIES) {
      error = path + " supports at most " + String(PROFILE_MAX_ENTITIES) +
              " entities";
      return false;
    }
    if (!parseGenericEntity(item, profile.genericEntityCount, profile,
                            profile.entities[profile.genericEntityCount], error,
                            path + "[" +
                                String(profile.genericEntityCount) + "]")) {
      return false;
    }
    ++profile.genericEntityCount;
  }
  return true;
}

void deriveDetectFromEntities(ProfileDef &profile) {
  if (profile.detect.requireCount > 0) return;
  for (size_t i = 0; i < profile.genericEntityCount; ++i) {
    const ProfileDp &primary = profile.entities[i].dps[0];
    if (!primary.present() || primary.optional()) continue;
    if (profile.detect.requireCount < 8) {
      profile.detect.require[profile.detect.requireCount++] = primary.dp;
    }
    if (profile.detect.requireCount >= 8) break;
  }
}

bool parseCustomProfile(JsonObject object, size_t index, ProfileDef &profile,
                        String &error) {
  const String path = String("profiles[") + String(index) + "]";
  memset(&profile, 0, sizeof(profile));

  if (object["id"].isNull() || !object["id"].is<const char *>()) {
    error = path + ".id is required and must be a string";
    return false;
  }
  const char *id = object["id"].as<const char *>();
  if (!id || !id[0] || strlen(id) >= sizeof(profile.id)) {
    error = path + ".id must be 1-23 characters";
    return false;
  }
  for (const char *p = id; *p; ++p) {
    const unsigned char c = static_cast<unsigned char>(*p);
    if (!isalnum(c) && c != '_' && c != '-') {
      error = path + ".id may contain only letters, digits, '_' and '-'";
      return false;
    }
  }
  if (strcmp(id, "auto") == 0 || strcmp(id, "fallback") == 0 ||
      strcmp(id, "unknown") == 0 || findBuiltin(id) ||
      (browserProfileLoaded() && strcmp(id, browserProfileId()) == 0)) {
    error = path + ".id is reserved";
    return false;
  }
  copyText(profile.id, sizeof(profile.id), id);

  if (object["name"].isNull() || !object["name"].is<const char *>()) {
    error = path + ".name is required and must be a string";
    return false;
  }
  const char *name = object["name"].as<const char *>();
  if (!name || !name[0] || strlen(name) >= sizeof(profile.name)) {
    error = path + ".name must be 1-39 characters";
    return false;
  }
  copyText(profile.name, sizeof(profile.name), name);

  if (!object["description"].isNull()) {
    if (!object["description"].is<const char *>()) {
      error = path + ".description must be a string";
      return false;
    }
    const char *description = object["description"].as<const char *>();
    if (description && strlen(description) >= sizeof(profile.description)) {
      error = path + ".description is too long";
      return false;
    }
    copyText(profile.description, sizeof(profile.description), description);
  }

  if (!object["phases"].isNull()) {
    long phases = 0;
    if (!parseIntegerValue(object["phases"], 0, 3, phases)) {
      error = path + ".phases must be an integer between 0 and 3";
      return false;
    }
    profile.phases = static_cast<uint8_t>(phases);
  } else {
    profile.phases = 1;
  }

  if (!parseDetect(object["detect"], profile.detect, error, path + ".detect")) {
    return false;
  }
  bool requireProvided = false;
  if (!object["detect"].isNull()) {
    JsonObject detect = object["detect"].as<JsonObject>();
    requireProvided = !detect["require"].isNull();
  }
  if (!parseState(object["state"], profile.state, error, path + ".state")) {
    return false;
  }
  if (!parseEntities(object["entities"], profile, error, path + ".entities")) {
    return false;
  }
  if (!stateHasAnyField(profile.state) && profile.genericEntityCount == 0) {
    error = path + ".state or .entities must define at least one DP";
    return false;
  }
  if (!parseControl(object["control"], profile.control, error,
                    path + ".control")) {
    return false;
  }
  if (!requireProvided) deriveDetectFromEntities(profile);

  return true;
}

bool parseCustomProfiles(const char *json, ProfileDef *out, size_t &count,
                         ProfileDef &scratch, String &error,
                         const char *wantId = nullptr,
                         bool *found = nullptr) {
  count = 0;
  if (found) *found = false;
  if (!json || !*json) return true;

  const char *start = json;
  while (*start && isspace(static_cast<unsigned char>(*start))) ++start;
  if (!*start) return true;

  JsonDocument document;
  const DeserializationError err = deserializeJson(document, json);
  if (err) {
    error = String("invalid JSON: ") + err.c_str();
    return false;
  }

  JsonArray profiles;
  if (document.is<JsonArray>()) {
    profiles = document.as<JsonArray>();
  } else if (!document["profiles"].isNull()) {
    if (!document["profiles"].is<JsonArray>()) {
      error = "profiles must be an array";
      return false;
    }
    profiles = document["profiles"].as<JsonArray>();
  } else {
    error = "expected a profiles array";
    return false;
  }

  if (profiles.size() > PROFILE_MAX_CUSTOM) {
    error = String("at most ") + String(PROFILE_MAX_CUSTOM) +
            " custom profiles are supported";
    return false;
  }

  char seen[PROFILE_MAX_CUSTOM][24] = {{0}};
  for (JsonVariant item : profiles) {
    if (!item.is<JsonObject>()) {
      error = "each custom profile must be an object";
      return false;
    }
    if (!parseCustomProfile(item.as<JsonObject>(), count, scratch, error)) {
      return false;
    }
    for (size_t i = 0; i < count; ++i) {
      if (strcmp(seen[i], scratch.id) == 0) {
        error = String("profiles[") + String(count) +
                "].id duplicates another custom profile";
        return false;
      }
    }
    if (wantId && strcmp(scratch.id, wantId) == 0 && found) *found = true;
    if (out) memcpy(&out[count], &scratch, sizeof(ProfileDef));
    copyText(seen[count], sizeof(seen[count]), scratch.id);
    ++count;
  }

  return true;
}

bool isBlank(const char *text) {
  if (!text) return true;
  for (const char *p = text; *p; ++p) {
    if (!isspace(static_cast<unsigned char>(*p))) return false;
  }
  return true;
}

String htmlEscape(const char *text) {
  String out;
  if (!text) return out;
  for (const char *p = text; *p; ++p) {
    switch (*p) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      case '{': out += "&#123;"; break;
      case '}': out += "&#125;"; break;
      default: out += *p; break;
    }
  }
  return out;
}

bool hasDp(JsonObject dps, uint16_t dp) {
  char key[8];
  snprintf(key, sizeof(key), "%u", dp);
  return !dps[key].isNull();
}

}  // namespace

void profilesInit() {
  if (initialized) return;
  initBuiltins();
  if (!customProfiles) {
    customProfiles = static_cast<ProfileDef *>(
        calloc(PROFILE_MAX_CUSTOM, sizeof(ProfileDef)));
    if (!customProfiles) {
      Serial.println("[profile] not enough memory for custom profiles");
    }
  }
  clearCustom();
  initialized = true;
}

void profilesLoadCustomFromNvs() {
  ensureInit();

  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, true)) {
    Serial.println("[profile] could not open NVS");
    return;
  }
  const String json = prefs.getString(NVS_PROFILES_KEY, "");
  prefs.end();

  if (json.length() == 0) {
    clearCustom();
    return;
  }
  if (json.length() > PROFILE_JSON_MAX) {
    Serial.printf("[profile] custom JSON is too large (%u bytes)\n",
                  static_cast<unsigned>(json.length()));
    clearCustom();
    return;
  }

  copyText(customJson, sizeof(customJson), json.c_str());
  size_t count = 0;
  String error;
  if (!customProfiles) {
    customCount = 0;
    return;
  }
  memset(customProfiles, 0, CUSTOM_PROFILES_BYTES);
  if (!parseCustomProfiles(customJson, customProfiles, count, parseScratch,
                           error)) {
    Serial.printf("[profile] custom JSON invalid: %s\n", error.c_str());
    memset(customProfiles, 0, CUSTOM_PROFILES_BYTES);
    customCount = 0;
    return;
  }

  customCount = count;
  Serial.printf("[profile] loaded %u custom profile(s)\n",
                static_cast<unsigned>(customCount));
}

bool profilesSaveCustomToNvs(const char *json, String &error) {
  ensureInit();

  if (!json) json = "";
  const bool blank = isBlank(json);
  if (!blank && strlen(json) > PROFILE_JSON_MAX) {
    error = String("custom profiles JSON must be at most ") +
            String(PROFILE_JSON_MAX) + " bytes";
    return false;
  }

  if (!blank && !customProfiles) {
    error = "not enough memory for custom profiles";
    return false;
  }

  size_t count = 0;
  if (!blank &&
      !parseCustomProfiles(json, nullptr, count, parseScratch, error)) {
    return false;
  }

  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, false)) {
    error = "could not open NVS";
    return false;
  }

  bool ok = true;
  if (blank) {
    if (prefs.isKey(NVS_PROFILES_KEY)) {
      ok = prefs.remove(NVS_PROFILES_KEY);
    }
  } else {
    ok = prefs.putString(NVS_PROFILES_KEY, json) > 0;
  }
  prefs.end();

  if (!ok) {
    error = "could not write custom profiles to NVS";
    return false;
  }

  if (blank) {
    clearCustom();
    return true;
  }

  memset(customProfiles, 0, CUSTOM_PROFILES_BYTES);
  if (!parseCustomProfiles(json, customProfiles, count, parseScratch, error)) {
    clearCustom();
    return false;
  }
  customCount = count;
  copyText(customJson, sizeof(customJson), json);
  return true;
}

bool profilesValidateCustomJson(const char *json, String &error) {
  ensureInit();
  if (!json || isBlank(json)) return true;
  if (strlen(json) > PROFILE_JSON_MAX) {
    error = String("custom profiles JSON must be at most ") +
            String(PROFILE_JSON_MAX) + " bytes";
    return false;
  }
  size_t count = 0;
  return parseCustomProfiles(json, nullptr, count, parseScratch, error);
}

bool profilesSelectionValid(const char *selection, const char *customJsonText,
                            String &error) {
  ensureInit();
  if (!selection || !selection[0]) {
    error = "select a profile";
    return false;
  }
  if (strcmp(selection, "auto") == 0) return true;
  if (findBuiltin(selection)) return true;
  if (browserProfileLoaded() && strcmp(selection, browserProfileId()) == 0) {
    return true;
  }

  if (!customJsonText || isBlank(customJsonText)) {
    error = String("profile '") + selection +
            "' is not a built-in profile and no custom profile defines it";
    return false;
  }

  size_t count = 0;
  String parseError;
  bool found = false;
  if (!parseCustomProfiles(customJsonText, nullptr, count, parseScratch,
                           parseError, selection, &found)) {
    error = String("custom profiles: ") + parseError;
    return false;
  }
  if (found) return true;
  error = String("profile '") + selection +
          "' is not defined by the built-in profiles or the custom JSON";
  return false;
}

void profilesClearCustom() {
  ensureInit();
  clearCustom();
}

const char *profilesCustomJson() { return customJson; }

size_t profilesCount() {
  ensureInit();
  return 3 + (customProfiles ? customCount : 0);
}

const ProfileDef *profilesAt(size_t index) {
  ensureInit();
  if (index < 3) return &builtinProfiles[index];
  index -= 3;
  if (!customProfiles || index >= customCount) return nullptr;
  return &customProfiles[index];
}

const ProfileDef *profilesFallback() {
  ensureInit();
  return &fallbackProfile;
}

const ProfileDef *profileFind(const char *id) {
  ensureInit();
  if (!id || !*id) return nullptr;
  const ProfileDef *builtin = findBuiltin(id);
  if (builtin) return builtin;
  if (!customProfiles) return nullptr;
  for (size_t i = 0; i < customCount; ++i) {
    if (strcmp(customProfiles[i].id, id) == 0) return &customProfiles[i];
  }
  return nullptr;
}

bool profileMatches(const ProfileDef &profile, JsonObject dps) {
  if (profile.detect.requireCount == 0) return true;
  for (uint8_t i = 0; i < profile.detect.requireCount; ++i) {
    if (!hasDp(dps, profile.detect.require[i])) return false;
  }
  for (uint8_t i = 0; i < profile.detect.excludeCount; ++i) {
    if (hasDp(dps, profile.detect.exclude[i])) return false;
  }
  return true;
}

const ProfileDef *profileAutoDetect(JsonObject dps) {
  ensureInit();
  const ProfileDef *best = &fallbackProfile;
  uint8_t bestPriority = 0;
  bool found = false;

  const size_t count = profilesCount();
  for (size_t i = 0; i < count; ++i) {
    const ProfileDef *profile = profilesAt(i);
    if (!profile || profile->detect.requireCount == 0) continue;
    if (!profileMatches(*profile, dps)) continue;
    if (!found || profile->detect.priority > bestPriority) {
      best = profile;
      bestPriority = profile->detect.priority;
      found = true;
    }
  }
  return best;
}

String profileOptions(const char *selected) {
  ensureInit();
  String out;
  bool found = false;

  out += "<option value=\"auto\"";
  if (selected && strcmp(selected, "auto") == 0) {
    out += " selected";
    found = true;
  }
  out += ">Auto-detect</option>";

  if (browserProfileLoaded()) {
    out += "<option value=\"";
    out += htmlEscape(browserProfileId());
    out += "\"";
    if (selected && strcmp(selected, browserProfileId()) == 0) {
      out += " selected";
      found = true;
    }
    if (browserProfileDeviceType()[0]) {
      out += " title=\"";
      out += htmlEscape(browserProfileDeviceType());
      out += "\"";
    }
    out += ">";
    out += htmlEscape(browserProfileName());
    out += " (browser pack)</option>";
  }

  const size_t count = profilesCount();
  for (size_t i = 0; i < count; ++i) {
    const ProfileDef *profile = profilesAt(i);
    if (!profile) continue;
    out += "<option value=\"";
    out += htmlEscape(profile->id);
    out += "\"";
    if (selected && strcmp(selected, profile->id) == 0) {
      out += " selected";
      found = true;
    }
    if (profile->description[0]) {
      out += " title=\"";
      out += htmlEscape(profile->description);
      out += "\"";
    }
    out += ">";
    out += htmlEscape(profile->name);
    out += " (";
    out += htmlEscape(profile->id);
    out += ")</option>";
  }

  if (selected && selected[0] && !found) {
    out += "<option value=\"";
    out += htmlEscape(selected);
    out += "\" selected>Missing: ";
    out += htmlEscape(selected);
    out += "</option>";
  }
  return out;
}

void profilesSetBuiltinControl(const char *id, uint16_t chargeDp,
                               uint16_t currentDp, uint16_t delayDp,
                               uint16_t modeDp) {
  ensureInit();
  if (!id) return;
  for (auto &profile : builtinProfiles) {
    if (strcmp(profile.id, id) != 0) continue;
    profile.control.chargeDp = chargeDp;
    profile.control.currentDp = currentDp;
    profile.control.delayDp = delayDp;
    profile.control.modeDp = modeDp;
    if (chargeDp == 0) profile.control.chargeType = PCT_NONE;
    return;
  }
}

}  // namespace tuya_lora
