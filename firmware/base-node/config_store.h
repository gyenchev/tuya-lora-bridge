#pragma once

#include <Arduino.h>

namespace tuya_lora {

// Runtime configuration for the base node. Values are stored in ESP32 NVS
// (the non-volatile "EEPROM" on this platform) and are edited through the web
// UI.
struct BaseSettings {
  // Network / UI
  uint16_t webUiPort;
  char setupApSsid[33];
  char setupApPassword[65];
  uint8_t setupApChannel;
  uint32_t wifiSetupFallbackMs;

  // SD-card update
  char sdFirmwareFile[32];
  uint32_t sdUpdateWaitMs;

  // WiFi / MQTT / Home Assistant
  char wifiSsid[33];
  char wifiPassword[65];
  char mqttHost[64];
  uint16_t mqttPort;
  char mqttUser[33];
  char mqttPassword[65];
  char loraPsk[33];
  char haName[33];
  char timezone[64];

  // LoRa radio
  float loraFrequencyMhz;
  uint8_t loraSyncWord;
  int8_t loraTxPower;
  uint8_t loraSf;
  uint32_t loraBandwidthHz;
  uint8_t loraCodingRate;
  uint16_t loraPreamble;

  // Timing
  uint32_t offlineAfterMs;
  uint32_t dpQueryIntervalMs;
  uint32_t dpQueryTimeoutMs;
  uint32_t dpChunkTimeoutMs;
  uint8_t dpAckRetries;
  uint32_t wifiRetryMs;
  uint32_t mqttReconnectMs;
  uint16_t mqttKeepaliveS;
  uint16_t mqttSocketTimeoutS;
  uint16_t mqttBufferSize;
  uint32_t displayRefreshMs;
  uint32_t displayOnMs;
  uint32_t splashMs;

  // MQTT topics
  char mqttTopicState[64];
  char mqttTopicLink[64];
  char mqttTopicAvailability[64];
  char mqttTopicResult[64];
  char mqttTopicDebug[64];
  char mqttTopicDps[64];
  char mqttTopicCmdCharge[64];
  char mqttTopicCmdStart[64];
  char mqttTopicCmdStop[64];
  char mqttTopicCmdCurrent[64];
  char mqttTopicCmdDelay[64];
  char mqttTopicCmdMode[64];
  char mqttTopicCmdQuery[64];

  // Home Assistant discovery
  char haDiscoveryPrefix[33];
  char haDeviceId[33];

  // Control
  bool enableCommands;
  uint8_t minCurrent;
  uint8_t maxCurrent;
  uint8_t minDelayH;
  uint8_t maxDelayH;
  uint16_t dpSingleChargeId;
  uint16_t dpSingleCurrentId;
  uint16_t dpSingleDelayId;
  uint16_t dpSingleModeId;
  uint16_t dpThreeChargeId;
  uint16_t dpThreeCurrentId;
  uint16_t dpThreeDelayId;
  uint16_t dpThreeModeId;
  uint16_t dpObz2ChargeId;
  uint16_t dpObz2CurrentId;
};

extern BaseSettings settings;

void loadSettings();
bool saveSettings();
void clearSettings();

bool wifiConfigured();
bool mqttConfigured();
bool loraPskConfigured();
bool isValidPsk(const char *psk);

}  // namespace tuya_lora
