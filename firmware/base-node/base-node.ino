/*
 * Tuya LoRa Bridge — base / receiver node.
 *
 * Board: TTGO LoRa32 V2.1 / T3 V1.6.1 (ESP32 + SX1276 + SSD1306)
 *
 * Requests raw Tuya DP snapshots over LoRa, reassembles them, publishes the
 * active profile entities, retained MQTT snapshots, availability and Home
 * Assistant MQTT discovery, and shows the link state on the OLED.
 *
 * The base also owns control: it maps MQTT commands to profile-specific Tuya
 * DP fragments and sends them to the remote node. Control is rejected until a
 * profile is loaded and a valid snapshot has been seen.
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
#include "browser_profile.h"
#include "config.h"
#include "config_store.h"
#include "ha_discovery.h"
#include "lora_protocol.h"
#include "lora_stats.h"
#include "lora_trial.h"
#include "sd_update.h"
#include "web_ui.h"

using namespace tuya_lora;

Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

LoraCodec codec;
ReplayGuard replay;
LoraStats loraStats;

bool haveState = false;
bool haveSeen = false;
bool remoteOnline = false;

uint32_t lastSeenMs = 0;
uint32_t lastSeq = 0;
uint32_t lastBootId = 0;
int lastRssi = 0;
float lastSnr = 0.0f;
uint8_t remoteFwVersion = 0;

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

// DP snapshot request/reassembly state. The device sends the raw Tuya DP
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

// The active profile is the single browser pack stored on the base. It owns
// the Home Assistant entities, telemetry and commands; the remote node stays a
// raw DP proxy.
char profileError[96] = "waiting for DP snapshot";
char lastDiscoveryProfile[24] = {0};
bool discoveryInitialized = false;
bool packDiscoveryCleared = false;

const char *profileActiveId() {
  return browserProfileLoaded() ? browserProfileId() : "none";
}

uint16_t profileEntityCount() {
  return browserProfileLoaded()
             ? static_cast<uint16_t>(browserProfileDiscoveryCount())
             : 0;
}

// Command state. Control is sent as a raw Tuya "dps" JSON fragment, so the
// remote node stays a dumb DP proxy and does not need a model-specific map.
uint32_t lastLoraTxSeq = 0;
bool cmdPending = false;
uint32_t lastCmdSeq = 0;
uint32_t cmdDeadlineMs = 0;
char cmdName[24] = "";

// Coordinated LoRa configuration change. The remote must acknowledge the new
// config before this node applies it. Both nodes keep the old config until the
// new link is proven by the READY/COMMIT handshake.
enum LoraConfigPhase : uint8_t {
  LORA_CFG_IDLE = 0,
  LORA_CFG_SENDING,
  LORA_CFG_WAITING_ACK,
  LORA_CFG_WAITING_READY,
  LORA_CFG_WAITING_COMMIT_ACK,
  LORA_CFG_ERROR,
};

LoraConfigPhase loraCfgPhase = LORA_CFG_IDLE;
LoraTrial loraCfgTrial;
LoraConfig loraCfgNew;
uint32_t loraCfgConfigId = 0;
uint32_t loraCfgDeadlineMs = 0;
uint32_t loraCfgStartMs = 0;
uint32_t loraCfgNextSetMs = 0;
uint32_t loraCfgNextProbeMs = 0;
uint32_t loraCfgNextCommitMs = 0;
uint32_t loraCfgLastContactMs = 0;
uint8_t loraCfgAttempts = 0;
char loraCfgError[96] = "";

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

  display.print("Device: ");
  display.println(haveState ? "available" : "unknown");

  display.print("Profile: ");
  String profileLabel = browserProfileLoaded() ? String(browserProfileName())
                                               : String("none");
  if (profileLabel.length() > 13) profileLabel = profileLabel.substring(0, 13);
  display.println(profileLabel);

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
  loraStats.configure(settings.loraSf, settings.loraBandwidthHz,
                      settings.loraCodingRate, settings.loraPreamble,
                      settings.loraTxPower, settings.loraFrequencyMhz);
  loraStats.reset(millis());

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
  loraStats.noteRx(len, rssi, snr, millis());

  uint8_t type = 0;
  uint8_t flags = 0;
  uint32_t seq = 0;
  uint32_t bootId = 0;
  const uint8_t *payload = nullptr;
  size_t payloadLen = 0;

  if (!codec.decode(frame, len, DIR_REMOTE_TO_BASE, type, flags, seq, bootId,
                     payload, payloadLen)) {
    loraStats.noteRxDecodeError();
    Serial.printf("[lora] bad frame len=%u\n", (unsigned)len);
    return;
  }
  loraStats.noteRxPayload(payloadLen);
  loraStats.noteRxType(type);

  // ACKs are idempotent and may be re-sent after a command retry. Other frame
  // types must be strictly monotonic per boot_id.
  const bool fresh = replay.accept(bootId, seq);
  if (!fresh && type != MSG_ACK) {
    loraStats.noteRxReplayDrop();
    Serial.printf("[lora] replay/drop boot=%08lX seq=%lu\n",
                  (unsigned long)bootId, (unsigned long)seq);
    return;
  }

  lastSeq = seq;
  lastBootId = bootId;
  lastRssi = rssi;
  lastSnr = snr;
  if (loraCfgPhase == LORA_CFG_WAITING_READY ||
      loraCfgPhase == LORA_CFG_WAITING_COMMIT_ACK) {
    loraCfgLastContactMs = millis();
  }

  bool ok = false;

  switch (type) {
    case MSG_HELLO: {
      HelloData hello;
      if (decodeHello(payload, payloadLen, hello)) {
        ok = true;
        remoteFwVersion = hello.fwVersion;
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

    case MSG_CONFIG_ACK:
      handleConfigAckPayload(payload, payloadLen);
      ok = true;
      break;

    case MSG_CONFIG_READY:
      handleConfigReadyPayload(payload, payloadLen);
      ok = true;
      break;

    case MSG_CONFIG_COMMIT_ACK:
      handleConfigCommitAckPayload(payload, payloadLen);
      ok = true;
      break;

    case MSG_CONFIG_PROBE_ACK:
      handleConfigProbeAckPayload(payload, payloadLen);
      ok = true;
      break;

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

  // Link diagnostics are useful even when the remote cannot reach the device.
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
    loraStats.noteTx(frameLen, payloadLen, millis());
    loraStats.noteTxType(type);
    Serial.printf("[lora] tx type=0x%02X seq=%lu len=%u\n", type,
                  (unsigned long)seq, (unsigned)frameLen);
  } else {
    loraStats.noteTxFailed();
    Serial.println("[lora] tx failed");
  }
  return ok;
}

// ---------------------------------------------------------------------------
// Coordinated LoRa link configuration
// ---------------------------------------------------------------------------
constexpr uint32_t LORA_CONFIG_TIMEOUT_MS = 300000UL;  // 5 minutes
constexpr uint32_t LORA_CONFIG_ACK_TIMEOUT_MS = 60000UL;
constexpr uint32_t LORA_CONFIG_RETRY_MS = 5000UL;
constexpr uint32_t LORA_CONFIG_PROBE_MS = 20000UL;

void loraConfigFromSettings(const BaseSettings &s, LoraConfig &out) {
  out.frequencyMhz = s.loraFrequencyMhz;
  out.syncWord = s.loraSyncWord;
  out.txPower = s.loraTxPower;
  out.sf = s.loraSf;
  out.bandwidthHz = s.loraBandwidthHz;
  out.codingRate = s.loraCodingRate;
  out.preamble = s.loraPreamble;
  strncpy(out.psk, s.loraPsk, sizeof(out.psk) - 1);
  out.psk[sizeof(out.psk) - 1] = '\0';
}

void loraConfigToSettings(const LoraConfig &in, BaseSettings &s) {
  s.loraFrequencyMhz = in.frequencyMhz;
  s.loraSyncWord = in.syncWord;
  s.loraTxPower = in.txPower;
  s.loraSf = in.sf;
  s.loraBandwidthHz = in.bandwidthHz;
  s.loraCodingRate = in.codingRate;
  s.loraPreamble = in.preamble;
  strncpy(s.loraPsk, in.psk, sizeof(s.loraPsk) - 1);
  s.loraPsk[sizeof(s.loraPsk) - 1] = '\0';
}

bool loraConfigEquals(const LoraConfig &a, const LoraConfig &b) {
  return fabsf(a.frequencyMhz - b.frequencyMhz) < 0.0005f &&
         a.syncWord == b.syncWord && a.txPower == b.txPower &&
         a.sf == b.sf && a.bandwidthHz == b.bandwidthHz &&
         a.codingRate == b.codingRate && a.preamble == b.preamble &&
         strcmp(a.psk, b.psk) == 0;
}

bool validateLoraConfig(const LoraConfig &c, String &error) {
  if (!isValidPsk(c.psk)) {
    error = "LoRa PSK must be exactly 32 hex characters.";
    return false;
  }
  if (c.frequencyMhz < 137.0f || c.frequencyMhz > 1020.0f) {
    error = "Frequency must be between 137 and 1020 MHz.";
    return false;
  }
  if (c.sf < 7 || c.sf > 12) {
    error = "Spreading factor must be 7-12.";
    return false;
  }
  if (c.bandwidthHz < 7800 || c.bandwidthHz > 500000) {
    error = "Bandwidth must be between 7800 and 500000 Hz.";
    return false;
  }
  if (c.codingRate < 5 || c.codingRate > 8) {
    error = "Coding rate must be 4/5 through 4/8.";
    return false;
  }
  if (c.preamble < 6) {
    error = "Preamble must be at least 6 symbols.";
    return false;
  }
  if (c.txPower < -1 || c.txPower > 20) {
    error = "TX power must be between -1 and 20 dBm.";
    return false;
  }
  return true;
}

bool loraConfigBusy() {
  return loraCfgPhase != LORA_CFG_IDLE && loraCfgPhase != LORA_CFG_ERROR;
}

const char *loraConfigPhaseName() {
  switch (loraCfgPhase) {
    case LORA_CFG_SENDING: return "sending";
    case LORA_CFG_WAITING_ACK: return "waiting_ack";
    case LORA_CFG_WAITING_READY: return "waiting_ready";
    case LORA_CFG_WAITING_COMMIT_ACK: return "waiting_commit_ack";
    case LORA_CFG_ERROR: return "error";
    default: return "idle";
  }
}

String loraConfigStatusJson() {
  JsonDocument doc;
  doc["state"] = loraConfigPhaseName();
  doc["busy"] = loraConfigBusy();
  doc["config_id"] = loraCfgConfigId;
  doc["attempts"] = loraCfgAttempts;
  doc["remote_fw_version"] = remoteFwVersion;
  if (loraCfgPhase == LORA_CFG_WAITING_READY ||
      loraCfgPhase == LORA_CFG_WAITING_COMMIT_ACK) {
    const uint32_t now = millis();
    doc["remaining_ms"] = static_cast<int32_t>(loraCfgDeadlineMs - now);
    doc["last_contact_age_ms"] =
        loraCfgLastContactMs ? static_cast<int32_t>(now - loraCfgLastContactMs)
                             : -1;
  } else {
    doc["remaining_ms"] = 0;
    doc["last_contact_age_ms"] = -1;
  }
  if (loraCfgError[0]) doc["error"] = loraCfgError;
  String out;
  serializeJson(doc, out);
  return out;
}

bool applyLoraRuntime() {
  radioReady = false;
  LoRa.end();
  initRadio();
  if (!radioReady) return false;
  if (!codec.setKeyHex(settings.loraPsk)) return false;

  // Discard in-flight DP/control state; the peer is on a different PHY now.
  dpQueryPending = false;
  dpChunkCount = 0;
  dpRxBitmap = 0;
  dpJsonLen = 0;
  dpJson[0] = '\0';
  cmdPending = false;
  nextDpQueryMs = millis() + 1500;
  lastRssi = 0;
  lastSnr = 0.0f;
  lastSeenMs = millis();
  return true;
}

void commitBaseLoraConfig() {
  loraTrialClear();
  loraCfgTrial = LoraTrial{};
  loraCfgPhase = LORA_CFG_IDLE;
  loraCfgError[0] = '\0';
  loraCfgLastContactMs = millis();
  Serial.println("[lora-cfg] committed");
}

void failBaseLoraConfig(const String &error) {
  snprintf(loraCfgError, sizeof(loraCfgError), "%s", error.c_str());
  Serial.printf("[lora-cfg] error: %s\n", loraCfgError);

  if (loraCfgTrial.active && loraCfgTrial.applied) {
    BaseSettings old = settings;
    loraConfigToSettings(loraCfgTrial.oldConfig, old);
    settings = old;
    saveSettings();
    if (!applyLoraRuntime()) {
      Serial.println("[lora-cfg] old config reinit failed; restarting");
      delay(100);
      ESP.restart();
    }
    remoteOnline = false;
    publishAvailability();
    publishLinkState(true);
  } else {
    loraTrialClear();
    loraCfgTrial = LoraTrial{};
  }
  loraCfgPhase = LORA_CFG_ERROR;
}

bool startLoraConfigChange(const LoraConfig &desired, String &error) {
  if (loraConfigBusy()) {
    error = "A LoRa configuration change is already in progress.";
    return false;
  }
  if (!remoteOnline || !haveSeen ||
      static_cast<int32_t>(millis() - lastSeenMs) > 60000L) {
    error = "The remote node has not been heard recently; LoRa settings were "
            "not changed.";
    return false;
  }
  if (remoteFwVersion != 0 && remoteFwVersion < 2) {
    error = "The remote firmware does not support coordinated LoRa changes. "
            "Flash both nodes first.";
    return false;
  }
  if (!validateLoraConfig(desired, error)) return false;

  LoraConfig current;
  loraConfigFromSettings(settings, current);
  if (loraConfigEquals(current, desired)) {
    error = "The LoRa settings are unchanged.";
    return false;
  }

  LoraTrial trial;
  trial.active = true;
  trial.applied = false;
  trial.configId = esp_random();
  if (trial.configId == 0) trial.configId = 1;
  trial.oldConfig = current;
  trial.newConfig = desired;
  if (!loraTrialSave(trial)) {
    error = "Could not store the LoRa trial record in NVS.";
    return false;
  }

  loraCfgTrial = trial;
  loraCfgNew = desired;
  loraCfgConfigId = trial.configId;
  loraCfgPhase = LORA_CFG_SENDING;
  loraCfgAttempts = 0;
  loraCfgError[0] = '\0';
  loraCfgLastContactMs = 0;
  loraCfgStartMs = millis();
  loraCfgNextSetMs = millis();
  Serial.printf("[lora-cfg] staged id=%lu\n",
                (unsigned long)loraCfgConfigId);
  return true;
}

void sendLoraConfigSet() {
  uint8_t payload[51];
  const size_t n = encodeConfigSet(loraCfgConfigId, loraCfgNew, payload,
                                   sizeof(payload));
  if (n == 0 || !sendLoraCommand(MSG_CONFIG_SET, payload, n)) {
    failBaseLoraConfig("Could not send the LoRa configuration to the remote.");
    return;
  }
  loraCfgAttempts++;
  loraCfgPhase = LORA_CFG_WAITING_ACK;
  loraCfgNextSetMs = millis() + LORA_CONFIG_RETRY_MS;
  Serial.printf("[lora-cfg] SET attempt %u\n", loraCfgAttempts);
}

void sendLoraConfigId(uint8_t type) {
  uint8_t payload[5];
  const size_t n = encodeConfigId(loraCfgConfigId, payload, sizeof(payload));
  if (n > 0) sendLoraCommand(type, payload, n);
}

void applyNewBaseLoraConfig() {
  BaseSettings next = settings;
  loraConfigToSettings(loraCfgNew, next);
  const BaseSettings old = settings;
  settings = next;
  if (!saveSettings()) {
    settings = old;
    failBaseLoraConfig("Could not save the new LoRa settings.");
    return;
  }

  loraCfgTrial.applied = true;
  if (!loraTrialSave(loraCfgTrial)) {
    settings = old;
    saveSettings();
    failBaseLoraConfig("Could not update the LoRa trial record.");
    return;
  }

  if (!applyLoraRuntime()) {
    settings = old;
    saveSettings();
    if (!applyLoraRuntime()) {
      Serial.println("[lora-cfg] old config reinit failed; restarting");
      delay(100);
      ESP.restart();
    }
    loraTrialClear();
    loraCfgTrial = LoraTrial{};
    loraCfgPhase = LORA_CFG_ERROR;
    snprintf(loraCfgError, sizeof(loraCfgError),
             "New LoRa radio configuration failed to start.");
    return;
  }

  loraCfgPhase = LORA_CFG_WAITING_READY;
  loraCfgDeadlineMs = millis() + LORA_CONFIG_TIMEOUT_MS;
  loraCfgLastContactMs = 0;
  loraCfgNextProbeMs = millis() + 300;
  Serial.println("[lora-cfg] new radio active; waiting for READY");
}

void handleConfigAckPayload(const uint8_t *payload, size_t len) {
  uint32_t id = 0;
  ConfigAckData ack;
  if (!decodeConfigAck(payload, len, id, ack)) return;
  if (id != loraCfgConfigId || loraCfgPhase != LORA_CFG_WAITING_ACK) return;

  if (ack.result != ACK_OK) {
    failBaseLoraConfig("The remote rejected the LoRa configuration.");
    return;
  }
  applyNewBaseLoraConfig();
}

void handleConfigReadyPayload(const uint8_t *payload, size_t len) {
  uint32_t id = 0;
  if (!decodeConfigId(payload, len, id)) return;
  if (id != loraCfgConfigId) return;
  loraCfgLastContactMs = millis();
  if (loraCfgPhase != LORA_CFG_WAITING_READY) return;

  loraCfgPhase = LORA_CFG_WAITING_COMMIT_ACK;
  loraCfgNextCommitMs = millis();
  sendLoraConfigId(MSG_CONFIG_COMMIT);
  Serial.println("[lora-cfg] remote READY; sent COMMIT");
}

void handleConfigCommitAckPayload(const uint8_t *payload, size_t len) {
  uint32_t id = 0;
  ConfigAckData ack;
  if (!decodeConfigAck(payload, len, id, ack)) return;
  if (id != loraCfgConfigId) return;
  loraCfgLastContactMs = millis();
  if (ack.result == ACK_OK) commitBaseLoraConfig();
}

void handleConfigProbeAckPayload(const uint8_t *payload, size_t len) {
  uint32_t id = 0;
  if (!decodeConfigId(payload, len, id)) return;
  if (id == loraCfgConfigId) loraCfgLastContactMs = millis();
}

void serviceLoraConfig() {
  if (loraCfgPhase == LORA_CFG_IDLE || loraCfgPhase == LORA_CFG_ERROR) return;
  const uint32_t now = millis();

  switch (loraCfgPhase) {
    case LORA_CFG_SENDING:
      sendLoraConfigSet();
      break;

    case LORA_CFG_WAITING_ACK:
      if (static_cast<int32_t>(now - loraCfgStartMs) >=
          static_cast<int32_t>(LORA_CONFIG_ACK_TIMEOUT_MS)) {
        failBaseLoraConfig("The remote did not acknowledge the LoRa "
                           "configuration.");
        break;
      }
      if (static_cast<int32_t>(now - loraCfgNextSetMs) >= 0 &&
          loraCfgAttempts < 20) {
        sendLoraConfigSet();
      }
      break;

    case LORA_CFG_WAITING_READY:
      if (static_cast<int32_t>(now - loraCfgDeadlineMs) >= 0) {
        failBaseLoraConfig("The remote did not confirm the new LoRa link; "
                           "restored the previous settings.");
        break;
      }
      if (static_cast<int32_t>(now - loraCfgNextProbeMs) >= 0) {
        sendLoraConfigId(MSG_CONFIG_PROBE);
        loraCfgNextProbeMs = now + LORA_CONFIG_PROBE_MS;
      }
      break;

    case LORA_CFG_WAITING_COMMIT_ACK:
      if (static_cast<int32_t>(now - loraCfgDeadlineMs) >= 0) {
        failBaseLoraConfig("The remote did not confirm the LoRa commit; "
                           "restored the previous settings.");
        break;
      }
      if (static_cast<int32_t>(now - loraCfgNextCommitMs) >= 0) {
        sendLoraConfigId(MSG_CONFIG_COMMIT);
        loraCfgNextCommitMs = now + LORA_CONFIG_RETRY_MS;
      }
      break;

    default:
      break;
  }
}

void initLoraConfigOnBoot() {
  LoraTrial trial;
  if (!loraTrialLoad(trial) || !trial.active) return;
  if (!trial.applied) {
    loraTrialClear();
    Serial.println("[lora-cfg] discarded unapplied trial");
    return;
  }

  loraConfigToSettings(trial.newConfig, settings);
  saveSettings();
  loraCfgTrial = trial;
  loraCfgNew = trial.newConfig;
  loraCfgConfigId = trial.configId;
  loraCfgPhase = LORA_CFG_WAITING_READY;
  loraCfgDeadlineMs = millis() + LORA_CONFIG_TIMEOUT_MS;
  loraCfgLastContactMs = millis();
  loraCfgNextProbeMs = millis() + 1000;
  Serial.printf("[lora-cfg] resumed trial id=%lu\n",
                (unsigned long)loraCfgConfigId);
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
    mqtt.publish(mqttTopicDps().c_str(), dpJson, true);
    if (browserProfileSelected()) {
      JsonDocument perDpDoc;
      const DeserializationError err = deserializeJson(perDpDoc, dpJson);
      if (!err && perDpDoc["dps"].is<JsonObject>()) {
        JsonObject dps = perDpDoc["dps"].as<JsonObject>();
        for (JsonPair kv : dps) {
          String topic = String(mqttTopicDps()) + "/" + kv.key().c_str();
          String value;
          if (kv.value().is<const char *>()) {
            value = kv.value().as<const char *>();
          } else {
            serializeJson(kv.value(), value);
          }
          mqtt.publish(topic.c_str(), value.c_str(), true);
        }
      }
    }
  }
  Serial.printf("[dp] raw JSON %u bytes: %s\n", (unsigned)dpJsonLen, dpJson);
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

  if (!browserProfileLoaded()) {
    snprintf(profileError, sizeof(profileError), "no profile loaded");
  } else {
    profileError[0] = '\0';
  }

  haveState = true;

  if (mqtt.connected()) {
    const bool loaded = browserProfileLoaded();
    const char *activeId = loaded ? browserProfileId() : "none";
    const bool discoveryChanged =
        !discoveryInitialized || strcmp(lastDiscoveryProfile, activeId) != 0;
    if (discoveryChanged) publishDiscovery();
  }

  if (mqtt.connected()) {
    JsonDocument dbg;
    dbg["profile"] = profileActiveId();
    dbg["profile_loaded"] = browserProfileLoaded();
    dbg["dps"] = dps.size();
    dbg["bytes"] = dpJsonLen;
    String out;
    out.reserve(192);
    serializeJson(dbg, out);
    mqtt.publish(mqttTopicDebug().c_str(), out.c_str(), false);
  }

  Serial.printf("[dp] profile=%s dps=%u bytes=%u\n", profileActiveId(),
                static_cast<unsigned>(dps.size()),
                static_cast<unsigned>(dpJsonLen));
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

  loraStats.noteDpComplete(true, millis());
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

  const bool newChunk = (dpRxBitmap & (1UL << index)) == 0;
  memcpy(dpJson + offset, payload + 4, dataLen);
  dpChunkLen[index] = static_cast<uint8_t>(dataLen);
  dpRxBitmap |= (1UL << index);
  if (newChunk) loraStats.noteDpChunk(dataLen);
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

  loraStats.noteDpStart(dpQueryStartedMs);
  Serial.printf("[dp] query req=%u\n", dpReqId);
}

void serviceDpQuery() {
  if (!radioReady) return;

  uint32_t now = millis();
  if (dpQueryPending) {
    // Re-send the bitmap ACK until the snapshot is complete. On a marginal
    // link the first ACK can be lost; without this retry the device waits for
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
      loraStats.noteDpComplete(false, now);
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
  mqtt.publish(mqttTopicCommand("result").c_str(), out.c_str(), false);
}

void publishCommandResult(const char *result, const String &detail) {
  publishCommandResultNamed(cmdName, result, detail);
}

void rejectCommand(const char *name, const String &reason) {
  publishCommandResultNamed(name, "rejected", reason);
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
  if (!browserProfileLoaded()) {
    publishCommandResultNamed(name, "unsupported_profile", "no profile loaded");
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


void serviceCommands() {
  if (!cmdPending) return;
  if (static_cast<int32_t>(millis() - cmdDeadlineMs) >= 0) {
    cmdPending = false;
    publishCommandResult("timeout", "no ACK from device");
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
                   mqttTopicAvailability().c_str(), 1, true, "offline")) {
    mqttWasConnected = true;
    Serial.println("[mqtt] connected");

    publishDiscovery();
    publishAvailability();
    publishLinkState(true);
    if (dpJsonLen > 0) publishDpJson();

    mqtt.subscribe(mqttTopicCommand("query").c_str());
    const String packSubscription = browserProfileCommandTopic();
    mqtt.subscribe(packSubscription.c_str());
  } else {
    Serial.printf("[mqtt] failed rc=%d\n", mqtt.state());
  }
}

void publishAvailability() {
  if (!mqtt.connected()) return;
  mqtt.publish(mqttTopicAvailability().c_str(), remoteOnline ? "online" : "offline",
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

  if (!mqtt.publish(mqttTopicLink().c_str(), json.c_str(), true)) {
    Serial.println("[mqtt] link publish failed");
  }
  lastLinkPublishMs = now;
}

// ---------------------------------------------------------------------------
// Home Assistant MQTT discovery
// ---------------------------------------------------------------------------
void publishSensor(const char *objectId, const String &name,
                   const char *valueTemplate, const char *unit,
                   const char *deviceClass, const char *stateClass,
                   const char *icon, int precision, const char *stateTopic) {
  JsonDocument doc;
  addHaCommon(doc);
  doc["name"] = name;
  const String entityId = haObjectId(objectId);
  doc["object_id"] = entityId;
  doc["unique_id"] = entityId;
  doc["state_topic"] = stateTopic;
  doc["value_template"] = valueTemplate;
  if (unit && unit[0]) doc["unit_of_measurement"] = unit;
  if (deviceClass && deviceClass[0]) doc["device_class"] = deviceClass;
  if (stateClass && stateClass[0]) doc["state_class"] = stateClass;
  if (icon && icon[0]) doc["icon"] = icon;
  if (precision >= 0) doc["suggested_display_precision"] = precision;

  publishHaConfig("sensor", objectId, doc);
}

String entityName(const char *suffix) {
  return String(settings.haName) + " " + suffix;
}

void publishLinkDiscovery() {
  publishSensor("link_age", entityName("link age"), "{{ value_json.age }}", "s",
                "duration", "measurement", "mdi:timer-sand", 0,
                mqttTopicLink().c_str());
  publishSensor("rssi", entityName("RSSI"), "{{ value_json.rssi }}", "dBm",
                "signal_strength", "measurement", "mdi:signal", 0,
                mqttTopicLink().c_str());
  publishSensor("snr", entityName("SNR"), "{{ value_json.snr }}", "dB", nullptr,
                "measurement", "mdi:signal-variant", 1,
                mqttTopicLink().c_str());
  publishSensor("last_seen", entityName("last seen"), "{{ value_json.last_seen }}",
                nullptr, "timestamp", nullptr, "mdi:clock-outline", -1,
                mqttTopicLink().c_str());
}

void clearLinkDiscovery() {
  clearHaConfig("sensor", "link_age");
  clearHaConfig("sensor", "rssi");
  clearHaConfig("sensor", "snr");
  clearHaConfig("sensor", "last_seen");
}

void publishDiscovery() {
  if (browserProfileLoaded()) {
    browserProfileClearDiscovery();
    browserProfilePublishDiscovery();
    packDiscoveryCleared = false;
  } else if (!packDiscoveryCleared) {
    browserProfileClearDiscovery();
    packDiscoveryCleared = true;
  }

  clearLinkDiscovery();
  publishLinkDiscovery();

  const char *activeId =
      browserProfileLoaded() ? browserProfileId() : "none";
  strncpy(lastDiscoveryProfile, activeId, sizeof(lastDiscoveryProfile) - 1);
  lastDiscoveryProfile[sizeof(lastDiscoveryProfile) - 1] = '\0';
  discoveryInitialized = true;
}

void mqttCallback(char *topic, byte *payload, unsigned int length) {
  String t(topic);
  String p;
  p.reserve(length);
  for (unsigned int i = 0; i < length; ++i) p += static_cast<char>(payload[i]);
  p.trim();
  Serial.printf("[mqtt] %s -> %s\n", t.c_str(), p.c_str());

  if (t == mqttTopicCommand("query")) {
    nextDpQueryMs = millis();
    return;
  }
  if (t == browserProfileCommandTopic()) {
    if (!browserProfileSelected()) {
      rejectCommand("pack", "no browser profile selected");
      return;
    }
    if (p.length() == 0) {
      rejectCommand("pack", "empty DP command");
      return;
    }
    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, p.c_str());
    if (err || !doc.is<JsonObject>()) {
      rejectCommand("pack", "payload must be a JSON dps object");
      return;
    }
    String fragment = p;
    if (doc["dps"].is<JsonObject>()) {
      fragment = "";
      serializeJson(doc["dps"], fragment);
    }
    sendDpSet(fragment, "pack");
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
  initLoraConfigOnBoot();
  browserProfileInit();
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

  // Let WiFi/MQTT come up before asking the device for its first snapshot.
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
  if (!remoteOnline || loraConfigBusy()) return;
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
  serviceLoraConfig();
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
