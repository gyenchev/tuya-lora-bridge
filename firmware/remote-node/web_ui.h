#pragma once

#include <Arduino.h>
#include <IPAddress.h>

namespace tuya_lora {

void webUiBegin();
void webUiLoop();

bool webUiSetupApActive();
IPAddress webUiApIp();

}  // namespace tuya_lora
