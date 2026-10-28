# Firmware

The project contains two Arduino sketches:

- **Remote node** — hosts a WiFi AP for the Tuya device, discovers it on the AP,
  speaks the Tuya local TCP protocol, and answers LoRa DP snapshots and commands.
- **Base node** — talks to the remote over LoRa, owns the DP map and control
  logic, publishes MQTT state and Home Assistant discovery, and serves the local
  status UI.

The base node owns all device-specific interpretation. The remote node only
forwards raw Tuya DP data.

## Layout

```text
firmware/
├── README.md
├── base-node/
│   ├── base-node.ino
│   ├── boot_guard.cpp
│   ├── boot_guard.h
│   ├── config.h
│   ├── config_store.cpp
│   ├── config_store.h
│   ├── browser_profile.cpp
│   ├── browser_profile.h
│   ├── entities.cpp
│   ├── entities.h
│   ├── ha_discovery.cpp
│   ├── ha_discovery.h
│   ├── lora_protocol.cpp
│   ├── lora_protocol.h
│   ├── partitions.csv
│   ├── profiles.cpp
│   ├── profiles.h
│   ├── sd_update.cpp
│   ├── sd_update.h
│   ├── web_assets.cpp
│   ├── web_assets.h
│   ├── web_ui.cpp
│   └── web_ui.h
└── remote-node/
    ├── remote-node.ino
    ├── boot_guard.cpp
    ├── boot_guard.h
    ├── config.h
    ├── config_store.cpp
    ├── config_store.h
    ├── lora_protocol.cpp
    ├── lora_protocol.h
    ├── sd_update.cpp
    ├── sd_update.h
    ├── tuya_client.cpp
    ├── tuya_client.h
    ├── web_ui.cpp
    └── web_ui.h
```

`lora_protocol` and `sd_update` are duplicated in both sketch directories
because Arduino only compiles sources inside the sketch directory. Keep the two
copies synchronized.

## Requirements

