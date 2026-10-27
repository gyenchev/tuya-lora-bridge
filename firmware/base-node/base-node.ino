/*
 * Tuya LoRa Bridge — base / receiver node.
 *
 * Board: TTGO LoRa32 V2.1 / T3 V1.6.1 (ESP32 + SX1276 + SSD1306)
 *
 * Requests raw Tuya DP snapshots over LoRa, reassembles them, maps the detected
 * profile to a stable state model, publishes retained MQTT state, availability
 * and Home Assistant MQTT discovery, and shows the link state on the OLED.
 *
 * The base also owns control: it maps MQTT commands to profile-specific Tuya
 * DP fragments and sends them to the remote node. Control is rejected until a
 * valid snapshot identifies a supported profile.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <esp_system.h>
#include <math.h>
#include <time.h>

#include "boot_guard.h"
#include "config.h"
#include "config_store.h"
#include "entities.h"
#include "ha_discovery.h"
#include "lora_protocol.h"
#include "profiles.h"
#include "sd_update.h"
#include "web_ui.h"

using namespace tuya_lora;

Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

LoraCodec codec;
ReplayGuard replay;

StatusData lastStatus;
bool haveState = false;
bool haveSeen = false;
bool remoteOnline = false;

uint32_t lastSeenMs = 0;
uint32_t lastSeq = 0;
uint32_t lastBootId = 0;
int lastRssi = 0;
float lastSnr = 0.0f;

bool radioReady = false;
bool displayReady = false;
bool displayAsleep = false;
bool wifiWasConnected = false;
bool mqttWasConnected = false;
bool timeSynced = false;

uint32_t lastWifiAttempt = 0;
uint32_t lastMqttAttempt = 0;
uint32_t lastDisplayMs = 0;
uint32_t lastLinkPublishMs = 0;

char mqttClientId[40];

uint8_t loraRx[160];

// DP snapshot request/reassembly state. The charger sends the raw Tuya DP
// JSON in chunks; this node owns the DP map and MQTT/HA publication.
uint16_t dpReqId = 0;
uint32_t nextDpQueryMs = 0;
bool dpQueryPending = false;
uint32_t dpQueryStartedMs = 0;
uint32_t dpLastChunkMs = 0;
uint8_t dpChunkCount = 0;
uint32_t dpRxBitmap = 0;
uint8_t dpChunkLen[DP_MAX_CHUNKS] = {0};
char dpJson[DP_MAX_JSON + 1] = {0};
size_t dpJsonLen = 0;
uint32_t dpLastAckMs = 0;
uint8_t dpAckAttempts = 0;

// The active profile is chosen from the NVS selection (or auto-detected) for
// every snapshot. The selected profile owns both state decoding and command
// encoding; the remote node stays a raw DP proxy.
const ProfileDef *activeProfile = nullptr;
const ProfileDef *detectedProfile = nullptr;
bool profileMatched = false;
char profileError[96] = "waiting for DP snapshot";
char lastDiscoveryProfile[24] = {0};
bool lastDiscoveryGeneric = false;
bool discoveryInitialized = false;
bool legacyDiscoveryCleared = false;
bool genericDiscoveryCleared = false;
uint8_t lastDelayHours = 0;

const char *profileActiveId() {
  return activeProfile ? activeProfile->id : "unknown";
}

const char *profileDetectedId() {
  return detectedProfile ? detectedProfile->id : "unknown";
}

uint8_t profileEntityCount() {
  const ProfileDef *profile = activeProfile;
  if (!profile && strcmp(settings.profileId, "auto") != 0) {
    profile = profileFind(settings.profileId);
  }
  if (!profile) profile = detectedProfile;
  return profile ? profile->genericEntityCount : 0;
}

// Command state. Control is sent as a raw Tuya "dps" JSON fragment, so the
// remote node stays a dumb DP proxy and does not need a model-specific map.
uint32_t lastLoraTxSeq = 0;
bool cmdPending = false;
uint32_t lastCmdSeq = 0;
uint32_t cmdDeadlineMs = 0;
char cmdName[24] = "";

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------
static void putBE16(uint8_t *p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v >> 8);
  p[1] = static_cast<uint8_t>(v);
}

static uint16_t getBE16(const uint8_t *p) {
  return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

static void putBE32(uint8_t *p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v >> 24);
  p[1] = static_cast<uint8_t>(v >> 16);
  p[2] = static_cast<uint8_t>(v >> 8);
  p[3] = static_cast<uint8_t>(v);
}

static uint16_t clampU16(long v) {
  if (v < 0) v = 0;
  if (v > 65535L) v = 65535L;
  return static_cast<uint16_t>(v);
}

static uint8_t clampU8(long v) {
  if (v < 0) v = 0;
  if (v > 255L) v = 255L;
  return static_cast<uint8_t>(v);
}

static uint32_t clampU32(long v) {
  if (v < 0) v = 0;
  return static_cast<uint32_t>(v);
}

static int16_t clampI16(long v) {
  if (v < -32768L) v = -32768L;
  if (v > 32767L) v = 32767L;
  return static_cast<int16_t>(v);
}

String isoTime() {
  time_t now = time(nullptr);
  if (now < 1600000000) return String();
  struct tm t;
  gmtime_r(&now, &t);
  char buf[24];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &t);
  return String(buf);
}

String isoLastSeenTime() {
  if (!haveSeen) return String();
  time_t now = time(nullptr);
  if (now < 1600000000) return String();
  const uint32_t ageSec = (millis() - lastSeenMs) / 1000;
  const time_t seen = now - static_cast<time_t>(ageSec);
  struct tm t;
  gmtime_r(&seen, &t);
  char buf[24];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &t);
  return String(buf);
}

String lastSeenString() {
  if (!haveSeen) return String("--");

  const uint32_t ageSec = (millis() - lastSeenMs) / 1000;

  // With NTP, show the wall-clock time of the last received frame. Without it,
  // fall back to the age so the row still tells you how stale the link is.
  if (timeSynced) {
    const time_t now = time(nullptr);
    if (now >= 1600000000) {
      const time_t seen = now - static_cast<time_t>(ageSec);
      struct tm t;
      localtime_r(&seen, &t);
      char buf[12];
      strftime(buf, sizeof(buf), "%H:%M:%S", &t);
      return String(buf);
    }
  }

  if (ageSec < 60) return String(ageSec) + "s ago";
  if (ageSec < 3600) return String(ageSec / 60) + "m ago";
  return String(ageSec / 3600) + "h ago";
}

// ---------------------------------------------------------------------------
// OLED
// ---------------------------------------------------------------------------
void initDisplay() {
  Wire.begin(OLED_SDA, OLED_SCL);
  Wire.setTimeOut(50);
  displayReady = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  if (!displayReady) {
    Serial.println("[oled] init failed");
    return;
  }
  display.clearDisplay();
  display.display();
}

void sleepDisplay() {
  if (!displayReady || displayAsleep) return;
  display.ssd1306_command(SSD1306_DISPLAYOFF);
  displayAsleep = true;
  Serial.println("[oled] off after 10 min");
}

void drawSdUpdate(const char *title, const char *detail, int secondsLeft) {
  if (!displayReady || displayAsleep) return;
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(title);
  display.setCursor(0, 16);
  display.println(detail);
  if (secondsLeft >= 0) {
    display.setCursor(0, 36);
    display.print("Starting in ");
    display.print(secondsLeft);
    display.println("s");
  }
  display.display();
}

void drawSplash() {
  if (!displayReady || displayAsleep) return;
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(8, 8);
  display.print("TUYA LORA");
  display.setTextSize(1);
  display.setCursor(8, 30);
  display.print("BASE / RECEIVER");
  display.setCursor(8, 42);
  display.print("LoRa ");
  display.print(settings.loraFrequencyMhz, 1);
  display.print(" MHz");
  display.setCursor(8, 52);
  display.print("MQTT bridge");
  display.display();
  delay(settings.splashMs);
}

void drawScreen() {
  if (!displayReady || displayAsleep) return;

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);

  display.print("TUYA LORA BASE ");
  display.println(FW_VERSION);

  display.print("WiFi:");
  if (webUiSetupApActive()) {
    display.print("SETUP");
  } else {
    display.print(wifiWasConnected ? "OK" : (wifiConfigured() ? ".." : "set"));
  }
  display.print(" MQTT:");
  display.println(mqtt.connected() ? "OK" : (mqttConfigured() ? ".." : "set"));

  if (webUiSetupApActive()) {
    display.print("AP ");
    String apName = settings.setupApSsid;
    if (apName.length() > 18) apName = apName.substring(0, 18);
    display.println(apName);
  }

  display.print("Remote: ");
  if (remoteOnline) {
    display.println("online");
  } else {
    display.println(haveSeen ? "offline" : "waiting");
  }

  display.print("Charger: ");
  display.println(haveState ? statusName(lastStatus.status) : "unknown");

  display.print("Power: ");
  if (haveState) {
    display.print(lastStatus.powerW / 1000.0f, 2);
    display.println(" kW");
  } else {
    display.println("--");
  }

  display.print("LoRa: ");
  if (haveSeen) {
    display.print(lastRssi);
    display.print("dBm ");
    display.print(lastSnr, 1);
    display.println("dB");
  } else {
    display.println("--");
  }

  if (!webUiSetupApActive()) {
    display.print("Last: ");
    display.println(lastSeenString());
  }

  display.print(webUiSetupApActive() ? "IP " : "IP: ");
  if (webUiSetupApActive()) {
    display.println(WiFi.softAPIP());
  } else {
    display.println(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString()
                                                  : String("--"));
  }

  display.display();
}

// ---------------------------------------------------------------------------
// LoRa
// ---------------------------------------------------------------------------
void initRadio() {
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_DIO0);

  for (int attempt = 1; attempt <= 3 && !radioReady; ++attempt) {
    Serial.printf("[lora] init attempt %d\n", attempt);
    radioReady = LoRa.begin(settings.loraFrequencyMhz * 1000000.0);
    if (!radioReady) delay(1000);
  }

  if (!radioReady) {
    Serial.println("[lora] init failed");
    return;
  }

  LoRa.setTxPower(settings.loraTxPower);
  LoRa.setSpreadingFactor(settings.loraSf);
  LoRa.setSignalBandwidth(settings.loraBandwidthHz);
  LoRa.setCodingRate4(settings.loraCodingRate);
  LoRa.setPreambleLength(settings.loraPreamble);
  LoRa.setSyncWord(settings.loraSyncWord);
  LoRa.enableCrc();
  LoRa.receive();

  Serial.printf("[lora] %.1f MHz SF%d BW%ld sync 0x%02X AES-128-GCM\n",
                settings.loraFrequencyMhz, settings.loraSf, (long)settings.loraBandwidthHz,
                settings.loraSyncWord);
}

void handleFrame(const uint8_t *frame, size_t len, int rssi, float snr) {
  uint8_t type = 0;
  uint8_t flags = 0;
  uint32_t seq = 0;
  uint32_t bootId = 0;
  const uint8_t *payload = nullptr;
  size_t payloadLen = 0;

  if (!codec.decode(frame, len, DIR_REMOTE_TO_BASE, type, flags, seq, bootId,
                     payload, payloadLen)) {
    Serial.printf("[lora] bad frame len=%u\n", (unsigned)len);
    return;
  }

  // ACKs are idempotent and may be re-sent after a command retry. Other frame
  // types must be strictly monotonic per boot_id.
  const bool fresh = replay.accept(bootId, seq);
  if (!fresh && type != MSG_ACK) {
    Serial.printf("[lora] replay/drop boot=%08lX seq=%lu\n",
                  (unsigned long)bootId, (unsigned long)seq);
    return;
  }

  lastSeq = seq;
  lastBootId = bootId;
  lastRssi = rssi;
  lastSnr = snr;

  bool ok = false;

  switch (type) {
    case MSG_HELLO: {
      HelloData hello;
      if (decodeHello(payload, payloadLen, hello)) {
        ok = true;
        Serial.printf("[lora] HELLO role=%u fw=%u boot=%08lX seq=%lu\n",
                      hello.role, hello.fwVersion,
                      (unsigned long)hello.bootId, (unsigned long)seq);
      }
      break;
    }

    case MSG_ACK: {
      AckData ack;
      if (decodeAck(payload, payloadLen, ack)) {
        ok = true;
        Serial.printf("[lora] ACK result=%u acked_seq=%lu detail=%u\n",
                      ack.result, (unsigned long)ack.ackedSeq, ack.detail);

        if (cmdPending && ack.ackedSeq == lastCmdSeq) {
          cmdPending = false;
          if (ack.result == ACK_OK) {
            publishCommandResult("ok", "");
            // Refresh the snapshot quickly so HA reflects the new state.
            nextDpQueryMs = millis() + 500;
          } else {
            String detail = String(static_cast<unsigned int>(ack.result));
            if (ack.detail != 0) {
              detail += ":";
              detail += String(ack.detail);
            }
            publishCommandResult("rejected", detail);
          }
        }
      }
      break;
    }

    case MSG_DP_CHUNK: {
      handleDpChunk(payload, payloadLen);
      ok = true;
      break;
    }

    case MSG_LOG: {
      char text[LORA_MAX_PAYLOAD + 1];
      size_t n = payloadLen < LORA_MAX_PAYLOAD ? payloadLen : LORA_MAX_PAYLOAD;
      if (n > 0) memcpy(text, payload, n);
      text[n] = '\0';
      Serial.printf("[lora] LOG %s\n", text);
      ok = true;
      break;
    }

    default:
      Serial.printf("[lora] type=0x%02X len=%u\n", type, (unsigned)payloadLen);
      ok = true;
      break;
  }

  if (!ok) {
    return;
  }

  haveSeen = true;
  lastSeenMs = millis();

  if (!remoteOnline) {
    remoteOnline = true;
    publishAvailability();
    Serial.println("[mqtt] remote link online");
  }

  // Link diagnostics are useful even when the remote cannot reach the charger.
  publishLinkState(false);
}

void pollRadio() {
  if (!radioReady) return;

  int packetSize = LoRa.parsePacket();
  if (packetSize <= 0) return;

  size_t n = 0;
  while (LoRa.available()) {
    int b = LoRa.read();
    if (b < 0) break;
    if (n < sizeof(loraRx)) loraRx[n++] = static_cast<uint8_t>(b);
  }

  int rssi = LoRa.packetRssi();
  float snr = LoRa.packetSnr();
  LoRa.receive();

  handleFrame(loraRx, n, rssi, snr);
}

// ---------------------------------------------------------------------------
// DP snapshot transport
// ---------------------------------------------------------------------------
bool sendLoraCommand(uint8_t type, const uint8_t *payload, size_t payloadLen) {
  if (!radioReady || !codec.hasKey()) return false;

  uint8_t frame[LORA_MAX_FRAME];
  size_t frameLen = 0;
  uint32_t seq = codec.nextSeq();

  if (!codec.encode(type, 0, seq, payload, payloadLen, DIR_BASE_TO_REMOTE,
                    frame, sizeof(frame), frameLen)) {
    Serial.println("[lora] encode failed");
    return false;
  }

  lastLoraTxSeq = seq;
  LoRa.beginPacket();
  LoRa.write(frame, frameLen);
  bool ok = LoRa.endPacket();
  LoRa.receive();

  if (ok) {
    Serial.printf("[lora] tx type=0x%02X seq=%lu len=%u\n", type,
                  (unsigned long)seq, (unsigned)frameLen);
  } else {
    Serial.println("[lora] tx failed");
  }
  return ok;
}

void sendDpAck() {
  if (!dpQueryPending) return;

  uint8_t payload[6];
  putBE16(payload, dpReqId);
  putBE32(payload + 2, dpRxBitmap);
  if (sendLoraCommand(MSG_DP_ACK, payload, sizeof(payload))) {
    Serial.printf("[dp] ACK req=%u bitmap=0x%08lX\n", dpReqId,
                  (unsigned long)dpRxBitmap);
  }
}

void publishDpJson() {
  if (dpJsonLen == 0) return;
  if (mqtt.connected()) {
    mqtt.publish(settings.mqttTopicDps, dpJson, true);
  }
  Serial.printf("[dp] raw JSON %u bytes: %s\n", (unsigned)dpJsonLen, dpJson);
}

bool dpHas(JsonObject dps, const char *id) {
  return !dps[id].isNull();
}

long dpInt(JsonObject dps, const char *id, long def = 0) {
  JsonVariant v = dps[id];
  if (v.isNull()) return def;
  if (v.is<const char *>()) return atol(v.as<const char *>());
  if (v.is<bool>()) return v.as<bool>() ? 1 : 0;
  if (v.is<float>()) return static_cast<long>(v.as<float>());
  return v.as<long>();
}

String dpStr(JsonObject dps, const char *id) {
  JsonVariant v = dps[id];
  if (v.isNull()) return String();
  if (v.is<const char *>()) return String(v.as<const char *>());
  if (v.is<bool>()) return v.as<bool>() ? "true" : "false";
  if (v.is<long>()) return String(v.as<long>());
  if (v.is<float>()) return String(v.as<float>(), 3);
  return String();
}

bool dpBool(JsonObject dps, const char *id) {
  JsonVariant v = dps[id];
  if (v.isNull()) return false;
  if (v.is<bool>()) return v.as<bool>();
  if (v.is<const char *>()) {
    String s = v.as<const char *>();
    s.toLowerCase();
    return s == "true" || s == "1" || s == "on";
  }
  return v.as<long>() != 0;
}

uint8_t statusFromDpText(const String &raw) {
  String s = raw;
  s.trim();
  s.toLowerCase();
  if (s.length() == 0) return ST_UNKNOWN;

  if (s == "no_connect" || s == "no_connet" || s == "charger_free")
    return ST_AVAILABLE;
  if (s == "connect" || s == "charger_insert") return ST_PLUGGED;
  if (s == "charing" || s == "charging" || s == "charger_charging")
    return ST_CHARGING;
  if (s == "wait_rfid" || s == "wait_charing" || s == "wait_charging" ||
      s == "charger_wait" || s == "charger_pause")
    return ST_WAITING;
  if (s == "finish" || s == "charger_end") return ST_CHARGED;
  if (s == "error" || s == "charger_fault" || s == "charger_free_fault")
    return ST_FAULT;

  // F-OBZ2 and some firmware revisions report a numeric state.
  bool numeric = true;
  for (size_t i = 0; i < s.length(); ++i) {
    if (!isdigit(static_cast<unsigned char>(s[i]))) {
      numeric = false;
      break;
    }
  }
  if (numeric) {
    switch (s.toInt()) {
      case 0: return ST_AVAILABLE;
      case 1: return ST_PLUGGED;
      case 2: return ST_WAITING;
      case 3: return ST_CHARGING;
      case 4: return ST_WAITING;
      case 5: return ST_CHARGED;
      case 6: return ST_FAULT;
      default: break;
    }
  }

  return ST_UNKNOWN;
}

uint32_t parseDurationText(const String &s) {
  int h = 0, m = 0, sec = 0;
  if (sscanf(s.c_str(), "%d:%d:%d", &h, &m, &sec) == 3) {
    return static_cast<uint32_t>(h) * 3600 + m * 60 + sec;
  }
  return 0;
}

// ---------------------------------------------------------------------------
// Profile state mapping
// ---------------------------------------------------------------------------
static bool fieldPresent(JsonObject dps, const ProfileField &field) {
  if (!field.present()) return false;
  char key[8];
  snprintf(key, sizeof(key), "%u", field.dp);
  return dpHas(dps, key);
}

static long fieldInt(JsonObject dps, const ProfileField &field) {
  char key[8];
  snprintf(key, sizeof(key), "%u", field.dp);
  return dpInt(dps, key);
}

static String fieldStr(JsonObject dps, const ProfileField &field) {
  char key[8];
  snprintf(key, sizeof(key), "%u", field.dp);
  return dpStr(dps, key);
}

static bool fieldBool(JsonObject dps, const ProfileField &field) {
  char key[8];
  snprintf(key, sizeof(key), "%u", field.dp);
  return dpBool(dps, key);
}

static float fieldScaled(JsonObject dps, const ProfileField &field) {
  return static_cast<float>(fieldInt(dps, field)) * field.scale;
}

static bool textInList(const String &raw, const char *list) {
  String value = raw;
  value.trim();
  value.toLowerCase();

  if (!list || !*list) {
    return value.length() > 0 && value != "0" && value != "false" &&
           value != "off" && value != "closecharging";
  }

  String choices(list);
  choices.toLowerCase();
  int start = 0;
  while (start <= static_cast<int>(choices.length())) {
    const int comma = choices.indexOf(',', start);
    String item = comma < 0 ? choices.substring(start)
                            : choices.substring(start, comma);
    item.trim();
    if (item.length() > 0 && value == item) return true;
    if (comma < 0) break;
    start = comma + 1;
  }
  return false;
}

static bool fieldFlag(JsonObject dps, const ProfileField &field) {
  if (!fieldPresent(dps, field)) return false;
  switch (field.type) {
    case PF_BOOL:
      return fieldBool(dps, field);
    case PF_INT: {
      const long raw = fieldInt(dps, field);
      if (field.match[0]) return raw == atol(field.match);
      return raw != 0;
    }
    case PF_TEXT:
      return textInList(fieldStr(dps, field), field.match);
    case PF_TEXT_CONTAINS: {
      String value = fieldStr(dps, field);
      value.trim();
      value.toLowerCase();
      String needle(field.match);
      needle.toLowerCase();
      return needle.length() == 0 ? value.length() > 0
                                  : value.indexOf(needle) >= 0;
    }
    default:
      return false;
  }
}

static uint32_t fieldDuration(JsonObject dps, const ProfileField &field) {
  if (!fieldPresent(dps, field)) return 0;
  if (field.type == PF_DURATION_HMS) {
    return parseDurationText(fieldStr(dps, field));
  }
  const long raw = fieldInt(dps, field);
  if (field.type == PF_DURATION_HOURS) {
    return clampU32(lroundf(raw * 3600.0f * field.scale));
  }
  if (field.type == PF_DURATION_TENTH_HOURS) {
    return clampU32(lroundf(raw * 360.0f * field.scale));
  }
  return 0;
}

static void applyProfileState(JsonObject dps, const ProfileDef &profile,
                              StatusData &state, uint8_t &delayHours) {
  state = StatusData();

  String statusText;
  if (fieldPresent(dps, profile.state.status)) {
    statusText = fieldStr(dps, profile.state.status);
  } else if (fieldPresent(dps, profile.state.statusFallback)) {
    statusText = fieldStr(dps, profile.state.statusFallback);
  }
  state.status = statusFromDpText(statusText);

  String statusLower = statusText;
  statusLower.trim();
  statusLower.toLowerCase();
  String authLower;
  if (fieldPresent(dps, profile.state.statusAuth)) {
    authLower = fieldStr(dps, profile.state.statusAuth);
  } else {
    authLower = statusText;
  }
  authLower.trim();
  authLower.toLowerCase();
  state.waitingForRfid =
      (statusLower == "wait_rfid" || authLower == "wait_rfid");

  if (fieldPresent(dps, profile.state.fault)) {
    state.faultFlags = clampU16(fieldInt(dps, profile.state.fault));
  }
  if (fieldPresent(dps, profile.state.power)) {
    state.powerW = clampU16(lroundf(fieldScaled(dps, profile.state.power)));
  }
  if (fieldPresent(dps, profile.state.energy)) {
    state.energyWh = clampU32(lroundf(fieldScaled(dps, profile.state.energy)));
  }
  if (fieldPresent(dps, profile.state.sessionEnergy)) {
    state.sessionWh =
        clampU32(lroundf(fieldScaled(dps, profile.state.sessionEnergy)));
  }
  if (fieldPresent(dps, profile.state.temperature)) {
    state.tempDc =
        clampI16(lroundf(fieldScaled(dps, profile.state.temperature) * 10.0f));
  }

  if (fieldPresent(dps, profile.state.voltageL1)) {
    if (profile.state.voltageL1.type == PF_VOLTAGE_AUTO) {
      const long raw = fieldInt(dps, profile.state.voltageL1);
      const float volts = raw > 500 ? raw / 10.0f : static_cast<float>(raw);
      state.voltageL1dV = clampU16(lroundf(volts * 10.0f));
    } else {
      state.voltageL1dV = clampU16(
          lroundf(fieldScaled(dps, profile.state.voltageL1) * 10.0f));
    }
  }
  if (fieldPresent(dps, profile.state.voltageL2)) {
    state.voltageL2dV = clampU16(
        lroundf(fieldScaled(dps, profile.state.voltageL2) * 10.0f));
  }
  if (fieldPresent(dps, profile.state.voltageL3)) {
    state.voltageL3dV = clampU16(
        lroundf(fieldScaled(dps, profile.state.voltageL3) * 10.0f));
  }
  if (fieldPresent(dps, profile.state.currentL1)) {
    state.currentL1dA = clampU16(
        lroundf(fieldScaled(dps, profile.state.currentL1) * 10.0f));
  }
  if (fieldPresent(dps, profile.state.currentL2)) {
    state.currentL2dA = clampU16(
        lroundf(fieldScaled(dps, profile.state.currentL2) * 10.0f));
  }
  if (fieldPresent(dps, profile.state.currentL3)) {
    state.currentL3dA = clampU16(
        lroundf(fieldScaled(dps, profile.state.currentL3) * 10.0f));
  }

  if (fieldPresent(dps, profile.state.currentLimit)) {
    state.currentLimit =
        clampU8(lroundf(fieldScaled(dps, profile.state.currentLimit)));
  } else if (fieldPresent(dps, profile.state.currentLimitAlt)) {
    state.currentLimit =
        clampU8(lroundf(fieldScaled(dps, profile.state.currentLimitAlt)));
  }

  if (fieldPresent(dps, profile.state.chargeSeconds)) {
    state.chargeSeconds = fieldDuration(dps, profile.state.chargeSeconds);
  } else if (fieldPresent(dps, profile.state.chargeSecondsAlt)) {
    state.chargeSeconds = fieldDuration(dps, profile.state.chargeSecondsAlt);
  }

  state.chargeEnabled = fieldFlag(dps, profile.state.chargeEnable);
  state.rfidActive = fieldFlag(dps, profile.state.rfid);
  state.scheduleActive = fieldFlag(dps, profile.state.schedule);
  delayHours = fieldPresent(dps, profile.state.delay)
                   ? clampU8(lroundf(fieldScaled(dps, profile.state.delay)))
                   : 0;

  // Some 3-phase firmware reports only the session energy (DP 112) and no
  // separate charge_energy_once (DP 25). Avoid publishing session_energy=0 in
  // that case.
  if (!fieldPresent(dps, profile.state.sessionEnergy) &&
      state.sessionWh == 0 && state.energyWh > 0) {
    state.sessionWh = state.energyWh;
  }
}

void applyDpJson() {
  if (dpJsonLen == 0) return;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, dpJson);
  if (err) {
    Serial.printf("[dp] JSON parse failed: %s\n", err.c_str());
    return;
  }

  JsonObject dps;
  if (doc["dps"].is<JsonObject>()) {
    dps = doc["dps"].as<JsonObject>();
  } else if (doc["data"]["dps"].is<JsonObject>()) {
    dps = doc["data"]["dps"].as<JsonObject>();
  } else {
    Serial.println("[dp] response has no dps object");
    return;
  }
  if (dps.isNull() || dps.size() == 0) {
    Serial.println("[dp] empty dps object");
    return;
  }

  detectedProfile = profileAutoDetect(dps);
  const bool autoMode = strcmp(settings.profileId, "auto") == 0;
  const ProfileDef *selected = autoMode ? nullptr : profileFind(settings.profileId);

  if (autoMode) {
    activeProfile = detectedProfile ? detectedProfile : profilesFallback();
    profileMatched = activeProfile != profilesFallback();
    if (profileMatched) {
      profileError[0] = '\0';
    } else {
      snprintf(profileError, sizeof(profileError),
               "no built-in or custom profile matched this DP snapshot");
    }
  } else if (selected) {
    activeProfile = selected;
    profileMatched = profileMatches(*selected, dps);
    if (profileMatched) {
      profileError[0] = '\0';
    } else {
      snprintf(profileError, sizeof(profileError),
               "selected profile '%s' does not match this DP snapshot",
               selected->id);
    }
  } else {
    activeProfile = profilesFallback();
    profileMatched = false;
    snprintf(profileError, sizeof(profileError),
             "selected profile '%s' is not loaded", settings.profileId);
  }
  if (!activeProfile) activeProfile = profilesFallback();

  StatusData st;
  uint8_t delayHours = 0;
  applyProfileState(dps, *activeProfile, st, delayHours);
  lastDelayHours = delayHours;

  lastStatus = st;
  haveState = true;

  if (mqtt.connected()) {
    const bool discoveryChanged =
        !discoveryInitialized || !activeProfile ||
        strcmp(lastDiscoveryProfile, activeProfile->id) != 0 ||
        lastDiscoveryGeneric != activeProfile->hasGenericEntities();
    if (discoveryChanged) {
      publishDiscovery();
    } else if (activeProfile->hasGenericEntities()) {
      genericPublishState(*activeProfile, dps);
    }
  }

  if (mqtt.connected()) {
    JsonDocument dbg;
    dbg["profile"] = profileActiveId();
    dbg["profile_selected"] = settings.profileId;
    dbg["profile_detected"] = profileDetectedId();
    dbg["profile_match"] = profileMatched;
    dbg["status"] = statusName(st.status);
    dbg["dps"] = dps.size();
    dbg["bytes"] = dpJsonLen;
    String out;
    out.reserve(256);
    serializeJson(dbg, out);
    mqtt.publish(settings.mqttTopicDebug, out.c_str(), false);
  }

  Serial.printf(
      "[dp] active=%s detected=%s match=%u status=%s pwr=%uW temp=%.1fC "
      "limit=%uA\n",
      profileActiveId(), profileDetectedId(), profileMatched ? 1 : 0,
      statusName(st.status), st.powerW, st.tempDc / 10.0f, st.currentLimit);

  publishState();
}

void finalizeDpJson() {
  if (dpChunkCount == 0) return;

  size_t total = static_cast<size_t>(dpChunkCount - 1) * DP_CHUNK_DATA_MAX +
                 dpChunkLen[dpChunkCount - 1];
  if (total > DP_MAX_JSON) total = DP_MAX_JSON;
  dpJson[total] = '\0';
  dpJsonLen = total;

  Serial.printf("[dp] complete req=%u bytes=%u chunks=%u\n", dpReqId,
                (unsigned)total, dpChunkCount);

  sendDpAck();
  dpQueryPending = false;
  nextDpQueryMs = millis() + settings.dpQueryIntervalMs;

  publishDpJson();
  applyDpJson();
}

void handleDpChunk(const uint8_t *payload, size_t payloadLen) {
  if (!dpQueryPending || payloadLen < 4) return;

  uint16_t id = getBE16(payload);
  uint8_t index = payload[2];
  uint8_t count = payload[3];
  if (id != dpReqId || count == 0 || count > DP_MAX_CHUNKS || index >= count)
    return;

  if (dpChunkCount != 0 && dpChunkCount != count) return;
  dpChunkCount = count;

  size_t dataLen = payloadLen - 4;
  if (dataLen > DP_CHUNK_DATA_MAX) dataLen = DP_CHUNK_DATA_MAX;
  size_t offset = static_cast<size_t>(index) * DP_CHUNK_DATA_MAX;
  if (offset >= DP_MAX_JSON) return;
  if (offset + dataLen > DP_MAX_JSON) dataLen = DP_MAX_JSON - offset;

  memcpy(dpJson + offset, payload + 4, dataLen);
  dpChunkLen[index] = static_cast<uint8_t>(dataLen);
  dpRxBitmap |= (1UL << index);
  dpLastChunkMs = millis();
  dpLastAckMs = millis();
  dpAckAttempts = 0;

  uint32_t mask = (count >= 32) ? 0xFFFFFFFFUL : ((1UL << count) - 1);
  Serial.printf("[dp] chunk %u/%u len=%u bitmap=0x%08lX\n", index + 1, count,
                (unsigned)dataLen, (unsigned long)dpRxBitmap);

  if ((dpRxBitmap & mask) == mask) finalizeDpJson();
}

void sendDpQuery() {
  if (dpQueryPending || !radioReady) return;

  dpReqId++;
  if (dpReqId == 0) dpReqId = 1;

  uint8_t payload[3];
  putBE16(payload, dpReqId);
  payload[2] = 0;  // 0 = full DP snapshot

  dpQueryPending = true;
  dpQueryStartedMs = millis();
  dpLastChunkMs = 0;
  dpChunkCount = 0;
  dpRxBitmap = 0;
  dpJsonLen = 0;
  dpJson[0] = '\0';
  dpLastAckMs = 0;
  dpAckAttempts = 0;
  memset(dpChunkLen, 0, sizeof(dpChunkLen));
  nextDpQueryMs = dpQueryStartedMs + settings.dpQueryIntervalMs;

  if (!sendLoraCommand(MSG_DP_QUERY, payload, sizeof(payload))) {
    dpQueryPending = false;
    nextDpQueryMs = millis() + 10000;
    return;
  }

  Serial.printf("[dp] query req=%u\n", dpReqId);
}

void serviceDpQuery() {
  if (!radioReady) return;

  uint32_t now = millis();
  if (dpQueryPending) {
    // Re-send the bitmap ACK until the snapshot is complete. On a marginal
    // link the first ACK can be lost; without this retry the charger waits for
    // its own timeout and the base may give up before the missing chunks arrive.
    if (dpChunkCount > 0 && dpAckAttempts < settings.dpAckRetries &&
        (now - dpLastAckMs) >= settings.dpChunkTimeoutMs) {
      sendDpAck();
      dpLastAckMs = now;
      dpAckAttempts++;
    }

    if ((now - dpQueryStartedMs) >= settings.dpQueryTimeoutMs) {
      Serial.printf("[dp] timeout req=%u chunks=%u bitmap=0x%08lX\n", dpReqId,
                    dpChunkCount, (unsigned long)dpRxBitmap);
      dpQueryPending = false;
      nextDpQueryMs = now + settings.dpQueryIntervalMs;
    }
    return;
  }

  // Do not start a snapshot while a control command is waiting for its ACK.
  if (cmdPending) return;

  if (static_cast<int32_t>(now - nextDpQueryMs) >= 0) sendDpQuery();
}

// ---------------------------------------------------------------------------
// Control commands
// ---------------------------------------------------------------------------
void publishCommandResultNamed(const char *name, const char *result,
                               const String &detail) {
  Serial.printf("[cmd] %s -> %s%s%s\n", name && name[0] ? name : "unknown",
                result, detail.length() ? " " : "", detail.c_str());
  if (!mqtt.connected()) return;

  JsonDocument doc;
  doc["command"] = name && name[0] ? name : "unknown";
  doc["result"] = result;
  doc["ok"] = strcmp(result, "ok") == 0;
  if (detail.length()) doc["detail"] = detail;
  if (timeSynced) doc["ts"] = isoTime();

  String out;
  out.reserve(160);
  serializeJson(doc, out);
  mqtt.publish(settings.mqttTopicResult, out.c_str(), false);
}

void publishCommandResult(const char *result, const String &detail) {
  publishCommandResultNamed(cmdName, result, detail);
}

void rejectCommand(const char *name, const String &reason) {
  publishCommandResultNamed(name, "rejected", reason);
}

String jsonQuoted(const char *text) {
  String out = "\"";
  for (const char *p = text; p && *p; ++p) {
    switch (*p) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(*p) < 0x20) {
          char escaped[7];
          snprintf(escaped, sizeof(escaped), "\\u%04x",
                   static_cast<unsigned char>(*p));
          out += escaped;
        } else {
          out += *p;
        }
        break;
    }
  }
  out += "\"";
  return out;
}

bool sendDpSet(const String &dpsJson, const char *name) {
  if (!settings.enableCommands) {
    publishCommandResultNamed(name, "disabled", "commands disabled");
    return false;
  }
  if (cmdPending) {
    publishCommandResultNamed(name, "busy", "another command is pending");
    return false;
  }
  if (dpQueryPending) {
    publishCommandResultNamed(name, "busy", "DP snapshot in progress");
    return false;
  }
  if (!activeProfile || !profileMatched ||
      activeProfile == profilesFallback()) {
    publishCommandResultNamed(name, "unsupported_profile", profileActiveId());
    return false;
  }
  if (dpsJson.length() == 0 || dpsJson.length() > LORA_MAX_PAYLOAD) {
    publishCommandResultNamed(name, "invalid", "DP payload too large");
    return false;
  }

  strncpy(cmdName, name, sizeof(cmdName) - 1);
  cmdName[sizeof(cmdName) - 1] = '\0';
  cmdPending = true;
  cmdDeadlineMs = millis() + 8000;

  if (!sendLoraCommand(MSG_DP_SET,
                       reinterpret_cast<const uint8_t *>(dpsJson.c_str()),
                       dpsJson.length())) {
    cmdPending = false;
    publishCommandResult("send_failed", "LoRa TX failed");
    return false;
  }

  lastCmdSeq = lastLoraTxSeq;
  Serial.printf("[cmd] %s -> %s\n", cmdName, dpsJson.c_str());
  return true;
}

bool sendChargeDp(bool on, const char *name) {
  if (!activeProfile || !activeProfile->control.hasCharge()) {
    rejectCommand(name, "profile has no known charge DP");
    return false;
  }
  const ProfileControl &control = activeProfile->control;
  String frag = "{\"" + String(control.chargeDp) + "\":";
  if (control.chargeType == PCT_STRING) {
    frag += jsonQuoted(on ? control.chargeOn : control.chargeOff);
  } else {
    frag += on ? control.chargeOn : control.chargeOff;
  }
  frag += "}";
  return sendDpSet(frag, name);
}

void commandCharge(bool on) {
  sendChargeDp(on, "charge");
}

void commandCurrent(long amps) {
  if (amps < settings.minCurrent || amps > settings.maxCurrent) {
    rejectCommand("current_limit",
                  String("range is ") + String(settings.minCurrent) + "-" +
                      String(settings.maxCurrent) + " A");
    return;
  }
  if (!activeProfile || !activeProfile->control.hasCurrent()) {
    rejectCommand("current_limit", "profile has no known current DP");
    return;
  }
  const ProfileControl &control = activeProfile->control;
  String frag = "{\"" + String(control.currentDp) + "\":" + String(amps) + "}";
  sendDpSet(frag, "current_limit");
}

void commandDelay(long hours) {
  if (hours < settings.minDelayH || hours > settings.maxDelayH) {
    rejectCommand("delay", String("range is ") + String(settings.minDelayH) +
                               "-" + String(settings.maxDelayH) + " h");
    return;
  }
  if (!activeProfile || !activeProfile->control.hasDelay()) {
    rejectCommand("delay", "profile has no known delay DP");
    return;
  }
  const ProfileControl &control = activeProfile->control;
  String frag = "{\"" + String(control.delayDp) + "\":" + String(hours);
  if (control.delaySetsMode && control.hasMode()) {
    const char *mode = hours > 0 ? control.modeSchedule : control.modeNow;
    frag += ",\"" + String(control.modeDp) + "\":" + jsonQuoted(mode);
  }
  frag += "}";
  sendDpSet(frag, "delay");
}

void commandMode(const String &modeRaw) {
  if (!activeProfile || !activeProfile->control.hasMode()) {
    rejectCommand("mode", "profile has no known mode DP");
    return;
  }

  String requested = modeRaw;
  requested.trim();
  requested.toLowerCase();
  const ProfileControl &control = activeProfile->control;

  // Map the generic MQTT command to the active profile's wire values. A custom
  // profile may use different strings than the Feyree defaults.
  String mode;
  if (requested == "now" || requested == "charge_now") {
    mode = control.modeNow;
  } else if (requested == "schedule" || requested == "charge_schedule") {
    mode = control.modeSchedule;
  } else if (requested == "charge_pct" || requested == "charge_energy") {
    mode = requested;
  } else {
    rejectCommand(
        "mode", "use charge_now, charge_schedule, charge_pct or charge_energy");
    return;
  }

  sendDpSet("{\"" + String(control.modeDp) + "\":" + jsonQuoted(mode.c_str()) + "}",
            "mode");
}

bool payloadOn(const String &raw) {
  String s = raw;
  s.trim();
  s.toUpperCase();
  return s == "ON" || s == "1" || s == "TRUE" || s == "START" ||
         s == "CHARGE";
}

void serviceCommands() {
  if (!cmdPending) return;
  if (static_cast<int32_t>(millis() - cmdDeadlineMs) >= 0) {
    cmdPending = false;
    publishCommandResult("timeout", "no ACK from charger");
  }
}

// ---------------------------------------------------------------------------
// WiFi + MQTT
// ---------------------------------------------------------------------------
void maintainWiFi() {
  if (!wifiConfigured()) return;

  if (WiFi.status() == WL_CONNECTED) {
    if (!wifiWasConnected) {
      wifiWasConnected = true;
      Serial.print("[wifi] connected, IP ");
      Serial.println(WiFi.localIP());
      Serial.print("[web] http://");
      Serial.print(WiFi.localIP());
      Serial.println("/");
      configTzTime(settings.timezone, "pool.ntp.org", "time.nist.gov");
    }
    timeSynced = time(nullptr) >= 1600000000;
    return;
  }

  if (wifiWasConnected) {
    wifiWasConnected = false;
    timeSynced = false;
    Serial.println("[wifi] lost");
    if (mqtt.connected()) mqtt.disconnect();
  }

  if (millis() - lastWifiAttempt >= settings.wifiRetryMs) {
    lastWifiAttempt = millis();
    Serial.println("[wifi] reconnecting...");
    WiFi.begin(settings.wifiSsid, settings.wifiPassword);
  }
}

void maintainMqtt() {
  if (!wifiConfigured() || !mqttConfigured()) return;
  if (WiFi.status() != WL_CONNECTED) return;

  if (mqtt.connected()) {
    mqttWasConnected = true;
    return;
  }

  if (mqttWasConnected) {
    mqttWasConnected = false;
    Serial.println("[mqtt] lost");
  }

  if (millis() - lastMqttAttempt < settings.mqttReconnectMs) return;
  lastMqttAttempt = millis();

  Serial.print("[mqtt] connecting to ");
  Serial.print(settings.mqttHost);
  Serial.print(':');
  Serial.println(settings.mqttPort);

  const char *mqttUser = settings.mqttUser[0] ? settings.mqttUser : nullptr;
  const char *mqttPass =
      settings.mqttPassword[0] ? settings.mqttPassword : nullptr;
  if (mqtt.connect(mqttClientId, mqttUser, mqttPass,
                   settings.mqttTopicAvailability, 1, true, "offline")) {
    mqttWasConnected = true;
    Serial.println("[mqtt] connected");

    publishDiscovery();
    publishAvailability();
    publishLinkState(true);
    if (haveState) publishState();
    if (dpJsonLen > 0) publishDpJson();

    mqtt.subscribe(settings.mqttTopicCmdCharge);
    mqtt.subscribe(settings.mqttTopicCmdStart);
    mqtt.subscribe(settings.mqttTopicCmdStop);
    mqtt.subscribe(settings.mqttTopicCmdCurrent);
    mqtt.subscribe(settings.mqttTopicCmdDelay);
    mqtt.subscribe(settings.mqttTopicCmdMode);
    mqtt.subscribe(settings.mqttTopicCmdQuery);
    const String genericSubscription = genericCommandWildcard();
    mqtt.subscribe(genericSubscription.c_str());
  } else {
    Serial.printf("[mqtt] failed rc=%d\n", mqtt.state());
  }
}

void publishAvailability() {
  if (!mqtt.connected()) return;
  mqtt.publish(settings.mqttTopicAvailability, remoteOnline ? "online" : "offline",
               true);
}

void publishLinkState(bool force) {
  if (!mqtt.connected()) return;

  const uint32_t now = millis();
  if (!force && lastLinkPublishMs != 0 &&
      (now - lastLinkPublishMs) < 1000) {
    return;
  }

  JsonDocument doc;
  doc["online"] = remoteOnline;
  doc["seen"] = haveSeen;
  if (haveSeen) {
    doc["rssi"] = lastRssi;
    doc["snr"] = lastSnr;
    doc["age"] = (now - lastSeenMs) / 1000;
    doc["seq"] = lastSeq;
    doc["boot_id"] = lastBootId;
    const String seenAt = isoLastSeenTime();
    if (seenAt.length()) {
      doc["last_seen"] = seenAt;
    } else {
      doc["last_seen"] = (const char *)nullptr;
    }
  }
  doc["fw"] = FW_VERSION;

  String json;
  json.reserve(384);
  serializeJson(doc, json);

  if (!mqtt.publish(settings.mqttTopicLink, json.c_str(), true)) {
    Serial.println("[mqtt] link publish failed");
  }
  lastLinkPublishMs = now;
}

void publishState() {
  if (!mqtt.connected() || !haveState) return;

  JsonDocument doc;
  doc["status"] = statusName(lastStatus.status);
  doc["fault"] = lastStatus.faultFlags;
  doc["plugged"] = lastStatus.plugged();
  doc["charging"] = lastStatus.charging();
  // The HA switch follows the live session state. DP 124 is a command value
  // and can remain OpenCharging while the charger is still waiting for RFID.
  doc["charge_enabled"] = lastStatus.chargeSessionEnabled();
  doc["charge_control"] = lastStatus.chargeEnabled;
  doc["waiting_for_authorization"] = lastStatus.waitingForRfid;
  doc["rfid"] = lastStatus.rfidActive;
  doc["schedule"] = lastStatus.scheduleActive;
  doc["current_limit"] = lastStatus.currentLimit;
  doc["voltage_l1"] = lastStatus.voltageL1dV / 10.0f;
  doc["voltage_l2"] = lastStatus.voltageL2dV / 10.0f;
  doc["voltage_l3"] = lastStatus.voltageL3dV / 10.0f;
  doc["current_l1"] = lastStatus.currentL1dA / 10.0f;
  doc["current_l2"] = lastStatus.currentL2dA / 10.0f;
  doc["current_l3"] = lastStatus.currentL3dA / 10.0f;
  doc["power"] = lastStatus.powerW / 1000.0f;
  doc["energy"] = lastStatus.energyWh / 1000.0f;
  doc["session_energy"] = lastStatus.sessionWh / 1000.0f;
  doc["temperature"] = lastStatus.tempDc / 10.0f;
  doc["charge_seconds"] = lastStatus.chargeSeconds;
  doc["fw"] = FW_VERSION;
  doc["profile"] = profileActiveId();
  doc["profile_selected"] = settings.profileId;
  doc["profile_detected"] = profileDetectedId();
  doc["profile_match"] = profileMatched;
  if (profileError[0]) doc["profile_error"] = profileError;
  doc["delay_hours"] = lastDelayHours;

  String json;
  json.reserve(896);
  serializeJson(doc, json);

  if (mqtt.publish(settings.mqttTopicState, json.c_str(), true)) {
    Serial.printf("[mqtt] state %s %.2fkW\n", statusName(lastStatus.status),
                  lastStatus.powerW / 1000.0f);
  } else {
    Serial.println("[mqtt] state publish failed");
  }
}

// ---------------------------------------------------------------------------
// Home Assistant MQTT discovery
// ---------------------------------------------------------------------------
void publishStatusDiscovery() {
  JsonDocument doc;
  addHaCommon(doc);
  doc["name"] = String(settings.haName) + " status";
  const String entityId = haObjectId("status");
  doc["object_id"] = entityId;
  doc["unique_id"] = entityId;
  doc["state_topic"] = settings.mqttTopicState;
  doc["value_template"] = "{{ value_json.status }}";
  doc["device_class"] = "enum";
  doc["icon"] = "mdi:ev-station";

  JsonArray options = doc["options"].to<JsonArray>();
  options.add("available");
  options.add("plugged_in");
  options.add("charging");
  options.add("waiting");
  options.add("charged");
  options.add("fault");
  options.add("unknown");

  publishHaConfig("sensor", "status", doc);
}

void publishSensor(const char *objectId, const String &name,
                   const char *valueTemplate, const char *unit,
                   const char *deviceClass, const char *stateClass,
                   const char *icon, int precision = -1,
                   const char *stateTopic = nullptr) {
  JsonDocument doc;
  addHaCommon(doc);
  doc["name"] = name;
  const String entityId = haObjectId(objectId);
  doc["object_id"] = entityId;
  doc["unique_id"] = entityId;
  doc["state_topic"] = stateTopic ? stateTopic : settings.mqttTopicState;
  doc["value_template"] = valueTemplate;
  if (unit && unit[0]) doc["unit_of_measurement"] = unit;
  if (deviceClass && deviceClass[0]) doc["device_class"] = deviceClass;
  if (stateClass && stateClass[0]) doc["state_class"] = stateClass;
  if (icon && icon[0]) doc["icon"] = icon;
  if (precision >= 0) doc["suggested_display_precision"] = precision;

  publishHaConfig("sensor", objectId, doc);
}

void publishBinarySensor(const char *objectId, const String &name,
                         const char *valueTemplate, const char *deviceClass,
                         const char *icon) {
  JsonDocument doc;
  addHaCommon(doc);
  doc["name"] = name;
  const String entityId = haObjectId(objectId);
  doc["object_id"] = entityId;
  doc["unique_id"] = entityId;
  doc["state_topic"] = settings.mqttTopicState;
  doc["value_template"] = valueTemplate;
  if (deviceClass && deviceClass[0]) doc["device_class"] = deviceClass;
  if (icon && icon[0]) doc["icon"] = icon;

  publishHaConfig("binary_sensor", objectId, doc);
}

void publishSwitchEntity(const char *objectId, const String &name,
                         const char *commandTopic, const char *valueTemplate,
                         const char *icon) {
  JsonDocument doc;
  addHaCommon(doc);
  doc["name"] = name;
  const String entityId = haObjectId(objectId);
  doc["object_id"] = entityId;
  doc["unique_id"] = entityId;
  doc["state_topic"] = settings.mqttTopicState;
  doc["value_template"] = valueTemplate;
  doc["command_topic"] = commandTopic;
  doc["payload_on"] = "ON";
  doc["payload_off"] = "OFF";
  if (icon && icon[0]) doc["icon"] = icon;

  publishHaConfig("switch", objectId, doc);
}

void publishButtonEntity(const char *objectId, const String &name,
                         const char *commandTopic, const char *icon) {
  JsonDocument doc;
  addHaCommon(doc);
  doc["name"] = name;
  const String entityId = haObjectId(objectId);
  doc["object_id"] = entityId;
  doc["unique_id"] = entityId;
  doc["command_topic"] = commandTopic;
  doc["payload_press"] = "PRESS";
  if (icon && icon[0]) doc["icon"] = icon;

  publishHaConfig("button", objectId, doc);
}

void publishNumberEntity(const char *objectId, const String &name,
                         const char *commandTopic, const char *valueTemplate,
                         const char *unit, int minValue, int maxValue,
                         int step, const char *icon) {
  JsonDocument doc;
  addHaCommon(doc);
  doc["name"] = name;
  const String entityId = haObjectId(objectId);
  doc["object_id"] = entityId;
  doc["unique_id"] = entityId;
  doc["state_topic"] = settings.mqttTopicState;
  doc["value_template"] = valueTemplate;
  doc["command_topic"] = commandTopic;
  doc["min"] = minValue;
  doc["max"] = maxValue;
  doc["step"] = step;
  doc["mode"] = "box";
  if (unit && unit[0]) doc["unit_of_measurement"] = unit;
  if (icon && icon[0]) doc["icon"] = icon;

  publishHaConfig("number", objectId, doc);
}

String entityName(const char *suffix) {
  return String(settings.haName) + " " + suffix;
}

void publishLinkDiscovery() {
  publishSensor("link_age", entityName("link age"), "{{ value_json.age }}", "s",
                "duration", "measurement", "mdi:timer-sand", 0,
                settings.mqttTopicLink);
  publishSensor("rssi", entityName("RSSI"), "{{ value_json.rssi }}", "dBm",
                "signal_strength", "measurement", "mdi:signal", 0,
                settings.mqttTopicLink);
  publishSensor("snr", entityName("SNR"), "{{ value_json.snr }}", "dB", nullptr,
                "measurement", "mdi:signal-variant", 1,
                settings.mqttTopicLink);
  publishSensor("last_seen", entityName("last seen"), "{{ value_json.last_seen }}",
                nullptr, "timestamp", nullptr, "mdi:clock-outline", -1,
                settings.mqttTopicLink);
}

void publishLegacyDiscovery() {
  publishStatusDiscovery();

  publishSensor("power", entityName("power"), "{{ value_json.power }}", "kW",
                "power", "measurement", "mdi:flash", 2);
  publishSensor("energy", entityName("energy"), "{{ value_json.energy }}", "kWh",
                "energy", "total_increasing", "mdi:lightning-bolt", 2);
  publishSensor("session_energy", entityName("session energy"),
                "{{ value_json.session_energy }}", "kWh", "energy",
                "total_increasing", "mdi:lightning-bolt-outline", 2);

  publishSensor("voltage_l1", entityName("voltage L1"), "{{ value_json.voltage_l1 }}",
                "V", "voltage", "measurement", "mdi:sine-wave", 1);
  publishSensor("voltage_l2", entityName("voltage L2"), "{{ value_json.voltage_l2 }}",
                "V", "voltage", "measurement", "mdi:sine-wave", 1);
  publishSensor("voltage_l3", entityName("voltage L3"), "{{ value_json.voltage_l3 }}",
                "V", "voltage", "measurement", "mdi:sine-wave", 1);

  publishSensor("current_l1", entityName("current L1"), "{{ value_json.current_l1 }}",
                "A", "current", "measurement", "mdi:current-ac", 2);
  publishSensor("current_l2", entityName("current L2"), "{{ value_json.current_l2 }}",
                "A", "current", "measurement", "mdi:current-ac", 2);
  publishSensor("current_l3", entityName("current L3"), "{{ value_json.current_l3 }}",
                "A", "current", "measurement", "mdi:current-ac", 2);

  publishSensor("temperature", entityName("temperature"),
                "{{ value_json.temperature }}", "°C", "temperature",
                "measurement", "mdi:thermometer", 1);
  publishSensor("charge_duration", entityName("charge duration"),
                "{{ value_json.charge_seconds }}", "s", "duration",
                "measurement", "mdi:timer-outline", 0);
  publishSensor("fault_code", entityName("fault code"), "{{ value_json.fault }}",
                nullptr, nullptr, nullptr, "mdi:alert-circle-outline");

  publishBinarySensor("plugged", entityName("plugged"),
                      "{{ 'ON' if value_json.plugged else 'OFF' }}", "plug",
                      "mdi:power-plug");
  publishBinarySensor("charging", entityName("charging"),
                      "{{ 'ON' if value_json.charging else 'OFF' }}", "running",
                      "mdi:ev-station");
  publishBinarySensor(
      "fault", entityName("fault"),
      "{{ 'ON' if (value_json.fault | int(0)) > 0 else 'OFF' }}", "problem",
      "mdi:alert");
  publishBinarySensor("rfid_active", entityName("RFID active"),
                      "{{ 'ON' if value_json.rfid else 'OFF' }}", "occupancy",
                      "mdi:credit-card-wireless");
  publishBinarySensor("schedule", entityName("schedule"),
                      "{{ 'ON' if value_json.schedule else 'OFF' }}", nullptr,
                      "mdi:calendar-clock");

  if (settings.enableCommands) {
    publishSwitchEntity("charge_control", entityName("charge control"),
                        settings.mqttTopicCmdCharge,
                        "{{ 'ON' if value_json.charge_enabled else 'OFF' }}",
                        "mdi:ev-station");
    publishButtonEntity("charge_enable", entityName("enable charging"),
                        settings.mqttTopicCmdStart, "mdi:play");
    publishButtonEntity("charge_disable", entityName("disable charging"),
                        settings.mqttTopicCmdStop, "mdi:stop");
    publishNumberEntity("current_limit", entityName("current limit"),
                        settings.mqttTopicCmdCurrent,
                        "{{ value_json.current_limit }}", "A",
                        settings.minCurrent, settings.maxCurrent, 1,
                        "mdi:current-ac");
    publishNumberEntity("charge_delay", entityName("charge delay"),
                        settings.mqttTopicCmdDelay,
                        "{{ value_json.delay_hours }}", "h",
                        settings.minDelayH, settings.maxDelayH, 1,
                        "mdi:timer-outline");
  }
}

struct LegacyDiscoveryEntity {
  const char *component;
  const char *objectId;
};

const LegacyDiscoveryEntity LEGACY_DISCOVERY[] = {
    {"sensor", "status"},
    {"sensor", "power"},
    {"sensor", "energy"},
    {"sensor", "session_energy"},
    {"sensor", "voltage_l1"},
    {"sensor", "voltage_l2"},
    {"sensor", "voltage_l3"},
    {"sensor", "current_l1"},
    {"sensor", "current_l2"},
    {"sensor", "current_l3"},
    {"sensor", "temperature"},
    {"sensor", "charge_duration"},
    {"sensor", "fault_code"},
    {"binary_sensor", "plugged"},
    {"binary_sensor", "charging"},
    {"binary_sensor", "fault"},
    {"binary_sensor", "rfid_active"},
    {"binary_sensor", "schedule"},
    {"switch", "charge_control"},
    {"button", "charge_enable"},
    {"button", "charge_disable"},
    {"number", "current_limit"},
    {"number", "charge_delay"},
};

void clearLegacyDiscovery() {
  for (size_t i = 0; i < sizeof(LEGACY_DISCOVERY) / sizeof(LEGACY_DISCOVERY[0]);
       ++i) {
    clearHaConfig(LEGACY_DISCOVERY[i].component,
                  LEGACY_DISCOVERY[i].objectId);
  }
}

void publishDiscovery() {
  const ProfileDef *profile = activeProfile;
  if (!profile && strcmp(settings.profileId, "auto") != 0) {
    profile = profileFind(settings.profileId);
  }
  const bool generic = profile && profile->hasGenericEntities();

  if (generic) {
    if (!legacyDiscoveryCleared) {
      clearLegacyDiscovery();
      legacyDiscoveryCleared = true;
    }
    const bool profileChanged =
        !discoveryInitialized ||
        strcmp(lastDiscoveryProfile, profile->id) != 0 ||
        !lastDiscoveryGeneric;
    if (profileChanged) genericClearState();
    genericPublishDiscovery(*profile);
    genericDiscoveryCleared = false;
    if (haveState && dpJsonLen > 0) {
      genericPublishStateFromJson(*profile, dpJson);
    }
  } else {
    if (!genericDiscoveryCleared) {
      genericClearDiscovery();
      genericClearState();
      genericDiscoveryCleared = true;
    }
    publishLegacyDiscovery();
    legacyDiscoveryCleared = false;
  }

  publishLinkDiscovery();

  if (profile) {
    strncpy(lastDiscoveryProfile, profile->id,
            sizeof(lastDiscoveryProfile) - 1);
    lastDiscoveryProfile[sizeof(lastDiscoveryProfile) - 1] = '\0';
  } else {
    lastDiscoveryProfile[0] = '\0';
  }
  lastDiscoveryGeneric = generic;
  discoveryInitialized = true;
}

void mqttCallback(char *topic, byte *payload, unsigned int length) {
  String t(topic);
  String p;
  p.reserve(length);
  for (unsigned int i = 0; i < length; ++i) p += static_cast<char>(payload[i]);
  p.trim();
  Serial.printf("[mqtt] %s -> %s\n", t.c_str(), p.c_str());

  if (t == settings.mqttTopicCmdQuery) {
    nextDpQueryMs = millis();
    return;
  }
  if (t == settings.mqttTopicCmdStart) {
    commandCharge(true);
    return;
  }
  if (t == settings.mqttTopicCmdStop) {
    commandCharge(false);
    return;
  }
  if (t == settings.mqttTopicCmdCharge) {
    if (p.length() == 0) {
      rejectCommand("charge", "payload must be ON or OFF");
    } else {
      commandCharge(payloadOn(p));
    }
    return;
  }
  if (t == settings.mqttTopicCmdCurrent) {
    commandCurrent(p.toInt());
    return;
  }
  if (t == settings.mqttTopicCmdDelay) {
    commandDelay(p.toInt());
    return;
  }
  if (t == settings.mqttTopicCmdMode) {
    commandMode(p);
    return;
  }

  const String genericPrefix = genericCommandTopic("");
  if (t.startsWith(genericPrefix)) {
    const String key = t.substring(genericPrefix.length());
    if (!activeProfile || !profileMatched) {
      rejectCommand("generic", "profile does not match the last snapshot");
      return;
    }
    String fragment;
    String error;
    if (genericBuildCommand(*activeProfile, key, p, fragment, error)) {
      sendDpSet(fragment, key.c_str());
    } else {
      rejectCommand(key.c_str(), error);
    }
    return;
  }
}

// ---------------------------------------------------------------------------
// Arduino entry points
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("Tuya LoRa base node " FW_VERSION);

  initDisplay();
  loadSettings();
  profilesInit();
  profilesLoadCustomFromNvs();
  profilesSetBuiltinControl("single-phase", settings.dpSingleChargeId,
                            settings.dpSingleCurrentId,
                            settings.dpSingleDelayId,
                            settings.dpSingleModeId);
  profilesSetBuiltinControl("3-phase", settings.dpThreeChargeId,
                            settings.dpThreeCurrentId,
                            settings.dpThreeDelayId,
                            settings.dpThreeModeId);
  profilesSetBuiltinControl("F-OBZ2", settings.dpObz2ChargeId,
                            settings.dpObz2CurrentId, 0, 0);
  if (strcmp(settings.profileId, "auto") != 0 &&
      !profileFind(settings.profileId)) {
    Serial.printf("[profile] selected '%s' is not loaded; using auto\n",
                  settings.profileId);
    snprintf(settings.profileId, sizeof(settings.profileId), "auto");
    saveSettings();
  }
  maybeSdFirmwareUpdate(settings.sdFirmwareFile, drawSdUpdate);
  bootGuardOnBoot();
  drawSplash();
  initRadio();

  if (!codec.setKeyHex(settings.loraPsk)) {
    Serial.println("[lora] invalid PSK (need 32 hex chars)");
  } else if (codec.isZeroKey()) {
    Serial.println("[lora] WARNING: PSK is all zero; set a random key");
  }

  uint32_t bootId = esp_random();
  if (bootId == 0) bootId = 1;
  codec.setBootId(bootId);
  Serial.printf("[lora] local boot_id %08lX\n", (unsigned long)bootId);

  // Let WiFi/MQTT come up before asking the charger for its first snapshot.
  nextDpQueryMs = millis() + 3000;

  webUiBegin();

  if (wifiConfigured()) {
    Serial.print("[wifi] connecting to ");
    Serial.println(settings.wifiSsid);
    WiFi.begin(settings.wifiSsid, settings.wifiPassword);
    lastWifiAttempt = millis();
  } else {
    Serial.println("[wifi] credentials MISSING; use the setup AP");
  }

  mqtt.setServer(settings.mqttHost, settings.mqttPort);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(settings.mqttBufferSize);
  mqtt.setKeepAlive(settings.mqttKeepaliveS);
  mqtt.setSocketTimeout(settings.mqttSocketTimeoutS);

  snprintf(mqttClientId, sizeof(mqttClientId), "tuya-lora-base-%06lX",
           (unsigned long)(ESP.getEfuseMac() & 0xFFFFFF));

  Serial.printf("[mqtt] %s\n", mqttConfigured() ? "configured" : "MISSING");
  Serial.print("[mqtt] client id ");
  Serial.println(mqttClientId);

  drawScreen();
}

void checkOffline() {
  if (!remoteOnline) return;
  if (haveSeen && (millis() - lastSeenMs) <= settings.offlineAfterMs) return;

  remoteOnline = false;
  publishAvailability();
  publishLinkState(true);
  Serial.println("[mqtt] remote link offline");
}

void loop() {
  webUiLoop();
  bootGuardLoop();
  maintainWiFi();
  maintainMqtt();

  if (mqtt.connected()) mqtt.loop();

  pollRadio();
  serviceDpQuery();
  serviceCommands();
  checkOffline();

  if (mqtt.connected() && remoteOnline &&
      (millis() - lastLinkPublishMs) >= LINK_STATE_REFRESH_MS) {
    publishLinkState(true);
  }

  if (!webUiSetupApActive() && displayReady && !displayAsleep &&
      millis() >= settings.displayOnMs) {
    sleepDisplay();
  }

  if (!displayAsleep && millis() - lastDisplayMs >= settings.displayRefreshMs) {
    lastDisplayMs = millis();
    drawScreen();
  }

  delay(2);
}
