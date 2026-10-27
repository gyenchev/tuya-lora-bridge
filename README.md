> [!CAUTION]
> **Disclaimer: 100% vibecoded / AI-generated.** This project has not been
> professionally reviewed. Use it entirely at your own risk. The author is not
> responsible for any damage to hardware, software, property, or persons. Read
> [`DISCLAIMER.md`](DISCLAIMER.md) before using it.

# Tuya LoRa Bridge

A two-node ESP32 bridge that carries a Tuya local DP stream over LoRa to Home
Assistant. The bridge is designed for locations where the Tuya device cannot
reach the home WiFi, but it does not modify the device or its mains wiring.

The remote node is a raw Tuya DP proxy. The base node owns the DP-to-entity map,
so many common Tuya devices can be described with a tuya-local-style JSON
descriptor without changing the firmware. Built-in Feyree and F-OBZ2 profiles
are provided as compatibility examples and can be replaced by custom
descriptors.

- **Firmware version:** 0.4.0
- **Radio band:** 868 MHz (EU)
- **LoRa link:** AES-128-GCM authenticated and encrypted
- **Home Assistant:** MQTT discovery, no custom integration

## What it does

The remote node hosts a private 2.4 GHz WiFi access point for the Tuya device.
It discovers the device on that AP, speaks the Tuya local TCP protocol on port
`6668`, and acts as a raw data-point proxy over LoRa. It does not interpret the
DP map.

The base node joins the home WiFi, owns the DP-to-entity map, publishes MQTT
state and Home Assistant discovery, and turns MQTT commands into Tuya DP writes.
The Tuya device stays on its own local protections and schedules.

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
- Descriptor-driven Home Assistant entities: sensors, binary sensors, switches,
  numbers, selects, buttons and text inputs, with value mappings, scaling,
  ranges and enum options.
- Selectable DP profiles with auto-detection, plus up to four custom profiles
  defined as validated JSON in the base node's NVS.
- Generated profile library with 104 tuya-local-compatible descriptors plus a
  converter for generating more from the tuya-local device collection.
- Built-in Feyree and F-OBZ2 compatibility profiles with stable entity IDs.
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
├── DISCLAIMER.md
├── docs/
│   ├── architecture.md
│   ├── hardware-safety.md
│   ├── home-assistant.md
│   ├── lora-protocol.md
│   ├── network-and-pairing.md
│   ├── testing.md
│   └── tuya-device.md
├── profiles/
│   ├── INDEX.md
│   ├── README.md
│   ├── pack.jsonl
│   └── <generated-profile>.json
├── tools/
│   ├── README.md
│   ├── requirements.txt
│   └── tuya_local_to_profile.py
└── firmware/
    ├── README.md
    ├── base-node/
    │   ├── base-node.ino
    │   ├── boot_guard.cpp
    │   ├── boot_guard.h
    │   ├── config.h
    │   ├── config_store.cpp
    │   ├── config_store.h
    │   ├── entities.cpp
    │   ├── entities.h
    │   ├── ha_discovery.cpp
    │   ├── ha_discovery.h
    │   ├── lora_protocol.cpp
    │   ├── lora_protocol.h
    │   ├── profiles.cpp
    │   ├── profiles.h
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

When a node has no saved configuration, it starts a setup AP:

| Node | Setup AP SSID | Default password | URL |
|---|---|---|---|
| Base | `TuyaLoraBase-Setup` | `tuya-lora-setup` | `http://192.168.4.1/` |
| Remote | `TuyaLoraRemote-Setup` | `tuya-lora-setup` | `http://192.168.4.1/` |

The default setup password is public. Change it in the **System** tab after the
first successful configuration.

### 2. Configure the remote node

Power the remote node and connect to its `TuyaLoraRemote-Setup` AP (default
password `tuya-lora-setup`), or to the configured device AP after first boot.
Open `http://192.168.4.1/settings` and enter:

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

Connect to the base node's `TuyaLoraBase-Setup` AP (default password
`tuya-lora-setup`) or its home-WiFi address. Open `/settings` and enter:

- home WiFi credentials;
- MQTT broker host, port and credentials;
- the same LoRa PSK and radio settings as the remote node;
- Home Assistant device name and discovery prefix;
- the **Profile** tab: auto-detect, a built-in profile, or a custom JSON profile;
- optional timing and control limits.

The Profile tab also shows the active, detected and match state from the last DP
snapshot. Built-in compatibility profiles are `3-phase`, `single-phase` and
`F-OBZ2`; custom profiles are limited to four entries and remain in NVS, not in
the repository. A generic custom profile describes Home Assistant entities in a
tuya-local-style `entities` list; the base then publishes discovery and state for
those entities automatically. The base node publishes Home Assistant discovery
automatically. The device name and identifier can be changed later; keep the
identifier stable to avoid creating a second Home Assistant device.

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
credentials. The selected DP profile and any custom profile JSON are also stored
in NVS, so changing devices does not require rebuilding the base firmware.

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
There is no custom component. The default topics keep the `charger` segment for
backward compatibility; they are configurable in the base web UI. The main
topics are:

