#include "boot_guard.h"

#include <Preferences.h>

#include "config.h"

namespace tuya_lora {

namespace {

constexpr const char *KEY_RAPID_BOOTS = "rapid_boots";
constexpr const char *KEY_FORCE_SETUP = "force_setup";

bool forceSetup = false;
uint32_t bootStartedMs = 0;
bool healthy = false;

}  // namespace

void bootGuardOnBoot() {
  bootStartedMs = millis();
  healthy = false;
  forceSetup = false;

  Preferences prefs;
  if (!prefs.begin(BOOT_GUARD_NVS_NAMESPACE, false)) {
    Serial.println("[guard] NVS open failed");
    return;
  }

  uint8_t count = prefs.getUChar(KEY_RAPID_BOOTS, 0);
  bool forced = prefs.getBool(KEY_FORCE_SETUP, false);
  if (count < 255) count++;
  if (count >= BOOT_GUARD_RESET_THRESHOLD) {
    count = BOOT_GUARD_RESET_THRESHOLD;
    forced = true;
  }
  prefs.putUChar(KEY_RAPID_BOOTS, count);
  prefs.putBool(KEY_FORCE_SETUP, forced);
  prefs.end();

  forceSetup = forced;
  Serial.printf("[guard] rapid boots %u/%u%s\n", count,
                BOOT_GUARD_RESET_THRESHOLD,
                forced ? " -> setup AP recovery" : "");
}

bool bootGuardForceSetupAp() { return forceSetup; }

void bootGuardLoop() {
  if (healthy || millis() - bootStartedMs < BOOT_GUARD_HEALTHY_MS) return;
  healthy = true;

  Preferences prefs;
  if (!prefs.begin(BOOT_GUARD_NVS_NAMESPACE, false)) return;
  prefs.putUChar(KEY_RAPID_BOOTS, 0);
  prefs.end();
  Serial.println("[guard] healthy uptime; rapid-boot counter cleared");
}

void bootGuardClear() {
  forceSetup = false;
  healthy = true;

  Preferences prefs;
  if (!prefs.begin(BOOT_GUARD_NVS_NAMESPACE, false)) return;
  prefs.remove(KEY_RAPID_BOOTS);
  prefs.remove(KEY_FORCE_SETUP);
  prefs.end();
  Serial.println("[guard] recovery state cleared");
}

}  // namespace tuya_lora
