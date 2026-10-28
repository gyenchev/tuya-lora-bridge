# Tuya device reference

This document covers the Tuya local API used by the remote node and the DP maps
that the base node can use. It is a reference for adapting the bridge to a
specific Tuya device.

The base stores one active profile generated from a tuya-local descriptor and
shown as editable JSON in the browser. Tuya products change their DP maps
between models and firmware versions, so always capture the map for the actual
device before enabling control.

## What the remote node needs

| Item | How to obtain it |
|---|---|
| Device ID | Tuya app, Tuya IoT Platform, or `tinytuya scan` |
| Local key | Tuya IoT Platform and `tinytuya wizard`, or `tuya-local` setup |
| IP address | DHCP lease, UDP discovery, or a TCP `:6668` scan |
| Protocol version | `tinytuya scan` reports 3.1 / 3.3 / 3.4 / 3.5 |
| DP map | Capture it from the device while operating it |

### Local key notes

- The local key is 16 characters.
- It changes when the device is removed, re-paired or factory-reset.
- It is a secret. Do not commit it or print it.
- The Tuya IoT Cloud trial expires; renew it to fetch keys for a new device.
- The device must be activated in the Tuya app first.

### Getting the key

1. Pair the device in Tuya Smart or Smart Life on a 2.4 GHz network with
   internet.
2. Create a Tuya IoT Platform project at <https://iot.tuya.com/>.
3. Link the Tuya app account to the project.
4. Subscribe to the **IoT Core** and **Authorization** APIs.
5. Run `python -m tinytuya wizard` with the project API ID, secret and region.
6. Enter the device ID and local key in the remote node's web UI.

`tuya-local` performs the same steps inside Home Assistant. It is useful for
validating the device and observing DPs before moving it to the remote AP.

## Tuya local protocol

This is the local TCP protocol, not the Tuya cloud/MQTT protocol.

### Transport

- TCP port `6668`.
- The device is the server; the remote node is the client.
- Most devices allow one local TCP client at a time.
- Use a heartbeat every 10–15 seconds and reconnect if the socket drops.

### Frame format

TinyTuya's `core/header.py` and `core/message_helper.py` are useful references.

```text
offset  size  field
0       4     prefix = 0x000055AA (big-endian)
4       4     sequence number
8       4     command
12      4     payload length (includes CRC/HMAC and suffix)
16      N     payload
16+N    4     CRC32 (or 32-byte HMAC-SHA256 for 3.4+)
...     4     suffix = 0x0000AA55
```

- **3.1:** CRC32 + 4-byte suffix; control payloads use AES-128-ECB with PKCS#7.
  Some query/status messages are plaintext.
- **3.2 / 3.3:** CRC32 + 4-byte suffix; payloads use AES-128-ECB with PKCS#7.
  The plaintext starts with a version header such as `3.3` plus zero padding.
- **3.4 / 3.5:** HMAC-SHA256 + suffix; payloads use AES-128-GCM and can use
  session-key negotiation.

### Command IDs

| Command | Value | Purpose |
|---|---:|---|
| `CONTROL` | 7 | set DPs |
| `STATUS` | 8 | status report |
| `HEART_BEAT` | 9 | keep-alive |
| `DP_QUERY` | 10 (`0x0a`) | query DPs |
| `CONTROL_NEW` | 13 (`0x0d`) | set DPs for 3.4/3.5 |
| `DP_QUERY_NEW` | 16 (`0x10`) | query DPs for 3.4/3.5 |
| `UPDATEDPS` | 18 (`0x12`) | request a DP refresh |

### Payload shapes

For 3.1/3.3, the payload is JSON:

```json
{"gwId": "", "devId": "", "uid": "", "t": ""}
{"devId": "", "uid": "", "t": "", "dps": {"112": true}}
```

For 3.4/3.5, `DP_QUERY_NEW` and `CONTROL_NEW` use newer shapes:

```json
{"protocol": 5, "t": "int", "data": {"dps": {"112": true}}}
```

The device response contains a `dps` object. Values are typed by the DP:
boolean, integer, enum string, string, bitfield or raw.

### ESP32 implementation notes

- 3.1/3.3 use AES-128-ECB, CRC32 and JSON; mbedTLS and ArduinoJson cover this.
- 3.4/3.5 add AES-GCM, HMAC-SHA256 and optional session-key negotiation.
- A Raspberry Pi Zero 2 W running `tinytuya` is a practical alternative if a
  device requires a protocol path that is not implemented on the ESP32.

