# Tuya LoRa Bridge

A two-node ESP32 bridge that carries a Tuya local API over LoRa to Home
Assistant. The bridge is designed for locations where the Tuya device cannot
reach the home WiFi, but it does not modify the device or its mains wiring.

The reference implementation targets a Feyree three-phase EV charger. The radio
link, Tuya proxy, MQTT layer and Home Assistant integration are generic; the DP
map is device-specific and is kept on the base node.

- **Firmware version:** 0.2.1
- **Radio band:** 868 MHz (EU)
- **LoRa link:** AES-128-GCM authenticated and encrypted
- **Home Assistant:** MQTT discovery, no custom integration

## What it does

The remote node hosts a private 2.4 GHz WiFi access point for the Tuya device.
It discovers the device on that AP, speaks the Tuya local TCP protocol on port
`6668`, and acts as a raw data-point proxy over LoRa. It does not interpret the
DP map.

The base node joins the home WiFi, owns the DP map, publishes MQTT state and
Home Assistant discovery, and turns MQTT commands into Tuya DP writes. The
charger stays on its own local protections and schedules.

```text
 Tuya device                     Remote node                  Base node
 (2.4 GHz WiFi)                  (TTGO LoRa32)                (TTGO LoRa32)
       │                              │                            │
       │ WiFi: device → AP            │      LoRa 868 MHz          │
       └─────────────────────────────►│ ~~~~~~~~~~~~~~~~~~~~~~~~~~►│
                                      │                            │
                               Tuya local TCP :6668          MQTT over WiFi
                                      │                            │
                                 raw DP snapshots / DP_SET        │
                                                                   ▼
                                                            Home Assistant
```

## Features

- Private WiFi AP for a Tuya device that has no home WiFi coverage.
- Tuya local TCP client for protocol versions 3.1, 3.3, 3.4 and 3.5.
- Raw DP snapshot transport with chunk ACKs and retransmission.
- AES-128-GCM encrypted LoRa frames with `boot_id` and sequence replay
  protection.
- Retained MQTT state, raw DP snapshot, availability and link diagnostics.
- Home Assistant MQTT discovery for monitoring and control entities.
- Web UI for all runtime settings; settings are stored in ESP32 NVS.
- OLED status display on both nodes.
- SD-card firmware updates for both nodes.
- Boot recovery: repeated short boots force the setup AP.

## Hardware

| Part | Quantity | Notes |
|---|---:|---|
| TTGO LoRa32 V2.1 / T3 V1.6.1 | 2 | ESP32 + SX1276 + SSD1306 OLED + microSD |
| 868 MHz antenna | 2 | Connect before transmitting |
| 5 V USB power supply | 2 | Quality adapter; remote node is outdoors |
| IP65 enclosure and cable gland | 1 | Remote node only |
| FAT32 microSD card | 1 | Optional, for firmware updates |
| External 868 MHz antenna | optional | Only with a suitable connector |

No relays, contactors, CT clamps or mains wiring are part of this project.

## Repository layout

```text
tuya-lora-bridge/
├── README.md
├── LICENSE
├── docs/
│   ├── architecture.md
│   ├── hardware-safety.md
│   ├── home-assistant.md
│   ├── lora-protocol.md
│   ├── network-and-pairing.md
│   ├── testing.md
│   └── tuya-device.md
└── firmware/
    ├── README.md
    ├── base-node/
    │   ├── base-node.ino
    │   ├── boot_guard.cpp
    │   ├── boot_guard.h
    │   ├── config.h
    │   ├── config_store.cpp
    │   ├── config_store.h
    │   ├── lora_protocol.cpp
    │   ├── lora_protocol.h
    │   ├── sd_update.cpp
    │   ├── sd_update.h
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

`lora_protocol.h/.cpp` and `sd_update.h/.cpp` are duplicated in both sketch
directories so each Arduino sketch is self-contained. Keep the copies
identical.

## Getting started

### 1. Build and flash

Use Arduino ESP32 core 3.1.0 and the libraries listed in
[`firmware/README.md`](firmware/README.md).

```bash
FQBN=esp32:esp32:ttgo-lora32:UploadSpeed=460800
PORT=/dev/cu.usbserial-REPLACE_ME
LIBS="$HOME/Documents/Arduino/libraries"

arduino-cli compile --fqbn "$FQBN" --libraries "$LIBS" firmware/remote-node
arduino-cli upload  --fqbn "$FQBN" --libraries "$LIBS" -p "$PORT" firmware/remote-node

arduino-cli compile --fqbn "$FQBN" --libraries "$LIBS" firmware/base-node
arduino-cli upload  --fqbn "$FQBN" --libraries "$LIBS" -p "$PORT" firmware/base-node
```

### 2. Configure the remote node

Power the remote node and connect to its `TuyaLoraRemote-Setup` AP, or to the
configured device AP after first boot. Open `http://192.168.4.1/settings` and
enter:

- the AP SSID, password and channel that the Tuya device expects;
- the Tuya device ID, 16-character local key and protocol version;
- the LoRa frequency, sync word and a random 16-byte PSK;
- optional timing and discovery values.

The remote node stores these values in NVS. No compile-time secrets are used.

### 3. Pair the Tuya device

The Tuya device must be activated in the Tuya app on a 2.4 GHz network with
internet. Use a phone hotspot or travel router with exactly the same SSID and
password that the remote node will host. After pairing, turn the hotspot off and
power the remote node; the device reconnects to the remote AP.

See [`docs/network-and-pairing.md`](docs/network-and-pairing.md) for the full
procedure and troubleshooting.

