/*
 * Tuya LoRa Bridge — remote node.
 *
 * What it does:
 *   1. Starts a 2.4 GHz WPA2 access point with the same SSID/password as the
 *      dummy hotspot the device was paired with.
 *   2. Finds the device on the AP and probes TCP :6668.
 *   3. Acts as a Tuya DP proxy over LoRa: answers DP_QUERY with the raw DP JSON
 *      in chunks, and applies DP_SET by sending the received `dps` fragment as
 *      a Tuya CONTROL/CONTROL_NEW frame.
 *
 * It deliberately does not know which DP means charging, current, or delay.
 * That mapping belongs to the base node.
 */

#include <WiFi.h>
#include <Wire.h>
#include <SPI.h>
#include <LoRa.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <esp_wifi_ap_get_sta_list.h>

#include "boot_guard.h"
#include "config.h"
#include "config_store.h"
#include "tuya_client.h"
#include "lora_protocol.h"
#include "sd_update.h"
#include "web_ui.h"

using namespace tuya_lora;

Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);
TuyaClient tuya;
tuya_lora::LoraCodec lora;

IPAddress apIP;
IPAddress deviceIP;
bool deviceFound = false;
bool useStaticIp = false;
uint8_t stationCount = 0;
IPAddress stationIPs[AP_MAX_CLIENTS];
uint8_t stationMACs[AP_MAX_CLIENTS][6];
uint8_t dpPage = 0;

uint32_t lastDisplayMs = 0;
uint32_t lastPageMs = 0;
uint32_t lastStationMs = 0;
uint32_t lastScanMs = 0;
uint32_t lastConnectMs = 0;
uint32_t lastQueryMs = 0;
uint32_t lastDeviceSeenMs = 0;
bool everConnected = false;

bool loraReady = false;
bool loraHelloDone = false;
bool displayReady = false;
bool displayAsleep = false;
uint32_t loraFrameCount = 0;
tuya_lora::ReplayGuard loraReplay;
uint8_t loraRx[160];

// DP snapshot proxy state. The base asks for a snapshot; this node performs
// the Tuya TCP query and returns the raw JSON in chunks. It does not map DPs.
bool dpSending = false;
uint16_t dpReqId = 0;
uint8_t dpChunkCount = 0;
uint32_t dpAckDeadline = 0;
uint8_t dpRetries = 0;
uint32_t dpLastAckBitmap = 0;
char dpJson[tuya_lora::DP_MAX_JSON + 1] = {0};
size_t dpJsonLen = 0;

static void putBE16(uint8_t *p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v >> 8);
  p[1] = static_cast<uint8_t>(v);
}

static uint16_t getBE16(const uint8_t *p) {
  return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

static uint32_t getBE32(const uint8_t *p) {
  return (static_cast<uint32_t>(p[0]) << 24) |
         (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) |
         static_cast<uint32_t>(p[3]);
}

String scanText = "";

// ---------------------------------------------------------------------------
// WiFi / discovery
// ---------------------------------------------------------------------------

bool tcpProbe(const IPAddress &ip, uint16_t port, uint32_t timeoutMs) {
  WiFiClient probe;
  probe.setTimeout(timeoutMs);
  bool ok = probe.connect(ip, port, timeoutMs);
  if (ok) probe.stop();
  return ok;
}

String macText(const uint8_t *mac) {
  char buf[18];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1],
           mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}