## Reference DP maps

The tables below are examples from two tuya-local device profiles. A DP number
is not portable between models. Use them as a starting point only.

### Single-phase profile

From `tuya-local`'s Feyree single-phase profile:

| DP | Type | Meaning | Scale / notes |
|---:|---|---|---|
| 3 | string | device status | optional detail |
| 10 | bitfield | alarm event | fault bits below |
| 11 | string | alarm set 1 | |
| 12 | string | alarm set 2 | |
| 14 | string | mode | `charge_now`, `charge_pct`, `charge_energy`, `charge_schedule` |
| 15 | integer | balance energy | kWh |
| 16 | boolean | clear energy | button |
| 18 | boolean | switch | |
| 20 | boolean | clear event | button |
| 23 | string | system version | |
| 25 | integer | charge energy once | kWh |
| 27 | string | online state | |
| 101 | string | status | `no_connect` → available, `connect` → plugged_in, `charing` → charging, `wait_rfid` → waiting, `finish` → charged, `wait_charing` → waiting, `error` → fault |
| 102 | integer | set charge current | 6–32 A |
| 103 | boolean | leak enable | |
| 104 | integer | charge delay | 0–15 h |
| 105 | boolean | require authorization / power switch | model-dependent |
| 106 | integer | energy | kWh, scale 10, total increasing |
| 107 | integer | voltage | V |
| 108 | integer | current | A, scale 10 |
| 109 | integer | power | kW, scale 10 |
| 110 | integer | temperature | °C, scale 10 |
| 111 | integer | leakage calibration | 20–250 |
| 112 | boolean | charging enable | |
| 113 | integer | charging time | h, scale 10 |
| 114 | integer | charging duration | 0–15 h |

Fault bits for DP 10:

| Bit | Meaning |
|---:|---|
| 1 | Under-voltage protection |
| 2 | Over-voltage protection |
| 4 | Over-current protection |
| 8 | Protective earth error |
| 16 | Temperature too high |
| 32 | Control pilot line error |
| 64 | Leak |
| 128 | Relay socket |
| 256 | Protective earth error 2 |
| 512 | Plug temperature too high |
| 1024 | PCB temperature too high |
| 2048 | Core temperature too high |
| 4096 | ESB |
| 8192 | Socket protective earth error |
| 16384 | Socket leakage |
| 32768 | Socket temperature too high |

### Three-phase profile

Feyree three-phase units can use the shared Goodcell/Feyree profile:

| DP | Type | Meaning | Scale / notes |
|---:|---|---|---|
| 3 | string | device status | `charger_free`, `charger_insert`, `charger_free_fault`, `charger_wait`, `charger_charging`, `charger_pause`, `charger_end`, `charger_fault` |
| 10 | bitfield | fault | problem binary sensor |
| 11 / 12 | string | alarm sets | |
| 14 | string | mode | `charge_now`, `charge_pct`, `charge_energy`, `charge_schedule` |
| 15 | integer | balance energy | kWh |
| 16 | boolean | clear energy | button |
| 18 | boolean | switch | |
| 23 | string | system version | |
| 25 | integer | charge energy once | kWh |
| 27 | string | online state | |
| 101 | string | status | same mapping as single-phase |
| 102 | integer | L1 voltage | V, scale 10 |
| 103 | integer | L2 voltage | V, scale 10 |
| 104 | integer | L3 voltage | V, scale 10 |
| 105 | integer | L1 current | A, scale 10 |
| 106 | integer | L2 current | A, scale 10 |
| 107 | integer | L3 current | A, scale 10 |
| 109 | integer | power | kW, scale 10 |
| 110 | integer | temperature | °C, scale 10 |
| 112 | integer | energy | kWh, scale 10, total increasing |
| 113 | string | max current | `Max16A` / `Max32A` |
| 114 | integer | current alternative | 6–16 A when max is 16 A |
| 115 | integer | set charge current | 6–32 A |
| 118 | integer | charge delay | 0–15 h |
| 119 | integer | maximum charging duration | 0–15 h |
| 120 | string | live session duration | `HH:MM:SS` |
| 121 / 122 | string | displays | |
| 123 | boolean | RFID active / enable | card-present or enable state, not a persistent authorization flag |
| 124 | string | charging control | `OpenCharging`, `CloseCharging`, `WaitOperation` |
| 125 | integer | displayed current | feedback |
| 126 | integer | maximum/rated current | 32 on a 32 A unit |

