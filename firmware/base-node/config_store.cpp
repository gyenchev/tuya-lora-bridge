#include "config_store.h"

#include <Preferences.h>
#include <string.h>

#include "config.h"

namespace tuya_lora {

namespace {

constexpr const char *NVS_NAMESPACE = "tuya-lora-base";
constexpr uint8_t CONFIG_VERSION = 2;

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

void setDefaults(BaseSettings &s) {
  memset(&s, 0, sizeof(s));

  s.webUiPort = WEB_UI_PORT;
  copyText(s.setupApSsid, sizeof(s.setupApSsid), SETUP_AP_SSID);
  copyText(s.setupApPassword, sizeof(s.setupApPassword), SETUP_AP_PASSWORD);
  s.setupApChannel = SETUP_AP_CHANNEL;
  s.wifiSetupFallbackMs = WIFI_SETUP_FALLBACK_MS;

  copyText(s.sdFirmwareFile, sizeof(s.sdFirmwareFile), SD_FIRMWARE_FILE);
  s.sdUpdateWaitMs = SD_UPDATE_WAIT_MS;

  s.mqttPort = 1883;
  copyText(s.haName, sizeof(s.haName), HA_DEVICE_NAME);
  copyText(s.timezone, sizeof(s.timezone), TIMEZONE);

  s.loraFrequencyMhz = LORA_FREQUENCY / 1000000.0f;
  s.loraSyncWord = LORA_SYNC_WORD;
  s.loraTxPower = LORA_TX_POWER;
  s.loraSf = LORA_SF;
  s.loraBandwidthHz = LORA_BW;
  s.loraCodingRate = LORA_CR;
  s.loraPreamble = LORA_PREAMBLE;

  s.offlineAfterMs = OFFLINE_AFTER_MS;
  s.dpQueryIntervalMs = DP_QUERY_INTERVAL_MS;
  s.dpQueryTimeoutMs = DP_QUERY_TIMEOUT_MS;
  s.dpChunkTimeoutMs = DP_CHUNK_TIMEOUT_MS;
  s.dpAckRetries = DP_ACK_RETRIES;
  s.wifiRetryMs = WIFI_RETRY_MS;
  s.mqttReconnectMs = MQTT_RECONNECT_MS;
  s.mqttKeepaliveS = MQTT_KEEPALIVE_S;
  s.mqttSocketTimeoutS = MQTT_SOCKET_TIMEOUT_S;
  s.mqttBufferSize = MQTT_BUFFER_SIZE;
  s.displayRefreshMs = DISPLAY_REFRESH_MS;
  s.displayOnMs = DISPLAY_ON_MS;
  s.splashMs = SPLASH_MS;

  copyText(s.mqttTopicState, sizeof(s.mqttTopicState), MQTT_TOPIC_STATE);
  copyText(s.mqttTopicLink, sizeof(s.mqttTopicLink), MQTT_TOPIC_LINK);
  copyText(s.mqttTopicAvailability, sizeof(s.mqttTopicAvailability),
           MQTT_TOPIC_AVAILABILITY);
  copyText(s.mqttTopicResult, sizeof(s.mqttTopicResult), MQTT_TOPIC_RESULT);
  copyText(s.mqttTopicDebug, sizeof(s.mqttTopicDebug), MQTT_TOPIC_DEBUG);
  copyText(s.mqttTopicDps, sizeof(s.mqttTopicDps), MQTT_TOPIC_DPS);
  copyText(s.mqttTopicCmdCharge, sizeof(s.mqttTopicCmdCharge),
           MQTT_TOPIC_CMD_CHARGE);
  copyText(s.mqttTopicCmdStart, sizeof(s.mqttTopicCmdStart),
           MQTT_TOPIC_CMD_START);
  copyText(s.mqttTopicCmdStop, sizeof(s.mqttTopicCmdStop), MQTT_TOPIC_CMD_STOP);
  copyText(s.mqttTopicCmdCurrent, sizeof(s.mqttTopicCmdCurrent),
           MQTT_TOPIC_CMD_CURRENT);
  copyText(s.mqttTopicCmdDelay, sizeof(s.mqttTopicCmdDelay),
           MQTT_TOPIC_CMD_DELAY);
  copyText(s.mqttTopicCmdMode, sizeof(s.mqttTopicCmdMode), MQTT_TOPIC_CMD_MODE);
  copyText(s.mqttTopicCmdQuery, sizeof(s.mqttTopicCmdQuery),
           MQTT_TOPIC_CMD_QUERY);

  copyText(s.haDiscoveryPrefix, sizeof(s.haDiscoveryPrefix),
           HA_DISCOVERY_PREFIX);
  copyText(s.haDeviceId, sizeof(s.haDeviceId), HA_DEVICE_ID);

  s.enableCommands = ENABLE_COMMANDS != 0;
  copyText(s.profileId, sizeof(s.profileId), DEFAULT_PROFILE_ID);
  s.minCurrent = CHARGER_MIN_CURRENT;
  s.maxCurrent = CHARGER_MAX_CURRENT;
  s.minDelayH = CHARGER_MIN_DELAY_H;
  s.maxDelayH = CHARGER_MAX_DELAY_H;
  s.dpSingleChargeId = DP_SINGLE_CHARGE_ID;
  s.dpSingleCurrentId = DP_SINGLE_CURRENT_ID;
  s.dpSingleDelayId = DP_SINGLE_DELAY_ID;
  s.dpSingleModeId = DP_SINGLE_MODE_ID;
  s.dpThreeChargeId = DP_THREE_CHARGE_ID;
  s.dpThreeCurrentId = DP_THREE_CURRENT_ID;
  s.dpThreeDelayId = DP_THREE_DELAY_ID;
  s.dpThreeModeId = DP_THREE_MODE_ID;
  s.dpObz2ChargeId = DP_OBZ2_CHARGE_ID;
  s.dpObz2CurrentId = DP_OBZ2_CURRENT_ID;
}

void readText(Preferences &prefs, const char *key, char *dst, size_t dstSize) {
  const String value = prefs.getString(key, "");
  copyText(dst, dstSize, value.c_str());
}

void sanitize(BaseSettings &s) {
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
  if (s.wifiSetupFallbackMs < 1000) {
    s.wifiSetupFallbackMs = WIFI_SETUP_FALLBACK_MS;
  }
  if (s.sdFirmwareFile[0] != '/') {
    copyText(s.sdFirmwareFile, sizeof(s.sdFirmwareFile), SD_FIRMWARE_FILE);
  }
  if (s.sdUpdateWaitMs < 1000) s.sdUpdateWaitMs = SD_UPDATE_WAIT_MS;
  if (s.mqttPort == 0) s.mqttPort = 1883;

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
  if (s.mqttKeepaliveS < 5) s.mqttKeepaliveS = MQTT_KEEPALIVE_S;
  if (s.mqttSocketTimeoutS < 1) {
    s.mqttSocketTimeoutS = MQTT_SOCKET_TIMEOUT_S;
  }
  if (s.mqttBufferSize < 256 || s.mqttBufferSize > 8192) {
    s.mqttBufferSize = MQTT_BUFFER_SIZE;
  }
  if (s.dpAckRetries > 20) s.dpAckRetries = DP_ACK_RETRIES;
  if (s.offlineAfterMs < 1000) s.offlineAfterMs = OFFLINE_AFTER_MS;
  if (s.dpQueryIntervalMs < 1000) {
    s.dpQueryIntervalMs = DP_QUERY_INTERVAL_MS;
  }
  if (s.dpQueryTimeoutMs < 1000) s.dpQueryTimeoutMs = DP_QUERY_TIMEOUT_MS;
  if (s.dpChunkTimeoutMs < 100) s.dpChunkTimeoutMs = DP_CHUNK_TIMEOUT_MS;
  if (s.wifiRetryMs < 1000) s.wifiRetryMs = WIFI_RETRY_MS;
  if (s.mqttReconnectMs < 1000) s.mqttReconnectMs = MQTT_RECONNECT_MS;
  if (s.displayRefreshMs < 100) s.displayRefreshMs = DISPLAY_REFRESH_MS;
  if (s.displayOnMs < 1000) s.displayOnMs = DISPLAY_ON_MS;
  if (s.splashMs > 60000) s.splashMs = SPLASH_MS;

  if (s.profileId[0] == '\0') {
    copyText(s.profileId, sizeof(s.profileId), DEFAULT_PROFILE_ID);
  }
  if (s.minCurrent < 1 || s.minCurrent > 63) {
    s.minCurrent = CHARGER_MIN_CURRENT;
  }
  if (s.maxCurrent < 1 || s.maxCurrent > 63) {
    s.maxCurrent = CHARGER_MAX_CURRENT;
  }
  if (s.minCurrent > s.maxCurrent) {
    s.minCurrent = CHARGER_MIN_CURRENT;
    s.maxCurrent = CHARGER_MAX_CURRENT;
  }
  if (s.minDelayH > 24) s.minDelayH = CHARGER_MIN_DELAY_H;
  if (s.maxDelayH > 24) s.maxDelayH = CHARGER_MAX_DELAY_H;
  if (s.minDelayH > s.maxDelayH) {
    s.minDelayH = CHARGER_MIN_DELAY_H;
    s.maxDelayH = CHARGER_MAX_DELAY_H;
  }

  if (s.haDiscoveryPrefix[0] == '\0') {
    copyText(s.haDiscoveryPrefix, sizeof(s.haDiscoveryPrefix),
             HA_DISCOVERY_PREFIX);
  }
  if (s.haDeviceId[0] == '\0') {
    copyText(s.haDeviceId, sizeof(s.haDeviceId), HA_DEVICE_ID);
  }
  if (s.mqttTopicState[0] == '\0') {
    copyText(s.mqttTopicState, sizeof(s.mqttTopicState), MQTT_TOPIC_STATE);
  }
  if (s.mqttTopicLink[0] == '\0') {
    copyText(s.mqttTopicLink, sizeof(s.mqttTopicLink), MQTT_TOPIC_LINK);
  }
  if (s.mqttTopicAvailability[0] == '\0') {
    copyText(s.mqttTopicAvailability, sizeof(s.mqttTopicAvailability),
             MQTT_TOPIC_AVAILABILITY);
  }
  if (s.mqttTopicResult[0] == '\0') {
    copyText(s.mqttTopicResult, sizeof(s.mqttTopicResult), MQTT_TOPIC_RESULT);
  }
  if (s.mqttTopicDebug[0] == '\0') {
    copyText(s.mqttTopicDebug, sizeof(s.mqttTopicDebug), MQTT_TOPIC_DEBUG);
  }
  if (s.mqttTopicDps[0] == '\0') {
    copyText(s.mqttTopicDps, sizeof(s.mqttTopicDps), MQTT_TOPIC_DPS);
  }
  if (s.mqttTopicCmdCharge[0] == '\0') {
    copyText(s.mqttTopicCmdCharge, sizeof(s.mqttTopicCmdCharge),
             MQTT_TOPIC_CMD_CHARGE);
  }
  if (s.mqttTopicCmdStart[0] == '\0') {
    copyText(s.mqttTopicCmdStart, sizeof(s.mqttTopicCmdStart),
             MQTT_TOPIC_CMD_START);
  }
  if (s.mqttTopicCmdStop[0] == '\0') {
    copyText(s.mqttTopicCmdStop, sizeof(s.mqttTopicCmdStop),
             MQTT_TOPIC_CMD_STOP);
  }
  if (s.mqttTopicCmdCurrent[0] == '\0') {
    copyText(s.mqttTopicCmdCurrent, sizeof(s.mqttTopicCmdCurrent),
             MQTT_TOPIC_CMD_CURRENT);
  }
  if (s.mqttTopicCmdDelay[0] == '\0') {
    copyText(s.mqttTopicCmdDelay, sizeof(s.mqttTopicCmdDelay),
             MQTT_TOPIC_CMD_DELAY);
  }
  if (s.mqttTopicCmdMode[0] == '\0') {
    copyText(s.mqttTopicCmdMode, sizeof(s.mqttTopicCmdMode),
             MQTT_TOPIC_CMD_MODE);
  }
  if (s.mqttTopicCmdQuery[0] == '\0') {
    copyText(s.mqttTopicCmdQuery, sizeof(s.mqttTopicCmdQuery),
             MQTT_TOPIC_CMD_QUERY);
  }
}

}  // namespace

BaseSettings settings;

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
      settings.wifiSetupFallbackMs =
          prefs.getULong("wifi_fb_ms", settings.wifiSetupFallbackMs);