| Library / component | Version | Use |
|---|---|---|
| Arduino ESP32 core | 3.1.0 | ESP32, WiFi, WebServer, Preferences, SD, Update |
| LoRa (Sandeep Mistry) | 0.8.0 | SX1276 radio |
| Adafruit SSD1306 | 2.5.13 | OLED |
| Adafruit GFX Library | 1.11.11 | graphics |
| ArduinoJson | 7.2.1 | Tuya and MQTT JSON |
| PubSubClient (Nick O'Leary) | 2.8.x | MQTT on the base node |
| mbedTLS | ESP32 core | AES-128-GCM |

## Build and flash

```bash
REMOTE_FQBN=esp32:esp32:ttgo-lora32:UploadSpeed=460800
BASE_FQBN=esp32:esp32:ttgo-lora32:PartitionScheme=min_spiffs,UploadSpeed=460800
PORT=/dev/cu.usbserial-REPLACE_ME
LIBS="$HOME/Documents/Arduino/libraries"

arduino-cli compile --fqbn "$REMOTE_FQBN" --libraries "$LIBS" firmware/remote-node
arduino-cli upload  --fqbn "$REMOTE_FQBN" --libraries "$LIBS" -p "$PORT" firmware/remote-node

arduino-cli compile --fqbn "$BASE_FQBN" --libraries "$LIBS" firmware/base-node
arduino-cli upload  --fqbn "$BASE_FQBN" --libraries "$LIBS" -p "$PORT" firmware/base-node
```

`UploadSpeed=460800` is reliable on the TTGO LoRa32 boards used for this
project. The base sketch contains its own `partitions.csv` (two 1.75 MB OTA app
partitions and 384 KB LittleFS); it overrides the FQBN partition scheme
automatically. Back up NVS at `0x9000`/`0x5000` before the first switch to this
table. If upload fails, hold **BOOT**, press **RESET**, release **BOOT**, and
retry at a lower speed.

## First start

Each node loads its settings from ESP32 NVS. There are no compiled-in
credentials.

If required values are missing, the node starts a setup AP:

| Node | Setup AP | Password | URL |
|---|---|---|---|
| Base | `TuyaLoraBase-Setup` | `tuya-lora-setup` | `http://192.168.4.1/` |
| Remote | `TuyaLoraRemote-Setup` | `tuya-lora-setup` | `http://192.168.4.1/` |

The default setup AP password is public. Change it in the **System** tab after
the first successful configuration.

The remote node also starts its setup AP when the configured device AP
credentials are missing. The base node starts its setup AP when the configured
home WiFi cannot be reached within the fallback delay (20 seconds by default).

## Configuration web UI

Each node serves:

- a read-only **Status** page at `/`, refreshed from `/api/status`;
- a **LoRa** page at `/lora` with base-local transfer telemetry (frames,
  wire/payload bytes, burst and per-frame speeds, last frames, 60-minute duty
  cycle and DP snapshot speed) that is not published to MQTT or Home Assistant;
- a tabbed **Settings** page at `/settings` with a **Save and reboot** button.

All runtime settings are stored in NVS. The web UI is unauthenticated, so keep
the nodes on trusted networks.

### Base node settings

| Tab | Settings |
|---|---|
| WiFi | Home WiFi SSID/password and network scan |
| MQTT | Broker host, port, username, password, keepalive, socket timeout and buffer size. Use at least 8192 bytes for browser packs; larger discovery configs are single MQTT messages. |
| LoRa | PSK (32 hex characters), frequency, sync word, TX power, spreading factor, bandwidth, coding rate and preamble |
| Home Assistant | Device name, timezone, discovery prefix and stable device identifier |
| Timing | Offline timeout, snapshot/chunk timeouts, retry intervals, WiFi/MQTT reconnect intervals and display timings |
| Control | Command enable. Command DPs and value ranges come from the active profile. |
| Profile | The single active profile, its type/entity count and a link to `/profiles`. The profile editor loads a device from the online catalog, shows the generated profile JSON in an editable textarea, and saves it to the base. |
| System | Setup AP credentials, web UI port, SD firmware filename and SD update countdown |

### Remote node settings

| Tab | Settings |
|---|---|
| Access point | Device AP SSID/password/channel and maximum AP clients |
| Tuya | Device ID, 16-character local key, protocol version, optional static IP, TCP port, timeouts and polling intervals |
| LoRa | PSK (32 hex characters), frequency, sync word, TX power, spreading factor, bandwidth, coding rate, preamble, DP ACK timeout and retries |
| Discovery | AP scan interval, TCP probe timeout, station refresh interval and device-lost grace period |
| Display | Splash duration, refresh interval, display-on time, DP page interval and DPs per page |
| System | Setup AP credentials, web UI port, SD firmware filename and SD update countdown |

Every field shows its compile-time default and a short explanation. Hardware pin
maps, OLED geometry and fixed protocol array sizes remain compile-time because
they describe the board or wire format.

If validation fails, the form reopens on the tab containing the error. The LoRa
PSK must be identical on both nodes. Changing the device AP SSID or password
requires re-pairing the Tuya device.

The profile editor is the single path for new devices. Open `/profiles`, choose
a descriptor from the online tuya-local catalog or paste/edit a profile JSON,
then save it. The base streams the pack as NDJSON and stores metadata plus one
small discovery file per entity in LittleFS. It supports all tuya-local entity
types; native MQTT platforms are used where they exist, and
media players/remotes/infrared are decomposed into standard MQTT entities. A
hidden **Raw DPs** sensor exposes every DP as an attribute, and the base
publishes per-DP topics under the raw snapshot topic.

There are no built-in or separately stored custom profiles. The JSON in the
editor is the one active profile; its telemetry, discovery entries and command
templates are all editable. Invalid profile JSON is rejected without replacing
the last valid profile.

### Setup AP recovery

Both nodes keep a rapid-boot counter in NVS. A boot that stays up for 60 seconds
is healthy and clears the counter. Four short boots in a row force the setup AP
on the next boot. The recovery state clears when settings are saved or a factory
reset is performed.

The threshold and healthy-uptime window are compile-time values in `config.h`:

```cpp
#define BOOT_GUARD_RESET_THRESHOLD 4
#define BOOT_GUARD_HEALTHY_MS      60000UL
```

## Remote node behavior

1. Starts a 2.4 GHz WPA2 AP with the configured device credentials, or the setup
   AP when credentials are missing or recovery mode is active.
2. Lists AP clients and their DHCP addresses.
3. Probes TCP `6668` on the clients and shows the Tuya device IP on the OLED.
4. Answers `DP_QUERY` by calling `tuya.query()` and returning the raw JSON in up
   to 32 chunks of 80 bytes. Retransmits missing chunks after `DP_ACK`.
5. Applies `DP_SET` by passing the raw Tuya `dps` fragment to `tuya.setDps()` and
   returns an `ACK`.
6. Shows AP, device, Tuya and LoRa status on the OLED and serial log.
7. Turns the OLED panel off 10 minutes after boot to reduce burn-in. The panel
   stays on while the setup AP is active.
8. Sends `HELLO` once after boot. The base initiates every DP snapshot.

The remote node does not know which DP means voltage, current or charging. That
mapping lives on the base node and can be changed without reflashing the remote.

## Base node behavior

1. Joins the home WiFi and connects to MQTT. Serves the status page at its
   home-WiFi address and settings at `/settings`.
2. Publishes retained Home Assistant discovery configs for the active profile
   from LittleFS and a hidden Raw DPs sensor.
3. Listens for LoRa frames using the configured radio settings.
4. Verifies the AES-GCM tag and rejects duplicate or old sequence numbers per
   remote boot ID.
5. Sends `DP_QUERY` every 60 seconds, reassembles chunks, and publishes the raw
   snapshot to retained `tuya-lora/<slug>/dps`.
6. Publishes the raw DP snapshot and per-DP topics; profile entities read the
   raw DP snapshot and the Raw DPs sensor attributes, then the base publishes
   retained availability.
7. Publishes `tuya-lora/<slug>/link` on every valid LoRa frame and every
   10 seconds while the remote link is online, so RSSI, SNR and last-seen data
   are available before the device itself is reachable.
8. Forwards profile-pack command templates through the generic DP command topic,
   sends `DP_SET`, and publishes the result to
   `tuya-lora/<slug>/command/result`.
9. Rejects control when no profile is loaded or no DP snapshot has been seen.
10. Marks the remote link offline after 10 minutes without a valid frame.
11. Turns the OLED panel off 10 minutes after boot to reduce burn-in.

The base **Status** page shows link/profile tiles and a profile-driven **Device
telemetry** table; packs without telemetry metadata fall back to raw DPs. The
**LoRa** page shows per-frame/burst speeds, byte counts, DP snapshot speed,
message mix and the rolling 60-minute duty cycle. The OLED shows device
readiness (`available`/`unknown`) and the active profile name instead of
charger-only status/power rows.

The base node is normally a WiFi station. It creates an AP only for initial
configuration or when the configured WiFi is unreachable.

Control is accepted only after a profile is loaded and the base has received a
valid snapshot. A command is rejected while another command or a snapshot is in
progress. A command timeout is not retried automatically because the command may
already have been applied.

## OLED and serial output

The base OLED shows WiFi/MQTT state, the remote LoRa link, device availability,
the active profile, RSSI/SNR, last packet time and the node IP. The remote OLED shows
the AP, device discovery, Tuya connection, snapshot activity and LoRa status.

Both sketches print boot diagnostics and frame/connection events to the serial
console at `115200` baud. Credentials are never printed.

## SD-card firmware update

Both boards can update from a FAT32 microSD card in the onboard slot.

1. Build both sketches and use the application images:

   ```text
   base-node.ino.bin
   remote-node.ino.bin
   ```

2. Copy them to the card root with these names:

   ```text
   base-node.bin
   remote-node.bin
   ```

3. Insert the card and reset the board. The OLED shows a 10-second countdown:

   ```text
   SD UPDATE
   base-node.bin
   Starting in 10s
   ```

4. After the countdown the board writes the image, shows `UPDATE OK`, renames
   the file to `.done`, and reboots.

One card can carry both images. Each board only reads its own file. If an update
fails, the board shows `UPDATE FAIL`, keeps the old firmware, and leaves the file
in place.

The update check runs before LoRa, WiFi or Tuya start. The SD slot uses a
separate HSPI bus (`SCK 14`, `MISO 2`, `MOSI 15`, `CS 13`) and does not disturb
the LoRa radio. Do not remove the card or power off during the update.

## Board notes

| Function | GPIO |
|---|---|
| LoRa SCK / MISO / MOSI | 5 / 19 / 27 |
| LoRa CS / RST / DIO0 | 18 / 23 / 26 |
| OLED SDA / SCL | 21 / 22 |
| OLED RST | not driven (`Adafruit_SSD1306(..., -1)`) |
| microSD SCK / MISO / MOSI / CS | 14 / 2 / 15 / 13 |

- Do not drive GPIO16; it causes a watchdog boot loop on this board revision.
- Very old TTGO LoRa32 V1.x boards may use GPIO14 for LoRa reset.
- Connect a 868 MHz antenna before transmitting.
- Keep the LoRa antenna away from the WiFi antenna and mains wiring.
- The remote node runs WiFi AP, LoRa and OLED together; use a quality 5 V
  supply with enough headroom.

## Security notes

- All credentials are entered through the web UI and stored in NVS.
- The repository contains no device keys, WiFi passwords, MQTT passwords or LoRa
  PSKs.
- The LoRa link is encrypted and authenticated, but the web UI is not.
- Restrict MQTT access to the bridge topics and use a dedicated MQTT user.
- Treat the remote node as physically accessible: use a strong WPA2 password and
  keep the enclosure closed.