#### Example three-phase snapshot

```json
{"dps":{"10":0,"101":"charing","102":2244,"103":2238,"104":2270,"105":66,"106":65,"107":65,"109":44,"110":294,"112":4,"113":"Max32A","115":8,"118":0,"119":0,"120":"00:06:27","121":"T:00H","122":"D:00H","123":false,"124":"OpenCharging","125":6,"126":32}}
```

Interpretation:

- `101 = charing` means charging. The firmware value contains a single `r`.
- `102/103/104` are phase voltages divided by 10.
- `105/106/107` are phase currents divided by 10.
- `109` is power in kW divided by 10.
- `110` is temperature in °C divided by 10.
- `112` is session energy in kWh divided by 10.
- `115` is the current limit.
- `120` is the live session duration; `119` is the configured maximum.
- `123` is the RFID-active/card-present state, not an authorization-granted
  flag.
- `124` is the charge-control command value. It can remain `OpenCharging` from a
  previous session while `101` still reports `wait_rfid`. The base publishes the
  raw value as `charge_control` and derives the switch state from `101` and the
  authorization flag.
- `125/126` are displayed and rated current.

### Different single-phase hardware

Some Feyree single-phase units use the internal TuyaMCU serial protocol rather
than the WiFi local TCP API. Their DP map differs:

| DP | Meaning |
|---:|---|
| 101 | state: 0 free, 1 car inserted, 2 ready, 3 charging, 4 paused, 5 ended, 6 fault |
| 102 | voltage, ÷10 |
| 105 | current, ÷10 |
| 109 | power, ÷10 |
| 110 | temperature, ÷10 |
| 112 | session energy, ÷100 kWh |
| 115 | current limit command, 6–32 A |
| 124 | charge command: 0 charge, 1 stop, 2 ready, 3 waiting, 10 not ready |
| 125 | current limit feedback |
| 120 | runtime string `HH:MM:SS` |
| 126 | minimum session current when charging |

### DP conflicts

The same DP number can mean different things:

| DP | Single-phase | Three-phase | TuyaMCU variant |
|---:|---|---|---|
| 102 | set charge current | L1 voltage | voltage |
| 105 | authorization/switch | L1 current | current |
| 112 | charging enable | energy | session energy |
| 115 | — | set charge current | current limit command |
| 124 | — | charge control | charge command |

Always capture the actual map before enabling control.

## Profiles

The base stores exactly **one active profile**. A profile is a precompiled
Home Assistant MQTT discovery pack plus telemetry metadata. It is created in the
browser from a tuya-local descriptor, or written directly as JSON in the profile
editor.

Open `/profiles` on the base:

1. The active profile is loaded from `/api/profile/export` and shown in the
   textarea as JSON.
2. The page fetches the tuya-local device tree for the selected repository/ref.
   It shows a flat searchable list immediately and builds a grouped index in the
   background; selecting a descriptor fetches its YAML, converts it and replaces
   the textarea content.
3. Edit the metadata, telemetry, discovery entries or command templates.
4. **Save to base** converts the JSON to NDJSON and uploads it to
   `/api/profile/save`. The base reboots with the new profile active.

There is no auto-detection, built-in profile list, generated catalog file,
separate custom-profile store or offline catalog. The browser queries upstream
directly, and the one JSON document in the editor is the profile.

### Profile JSON

The editor uses a single object:

```json
{
  "format": 1,
  "id": "generic_switch",
  "name": "Tuya device",
  "device_type": "Switches & controls",
  "source": "tuya-local <commit>/example.yaml",
  "telemetry": [
    {
      "key": "value",
      "label": "Value",
      "dp": "1",
      "kind": "number",
      "unit": ""
    }
  ],
  "discovery": [
    {
      "component": "sensor",
      "object_id": "status",
      "config": {
        "name": "Status",
        "state_topic": "tuya-lora/<slug>/dps",
        "value_template": "{{ ... }}"
      }
    }
  ]
}
```

