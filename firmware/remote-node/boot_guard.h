#pragma once

#include <Arduino.h>

namespace tuya_lora {

// Call once at boot, after loadSettings() and after any SD update check.
void bootGuardOnBoot();

// True when repeated short boots were detected and the setup AP must stay up.
bool bootGuardForceSetupAp();

// Call from the main loop. Clears the rapid-boot counter after healthy uptime.
void bootGuardLoop();

// Clear recovery state after settings are saved or a factory reset.
void bootGuardClear();

}  // namespace tuya_lora
