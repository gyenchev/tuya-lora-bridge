#include "lora_trial.h"

#include <Preferences.h>

namespace tuya_lora {

namespace {

constexpr const char *TRIAL_NAMESPACE = "tuya-lora-trial";
constexpr uint8_t TRIAL_VERSION = 1;

void writeConfig(Preferences &prefs, const char *prefix,
                 const LoraConfig &config) {
  String base(prefix);
  prefs.putFloat((base + "f").c_str(), config.frequencyMhz);
  prefs.putUChar((base + "s").c_str(), config.syncWord);
  prefs.putChar((base + "p").c_str(), config.txPower);
  prefs.putUChar((base + "sf").c_str(), config.sf);
  prefs.putULong((base + "bw").c_str(), config.bandwidthHz);
  prefs.putUChar((base + "cr").c_str(), config.codingRate);
  prefs.putUShort((base + "pre").c_str(), config.preamble);
  prefs.putString((base + "k").c_str(), config.psk);
}

void readConfig(Preferences &prefs, const char *prefix, LoraConfig &config) {
  String base(prefix);
  config.frequencyMhz = prefs.getFloat((base + "f").c_str(), config.frequencyMhz);
  config.syncWord = prefs.getUChar((base + "s").c_str(), config.syncWord);
  config.txPower = prefs.getChar((base + "p").c_str(), config.txPower);
  config.sf = prefs.getUChar((base + "sf").c_str(), config.sf);
  config.bandwidthHz =
      prefs.getULong((base + "bw").c_str(), config.bandwidthHz);
  config.codingRate =
      prefs.getUChar((base + "cr").c_str(), config.codingRate);
  config.preamble = prefs.getUShort((base + "pre").c_str(), config.preamble);
  const String psk = prefs.getString((base + "k").c_str(), "");
  strncpy(config.psk, psk.c_str(), sizeof(config.psk) - 1);
  config.psk[sizeof(config.psk) - 1] = '\0';
}

}  // namespace

bool loraTrialLoad(LoraTrial &out) {
  out = LoraTrial{};
  Preferences prefs;
  if (!prefs.begin(TRIAL_NAMESPACE, true)) return false;
  const bool active = prefs.getUChar("ver", 0) == TRIAL_VERSION &&
                      prefs.getBool("active", false);
  if (active) {
    out.active = true;
    out.applied = prefs.getBool("appl", false);
    out.configId = prefs.getULong("id", 0);
    readConfig(prefs, "old_", out.oldConfig);
    readConfig(prefs, "new_", out.newConfig);
  }
  prefs.end();
  return out.active;
}

bool loraTrialSave(const LoraTrial &in) {
  Preferences prefs;
  if (!prefs.begin(TRIAL_NAMESPACE, false)) return false;
  prefs.clear();
  bool ok = prefs.putUChar("ver", TRIAL_VERSION) > 0;
  ok = prefs.putBool("active", in.active) && ok;
  ok = prefs.putBool("appl", in.applied) && ok;
  ok = prefs.putULong("id", in.configId) > 0 && ok;
  writeConfig(prefs, "old_", in.oldConfig);
  writeConfig(prefs, "new_", in.newConfig);
  prefs.end();
  return ok;
}

void loraTrialClear() {
  Preferences prefs;
  if (prefs.begin(TRIAL_NAMESPACE, false)) {
    prefs.clear();
    prefs.end();
  }
}

}  // namespace tuya_lora