- `id` is 1–23 safe characters (letters, digits, `_`, `-`).
- `name` is 1–47 characters; `device_type` is up to 23 characters.
- `discovery` is required and contains one entry per Home Assistant entity.
  Each entry has a `component`, an `object_id` and a `config` object in Home
  Assistant MQTT discovery format.
- `telemetry` is optional and drives the status page. Fields contain a label,
  a primary `dp`, a kind (`number`, `bool`, `enum` or `text`) and optional
  scaling, maps, redirects or fallbacks.
- `source` and `warnings` are informational.

The base adds the device, availability and origin blocks when publishing. It
also publishes a hidden **Raw DPs** sensor whose `dps` attribute contains the
complete raw snapshot. Command templates use that sensor for conditional
mappings and read-modify-write operations.

The converter supports native MQTT platforms for light, fan, cover, climate,
water heater, humidifier, lock, alarm, siren, valve, event, time/datetime, lawn
mower, camera, sensor, binary sensor, switch, number, select, button and text.
Vacuums, media players, remotes and infrared emitters are decomposed into
standard MQTT controls and sensors.

### Storage and limits

The browser converts the edited JSON to newline-delimited JSON:

- first line: profile metadata (`format`, `id`, `name`, `device_type`,
  `source`, `telemetry`, `discovery_count`);
- following lines: one discovery entry per line.

The base writes each entry to a small LittleFS file and commits the metadata
last. Limits are 250 KB per upload, 256 discovery entries, 24 KB per entry and
20 KB of metadata. The custom partition gives LittleFS 384 KB.

## Capturing a device's DP map

### Bench setup

1. Put the device on a 2.4 GHz test network with internet.
2. Pair it in Tuya Smart or Smart Life.
3. Get the device ID, local key and protocol version with `tinytuya`.
4. Run:

   ```bash
   python -m tinytuya scan
   python -m tinytuya wizard
   python -m tinytuya monitor --id DEVICE_ID --key LOCAL_KEY --ip DEVICE_IP --version 3.3
   ```

5. Operate the device while monitoring:
   - idle;
   - active or in use;
   - change a setpoint or mode;
   - stop;
   - cycle complete;
   - a safe fault state if available.
6. Record each DP with raw value, type, unit/scale, when it changes and whether
   it is writable.

### Tools

| Tool | Use |
|---|---|
| `tinytuya scan` | find IP, device ID and protocol version |
| `tinytuya wizard` | fetch local keys from Tuya IoT Cloud |
| `tinytuya monitor` | live DP stream |
| Tuya IoT Platform device debug | cloud-side DP logs |
| `tuya-local` diagnostics | DPs used by Home Assistant |

Keep the DP numbers and scales in the active profile JSON on the base. Keep the
device ID and local key in the remote node's NVS-backed web UI.

## Offline local control

Tuya devices normally keep a local TCP server available after activation, even
when the cloud is unreachable. Test it before installation:

1. Pair and retrieve the key on a network with internet.
2. Move the device to an AP with no internet.
3. Query status and test state and control commands from a laptop on that AP.

If local control works offline, the remote AP design is viable. If the device
drops the AP or stops responding, use a conventional WiFi bridge instead.

## `tuya-local` alternative

If the device can reach the same network as Home Assistant, `tuya-local` is the
simpler integration:

- install `tuya-local` through HACS;
- add the device with its ID, IP, local key and protocol version;
- use the matching device profile;
- expose status, current, voltage, power, energy, temperature and control
  entities as supported by the model.

The LoRa bridge is for installations where direct WiFi coverage is unavailable.
The base profile editor converts the matching tuya-local descriptor into the
precompiled profile JSON. Tuya-local features that the converter cannot express
as native MQTT entities are exposed through the Raw DPs sensor and per-DP
topics.

## References

- [`make-all/tuya-local`](https://github.com/make-all/tuya-local) — local Tuya
  integration and device profiles.
- [`jasonacox/tinytuya`](https://github.com/jasonacox/tinytuya) — Python local
  Tuya library and protocol reference.
- [`plam40/Feyree-EV-charger`](https://github.com/plam40/Feyree-EV-charger) —
  Tasmota/TuyaMCU reverse engineering for different hardware.
- [`vm03/esphome-feyree-ev-charger`](https://github.com/vm03/esphome-feyree-ev-charger)
  — ESPHome hardware modification for a different hardware path.
- [Tuya IoT Platform](https://iot.tuya.com/)