| Topic | Purpose |
|---|---|
| `tuya-lora/charger/state` | mapped state JSON |
| `tuya-lora/charger/state/entities/e0` … `e9` | per-entity state for generic profiles |
| `tuya-lora/charger/link` | RSSI, SNR, age and last-seen diagnostics |
| `tuya-lora/charger/dps` | raw Tuya DP snapshot |
| `tuya-lora/charger/availability` | `online` / `offline` |
| `tuya-lora/charger/command/*` | legacy control and query commands |
| `tuya-lora/charger/command/generic/e0` … `e9` | per-entity commands for generic profiles |
| `tuya-lora/charger/command/result` | command result |

The default entity prefix is `tuya_<device-name>_<suffix>`. Generic profile
entities use `tuya_<device-name>_profile_e0` and similar stable suffixes.
See [`docs/home-assistant.md`](docs/home-assistant.md) for the entity list,
payloads, automations and troubleshooting.

## Safety

This project does not open the device or touch its mains wiring. It uses only
the Tuya network API.

- The device's own protections remain authoritative. For an EV charger, this
  includes the RCD/RCBO, control pilot and local protections.
- Do not bypass or modify protective earth, RCD/RCBO, CP or PP circuits.
- Do not use a DIY relay or contactor to interrupt mains.
- The bridge must never be the only safeguard against an unsafe condition.
- Control is intentionally limited to slow monitoring and the controls exposed
  by the active profile; it is not a real-time load-balancing system.

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

- The bridge is generic, but the descriptor subset is deliberately compact. It
  supports `sensor`, `binary_sensor`, `switch`, `number`, `select`, `button` and
  `text` entities with value mappings, scaling, ranges and enum options. It does
  not implement the full tuya-local schema: climate, light, cover, fan, lock,
  vacuum, `value_redirect`, `constraint`/`conditions`, bitfield decoding,
  `force`/`persist` handling and product-ID matching are not supported yet.
  Generic state is built from the latest snapshot; absent DPs are not persisted.
- The built-in `3-phase`, `single-phase` and `F-OBZ2` profiles use a legacy
  compatibility state model and keep their existing Home Assistant entity IDs.
  Other devices should use a custom generic `entities` profile.
- One Tuya device is supported per bridge. Simultaneous multi-device support is
  not implemented.
- A generic profile can define up to 10 entities, 4 DPs per entity and 32 value
  mapping rules; the custom JSON limit is 8192 bytes.
- The generated library contains 104 profiles that fit the compact schema. The
  full tuya-local collection has 1779 descriptors; many use unsupported entity
  types such as climate, light, fan, cover or lock, or exceed the compact
  limits.
- The firmware loads at most four custom profiles in NVS. Generated profiles are
  pasted one at a time; `profiles/pack.jsonl` is prepared for a future on-device
  profile-pack loader.
- Raw DP snapshots are capped at 2560 bytes (32 LoRa chunks of 80 bytes).
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
| [`docs/tuya-device.md`](docs/tuya-device.md) | Tuya local API, key retrieval, reference DP maps, generic entity descriptors and the legacy custom schema |
| [`docs/lora-protocol.md`](docs/lora-protocol.md) | Frame format, messages, ACKs, encryption and airtime |
| [`docs/home-assistant.md`](docs/home-assistant.md) | MQTT topics, discovery, entities, automations and dashboards |
| [`docs/hardware-safety.md`](docs/hardware-safety.md) | BOM, power, enclosure, radio rules and EVSE safety |
| [`docs/testing.md`](docs/testing.md) | Validation matrix, acceptance checks and known limitations |
| [`profiles/README.md`](profiles/README.md) | Generated tuya-local profile library, coverage and usage |
| [`tools/README.md`](tools/README.md) | tuya-local YAML to compact profile converter |
| [`firmware/README.md`](firmware/README.md) | Build, flash, web UI settings, SD updates and board notes |

## License and disclaimer

This project is released into the public domain under the **Unlicense**. You may
copy, modify, publish, use, compile, sell, or distribute it for any purpose,
commercial or non-commercial, without attribution. See [`LICENSE`](LICENSE).

This project is **100% vibecoded / AI-generated** and is not a certified
controller or safety device for any application. It can control EV chargers and
other Tuya devices, but the supported profile set can change at any time. The
author is not responsible for any damage to hardware, software, property, or
persons. Use it at your own risk and read [`DISCLAIMER.md`](DISCLAIMER.md). Use
it only with equipment that keeps its own safety and protective functions, and
have any mains installation checked by a qualified electrician.
