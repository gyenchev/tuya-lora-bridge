#pragma once

#include <Arduino.h>

#include "lora_protocol.h"

namespace tuya_lora {

// Persisted old/new configuration while a LoRa config change is being committed.
struct LoraTrial {
  bool active = false;
  bool applied = false;
  uint32_t configId = 0;
  LoraConfig oldConfig;
  LoraConfig newConfig;
};

bool loraTrialLoad(LoraTrial &out);
bool loraTrialSave(const LoraTrial &in);
void loraTrialClear();

}  // namespace tuya_lora