      readText(prefs, "sd_file", settings.sdFirmwareFile,
               sizeof(settings.sdFirmwareFile));
      settings.sdUpdateWaitMs =
          prefs.getULong("sd_wait_ms", settings.sdUpdateWaitMs);

      readText(prefs, "wifi_ssid", settings.wifiSsid, sizeof(settings.wifiSsid));
      readText(prefs, "wifi_pass", settings.wifiPassword,
               sizeof(settings.wifiPassword));
      readText(prefs, "mqtt_host", settings.mqttHost, sizeof(settings.mqttHost));
      settings.mqttPort = prefs.getUShort("mqtt_port", settings.mqttPort);
      readText(prefs, "mqtt_user", settings.mqttUser, sizeof(settings.mqttUser));
      readText(prefs, "mqtt_pass", settings.mqttPassword,
               sizeof(settings.mqttPassword));
      readText(prefs, "lora_psk", settings.loraPsk, sizeof(settings.loraPsk));
      readText(prefs, "ha_name", settings.haName, sizeof(settings.haName));
      readText(prefs, "timezone", settings.timezone, sizeof(settings.timezone));

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

      settings.offlineAfterMs =
          prefs.getULong("off_ms", settings.offlineAfterMs);
      settings.dpQueryIntervalMs =
          prefs.getULong("dp_q_ms", settings.dpQueryIntervalMs);
      settings.dpQueryTimeoutMs =
          prefs.getULong("dp_t_ms", settings.dpQueryTimeoutMs);
      settings.dpChunkTimeoutMs =
          prefs.getULong("dp_c_ms", settings.dpChunkTimeoutMs);
      settings.dpAckRetries =
          prefs.getUChar("dp_retry", settings.dpAckRetries);
      settings.wifiRetryMs =
          prefs.getULong("wifi_r_ms", settings.wifiRetryMs);
      settings.mqttReconnectMs =
          prefs.getULong("mq_r_ms", settings.mqttReconnectMs);
      settings.mqttKeepaliveS =
          prefs.getUShort("mq_keep", settings.mqttKeepaliveS);
      settings.mqttSocketTimeoutS =
          prefs.getUShort("mq_sock", settings.mqttSocketTimeoutS);
      settings.mqttBufferSize =
          prefs.getUShort("mq_buf", settings.mqttBufferSize);
      settings.displayRefreshMs =
          prefs.getULong("dsp_r_ms", settings.displayRefreshMs);
      settings.displayOnMs =
          prefs.getULong("dsp_on_ms", settings.displayOnMs);
      settings.splashMs = prefs.getULong("splash_ms", settings.splashMs);