void refreshStations() {
  if (millis() - lastStationMs < settings.stationRefreshMs) return;
  lastStationMs = millis();

  wifi_sta_list_t list;
  if (esp_wifi_ap_get_sta_list(&list) != ESP_OK) return;
  stationCount = static_cast<uint8_t>(list.num);

  wifi_sta_mac_ip_list_t ipList;
  if (esp_wifi_ap_get_sta_list_with_ip(&list, &ipList) != ESP_OK) return;

  uint8_t n = static_cast<uint8_t>(ipList.num);
  uint8_t maxClients = settings.apMaxClients;
  if (maxClients < 1) maxClients = 1;
  if (maxClients > AP_MAX_CLIENTS) maxClients = AP_MAX_CLIENTS;
  if (n > maxClients) n = maxClients;
  stationCount = n;
  for (uint8_t i = 0; i < n; ++i) {
    memcpy(stationMACs[i], ipList.sta[i].mac, 6);
    stationIPs[i] = IPAddress(ipList.sta[i].ip.addr);
  }
  for (uint8_t i = n; i < AP_MAX_CLIENTS; ++i) {
    stationIPs[i] = IPAddress(0, 0, 0, 0);
  }

  if (deviceFound && !useStaticIp) {
    bool stillThere = false;
    for (uint8_t i = 0; i < n; ++i) {
      if (stationIPs[i] == deviceIP) {
        stillThere = true;
        break;
      }
    }
    if (stillThere) {
      lastDeviceSeenMs = millis();
    } else if (millis() - lastDeviceSeenMs > settings.deviceLostGraceMs) {
      Serial.println("[ap] device left the AP");
      deviceFound = false;
      tuya.disconnect();
      lastScanMs = 0;
    }
  }
}

bool findDeviceOnStations() {
  refreshStations();
  for (uint8_t i = 0; i < stationCount; ++i) {
    if (stationIPs[i] == IPAddress(0, 0, 0, 0)) continue;
    if (stationIPs[i] == apIP) continue;
    if (tcpProbe(stationIPs[i], settings.tuyaPort, 300)) {
      deviceIP = stationIPs[i];
      deviceFound = true;
      lastDeviceSeenMs = millis();
      Serial.print("[ap] device found on ");
      Serial.print(deviceIP);
      Serial.print(" (");
      Serial.print(macText(stationMACs[i]));
      Serial.println(")");
      return true;
    }
  }
  return false;
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
  display.print("REMOTE NODE");
  display.setCursor(8, 42);
  display.print("FW ");
  display.print(FW_VERSION);
  display.setCursor(8, 52);
  display.print("LoRa ");
  display.print(settings.loraFrequencyMhz, 1);
  display.print(" MHz");
  display.display();
  delay(settings.splashMs);
}

void drawScanProgress(uint8_t host) {
  if (!displayReady || displayAsleep) return;
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print("TUYA LORA scan");
  display.setCursor(0, 8);
  display.print("AP ");
  display.print(apIP);
  display.setCursor(0, 16);
  display.print("Clients ");
  display.print(stationCount);
  display.setCursor(0, 24);
  display.print(apIP[0]);
  display.print('.');
  display.print(apIP[1]);
  display.print('.');
  display.print(apIP[2]);
  display.print('.');
  display.print(host);
  display.drawRect(0, 40, OLED_WIDTH, 10, SSD1306_WHITE);
  int w = map(host, 2, 254, 0, OLED_WIDTH - 2);
  if (w > 0) display.fillRect(1, 41, w, 8, SSD1306_WHITE);
  display.setCursor(0, 56);
  display.print("FW ");
  display.print(FW_VERSION);
  display.display();
}

bool scanNetwork() {
  Serial.print("[scan] scanning ");
  Serial.print(apIP);
  Serial.println("/24 for TCP 6668");
  IPAddress base = apIP;
  for (int host = 2; host < 255; ++host) {
    if (host == base[3]) continue;
    IPAddress ip(base[0], base[1], base[2], host);
    if (tcpProbe(ip, settings.tuyaPort, settings.scanProbeTimeoutMs)) {
      deviceIP = ip;
      deviceFound = true;
      lastDeviceSeenMs = millis();
      Serial.print("[scan] found device on ");
      Serial.println(deviceIP);
      return true;
    }
    if ((host & 0x0F) == 0) {
      drawScanProgress(static_cast<uint8_t>(host));
      yield();
    }
  }
  return false;
}

// ---------------------------------------------------------------------------
// OLED
// ---------------------------------------------------------------------------

String clip(const String &s, size_t maxLen = 21) {
  if (s.length() <= maxLen) return s;
  return s.substring(0, maxLen);
}