### 4. Configure the base node

Connect to the base node's setup AP or its home-WiFi address. Open `/settings`
and enter:

- home WiFi credentials;
- MQTT broker host, port and credentials;
- the same LoRa PSK and radio settings as the remote node;
- Home Assistant device name and discovery prefix;
- optional timing and control limits.

The base node publishes Home Assistant discovery automatically. The device name
and identifier can be changed later; keep the identifier stable to avoid
creating a second Home Assistant device.

### 5. Verify

```bash
mosquitto_sub -v -t 'tuya-lora/#'
mosquitto_sub -v -t 'homeassistant/+/+/config'
```

Request a fresh snapshot:

```bash
mosquitto_pub -t tuya-lora/charger/command/query -m query
```

The retained `tuya-lora/charger/state` and `tuya-lora/charger/link` topics should
update, and Home Assistant should create the entities described in
[`docs/home-assistant.md`](docs/home-assistant.md).

## Configuration model

All network, MQTT, Tuya and LoRa credentials are runtime settings stored in
ESP32 NVS and edited through the node web UI. The firmware does not compile in
credentials.

- Base status: `http://<base-ip>/`
- Base settings: `http://<base-ip>/settings`
- Remote status: `http://192.168.4.1/`
- Remote settings: `http://192.168.4.1/settings`

Factory reset clears NVS and returns the node to its setup AP. If a node resets
four times without staying up for 60 seconds, the next boot forces the setup AP
until settings are saved or a factory reset is performed.

## Firmware updates

Both boards can update from a FAT32 microSD card. Copy the application images to
the card root:

```text
base-node.bin
remote-node.bin
```

Insert the card and reset the board. The OLED shows a 10-second countdown, then
flashes the matching image and reboots. Each board reads only its own file.
Keep the card inserted until the update finishes.

## Home Assistant

The base node publishes ordinary MQTT topics and retained discovery configs.
There is no custom component. The main topics are:

| Topic | Purpose |
|---|---|
| `tuya-lora/charger/state` | mapped state JSON |
| `tuya-lora/charger/link` | RSSI, SNR, age and last-seen diagnostics |
| `tuya-lora/charger/dps` | raw Tuya DP snapshot |
| `tuya-lora/charger/availability` | `online` / `offline` |
| `tuya-lora/charger/command/*` | control and query commands |
| `tuya-lora/charger/command/result` | command result |

The default entity prefix is `tuya_<device-name>_<suffix>`. See
[`docs/home-assistant.md`](docs/home-assistant.md) for the entity list, payloads,
automations and troubleshooting.

## Safety

This project does not open the device or touch its mains wiring. It uses only
the Tuya network API.

- The device's RCD/RCBO, control pilot and local protections remain
  authoritative.
- Do not bypass or modify protective earth, RCD/RCBO, CP or PP circuits.
- Do not use a DIY relay or contactor to interrupt mains.
- The bridge must never be the only safeguard against an unsafe condition.
- Control is intentionally limited to slow monitoring and charging control;
  it is not a real-time load-balancing system.

See [`docs/hardware-safety.md`](docs/hardware-safety.md) before installing.

## Security

- LoRa frames use a pre-shared AES-128-GCM key and replay protection.
- The Tuya local key, WiFi credentials, MQTT password and LoRa PSK are stored in
  NVS and entered through the web UI.
- The configuration and status web UIs are unauthenticated. Keep them on a
  trusted network.
- Use a dedicated MQTT user limited to the bridge topics.
- Treat `tuya-lora/charger/command/*` as privileged: any MQTT client that can
  publish there can control charging.
- Do not expose MQTT or Home Assistant to the internet without TLS and
  authentication or a VPN.
- The firmware does not log credentials.

## Scope and limitations

- The included DP map targets a Feyree three-phase charger. Other Tuya devices
  need their own DP map and control profile.
- The base node publishes a full snapshot every 60 seconds. Commands add an
  immediate refresh, but this is not a real-time control link.
- Most Tuya devices accept one local TCP client at a time. Close the Tuya app or
  other local clients while the remote node is connected.
- The firmware does not include a physical arm-switch input.
- Firmware updates use the SD card; network OTA is not included.
- LoRa range and duty cycle depend on the antenna, enclosure and local
  interference. The default SF7 setting is intended for EU 868 MHz use.

## Documentation

| Document | Contents |
|---|---|
| [`docs/architecture.md`](docs/architecture.md) | Topology, node responsibilities, data flow and design constraints |
| [`docs/network-and-pairing.md`](docs/network-and-pairing.md) | AP setup, Tuya pairing, discovery and connectivity checks |
| [`docs/tuya-device.md`](docs/tuya-device.md) | Tuya local API, key retrieval and reference DP maps |
| [`docs/lora-protocol.md`](docs/lora-protocol.md) | Frame format, messages, ACKs, encryption and airtime |
| [`docs/home-assistant.md`](docs/home-assistant.md) | MQTT topics, discovery, entities, automations and dashboards |
| [`docs/hardware-safety.md`](docs/hardware-safety.md) | BOM, power, enclosure, radio rules and EVSE safety |
| [`docs/testing.md`](docs/testing.md) | Validation matrix, acceptance checks and known limitations |
| [`firmware/README.md`](firmware/README.md) | Build, flash, web UI settings, SD updates and board notes |

## License

MIT. See [`LICENSE`](LICENSE).

This is not a certified EV charging controller. Use it only with equipment that
keeps its own safety and protective functions, and have any mains installation
checked by a qualified electrician.