      readText(prefs, "t_state", settings.mqttTopicState,
               sizeof(settings.mqttTopicState));
      readText(prefs, "t_link", settings.mqttTopicLink,
               sizeof(settings.mqttTopicLink));
      readText(prefs, "t_avail", settings.mqttTopicAvailability,
               sizeof(settings.mqttTopicAvailability));
      readText(prefs, "t_result", settings.mqttTopicResult,
               sizeof(settings.mqttTopicResult));
      readText(prefs, "t_debug", settings.mqttTopicDebug,
               sizeof(settings.mqttTopicDebug));
      readText(prefs, "t_dps", settings.mqttTopicDps,
               sizeof(settings.mqttTopicDps));
      readText(prefs, "t_c_charge", settings.mqttTopicCmdCharge,
               sizeof(settings.mqttTopicCmdCharge));
      readText(prefs, "t_c_start", settings.mqttTopicCmdStart,
               sizeof(settings.mqttTopicCmdStart));
      readText(prefs, "t_c_stop", settings.mqttTopicCmdStop,
               sizeof(settings.mqttTopicCmdStop));
      readText(prefs, "t_c_curr", settings.mqttTopicCmdCurrent,
               sizeof(settings.mqttTopicCmdCurrent));
      readText(prefs, "t_c_delay", settings.mqttTopicCmdDelay,
               sizeof(settings.mqttTopicCmdDelay));
      readText(prefs, "t_c_mode", settings.mqttTopicCmdMode,
               sizeof(settings.mqttTopicCmdMode));
      readText(prefs, "t_c_query", settings.mqttTopicCmdQuery,
               sizeof(settings.mqttTopicCmdQuery));

