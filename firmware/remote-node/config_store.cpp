#include "config_store.h"

#include <Preferences.h>
#include <string.h>

#include "config.h"

namespace tuya_lora {

namespace {

// NVS namespace names are limited to 15 characters.
constexpr const char *NVS_NAMESPACE = "tuya-lora-rem";
constexpr uint8_t CONFIG_VERSION = 3;

void copyText(char *dst, size_t dstSize, const char *src) {
  if (!dst || dstSize == 0) return;
  if (!src) {
    dst[0] = '\0';
    return;
  }
  strncpy(dst, src, dstSize - 1);
  dst[dstSize - 1] = '\0';
}

bool putText(Preferences &prefs, const char *key, const char *value) {
  if (!value || value[0] == '\0') {
    // Preferences::putString() returns the string length, so an empty string
    // reports 0 even when it succeeds. Remove the key instead so an old value
    // cannot survive, and treat an absent key as success.
    return !prefs.isKey(key) || prefs.remove(key);
  }
  return prefs.putString(key, value) > 0;
}

void setDefaults(RemoteSettings &s) {
  memset(&s, 0, sizeof(s));

  s.webUiPort = WEB_UI_PORT;
  copyText(s.setupApSsid, sizeof(s.setupApSsid), SETUP_AP_SSID);
  copyText(s.setupApPassword, sizeof(s.setupApPassword), SETUP_AP_PASSWORD);
  s.setupApChannel = SETUP_AP_CHANNEL;

  copyText(s.sdFirmwareFile, sizeof(s.sdFirmwareFile), SD_FIRMWARE_FILE);
  s.sdUpdateWaitMs = SD_UPDATE_WAIT_MS;

  s.apChannel = 6;
  s.apMaxClients = AP_MAX_CLIENTS;
  s.tuyaVersion = 0.0f;
  s.tuyaPort = TUYA_PORT;

  s.scanProbeTimeoutMs = SCAN_PROBE_TIMEOUT_MS;
  s.scanIntervalMs = SCAN_INTERVAL_MS;
  s.stationRefreshMs = STATION_REFRESH_MS;
  s.deviceLostGraceMs = DEVICE_LOST_GRACE_MS;
  s.tuyaConnectTimeoutMs = TUYA_CONNECT_TIMEOUT_MS;
  s.tuyaReadTimeoutMs = TUYA_READ_TIMEOUT_MS;
  s.tuyaPollIntervalMs = TUYA_POLL_INTERVAL_MS;
  s.tuyaReconnectIntervalMs = TUYA_RECONNECT_INTERVAL_MS;

  s.loraFrequencyMhz = LORA_FREQUENCY / 1000000.0f;
  s.loraSyncWord = LORA_SYNC_WORD;
  s.loraTxPower = LORA_TX_POWER;
  s.loraSf = LORA_SF;
  s.loraBandwidthHz = LORA_BW;
  s.loraCodingRate = LORA_CR;
  s.loraPreamble = LORA_PREAMBLE;
  s.dpAckTimeoutMs = DP_ACK_TIMEOUT_MS;
  s.dpAckRetries = DP_ACK_RETRIES;

  s.displayRefreshMs = DISPLAY_REFRESH_MS;
  s.displayOnMs = DISPLAY_ON_MS;
  s.splashMs = SPLASH_MS;
  s.dpPageIntervalMs = DP_PAGE_INTERVAL_MS;
  s.dpPerPage = DP_PER_PAGE;
}

void readText(Preferences &prefs, const char *key, char *dst, size_t dstSize) {
  const String value = prefs.getString(key, "");
  copyText(dst, dstSize, value.c_str());
}

void sanitize(RemoteSettings &s) {
  if (s.webUiPort == 0) s.webUiPort = WEB_UI_PORT;
  if (s.setupApSsid[0] == '\0') {
    copyText(s.setupApSsid, sizeof(s.setupApSsid), SETUP_AP_SSID);
  }
  if (strlen(s.setupApPassword) < 8) {
    copyText(s.setupApPassword, sizeof(s.setupApPassword), SETUP_AP_PASSWORD);
  }
  if (strlen(s.setupApPassword) > 63) s.setupApPassword[63] = '\0';
  if (s.setupApChannel < 1 || s.setupApChannel > 13) {
    s.setupApChannel = SETUP_AP_CHANNEL;
  }
  if (s.sdFirmwareFile[0] != '/') {
    copyText(s.sdFirmwareFile, sizeof(s.sdFirmwareFile), SD_FIRMWARE_FILE);
  }
  if (s.sdUpdateWaitMs < 1000) s.sdUpdateWaitMs = SD_UPDATE_WAIT_MS;

  if (s.apChannel < 1 || s.apChannel > 13) s.apChannel = 6;
  if (s.apMaxClients < 1 || s.apMaxClients > AP_MAX_CLIENTS) {
    s.apMaxClients = AP_MAX_CLIENTS;
  }
  if (s.tuyaPort == 0) s.tuyaPort = TUYA_PORT;
  if (s.tuyaVersion < 0.0f || s.tuyaVersion > 4.0f) s.tuyaVersion = 0.0f;

  if (s.scanProbeTimeoutMs < 10) {
    s.scanProbeTimeoutMs = SCAN_PROBE_TIMEOUT_MS;
  }
  if (s.scanIntervalMs < 1000) s.scanIntervalMs = SCAN_INTERVAL_MS;
  if (s.stationRefreshMs < 100) s.stationRefreshMs = STATION_REFRESH_MS;
  if (s.tuyaConnectTimeoutMs < 100) {
    s.tuyaConnectTimeoutMs = TUYA_CONNECT_TIMEOUT_MS;
  }
  if (s.tuyaReadTimeoutMs < 100) {
    s.tuyaReadTimeoutMs = TUYA_READ_TIMEOUT_MS;
  }
  if (s.tuyaPollIntervalMs < 1000) {
    s.tuyaPollIntervalMs = TUYA_POLL_INTERVAL_MS;
  }
  if (s.tuyaReconnectIntervalMs < 1000) {
    s.tuyaReconnectIntervalMs = TUYA_RECONNECT_INTERVAL_MS;
  }

  if (s.loraFrequencyMhz < 137.0f || s.loraFrequencyMhz > 1020.0f) {
    s.loraFrequencyMhz = LORA_FREQUENCY / 1000000.0f;
  }
  if (s.loraSf < 6 || s.loraSf > 12) s.loraSf = LORA_SF;
  if (s.loraTxPower < -1 || s.loraTxPower > 20) s.loraTxPower = LORA_TX_POWER;
  if (s.loraBandwidthHz < 7800 || s.loraBandwidthHz > 500000) {
    s.loraBandwidthHz = LORA_BW;
  }
  if (s.loraCodingRate < 5 || s.loraCodingRate > 8) {
    s.loraCodingRate = LORA_CR;
  }
  if (s.loraPreamble < 6) s.loraPreamble = LORA_PREAMBLE;
  if (s.dpAckTimeoutMs < 100) s.dpAckTimeoutMs = DP_ACK_TIMEOUT_MS;
  if (s.dpAckRetries > 20) s.dpAckRetries = DP_ACK_RETRIES;

  if (s.displayRefreshMs < 100) s.displayRefreshMs = DISPLAY_REFRESH_MS;
  if (s.displayOnMs < 1000) s.displayOnMs = DISPLAY_ON_MS;
  if (s.splashMs > 60000) s.splashMs = SPLASH_MS;
  if (s.dpPageIntervalMs < 500) s.dpPageIntervalMs = DP_PAGE_INTERVAL_MS;
  if (s.dpPerPage < 1 || s.dpPerPage > 8) s.dpPerPage = DP_PER_PAGE;
}

}  // namespace

RemoteSettings settings;

void loadSettings() {
  Preferences prefs;
  setDefaults(settings);

  bool haveConfig = false;
  if (prefs.begin(NVS_NAMESPACE, true)) {
    haveConfig = prefs.getUChar("cfg_ver", 0) == CONFIG_VERSION;
    if (haveConfig) {
      settings.webUiPort = prefs.getUShort("web_port", settings.webUiPort);
      readText(prefs, "setup_ssid", settings.setupApSsid,
               sizeof(settings.setupApSsid));
      readText(prefs, "setup_pass", settings.setupApPassword,
               sizeof(settings.setupApPassword));
      settings.setupApChannel =
          prefs.getUChar("setup_chan", settings.setupApChannel);

      readText(prefs, "sd_file", settings.sdFirmwareFile,
               sizeof(settings.sdFirmwareFile));
      settings.sdUpdateWaitMs =
          prefs.getULong("sd_wait_ms", settings.sdUpdateWaitMs);

      readText(prefs, "ap_ssid", settings.apSsid, sizeof(settings.apSsid));
      readText(prefs, "ap_pass", settings.apPassword,
               sizeof(settings.apPassword));
      settings.apChannel = prefs.getUChar("ap_channel", settings.apChannel);
      settings.apMaxClients =
          prefs.getUChar("ap_max", settings.apMaxClients);

      readText(prefs, "tuya_dev_id", settings.tuyaDeviceId,
               sizeof(settings.tuyaDeviceId));
      readText(prefs, "tuya_key", settings.tuyaLocalKey,
               sizeof(settings.tuyaLocalKey));
      settings.tuyaVersion = prefs.getFloat("tuya_ver", settings.tuyaVersion);
      readText(prefs, "tuya_ip", settings.tuyaStaticIp,
               sizeof(settings.tuyaStaticIp));
      settings.tuyaPort = prefs.getUShort("tuya_port", settings.tuyaPort);

      settings.scanProbeTimeoutMs =
          prefs.getULong("scan_probe", settings.scanProbeTimeoutMs);
      settings.scanIntervalMs =
          prefs.getULong("scan_int", settings.scanIntervalMs);
      settings.stationRefreshMs =
          prefs.getULong("sta_ref", settings.stationRefreshMs);
      settings.deviceLostGraceMs =
          prefs.getULong("lost_ms", settings.deviceLostGraceMs);
      settings.tuyaConnectTimeoutMs =
          prefs.getULong("tuya_ct_ms", settings.tuyaConnectTimeoutMs);
      settings.tuyaReadTimeoutMs =
          prefs.getULong("tuya_rd_ms", settings.tuyaReadTimeoutMs);
      settings.tuyaPollIntervalMs =
          prefs.getULong("tuya_p_ms", settings.tuyaPollIntervalMs);
      settings.tuyaReconnectIntervalMs =
          prefs.getULong("tuya_r_ms", settings.tuyaReconnectIntervalMs);

      readText(prefs, "lora_psk", settings.loraPsk, sizeof(settings.loraPsk));
      settings.loraFrequencyMhz =
          prefs.getFloat("lora_freq", settings.loraFrequencyMhz);
      settings.loraSyncWord =
          prefs.getUChar("lora_sync", settings.loraSyncWord);
      settings.loraTxPower = prefs.getChar("lora_txp", settings.loraTxPower);
      settings.loraSf = prefs.getUChar("lora_sf", settings.loraSf);
      settings.loraBandwidthHz =
          prefs.getULong("lora_bw", settings.loraBandwidthHz);
      settings.loraCodingRate =
          prefs.getUChar("lora_cr", settings.loraCodingRate);
      settings.loraPreamble =
          prefs.getUShort("lora_pre", settings.loraPreamble);
      settings.dpAckTimeoutMs =
          prefs.getULong("dp_ack_ms", settings.dpAckTimeoutMs);
      settings.dpAckRetries =
          prefs.getUChar("dp_retry", settings.dpAckRetries);

      settings.displayRefreshMs =
          prefs.getULong("dsp_r_ms", settings.displayRefreshMs);
      settings.displayOnMs =
          prefs.getULong("dsp_on_ms", settings.displayOnMs);
      settings.splashMs = prefs.getULong("splash_ms", settings.splashMs);
      settings.dpPageIntervalMs =
          prefs.getULong("dp_page_ms", settings.dpPageIntervalMs);
      settings.dpPerPage = prefs.getUChar("dp_page", settings.dpPerPage);
    }
    prefs.end();
  }

  sanitize(settings);

  if (!haveConfig) {
    saveSettings();
  }

  settings.setupApSsid[sizeof(settings.setupApSsid) - 1] = '\0';
  settings.setupApPassword[sizeof(settings.setupApPassword) - 1] = '\0';
  settings.sdFirmwareFile[sizeof(settings.sdFirmwareFile) - 1] = '\0';
  settings.apSsid[sizeof(settings.apSsid) - 1] = '\0';
  settings.apPassword[sizeof(settings.apPassword) - 1] = '\0';
  settings.tuyaDeviceId[sizeof(settings.tuyaDeviceId) - 1] = '\0';
  settings.tuyaLocalKey[sizeof(settings.tuyaLocalKey) - 1] = '\0';
  settings.tuyaStaticIp[sizeof(settings.tuyaStaticIp) - 1] = '\0';
  settings.loraPsk[sizeof(settings.loraPsk) - 1] = '\0';
}

bool saveSettings() {
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, false)) return false;

  // Invalidate the version first so a partial write is never loaded as a valid
  // configuration after a power loss.
  prefs.remove("cfg_ver");

  bool ok = true;
  ok = (prefs.putUShort("web_port", settings.webUiPort) > 0) && ok;
  ok = putText(prefs, "setup_ssid", settings.setupApSsid) && ok;
  ok = putText(prefs, "setup_pass", settings.setupApPassword) && ok;
  ok = (prefs.putUChar("setup_chan", settings.setupApChannel) > 0) && ok;

  ok = putText(prefs, "sd_file", settings.sdFirmwareFile) && ok;
  ok = (prefs.putULong("sd_wait_ms", settings.sdUpdateWaitMs) > 0) && ok;

  ok = putText(prefs, "ap_ssid", settings.apSsid) && ok;
  ok = putText(prefs, "ap_pass", settings.apPassword) && ok;
  ok = (prefs.putUChar("ap_channel", settings.apChannel) > 0) && ok;
  ok = (prefs.putUChar("ap_max", settings.apMaxClients) > 0) && ok;

  ok = putText(prefs, "tuya_dev_id", settings.tuyaDeviceId) && ok;
  ok = putText(prefs, "tuya_key", settings.tuyaLocalKey) && ok;
  ok = (prefs.putFloat("tuya_ver", settings.tuyaVersion) > 0) && ok;
  ok = putText(prefs, "tuya_ip", settings.tuyaStaticIp) && ok;
  ok = (prefs.putUShort("tuya_port", settings.tuyaPort) > 0) && ok;

  ok = (prefs.putULong("scan_probe", settings.scanProbeTimeoutMs) > 0) && ok;
  ok = (prefs.putULong("scan_int", settings.scanIntervalMs) > 0) && ok;
  ok = (prefs.putULong("sta_ref", settings.stationRefreshMs) > 0) && ok;
  ok = (prefs.putULong("lost_ms", settings.deviceLostGraceMs) > 0) && ok;
  ok = (prefs.putULong("tuya_ct_ms", settings.tuyaConnectTimeoutMs) > 0) && ok;
  ok = (prefs.putULong("tuya_rd_ms", settings.tuyaReadTimeoutMs) > 0) && ok;
  ok = (prefs.putULong("tuya_p_ms", settings.tuyaPollIntervalMs) > 0) && ok;
  ok = (prefs.putULong("tuya_r_ms", settings.tuyaReconnectIntervalMs) > 0) &&
       ok;

  ok = putText(prefs, "lora_psk", settings.loraPsk) && ok;
  ok = (prefs.putFloat("lora_freq", settings.loraFrequencyMhz) > 0) && ok;
  ok = (prefs.putUChar("lora_sync", settings.loraSyncWord) > 0) && ok;
  ok = (prefs.putChar("lora_txp", settings.loraTxPower) > 0) && ok;
  ok = (prefs.putUChar("lora_sf", settings.loraSf) > 0) && ok;
  ok = (prefs.putULong("lora_bw", settings.loraBandwidthHz) > 0) && ok;
  ok = (prefs.putUChar("lora_cr", settings.loraCodingRate) > 0) && ok;
  ok = (prefs.putUShort("lora_pre", settings.loraPreamble) > 0) && ok;
  ok = (prefs.putULong("dp_ack_ms", settings.dpAckTimeoutMs) > 0) && ok;
  ok = (prefs.putUChar("dp_retry", settings.dpAckRetries) > 0) && ok;

  ok = (prefs.putULong("dsp_r_ms", settings.displayRefreshMs) > 0) && ok;
  ok = (prefs.putULong("dsp_on_ms", settings.displayOnMs) > 0) && ok;
  ok = (prefs.putULong("splash_ms", settings.splashMs) > 0) && ok;
  ok = (prefs.putULong("dp_page_ms", settings.dpPageIntervalMs) > 0) && ok;
  ok = (prefs.putUChar("dp_page", settings.dpPerPage) > 0) && ok;

  if (ok) ok = prefs.putUChar("cfg_ver", CONFIG_VERSION) > 0;
  prefs.end();
  return ok;
}

