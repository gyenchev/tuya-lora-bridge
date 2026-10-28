# Home Assistant integration

The base node publishes ordinary MQTT topics and retained Home Assistant
discovery configs. No custom integration is required.

## MQTT topics

### Published by the base

| Topic | Retain | Payload |
|---|---|---|
| `tuya-lora/<slug>/state` | yes | mapped state JSON for diagnostics |
| `tuya-lora/<slug>/link` | yes | LoRa link diagnostics |
| `tuya-lora/<slug>/dps` | yes | raw Tuya DP snapshot |
| `tuya-lora/<slug>/dps/<dp-id>` | yes | per-DP value, published in browser-pack mode |
| `tuya-lora/<slug>/availability` | yes | `online` / `offline` |
| `tuya-lora/<slug>/command/result` | no | command result JSON |
| `tuya-lora/<slug>/debug` | no | profile and snapshot summary |

Example `state`:

```json
{
  "status": "charging",
  "fault": 0,
  "plugged": true,
  "charging": true,
  "charge_enabled": true,
  "charge_control": true,
  "waiting_for_authorization": false,
  "rfid": false,
  "schedule": false,
  "current_limit": 16,
  "voltage_l1": 231.2,
  "voltage_l2": 0.0,
  "voltage_l3": 0.0,
  "current_l1": 6.7,
  "current_l2": 0.0,
  "current_l3": 0.0,
  "power": 1.55,
  "energy": 123.45,
  "session_energy": 1.23,
  "temperature": 31.2,
  "charge_seconds": 3600,
  "profile": "feyree_43_evcharger",
  "profile_entities": 22,
  "delay_hours": 0
}
```

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
  "fw": "0.3.0"
}
```

The base publishes `link` as soon as it receives a valid LoRa frame, before the
device itself is reachable. Device entities remain `unknown` until the first DP
snapshot arrives.

Profile fields:

- `profile` is the active profile ID, or `none` before a profile is saved.
- `profile_entities` is the number of discovery entities in the active profile.
- `profile_error` is present when no profile is loaded or no DP snapshot has
  been seen.

`delay_hours` is the last reported delay from the active profile's delay DP. It
can be `0` when the profile does not expose a delay.

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

### Browser pack entities

A browser pack does not use per-entity state topics. It publishes precompiled
Home Assistant discovery configs; the templates read the raw snapshot at
`tuya-lora/<slug>/dps` and command templates publish a JSON `dps` fragment to
`tuya-lora/<slug>/command/dp`.

The base also publishes:

- a hidden **Raw DPs** sensor at `sensor.<ha-name>_pack_raw`, with the complete
  `dps` map in its `dps` attribute;
- per-DP retained topics under `tuya-lora/<slug>/dps/<dp-id>`, used by the
  camera image entity and available to automations.

Object IDs are derived from the entity name, lowercased with underscores, and
prefixed with `pack_` on the base. Examples:

```text
homeassistant/sensor/tuya_lora_bridge/pack_status/config
homeassistant/number/tuya_lora_bridge/pack_charge_current/config
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
homeassistant/number/tuya_lora_bridge/pack_charge_current/config
homeassistant/sensor/tuya_lora_bridge/pack_raw/config
```

When the active profile changes, the base clears stale profile discovery and
republishes the new set.

Common device block:

```json
{
  "device": {
    "identifiers": ["tuya_lora_bridge"],
    "name": "Tuya charger",
    "manufacturer": "",
    "model": "Tuya device via Tuya LoRa Bridge",
    "sw_version": "0.3.0"
  },
  "origin": {
    "name": "tuya-lora-bridge",
    "sw_version": "0.3.0"
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

- Device name `Tuya charger` → `sensor.tuya_charger_power`
- Device name `CarCharger` → `sensor.tuya_carcharger_power`

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

### Control

Controls are defined by the active profile's discovery entries and command
templates. A typical charger profile exposes a charge current number, charge
delay/duration numbers and start/stop buttons. Their entity IDs use the
`tuya_<device-name>_pack_<entity-name>` prefix (for example
`number.tuya_carcharger_pack_charge_current`) and are listed in the profile
JSON.

### Status values

| Value | Meaning |
|---|---|
| `available` | no vehicle connected |
| `plugged_in` | vehicle connected, not charging |
| `charging` | charging |
| `waiting` | waiting for authorization or schedule |
| `charged` | session complete |
| `fault` | device fault |
| `unknown` | bridge cannot read the device |

### Charge switch behavior

The switch reports `ON` only when the session is active and the charge-control
DP is `OpenCharging`. It reports `OFF` for `available`, `waiting` while
authorization is required, `charged` and `fault`. It also reports `OFF` after a
stop command even if the status still says `plugged_in`. The raw DP `124` value
is published separately as `charge_control` for diagnostics. The charging binary
sensor shows whether the vehicle is drawing power now.

Turning the switch `ON` sends the profile's start DP; turning it `OFF` sends the
stop DP. The momentary **Enable charging** and **Disable charging** buttons send
the start and stop commands directly.

### Charge delay behavior

Delay is mapped only for the single-phase (`104`) and 3-phase (`118`) profiles.
The F-OBZ2 profile has no public delay DP, so delay commands are rejected. For
single-phase, setting a delay also sets DP `14` to `charge_schedule`; setting
`0` restores `charge_now`. The 3-phase profile writes only DP `118`.

A delay is not a start command. Set the delay first, then turn the charge switch
on or press **Enable charging**.

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

### Off-peak charging

```yaml
alias: Tuya charger off-peak charge
triggers:
  - trigger: time
    at: "23:00:00"
conditions:
  - condition: state
    entity_id: binary_sensor.tuya_charger_plugged
    state: "on"
  - condition: state
    entity_id: input_boolean.tuya_charger_auto_charge
    state: "on"
actions:
  - action: number.set_value
    target:
      entity_id: number.tuya_charger_current_limit
    data:
      value: 16
  - action: switch.turn_on
    target:
      entity_id: switch.tuya_charger_charge_control
mode: single
```

Stop in the morning:

```yaml
alias: Tuya charger stop at 07:00
triggers:
  - trigger: time
    at: "07:00:00"
actions:
  - action: switch.turn_off
    target:
      entity_id: switch.tuya_charger_charge_control
mode: single
```

### Fault notification

```yaml
alias: Tuya charger fault notify
triggers:
  - trigger: state
    entity_id: binary_sensor.tuya_charger_fault
    to: "on"
actions:
  - action: notify.notify
    data:
      message: >-
        Tuya charger fault. Raw fault code:
        {{ states('sensor.tuya_charger_fault_code') }}
mode: single
```

Use any notification service configured in Home Assistant.

### Charge complete

```yaml
alias: Tuya charger charge complete
triggers:
  - trigger: state
    entity_id: sensor.tuya_charger_status
    to: "charged"
actions:
  - action: notify.notify
    data:
      message: >-
        Car charging complete. Session:
        {{ states('sensor.tuya_charger_session_energy') }} kWh.
mode: single
```

### Link lost warning

```yaml
alias: Tuya LoRa Bridge offline
triggers:
  - trigger: state
    entity_id: sensor.tuya_charger_status
    to: "unavailable"
    for: "00:05:00"
actions:
  - action: notify.notify
    data:
      message: "Tuya LoRa Bridge has been offline for 5 minutes."
mode: single
```

### Slow solar surplus control

The link updates every 60 seconds. Use it for slow current adjustments, not fast
load balancing.

```yaml
alias: Tuya charger solar current
triggers:
  - trigger: time_pattern
    minutes: "/5"
conditions:
  - condition: state
    entity_id: binary_sensor.tuya_charger_charging
    state: "on"
actions:
  - action: number.set_value
    target:
      entity_id: number.tuya_charger_current_limit
    data:
      value: >-
        {{ [6, [16, (states('sensor.solar_excess_power') | float / 230) | int] | min] | max }}
mode: single
```

## Dashboard

```yaml
type: entities
title: Tuya charger
entities:
  - sensor.tuya_charger_status
  - sensor.tuya_charger_power
  - sensor.tuya_charger_current_l1
  - sensor.tuya_charger_voltage_l1
  - sensor.tuya_charger_energy
  - sensor.tuya_charger_temperature
  - binary_sensor.tuya_charger_plugged
  - binary_sensor.tuya_charger_charging
  - binary_sensor.tuya_charger_fault
  - number.tuya_charger_current_limit
  - number.tuya_charger_charge_delay
  - switch.tuya_charger_charge_control
  - button.tuya_charger_charge_enable
  - button.tuya_charger_charge_disable
  - sensor.tuya_charger_last_seen
```

Add a history graph for `sensor.tuya_charger_power` and an energy dashboard
entry for `sensor.tuya_charger_energy`.

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
