#pragma once

// Compile-time settings for the remote node.
// AP credentials, Tuya keys and LoRa PSK are stored in NVS and configured
// through the web UI.

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

#ifndef FW_VERSION
#define FW_VERSION "0.5.0"
#endif

// ---------------------------------------------------------------------------
// Configuration web UI
// ---------------------------------------------------------------------------
#define WEB_UI_PORT 80
#define SETUP_AP_SSID     "TuyaLoraRemote-Setup"
#define SETUP_AP_PASSWORD "tuya-lora-setup"
#define SETUP_AP_CHANNEL  6

// Recovery guard: if the node resets before staying up for BOOT_GUARD_HEALTHY_MS
// this many times in a row, keep the setup AP active until settings are saved or
// a factory reset is performed.
#define BOOT_GUARD_NVS_NAMESPACE "tuya-lora-guard"
#define BOOT_GUARD_RESET_THRESHOLD 4
#define BOOT_GUARD_HEALTHY_MS 60000UL

// ---------------------------------------------------------------------------
// OLED (TTGO LoRa32 on-board SSD1306)
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
#define SD_FIRMWARE_FILE  "/remote-node.bin"

// ---------------------------------------------------------------------------
// WiFi AP / device discovery
// ---------------------------------------------------------------------------
#define AP_MAX_CLIENTS 4

// Tuya local API
#define TUYA_PORT 6668

// Timeout for one TCP :6668 probe during a network scan.
#define SCAN_PROBE_TIMEOUT_MS 60

// Full /24 scan is only a fallback. The ESP32 IDF can usually list AP clients
// together with their DHCP addresses, so the normal path is a single probe.
#define SCAN_INTERVAL_MS 30000

// Re-check the AP client list this often.
#define STATION_REFRESH_MS 2000

// If the device disappears from the AP client list, keep trying its old IP
// for this long before starting a new scan.
#define DEVICE_LOST_GRACE_MS 10000

// ---------------------------------------------------------------------------
// Tuya client
// ---------------------------------------------------------------------------
#define TUYA_CONNECT_TIMEOUT_MS    3000
#define TUYA_READ_TIMEOUT_MS       2500
#define TUYA_POLL_INTERVAL_MS      10000
#define TUYA_RECONNECT_INTERVAL_MS 5000

// ---------------------------------------------------------------------------
// LoRa link
// ---------------------------------------------------------------------------
#define LORA_SCK    5
#define LORA_MISO   19
#define LORA_MOSI   27
#define LORA_CS     18
#define LORA_RST    23
#define LORA_DIO0   26

#define LORA_FREQUENCY 868.1E6
#define LORA_SYNC_WORD 0xA6
#define LORA_TX_POWER  14
#define LORA_SF        7
#define LORA_BW        125000L
#define LORA_CR        5
#define LORA_PREAMBLE  8

// DP snapshot transport (base -> device query, device -> base chunks).
#define DP_ACK_TIMEOUT_MS 2500UL
#define DP_ACK_RETRIES    3

// ---------------------------------------------------------------------------
// Display
// ---------------------------------------------------------------------------
#define DISPLAY_REFRESH_MS 500
#define DISPLAY_ON_MS      600000UL   // turn the OLED off 10 min after boot
#define SPLASH_MS          3000UL
#define DP_PAGE_INTERVAL_MS 3000
#define DP_PER_PAGE 5
#define DP_MAX 48