      readText(prefs, "ha_prefix", settings.haDiscoveryPrefix,
               sizeof(settings.haDiscoveryPrefix));
      readText(prefs, "ha_dev_id", settings.haDeviceId,
               sizeof(settings.haDeviceId));

      settings.enableCommands =
          prefs.getBool("cmd_en", settings.enableCommands);
      readText(prefs, "profile_id", settings.profileId,
               sizeof(settings.profileId));
      settings.minCurrent = prefs.getUChar("cur_min", settings.minCurrent);
      settings.maxCurrent = prefs.getUChar("cur_max", settings.maxCurrent);
      settings.minDelayH = prefs.getUChar("dly_min", settings.minDelayH);
      settings.maxDelayH = prefs.getUChar("dly_max", settings.maxDelayH);
      settings.dpSingleChargeId =
          prefs.getUShort("dp_s_chg", settings.dpSingleChargeId);
      settings.dpSingleCurrentId =
          prefs.getUShort("dp_s_cur", settings.dpSingleCurrentId);
      settings.dpSingleDelayId =
          prefs.getUShort("dp_s_dly", settings.dpSingleDelayId);
      settings.dpSingleModeId =
          prefs.getUShort("dp_s_mode", settings.dpSingleModeId);
      settings.dpThreeChargeId =
          prefs.getUShort("dp_3_chg", settings.dpThreeChargeId);
      settings.dpThreeCurrentId =
          prefs.getUShort("dp_3_cur", settings.dpThreeCurrentId);
      settings.dpThreeDelayId =
          prefs.getUShort("dp_3_dly", settings.dpThreeDelayId);
      settings.dpThreeModeId =
          prefs.getUShort("dp_3_mode", settings.dpThreeModeId);
      settings.dpObz2ChargeId =
          prefs.getUShort("dp_o_chg", settings.dpObz2ChargeId);
      settings.dpObz2CurrentId =
          prefs.getUShort("dp_o_cur", settings.dpObz2CurrentId);
    }
    prefs.end();
  }

  sanitize(settings);

  if (!haveConfig) {
    saveSettings();
  }

  // Never leave a partially-written string without a terminator.
  settings.setupApSsid[sizeof(settings.setupApSsid) - 1] = '\0';
  settings.setupApPassword[sizeof(settings.setupApPassword) - 1] = '\0';
  settings.sdFirmwareFile[sizeof(settings.sdFirmwareFile) - 1] = '\0';
  settings.wifiSsid[sizeof(settings.wifiSsid) - 1] = '\0';
  settings.wifiPassword[sizeof(settings.wifiPassword) - 1] = '\0';
  settings.mqttHost[sizeof(settings.mqttHost) - 1] = '\0';
  settings.mqttUser[sizeof(settings.mqttUser) - 1] = '\0';
  settings.mqttPassword[sizeof(settings.mqttPassword) - 1] = '\0';
  settings.loraPsk[sizeof(settings.loraPsk) - 1] = '\0';
  settings.haName[sizeof(settings.haName) - 1] = '\0';
  settings.timezone[sizeof(settings.timezone) - 1] = '\0';
  settings.mqttTopicState[sizeof(settings.mqttTopicState) - 1] = '\0';
  settings.mqttTopicLink[sizeof(settings.mqttTopicLink) - 1] = '\0';
  settings.mqttTopicAvailability[sizeof(settings.mqttTopicAvailability) - 1] =
      '\0';
  settings.mqttTopicResult[sizeof(settings.mqttTopicResult) - 1] = '\0';
  settings.mqttTopicDebug[sizeof(settings.mqttTopicDebug) - 1] = '\0';
  settings.mqttTopicDps[sizeof(settings.mqttTopicDps) - 1] = '\0';
  settings.mqttTopicCmdCharge[sizeof(settings.mqttTopicCmdCharge) - 1] = '\0';
  settings.mqttTopicCmdStart[sizeof(settings.mqttTopicCmdStart) - 1] = '\0';
  settings.mqttTopicCmdStop[sizeof(settings.mqttTopicCmdStop) - 1] = '\0';
  settings.mqttTopicCmdCurrent[sizeof(settings.mqttTopicCmdCurrent) - 1] = '\0';
  settings.mqttTopicCmdDelay[sizeof(settings.mqttTopicCmdDelay) - 1] = '\0';
  settings.mqttTopicCmdMode[sizeof(settings.mqttTopicCmdMode) - 1] = '\0';
  settings.mqttTopicCmdQuery[sizeof(settings.mqttTopicCmdQuery) - 1] = '\0';
  settings.haDiscoveryPrefix[sizeof(settings.haDiscoveryPrefix) - 1] = '\0';
  settings.haDeviceId[sizeof(settings.haDeviceId) - 1] = '\0';
  settings.profileId[sizeof(settings.profileId) - 1] = '\0';
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
  ok = (prefs.putULong("wifi_fb_ms", settings.wifiSetupFallbackMs) > 0) && ok;

  ok = putText(prefs, "sd_file", settings.sdFirmwareFile) && ok;
  ok = (prefs.putULong("sd_wait_ms", settings.sdUpdateWaitMs) > 0) && ok;

  ok = putText(prefs, "wifi_ssid", settings.wifiSsid) && ok;
  ok = putText(prefs, "wifi_pass", settings.wifiPassword) && ok;
  ok = putText(prefs, "mqtt_host", settings.mqttHost) && ok;
  ok = (prefs.putUShort("mqtt_port", settings.mqttPort) > 0) && ok;
  ok = putText(prefs, "mqtt_user", settings.mqttUser) && ok;
  ok = putText(prefs, "mqtt_pass", settings.mqttPassword) && ok;
  ok = putText(prefs, "lora_psk", settings.loraPsk) && ok;
  ok = putText(prefs, "ha_name", settings.haName) && ok;
  ok = putText(prefs, "timezone", settings.timezone) && ok;

  ok = (prefs.putFloat("lora_freq", settings.loraFrequencyMhz) > 0) && ok;
  ok = (prefs.putUChar("lora_sync", settings.loraSyncWord) > 0) && ok;
  ok = (prefs.putChar("lora_txp", settings.loraTxPower) > 0) && ok;
  ok = (prefs.putUChar("lora_sf", settings.loraSf) > 0) && ok;
  ok = (prefs.putULong("lora_bw", settings.loraBandwidthHz) > 0) && ok;
  ok = (prefs.putUChar("lora_cr", settings.loraCodingRate) > 0) && ok;
  ok = (prefs.putUShort("lora_pre", settings.loraPreamble) > 0) && ok;

  ok = (prefs.putULong("off_ms", settings.offlineAfterMs) > 0) && ok;
  ok = (prefs.putULong("dp_q_ms", settings.dpQueryIntervalMs) > 0) && ok;
  ok = (prefs.putULong("dp_t_ms", settings.dpQueryTimeoutMs) > 0) && ok;
  ok = (prefs.putULong("dp_c_ms", settings.dpChunkTimeoutMs) > 0) && ok;
  ok = (prefs.putUChar("dp_retry", settings.dpAckRetries) > 0) && ok;
  ok = (prefs.putULong("wifi_r_ms", settings.wifiRetryMs) > 0) && ok;
  ok = (prefs.putULong("mq_r_ms", settings.mqttReconnectMs) > 0) && ok;
  ok = (prefs.putUShort("mq_keep", settings.mqttKeepaliveS) > 0) && ok;
  ok = (prefs.putUShort("mq_sock", settings.mqttSocketTimeoutS) > 0) && ok;
  ok = (prefs.putUShort("mq_buf", settings.mqttBufferSize) > 0) && ok;
  ok = (prefs.putULong("dsp_r_ms", settings.displayRefreshMs) > 0) && ok;
  ok = (prefs.putULong("dsp_on_ms", settings.displayOnMs) > 0) && ok;
  ok = (prefs.putULong("splash_ms", settings.splashMs) > 0) && ok;

  ok = putText(prefs, "t_state", settings.mqttTopicState) && ok;
  ok = putText(prefs, "t_link", settings.mqttTopicLink) && ok;
  ok = putText(prefs, "t_avail", settings.mqttTopicAvailability) && ok;
  ok = putText(prefs, "t_result", settings.mqttTopicResult) && ok;
  ok = putText(prefs, "t_debug", settings.mqttTopicDebug) && ok;
  ok = putText(prefs, "t_dps", settings.mqttTopicDps) && ok;
  ok = putText(prefs, "t_c_charge", settings.mqttTopicCmdCharge) && ok;
  ok = putText(prefs, "t_c_start", settings.mqttTopicCmdStart) && ok;
  ok = putText(prefs, "t_c_stop", settings.mqttTopicCmdStop) && ok;
  ok = putText(prefs, "t_c_curr", settings.mqttTopicCmdCurrent) && ok;
  ok = putText(prefs, "t_c_delay", settings.mqttTopicCmdDelay) && ok;
  ok = putText(prefs, "t_c_mode", settings.mqttTopicCmdMode) && ok;
  ok = putText(prefs, "t_c_query", settings.mqttTopicCmdQuery) && ok;

  ok = putText(prefs, "ha_prefix", settings.haDiscoveryPrefix) && ok;
  ok = putText(prefs, "ha_dev_id", settings.haDeviceId) && ok;

  ok = (prefs.putBool("cmd_en", settings.enableCommands) > 0) && ok;
  ok = putText(prefs, "profile_id", settings.profileId) && ok;
  ok = (prefs.putUChar("cur_min", settings.minCurrent) > 0) && ok;
  ok = (prefs.putUChar("cur_max", settings.maxCurrent) > 0) && ok;
  ok = (prefs.putUChar("dly_min", settings.minDelayH) > 0) && ok;
  ok = (prefs.putUChar("dly_max", settings.maxDelayH) > 0) && ok;
  ok = (prefs.putUShort("dp_s_chg", settings.dpSingleChargeId) > 0) && ok;
  ok = (prefs.putUShort("dp_s_cur", settings.dpSingleCurrentId) > 0) && ok;
  ok = (prefs.putUShort("dp_s_dly", settings.dpSingleDelayId) > 0) && ok;
  ok = (prefs.putUShort("dp_s_mode", settings.dpSingleModeId) > 0) && ok;
  ok = (prefs.putUShort("dp_3_chg", settings.dpThreeChargeId) > 0) && ok;
  ok = (prefs.putUShort("dp_3_cur", settings.dpThreeCurrentId) > 0) && ok;
  ok = (prefs.putUShort("dp_3_dly", settings.dpThreeDelayId) > 0) && ok;
  ok = (prefs.putUShort("dp_3_mode", settings.dpThreeModeId) > 0) && ok;
  ok = (prefs.putUShort("dp_o_chg", settings.dpObz2ChargeId) > 0) && ok;
  ok = (prefs.putUShort("dp_o_cur", settings.dpObz2CurrentId) > 0) && ok;

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

bool wifiConfigured() {
  if (settings.wifiSsid[0] == '\0') return false;
  if (strlen(settings.wifiSsid) > 32) return false;
  if (strcmp(settings.wifiSsid, "your-home-ssid") == 0) return false;
  if (settings.wifiPassword[0] != '\0' &&
      (strlen(settings.wifiPassword) < 8 ||
       strlen(settings.wifiPassword) > 63)) {
    return false;
  }
  return true;
}

bool mqttConfigured() {
  if (settings.mqttHost[0] == '\0' || settings.mqttPort == 0) return false;
  if (strcmp(settings.mqttPassword,
             "replace-with-a-dedicated-mqtt-password") == 0) {
    return false;
  }
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
