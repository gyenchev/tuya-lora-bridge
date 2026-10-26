#pragma once

// Compile-time settings for the base / receiver node.
// WiFi, MQTT and LoRa PSK values are stored in NVS and configured through the
// web UI.

// The generic ttgo-lora32 board variant targets an older revision and defines
// different OLED/LoRa reset pins. This firmware uses the V2.1/T3 V1.6.1 wiring,
// so clear the variant defaults before applying the pins below.
#undef LORA_RST
#undef OLED_SDA
#undef OLED_SCL
#undef SD_SCK
#undef SD_MISO
#undef SD_MOSI
#undef SD_CS

#define FW_VERSION "0.2.1"

// ---------------------------------------------------------------------------
// Configuration web UI
// ---------------------------------------------------------------------------
#define WEB_UI_PORT 80
#define SETUP_AP_SSID     "TuyaLoraBase-Setup"
#define SETUP_AP_PASSWORD "tuya-lora-setup"
#define SETUP_AP_CHANNEL  6
#define WIFI_SETUP_FALLBACK_MS 20000UL  // start the setup AP after this long

// Recovery guard: if the node resets before staying up for BOOT_GUARD_HEALTHY_MS
// this many times in a row, keep the setup AP active until settings are saved or
// a factory reset is performed.
#define BOOT_GUARD_NVS_NAMESPACE "tuya-lora-guard"
#define BOOT_GUARD_RESET_THRESHOLD 4
#define BOOT_GUARD_HEALTHY_MS 60000UL

// ---------------------------------------------------------------------------
// TTGO LoRa32 V2.1 / T3 V1.6.1
// ---------------------------------------------------------------------------
#define LORA_SCK   5
#define LORA_MISO  19
#define LORA_MOSI  27
#define LORA_CS    18
#define LORA_RST   23
#define LORA_DIO0  26

#define LORA_FREQUENCY 868.1E6
#define LORA_SYNC_WORD 0xA6
#define LORA_TX_POWER  14
#define LORA_SF        7
#define LORA_BW        125000L
#define LORA_CR        5
#define LORA_PREAMBLE  8

// ---------------------------------------------------------------------------
// OLED (SSD1306 128x64)
// ---------------------------------------------------------------------------
#define OLED_WIDTH  128
#define OLED_HEIGHT 64
#define OLED_ADDR   0x3C
#define OLED_SDA    21
#define OLED_SCL    22

// ---------------------------------------------------------------------------
// SD card firmware update (TTGO LoRa32 V2.1 / T3 V1.6.1 slot)
// ---------------------------------------------------------------------------
#define SD_SCK      14
#define SD_MISO     2
#define SD_MOSI     15
#define SD_CS       13
#define SD_UPDATE_WAIT_MS 10000UL
#define SD_FIRMWARE_FILE  "/base-node.bin"

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------
#define OFFLINE_AFTER_MS     600000UL   // no valid LoRa frame for 10 min
#define DP_QUERY_INTERVAL_MS 60000UL    // full DP snapshot interval
#define DP_QUERY_TIMEOUT_MS  30000UL    // give up on a snapshot after this
#define DP_CHUNK_TIMEOUT_MS  1200UL     // re-send missing-chunk ACK after this
#define DP_ACK_RETRIES       6
#define WIFI_RETRY_MS        10000UL
#define MQTT_RECONNECT_MS    5000UL
#define MQTT_KEEPALIVE_S     30
#define MQTT_SOCKET_TIMEOUT_S 5
#define MQTT_BUFFER_SIZE     2048
#define DISPLAY_REFRESH_MS   500UL
#define DISPLAY_ON_MS        600000UL   // turn the OLED off 10 min after boot
#define SPLASH_MS            3000UL
#define LINK_STATE_REFRESH_MS 10000UL   // refresh RSSI/SNR age without new frames

// Europe/Sofia. Used for the OLED clock and NTP.
#define TIMEZONE "EET-2EEST,M3.5.0/3,M10.5.0/4"

// ---------------------------------------------------------------------------
// MQTT / Home Assistant
// ---------------------------------------------------------------------------
#define MQTT_TOPIC_STATE        "tuya-lora/charger/state"
#define MQTT_TOPIC_LINK         "tuya-lora/charger/link"
#define MQTT_TOPIC_AVAILABILITY "tuya-lora/charger/availability"
#define MQTT_TOPIC_RESULT       "tuya-lora/charger/command/result"
#define MQTT_TOPIC_DEBUG        "tuya-lora/charger/debug"
#define MQTT_TOPIC_DPS          "tuya-lora/charger/dps"

#define MQTT_TOPIC_CMD_CHARGE  "tuya-lora/charger/command/charge"
#define MQTT_TOPIC_CMD_START   "tuya-lora/charger/command/start"
#define MQTT_TOPIC_CMD_STOP    "tuya-lora/charger/command/stop"
#define MQTT_TOPIC_CMD_CURRENT "tuya-lora/charger/command/current_limit"
#define MQTT_TOPIC_CMD_DELAY   "tuya-lora/charger/command/delay"
#define MQTT_TOPIC_CMD_MODE    "tuya-lora/charger/command/mode"
#define MQTT_TOPIC_CMD_QUERY   "tuya-lora/charger/command/query"

#define HA_DISCOVERY_PREFIX "homeassistant"

// Stable internal identifier used in discovery topics and device identifiers.
// Keep this unchanged so Home Assistant updates the existing device instead of
// creating a second one.
#define HA_DEVICE_ID        "tuya_lora_bridge"

// Friendly name shown in Home Assistant.
#define HA_DEVICE_NAME      "Tuya charger"

// Control is enabled, but the base only allows a command after it has seen a
// valid DP snapshot and detected a supported profile. Unknown profiles reject
// control until the DP map is confirmed.
#define ENABLE_COMMANDS 1

#define CHARGER_MIN_CURRENT 6
#define CHARGER_MAX_CURRENT 32
#define CHARGER_MIN_DELAY_H 0
#define CHARGER_MAX_DELAY_H 15

// Control DP numbers. These are the best-known values from public Tuya charger
// profiles; confirm them against tuya-lora/charger/dps before relying on control.
#define DP_SINGLE_CHARGE_ID  112
#define DP_SINGLE_CURRENT_ID 102
#define DP_SINGLE_DELAY_ID   104
#define DP_SINGLE_MODE_ID    14

#define DP_THREE_CHARGE_ID   124
#define DP_THREE_CURRENT_ID  115
#define DP_THREE_DELAY_ID    118
#define DP_THREE_MODE_ID     14

#define DP_OBZ2_CHARGE_ID    124
#define DP_OBZ2_CURRENT_ID   115
