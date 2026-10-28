# Home Assistant integration

The base node publishes ordinary MQTT topics and retained Home Assistant
discovery configs. No custom integration is required.

## MQTT topics

### Published by the base

| Topic | Retain | Payload |
|---|---|---|
| `tuya-lora/<slug>/link` | yes | LoRa link diagnostics |
| `tuya-lora/<slug>/dps` | yes | raw Tuya DP snapshot |
| `tuya-lora/<slug>/dps/<dp-id>` | yes | per-DP value |
| `tuya-lora/<slug>/availability` | yes | `online` / `offline` |
| `tuya-lora/<slug>/command/result` | no | command result JSON |
| `tuya-lora/<slug>/debug` | no | profile and snapshot summary |

Example `link`:

```json
{
  "online": true,
  "seen": true,
  "rssi": -87,
  "snr": 8.5,
  "age": 2,
  "seq": 42,
  "boot_id": 123456789,
  "last_seen": "2026-10-22T11:32:05Z",
  "fw": "0.5.0"
}
```

The base publishes `link` as soon as it receives a valid LoRa frame, before the
device itself is reachable. Device entities remain `unknown` until the first DP
snapshot arrives.

Profile diagnostics are available from the base HTTP API and the `debug` topic:

- `profile` is the active profile ID, or `none` before a profile is saved.
- `profile_entities` is the number of discovery entities in the active profile.
- `profile_error` is set when no profile is loaded.

### Commands published to the base

| Topic | Payload |
|---|---|
| `tuya-lora/<slug>/command/dp` | JSON DP fragment from a profile command template |
| `tuya-lora/<slug>/command/query` | any payload; requests a fresh snapshot |

The base sends the fragment as a LoRa `DP_SET`, waits for the remote ACK, and
publishes:

```json
{
  "command": "pack",
  "result": "ok",
  "ok": true,
  "ts": "2026-10-22T14:32:08+03:00"
}
```

Result values:

- `ok`
- `rejected`
- `disabled`
- `unsupported_profile`
- `invalid`
- `busy`
- `timeout`
- `send_failed`

Control is accepted only after a profile is loaded and the base has received a
valid DP snapshot. A command is rejected while another command or a snapshot is
in progress. The remote does not validate DP semantics; it only forwards the
fragment to Tuya after an authenticated LoRa frame.

### Profile entities

Profile entities do not use per-entity state topics. The base publishes
precompiled Home Assistant discovery configs whose templates read the raw
snapshot at `tuya-lora/<slug>/dps`; command templates publish a JSON `dps`
fragment to `tuya-lora/<slug>/command/dp`.

The base also publishes:

- a hidden **Raw DPs** sensor at `sensor.<ha-name>_pack_raw`, with the complete
  `dps` map in its `dps` attribute;
- per-DP retained topics under `tuya-lora/<slug>/dps/<dp-id>`, used by the
  camera image entity and available to automations.

Object IDs are derived from the entity name, lowercased with underscores, and
prefixed with `pack_` on the base. Examples:

```text
homeassistant/sensor/tuya_lora_bridge/pack_status/config
homeassistant/number/tuya_lora_bridge/pack_value/config
homeassistant/sensor/tuya_lora_bridge/pack_raw/config
```

## MQTT discovery

The base publishes retained discovery topics under:

```text
<discovery-prefix>/<component>/<device-id>/<object-id>/config
```

The default discovery prefix is `homeassistant`. Example topics:

```text
homeassistant/sensor/tuya_lora_bridge/rssi/config
homeassistant/sensor/tuya_lora_bridge/pack_status/config
homeassistant/number/tuya_lora_bridge/pack_value/config
homeassistant/sensor/tuya_lora_bridge/pack_raw/config
```

When the active profile changes, the base clears stale profile discovery and
republishes the new set.

Common device block:

```json
{
  "device": {
    "identifiers": ["tuya_lora_bridge"],
    "name": "Tuya device",
    "manufacturer": "",
    "model": "Tuya device via Tuya LoRa Bridge",
    "sw_version": "0.5.0"
  },
  "origin": {
    "name": "tuya-lora-bridge",
    "sw_version": "0.5.0"
  }
}
```

The friendly name comes from the **Device name** field in the base web UI. The
stable identifier comes from the **Device identifier** field. Keep the
identifier unchanged when renaming the device; otherwise Home Assistant creates
a second device and the old retained discovery topics remain.

Each entity publishes `unique_id`, `object_id` and `default_entity_id` as
`tuya_<device-slug>_<suffix>`. The slug lowercases the device name and replaces
non-alphanumeric characters with `_`. If the name already starts with `tuya_`,
the prefix is not duplicated.

Examples:

- Device name `Tuya device` → `sensor.tuya_device_pack_status`
- Device name `Workshop sensor` → `sensor.tuya_workshop_sensor_pack_temperature`

## Entities

Entity IDs and entity types come from the active profile. The profile editor
shows the generated discovery entries; the raw DP sensor is always available.
The link diagnostics below are published by the base for every profile.

### Link diagnostics