void drawNoDevice() {
  if (!displayReady || displayAsleep) return;
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print("TUYA LORA REMOTE");
  display.setCursor(0, 8);
  display.print(webUiSetupApActive() ? "SETUP AP" : "AP");
  display.setCursor(0, 16);
  display.print(clip(webUiSetupApActive() ? String(settings.setupApSsid)
                                         : String(settings.apSsid)));
  display.setCursor(0, 26);
  display.print(apIP);
  display.print("  web");
  display.setCursor(0, 36);
  if (webUiSetupApActive()) {
    display.print("Configure in browser");
  } else {
    display.print("Waiting for device");
  }
  display.setCursor(0, 48);
  display.print(scanText);
  display.setCursor(0, 56);
  display.print("FW ");
  display.print(FW_VERSION);
  display.display();
}

void drawStatus() {
  if (!displayReady || displayAsleep) return;
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  uint8_t pages = 1;
  if (tuya.dpCount() > 0) {
    pages = (tuya.dpCount() + settings.dpPerPage - 1) / settings.dpPerPage;
    if (dpPage >= pages) dpPage = 0;
  } else {
    dpPage = 0;
  }

  String header = "TUYA LORA v";
  header += FW_VERSION;
  header += " P";
  header += String(dpPage + 1);
  header += "/";
  header += String(pages);
  display.setCursor(0, 0);
  display.print(clip(header));

  display.setCursor(0, 8);
  display.print("DEV ");
  if (deviceFound) {
    display.print(deviceIP);
  } else {
    display.print("not found");
  }
  display.print(" AP:");
  display.print(stationCount);

  display.setCursor(0, 16);
  if (tuya.connected()) {
    display.print("Tuya OK v");
    display.print(tuya.versionText());
    display.print("  ");
    display.print(tuya.dpCount());
    display.print("dp");
  } else if (!tuya.canQuery()) {
    display.print("Tuya: set in web UI");
  } else if (tuya.lastError().length()) {
    display.print(clip(String("Tuya ERR ") + tuya.lastError()));
  } else {
    display.print("Tuya connecting...");
  }

  display.drawFastHLine(0, 23, OLED_WIDTH, SSD1306_WHITE);

  if (tuya.dpCount() == 0) {
    display.setCursor(0, 28);
    display.print("No data points yet");
    display.setCursor(0, 38);
    if (!tuya.canQuery()) {
      display.print("Set Tuya keys in");
      display.setCursor(0, 48);
      display.print("the web UI");
    } else if (tuya.lastError().length()) {
      display.print(clip(tuya.lastError(), 21));
      display.setCursor(0, 48);
      display.print("check key / version");
    } else {
      display.print("Waiting for response");
    }
  } else {
    for (uint8_t i = 0; i < settings.dpPerPage; ++i) {
      uint8_t idx = dpPage * settings.dpPerPage + i;
      if (idx >= tuya.dpCount()) break;
      String line = tuya.dps()[idx].id + " " + tuya.dps()[idx].value;
      display.setCursor(0, 24 + i * 8);
      display.print(clip(line));
    }
  }

  display.display();
}

void updateDisplay() {
  if (!displayReady || displayAsleep) return;
  if (!deviceFound) {
    drawNoDevice();
  } else {
    drawStatus();
  }
}

// ---------------------------------------------------------------------------
// Tuya connection loop
// ---------------------------------------------------------------------------