void clearSettings() {
  Preferences prefs;
  if (prefs.begin(NVS_NAMESPACE, false)) {
    prefs.clear();
    prefs.end();
  }
  setDefaults(settings);
}

bool apConfigured() {
  if (settings.apSsid[0] == '\0') return false;
  if (strlen(settings.apSsid) > 32) return false;
  if (settings.apPassword[0] == '\0') return false;
  if (strlen(settings.apPassword) < 8) return false;
  if (strlen(settings.apPassword) > 63) return false;
  if (settings.apChannel < 1 || settings.apChannel > 13) return false;
  if (strcmp(settings.apPassword,
             "replace-with-a-strong-12-plus-character-password") == 0) {
    return false;
  }
  return true;
}

bool tuyaConfigured() {
  if (settings.tuyaDeviceId[0] == '\0') return false;
  if (strcmp(settings.tuyaDeviceId, "replace-with-device-id") == 0) {
    return false;
  }
  if (strlen(settings.tuyaLocalKey) != 16) return false;
  if (strcmp(settings.tuyaLocalKey, "replace-16-chars") == 0) return false;
  return true;
}

bool isValidPsk(const char *psk) {
  if (!psk || strlen(psk) != 32) return false;
  for (size_t i = 0; i < 32; ++i) {
    const char c = psk[i];
    const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                     (c >= 'A' && c <= 'F');
    if (!hex) return false;
  }
  return true;
}

bool loraPskConfigured() { return isValidPsk(settings.loraPsk); }

}  // namespace tuya_lora
