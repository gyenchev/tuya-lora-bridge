#pragma once

#include <Arduino.h>

namespace tuya_lora {

// Runtime configuration for the remote node. Stored in ESP32 NVS and edited
// through the web UI.
struct RemoteSettings {
  // Network / UI
  uint16_t webUiPort;
  char setupApSsid[33];
  char setupApPassword[65];
  uint8_t setupApChannel;

  // SD-card update
  char sdFirmwareFile[32];
  uint32_t sdUpdateWaitMs;

  // Device AP
  char apSsid[33];
  char apPassword[65];
  uint8_t apChannel;
  uint8_t apMaxClients;

  // Tuya
  char tuyaDeviceId[64];
  char tuyaLocalKey[17];
  float tuyaVersion;
  char tuyaStaticIp[16];
  uint16_t tuyaPort;

  // Discovery / timing
  uint32_t scanProbeTimeoutMs;
  uint32_t scanIntervalMs;
  uint32_t stationRefreshMs;
  uint32_t deviceLostGraceMs;
  uint32_t tuyaConnectTimeoutMs;
  uint32_t tuyaReadTimeoutMs;
  uint32_t tuyaPollIntervalMs;
  uint32_t tuyaReconnectIntervalMs;

  // LoRa link
  char loraPsk[33];
  float loraFrequencyMhz;
  uint8_t loraSyncWord;
  int8_t loraTxPower;
  uint8_t loraSf;
  uint32_t loraBandwidthHz;
  uint8_t loraCodingRate;
  uint16_t loraPreamble;
  uint32_t dpAckTimeoutMs;
  uint8_t dpAckRetries;

  // Display
  uint32_t splashMs;
  uint32_t displayRefreshMs;
  uint32_t displayOnMs;
  uint32_t dpPageIntervalMs;
  uint8_t dpPerPage;
};

extern RemoteSettings settings;

void loadSettings();
bool saveSettings();
void clearSettings();

bool apConfigured();
bool tuyaConfigured();
bool loraPskConfigured();
bool isValidPsk(const char *psk);

}  // namespace tuya_lora
