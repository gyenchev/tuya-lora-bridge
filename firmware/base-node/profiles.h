#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

namespace tuya_lora {

// A single Tuya DP field used by a profile. `scale` converts the raw DP value
// to the base unit used by the state model (V, A, W, Wh, °C or hours).
enum ProfileFieldType : uint8_t {
  PF_NONE = 0,
  PF_INT,
  PF_BOOL,
  PF_TEXT,
  PF_TEXT_CONTAINS,
  PF_DURATION_HMS,
  PF_DURATION_HOURS,
  PF_DURATION_TENTH_HOURS,
  PF_VOLTAGE_AUTO,
};

enum ProfileChargeType : uint8_t {
  PCT_NONE = 0,
  PCT_BOOL,
  PCT_STRING,
  PCT_INT,
};

struct ProfileField {
  uint16_t dp = 0;
  uint8_t type = PF_NONE;
  float scale = 1.0f;
  char match[32] = {0};

  bool present() const { return dp != 0 && type != PF_NONE; }
};

struct ProfileState {
  ProfileField status;
  ProfileField statusFallback;
  ProfileField statusAuth;
  ProfileField fault;
  ProfileField voltageL1;
  ProfileField voltageL2;
  ProfileField voltageL3;
  ProfileField currentL1;
  ProfileField currentL2;
  ProfileField currentL3;
  ProfileField power;
  ProfileField energy;
  ProfileField sessionEnergy;
  ProfileField temperature;
  ProfileField currentLimit;
  ProfileField currentLimitAlt;
  ProfileField chargeSeconds;
  ProfileField chargeSecondsAlt;
  ProfileField chargeEnable;
  ProfileField rfid;
  ProfileField schedule;
  ProfileField delay;
};

struct ProfileControl {
  uint16_t chargeDp = 0;
  uint8_t chargeType = PCT_NONE;
  char chargeOn[24] = {0};
  char chargeOff[24] = {0};
  uint16_t currentDp = 0;
  uint16_t delayDp = 0;
  bool delaySetsMode = false;
  uint16_t modeDp = 0;
  char modeNow[24] = {0};
  char modeSchedule[24] = {0};

  bool hasCharge() const {
    return chargeDp != 0 && chargeType != PCT_NONE;
  }
  bool hasCurrent() const { return currentDp != 0; }
  bool hasDelay() const { return delayDp != 0; }
  bool hasMode() const { return modeDp != 0; }
};

struct ProfileDetect {
  uint16_t require[8] = {0};
  uint8_t requireCount = 0;
  uint16_t exclude[8] = {0};
  uint8_t excludeCount = 0;
  uint8_t priority = 0;
};

// Generic tuya-local-style entity descriptors. These are intentionally a
// compact subset of the tuya-local YAML schema: enough to expose common Tuya
// DPs as Home Assistant entities without hardcoding a charger-specific state
// model.
enum ProfileDpType : uint8_t {
  PDT_STRING = 0,
  PDT_BOOL,
  PDT_INT,
  PDT_FLOAT,
  PDT_BITFIELD,
};

enum ProfileValueType : uint8_t {
  PVT_STRING = 0,
  PVT_BOOL,
  PVT_INT,
  PVT_FLOAT,
};

enum ProfileEntityType : uint8_t {
  PET_SENSOR = 0,
  PET_BINARY_SENSOR,
  PET_SWITCH,
  PET_NUMBER,
  PET_SELECT,
  PET_BUTTON,
  PET_TEXT,
};

enum ProfileDpFlags : uint8_t {
  PDF_OPTIONAL = 1 << 0,
  PDF_READONLY = 1 << 1,
  PDF_HIDDEN = 1 << 2,
};

enum ProfileEntityFlags : uint8_t {
  PEF_HIDDEN = 1 << 0,
};

struct ProfileMapping {
  char from[24] = {0};  // canonical raw DP value; empty means default rule
  char to[48] = {0};    // canonical Home Assistant value
  uint8_t toType = PVT_STRING;
  bool hasFrom = false;
  bool hidden = false;
};

struct ProfileDp {
  uint16_t dp = 0;
  uint8_t type = PDT_INT;
  uint8_t mappingStart = 0;
  uint8_t mappingCount = 0;
  uint8_t flags = 0;
  float scale = 1.0f;  // tuya-local style divisor
  float min = 0.0f;
  float max = 0.0f;
  float step = 1.0f;
  bool hasRange = false;
  char name[24] = {0};

  bool present() const { return dp != 0; }
  bool optional() const { return (flags & PDF_OPTIONAL) != 0; }
  bool readonly() const { return (flags & PDF_READONLY) != 0; }
  bool hidden() const { return (flags & PDF_HIDDEN) != 0; }
};

struct ProfileEntity {
  uint8_t type = PET_SENSOR;
  uint8_t dpCount = 0;
  uint8_t flags = 0;
  int8_t precision = -1;
  char key[6] = {0};
  char name[32] = {0};
  char deviceClass[32] = {0};
  char category[12] = {0};
  char icon[36] = {0};
  char unit[12] = {0};
  char stateClass[20] = {0};
  ProfileDp dps[4];

  bool hidden() const { return (flags & PEF_HIDDEN) != 0; }
};

struct ProfileDef {
  char id[24] = {0};
  char name[40] = {0};
  char description[96] = {0};
  uint8_t phases = 0;
  ProfileDetect detect;
  ProfileState state;
  ProfileControl control;

  uint8_t genericEntityCount = 0;
  uint8_t mappingCount = 0;
  ProfileEntity entities[10];
  ProfileMapping mappings[32];

  bool hasGenericEntities() const { return genericEntityCount > 0; }
};

constexpr size_t PROFILE_MAX_CUSTOM = 4;
constexpr size_t PROFILE_MAX_ENTITIES = 10;
constexpr size_t PROFILE_MAX_DP_PER_ENTITY = 4;
constexpr size_t PROFILE_MAX_MAPPINGS = 32;
constexpr size_t PROFILE_JSON_MAX = 8192;

void profilesInit();
void profilesLoadCustomFromNvs();
bool profilesSaveCustomToNvs(const char *json, String &error);
bool profilesValidateCustomJson(const char *json, String &error);
bool profilesSelectionValid(const char *selection, const char *customJson,
                            String &error);
void profilesClearCustom();
const char *profilesCustomJson();

size_t profilesCount();
const ProfileDef *profilesAt(size_t index);
const ProfileDef *profilesFallback();
const ProfileDef *profileFind(const char *id);
const ProfileDef *profileAutoDetect(JsonObject dps);
bool profileMatches(const ProfileDef &profile, JsonObject dps);
String profileOptions(const char *selected);

// Apply the legacy per-profile command DP settings to a built-in profile.
// Custom profiles always use the control map from their JSON definition.
void profilesSetBuiltinControl(const char *id, uint16_t chargeDp,
                               uint16_t currentDp, uint16_t delayDp,
                               uint16_t modeDp);

}  // namespace tuya_lora