| Entity | Unit / class | Source topic |
|---|---|---|
| `sensor.tuya_<name>_rssi` | dBm, signal strength | `tuya-lora/<slug>/link` |
| `sensor.tuya_<name>_snr` | dB | `tuya-lora/<slug>/link` |
| `sensor.tuya_<name>_link_age` | s, duration | `tuya-lora/<slug>/link` |
| `sensor.tuya_<name>_last_seen` | timestamp | `tuya-lora/<slug>/link` |

These entities work independently of the device state. They show whether the
LoRa link is alive even when the Tuya device is unreachable.

Additional transfer telemetry — frame/byte counts, burst and per-frame speeds,
60-minute duty cycle and DP snapshot speed — is available only on the base
**LoRa** tab at `/lora` and in `/api/status`. It is deliberately not published
to MQTT or Home Assistant.

### Controls

Controls are defined by the active profile's discovery entries and command
templates. Depending on the profile, this can be a switch, number, select,
button or text entity. Their entity IDs use the
`tuya_<device-name>_pack_<entity-name>` prefix and are listed in the profile
JSON.

## Availability

Two layers control availability:

1. **Base node alive:** MQTT Last Will and Testament sets
   `tuya-lora/<slug>/availability` to `offline` if the base disconnects.
2. **Remote link alive:** the base marks the remote link offline after 10
   minutes without a valid LoRa frame and publishes `offline`. The next valid
   frame publishes `online`.

Link availability is not proof that the device is reachable. Device entities
remain `unknown` until a DP snapshot succeeds. State is retained so Home
Assistant can show the last known values, while availability tells automations
not to trust stale data.

## Automations

Replace the example entity IDs with the IDs generated for your profile.

### Timed control

```yaml
alias: Tuya device scheduled control
triggers:
  - trigger: time
    at: "23:00:00"
conditions:
  - condition: state
    entity_id: binary_sensor.tuya_device_pack_ready
    state: "on"
actions:
  - action: number.set_value
    target:
      entity_id: number.tuya_device_pack_value
    data:
      value: 16
  - action: switch.turn_on
    target:
      entity_id: switch.tuya_device_pack_switch
mode: single
```

Stop later:

```yaml
alias: Tuya device stop
triggers:
  - trigger: time
    at: "07:00:00"
actions:
  - action: switch.turn_off
    target:
      entity_id: switch.tuya_device_pack_switch
mode: single
```

### Fault notification

```yaml
alias: Tuya device fault notify
triggers:
  - trigger: state
    entity_id: binary_sensor.tuya_device_pack_problem
    to: "on"
actions:
  - action: notify.notify
    data:
      message: >-
        Tuya device fault. Raw fault code:
        {{ states('sensor.tuya_device_pack_fault_code') }}
mode: single
```

Use any notification service configured in Home Assistant.

### Link lost warning

```yaml
alias: Tuya LoRa Bridge offline
triggers:
  - trigger: state
    entity_id: sensor.tuya_device_link_age
    for: "00:05:00"
actions:
  - action: notify.notify
    data:
      message: "Tuya LoRa Bridge has been offline for 5 minutes."
mode: single
```

### Slow value adjustment

The link updates every 60 seconds. Use it for slow adjustments, not real-time
control.

```yaml
alias: Tuya device slow adjustment
triggers:
  - trigger: time_pattern
    minutes: "/5"
conditions:
  - condition: state
    entity_id: binary_sensor.tuya_device_pack_active
    state: "on"
actions:
  - action: number.set_value
    target:
      entity_id: number.tuya_device_pack_value
    data:
      value: >-
        {{ [6, [16, (states('sensor.example_excess_power') | float / 230) | int] | min] | max }}
mode: single
```

## Dashboard

```yaml
type: entities
title: Tuya device
entities:
  - sensor.tuya_device_pack_status
  - sensor.tuya_device_pack_value
  - binary_sensor.tuya_device_pack_problem
  - switch.tuya_device_pack_switch
  - sensor.tuya_device_rssi
  - sensor.tuya_device_last_seen
```

Add history graphs for the profile sensors you care about and an energy
dashboard entry for any cumulative energy sensor in the profile.

## Testing MQTT without Home Assistant

```bash
mosquitto_sub -v -t 'tuya-lora/#'
mosquitto_pub -t tuya-lora/<slug>/command/query -m query
mosquitto_sub -v -t tuya-lora/<slug>/dps
mosquitto_sub -v -t 'homeassistant/+/tuya_lora_bridge/+/config'
```

If discovery entities do not appear, check:

- the discovery prefix configured in the base web UI;
- the retained flag on the discovery topic;
- the JSON payload;
- unique IDs;
- Home Assistant's MQTT integration is connected to the same broker.

## Security

- Use a dedicated MQTT user with access limited to the bridge topics.
- Do not expose the broker to the internet without TLS and authentication.
- Do not put MQTT credentials in dashboards or Git.
- The LoRa PSK and Tuya local key are stored in NVS and configured through the
  node web UI.
- The web UI is unauthenticated; keep it on a trusted network.
- Treat `tuya-lora/<slug>/command/*` as privileged.