void serviceTuya() {
  if (!deviceFound) {
    tuya.disconnect();
    return;
  }
  if (!tuya.canQuery()) {
    tuya.disconnect();
    return;
  }

  if (!tuya.connected()) {
    if (millis() - lastConnectMs < settings.tuyaReconnectIntervalMs) return;
    lastConnectMs = millis();
    Serial.print("[tuya] connecting to ");
    Serial.print(deviceIP);
    Serial.print(":");
    Serial.println(settings.tuyaPort);
    if (tuya.connect(deviceIP, settings.tuyaPort)) {
      everConnected = true;
      lastQueryMs = millis();
      Serial.print("[tuya] connected, version ");
      Serial.println(tuya.versionText());
      Serial.print("[tuya] ");
      Serial.print(tuya.dpCount());
      Serial.println(" DPs in first read");
      Serial.println(tuya.lastJson());
    } else {
      Serial.print("[tuya] connect failed: ");
      Serial.println(tuya.lastError());
    }
    return;
  }

  tuya.poll();

  if (millis() - lastQueryMs >= settings.tuyaPollIntervalMs) {
    lastQueryMs = millis();
    if (tuya.query()) {
      Serial.print("[tuya] v");
      Serial.print(tuya.versionText());
      Serial.print("  ");
      Serial.print(tuya.dpCount());
      Serial.print(" DPs: ");
      Serial.println(tuya.lastJson());
      for (uint8_t i = 0; i < tuya.dpCount(); ++i) {
        Serial.print("  DP ");
        Serial.print(tuya.dps()[i].id);
        Serial.print(" = ");
        Serial.println(tuya.dps()[i].value);
      }
    } else {
      Serial.print("[tuya] query failed: ");
      Serial.println(tuya.lastError());
      tuya.disconnect();
    }
  }
}

// ---------------------------------------------------------------------------
// LoRa link
// ---------------------------------------------------------------------------

void initLora() {
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_DIO0);

  for (int attempt = 1; attempt <= 3 && !loraReady; ++attempt) {
    Serial.printf("[lora] init attempt %d\n", attempt);
    loraReady = LoRa.begin(settings.loraFrequencyMhz * 1000000.0);
    if (!loraReady) delay(500);
  }

  if (!loraReady) {
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

void sendLoraFrame(uint8_t type, const uint8_t *payload, size_t payloadLen,
                   bool listen = true) {
  if (!loraReady || !lora.hasKey()) return;

  uint8_t frame[tuya_lora::LORA_MAX_FRAME];
  size_t frameLen = 0;
  uint32_t seq = lora.nextSeq();

  if (!lora.encode(type, 0, seq, payload, payloadLen,
                   tuya_lora::DIR_REMOTE_TO_BASE, frame, sizeof(frame), frameLen)) {
    Serial.println("[lora] encode failed");
    return;
  }

  LoRa.beginPacket();
  LoRa.write(frame, frameLen);
  bool ok = LoRa.endPacket();
  if (listen) LoRa.receive();

  if (ok) {
    loraFrameCount++;
    Serial.printf("[lora] tx type=0x%02X seq=%lu len=%u\n", type,
                  (unsigned long)seq, (unsigned)frameLen);
  } else {
    Serial.println("[lora] tx failed");
  }
}

void sendLoraHello() {
  tuya_lora::HelloData hello;
  hello.role = 1;  // remote node
  hello.fwVersion = 1;
  hello.bootId = lora.bootId();

  uint8_t payload[8];
  size_t n = tuya_lora::encodeHello(hello, payload, sizeof(payload));
  sendLoraFrame(tuya_lora::MSG_HELLO, payload, n);
}

void startDpResponse(uint16_t reqId, const String &json) {
  dpReqId = reqId;
  dpJsonLen = json.length();
  if (dpJsonLen > tuya_lora::DP_MAX_JSON) dpJsonLen = tuya_lora::DP_MAX_JSON;
  memcpy(dpJson, json.c_str(), dpJsonLen);
  dpJson[dpJsonLen] = '\0';

  dpChunkCount = (dpJsonLen + tuya_lora::DP_CHUNK_DATA_MAX - 1) /
                 tuya_lora::DP_CHUNK_DATA_MAX;
  if (dpChunkCount == 0) dpChunkCount = 1;
  if (dpChunkCount > tuya_lora::DP_MAX_CHUNKS) {
    dpChunkCount = tuya_lora::DP_MAX_CHUNKS;
  }
  size_t maxLen = static_cast<size_t>(dpChunkCount) *
                  tuya_lora::DP_CHUNK_DATA_MAX;
  if (dpJsonLen > maxLen) {
    dpJsonLen = maxLen;
    dpJson[dpJsonLen] = '\0';
  }

  dpRetries = 0;
  dpLastAckBitmap = 0;
  Serial.printf("[lora] DP response req=%u %u bytes %u chunks\n", dpReqId,
                (unsigned)dpJsonLen, dpChunkCount);
  sendDpChunks(0);
}

void sendDpChunks(uint32_t ackBitmap) {
  if (!loraReady || dpChunkCount == 0) return;

  for (uint8_t i = 0; i < dpChunkCount; ++i) {
    if (ackBitmap & (1UL << i)) continue;

    size_t offset = static_cast<size_t>(i) * tuya_lora::DP_CHUNK_DATA_MAX;
    if (offset >= dpJsonLen && dpJsonLen != 0) continue;
    size_t len = (offset < dpJsonLen) ? (dpJsonLen - offset) : 0;
    if (len > tuya_lora::DP_CHUNK_DATA_MAX) len = tuya_lora::DP_CHUNK_DATA_MAX;

    uint8_t payload[4 + tuya_lora::DP_CHUNK_DATA_MAX];
    putBE16(payload, dpReqId);
    payload[2] = i;
    payload[3] = dpChunkCount;
    memcpy(payload + 4, dpJson + offset, len);
    sendLoraFrame(tuya_lora::MSG_DP_CHUNK, payload, 4 + len, false);
  }

  LoRa.receive();
  dpAckDeadline = millis() + settings.dpAckTimeoutMs;
  dpSending = true;
  Serial.printf("[lora] DP chunks sent req=%u missing=0x%08lX\n", dpReqId,
                (unsigned long)ackBitmap);
}

void handleDpAck(uint16_t reqId, uint32_t bitmap) {
  if (!dpSending || reqId != dpReqId) return;

  uint32_t mask = (dpChunkCount >= 32) ? 0xFFFFFFFFUL
                                       : ((1UL << dpChunkCount) - 1);
  if ((bitmap & mask) == mask) {
    dpSending = false;
    Serial.printf("[lora] DP ACK complete req=%u\n", reqId);
    return;
  }

  if (dpRetries >= settings.dpAckRetries) {
    dpSending = false;
    Serial.printf("[lora] DP ACK incomplete req=%u, retries exhausted\n",
                  reqId);
    return;
  }

  dpRetries++;
  dpLastAckBitmap = bitmap;
  Serial.printf("[lora] DP ACK partial req=%u bitmap=0x%08lX retry=%u\n",
                reqId, (unsigned long)bitmap, dpRetries);
  sendDpChunks(bitmap);
}

void handleDpQuery(uint16_t reqId) {
  Serial.printf("[lora] DP query req=%u\n", reqId);

  bool ok = false;
  if (deviceFound && tuya.canQuery()) {
    if (!tuya.connected() &&
        millis() - lastConnectMs >= settings.tuyaReconnectIntervalMs) {
      lastConnectMs = millis();
      if (tuya.connect(deviceIP, settings.tuyaPort)) {
        everConnected = true;
        Serial.println("[tuya] connected for DP query");
      }
    }
    if (tuya.connected()) {
      ok = tuya.query();
      lastQueryMs = millis();
    }
  }

  String json;
  if (ok && tuya.lastJson().length() > 0) {
    json = tuya.lastJson();
  } else {
    String err;
    if (!deviceFound) {
      err = "device not found";
    } else if (!tuya.canQuery()) {
      err = "tuya credentials missing";
    } else if (!tuya.connected()) {
      err = "tuya not connected";
    } else if (ok && tuya.lastJson().length() == 0) {
      err = "empty response";
    } else if (tuya.lastError().length()) {
      err = tuya.lastError();
    } else {
      err = "query failed";
    }
    err.replace("\"", "'");
    err.replace("\\", "/");
    json = "{\"error\":\"" + err + "\"}";
  }

  startDpResponse(reqId, json);
}

void sendLoraAck(uint32_t ackedSeq, uint8_t result, uint16_t detail = 0) {
  tuya_lora::AckData ack;
  ack.result = result;
  ack.ackedSeq = ackedSeq;
  ack.detail = detail;
  uint8_t payload[16];
  size_t n = tuya_lora::encodeAck(ack, payload, sizeof(payload));
  sendLoraFrame(tuya_lora::MSG_ACK, payload, n);
}

void sendLoraLog(const String &text) {
  String msg = text;
  if (msg.length() > tuya_lora::LORA_MAX_PAYLOAD) {
    msg = msg.substring(0, tuya_lora::LORA_MAX_PAYLOAD);
  }
  sendLoraFrame(tuya_lora::MSG_LOG,
                reinterpret_cast<const uint8_t *>(msg.c_str()), msg.length());
}

void handleDpSet(uint32_t seq, const uint8_t *payload, size_t payloadLen) {
  if (payloadLen == 0 || payloadLen > tuya_lora::LORA_MAX_PAYLOAD) {
    sendLoraAck(seq, tuya_lora::ACK_INVALID);
    return;
  }

  char json[tuya_lora::LORA_MAX_PAYLOAD + 1];
  memcpy(json, payload, payloadLen);
  json[payloadLen] = '\0';
  Serial.printf("[lora] DP set seq=%lu %s\n", (unsigned long)seq, json);

  // The base has already accepted the snapshot (commands are rejected while a
  // query is pending), so stop any late chunk retransmission before we block on
  // the Tuya control exchange.
  dpSending = false;

  if (!deviceFound || !tuya.canQuery()) {
    sendLoraAck(seq, tuya_lora::ACK_REJECTED, 1);
    return;
  }

  if (!tuya.connected() &&
      millis() - lastConnectMs >= settings.tuyaReconnectIntervalMs) {
    lastConnectMs = millis();
    if (tuya.connect(deviceIP, settings.tuyaPort)) everConnected = true;
  }
  if (!tuya.connected()) {
    sendLoraAck(seq, tuya_lora::ACK_HARDWARE_ERR, 2);
    return;
  }

  lastQueryMs = millis();
  if (tuya.setDps(String(json))) {
    sendLoraAck(seq, tuya_lora::ACK_OK);
    Serial.println("[tuya] DP set accepted");
  } else {
    sendLoraAck(seq, tuya_lora::ACK_REJECTED, 3);
    sendLoraLog(tuya.lastError());
    Serial.print("[tuya] DP set failed: ");
    Serial.println(tuya.lastError());
  }
}

void pollLoraCommands() {
  if (!loraReady) return;
  int packetSize = LoRa.parsePacket();
  if (packetSize <= 0) return;

  size_t n = 0;
  while (LoRa.available()) {
    int b = LoRa.read();
    if (b < 0) break;
    if (n < sizeof(loraRx)) loraRx[n++] = static_cast<uint8_t>(b);
  }
  LoRa.receive();

  uint8_t type = 0;
  uint8_t flags = 0;
  uint32_t seq = 0;
  uint32_t bootId = 0;
  const uint8_t *payload = nullptr;
  size_t payloadLen = 0;
  if (!lora.decode(loraRx, n, tuya_lora::DIR_BASE_TO_REMOTE, type, flags, seq,
                   bootId, payload, payloadLen)) {
    Serial.printf("[lora] bad base frame len=%u\n", (unsigned)n);
    return;
  }

  if (!loraReplay.accept(bootId, seq)) {
    Serial.printf("[lora] replay base boot=%08lX seq=%lu\n",
                  (unsigned long)bootId, (unsigned long)seq);
    return;
  }

  Serial.printf("[lora] rx base type=0x%02X seq=%lu len=%u\n", type,
                (unsigned long)seq, (unsigned)payloadLen);

  switch (type) {
    case tuya_lora::MSG_DP_QUERY:
      if (payloadLen >= 2) handleDpQuery(getBE16(payload));
      break;
    case tuya_lora::MSG_DP_ACK:
      if (payloadLen >= 6) handleDpAck(getBE16(payload), getBE32(payload + 2));
      break;
    case tuya_lora::MSG_DP_SET:
      handleDpSet(seq, payload, payloadLen);
      break;
    default:
      Serial.printf("[lora] unhandled base type=0x%02X\n", type);
      break;
  }
}

void serviceLora() {
  if (!loraReady) return;

  if (!loraHelloDone) {
    sendLoraHello();
    loraHelloDone = true;
  }

  pollLoraCommands();

  if (dpSending && static_cast<int32_t>(millis() - dpAckDeadline) >= 0) {
    if (dpRetries >= settings.dpAckRetries) {
      dpSending = false;
      Serial.printf("[lora] DP ACK timeout req=%u, giving up\n", dpReqId);
    } else {
      dpRetries++;
      dpLastAckBitmap = 0;
      Serial.printf("[lora] DP ACK timeout req=%u, resend all retry=%u\n",
                    dpReqId, dpRetries);
      sendDpChunks(0);
    }
  }
}

// ---------------------------------------------------------------------------
// Arduino entry points
// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("Tuya LoRa remote node " FW_VERSION);

  Wire.begin(OLED_SDA, OLED_SCL);
  displayReady = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  if (!displayReady) {
    Serial.println("[oled] init failed");
  } else {
    display.clearDisplay();
    display.display();
  }

  tuya_lora::loadSettings();
  tuya_lora::maybeSdFirmwareUpdate(tuya_lora::settings.sdFirmwareFile,
                                drawSdUpdate);
  tuya_lora::bootGuardOnBoot();
  drawSplash();

  if (!lora.setKeyHex(tuya_lora::settings.loraPsk)) {
    Serial.println("[lora] invalid PSK (need 32 hex chars)");
  } else if (lora.isZeroKey()) {
    Serial.println("[lora] WARNING: PSK is all zero; set a random key");
  }
  uint32_t loraBootId = esp_random();
  if (loraBootId == 0) loraBootId = 1;
  lora.setBootId(loraBootId);
  Serial.printf("[lora] local boot_id %08lX\n", (unsigned long)loraBootId);

  initLora();

  tuya_lora::webUiBegin();
  apIP = WiFi.softAPIP();

  tuya.setCredentials(tuya_lora::settings.tuyaDeviceId,
                      tuya_lora::settings.tuyaLocalKey,
                      tuya_lora::settings.tuyaVersion);
  Serial.print("[tuya] device id: ");
  Serial.println(tuya.hasDeviceId() ? "set" : "MISSING");
  Serial.print("[tuya] local key: ");
  Serial.println(tuya.hasLocalKey() ? "set" : "MISSING");
  Serial.print("[tuya] protocol: ");
  Serial.println(tuya.versionText());

  if (tuya_lora::settings.tuyaStaticIp[0] != '\0') {
    IPAddress staticIp;
    if (staticIp.fromString(tuya_lora::settings.tuyaStaticIp)) {
      deviceIP = staticIp;
      deviceFound = true;
      useStaticIp = true;
      Serial.print("[tuya] static IP ");
      Serial.println(deviceIP);
    } else {
      Serial.println("[tuya] invalid static IP in config; using discovery");
    }
  }

  lastDeviceSeenMs = millis();
  scanText = useStaticIp ? "static IP" : "starting...";
  updateDisplay();
}

void loop() {
  webUiLoop();
  tuya_lora::bootGuardLoop();
  refreshStations();

  if (!deviceFound) {
    if (!findDeviceOnStations()) {
      if (millis() - lastScanMs > settings.scanIntervalMs) {
        lastScanMs = millis();
        scanText = "full scan";
        if (scanNetwork()) {
          scanText = "found";
        } else {
          scanText = "no device yet";
          Serial.println("[scan] no device found");
        }
      }
    }
  }

  serviceTuya();
  serviceLora();

  if (!webUiSetupApActive() && displayReady && !displayAsleep &&
      millis() >= settings.displayOnMs) {
    sleepDisplay();
  }

  if (!displayAsleep && millis() - lastDisplayMs >= settings.displayRefreshMs) {
    lastDisplayMs = millis();
    updateDisplay();
  }

  if (tuya.dpCount() > 0 && millis() - lastPageMs >= settings.dpPageIntervalMs) {
    lastPageMs = millis();
    uint8_t pages = (tuya.dpCount() + settings.dpPerPage - 1) / settings.dpPerPage;
    if (pages > 1) dpPage = (dpPage + 1) % pages;
  }

  delay(5);
}
