# Home Assistant integration

The base node publishes ordinary MQTT topics and retained Home Assistant
discovery configs. No custom integration is required.

## MQTT topics

### Published by the base

| Topic | Retain | Payload |
|---|---|---|
| `tuya-lora/charger/state` | yes | mapped state JSON |
| `tuya-lora/charger/link` | yes | LoRa link diagnostics |
| `tuya-lora/charger/dps` | yes | raw Tuya DP snapshot |
| `tuya-lora/charger/availability` | yes | `online` / `offline` |
| `tuya-lora/charger/command/result` | no | command result JSON |
| `tuya-lora/charger/debug` | no | profile and snapshot summary |

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
  "profile": "3-phase",
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
  "fw": "0.2.1"
}
```

The base publishes `link` as soon as it receives a valid LoRa frame, before the
device itself is reachable. Device entities remain `unknown` until the first DP
snapshot arrives.

`profile` is detected from the raw DP keys:

- `single-phase`
- `3-phase`
- `F-OBZ2`
- `fallback`
- `unknown`

Control is rejected for `fallback` and `unknown`. `delay_hours` is the last
reported delay from DP `104` or `118`; it can be `0` when the profile does not
expose a delay.

### Commands published to the base

| Topic | Payload |
|---|---|
| `tuya-lora/charger/command/charge` | `ON` / `OFF` |
| `tuya-lora/charger/command/start` | any payload; starts charging |
| `tuya-lora/charger/command/stop` | any payload; stops charging |
| `tuya-lora/charger/command/current_limit` | number, for example `16` |
| `tuya-lora/charger/command/delay` | hours, `0`–`15` |
| `tuya-lora/charger/command/mode` | `charge_now` / `charge_schedule` |
| `tuya-lora/charger/command/query` | any payload; requests a fresh snapshot |

The base maps a command to the detected profile's DP IDs, sends a LoRa
`DP_SET`, waits for the remote ACK, and publishes:

```json
{
  "command": "charge",
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

Control is accepted only after the base has received a valid DP snapshot and
detected a supported profile. A command is rejected while another command or a
snapshot is in progress. The remote does not validate DP semantics; it only
forwards the fragment to Tuya after an authenticated LoRa frame.

## MQTT discovery

The base publishes retained discovery topics under:

```text
<discovery-prefix>/<component>/<device-id>/<object-id>/config
```

The default discovery prefix is `homeassistant`. Example topics:

```text
homeassistant/sensor/tuya_lora_bridge/status/config
homeassistant/sensor/tuya_lora_bridge/power/config
homeassistant/sensor/tuya_lora_bridge/energy/config
homeassistant/sensor/tuya_lora_bridge/rssi/config
homeassistant/binary_sensor/tuya_lora_bridge/plugged/config
homeassistant/number/tuya_lora_bridge/current_limit/config
homeassistant/switch/tuya_lora_bridge/charge_control/config
```

Common device block:

```json
{
  "device": {
    "identifiers": ["tuya_lora_bridge"],
    "name": "Tuya charger",
    "manufacturer": "",
    "model": "Tuya device via Tuya LoRa Bridge",
    "sw_version": "0.2.1"
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

### Monitoring

| Entity | Unit / class | Source |
|---|---|---|
| `sensor.tuya_<name>_status` | enum | `state.status` |
| `sensor.tuya_<name>_power` | kW, power | `state.power` |
| `sensor.tuya_<name>_energy` | kWh, energy, total increasing | `state.energy` |
| `sensor.tuya_<name>_session_energy` | kWh, energy | `state.session_energy` |
| `sensor.tuya_<name>_voltage_l1/l2/l3` | V, voltage | phase voltages |
| `sensor.tuya_<name>_current_l1/l2/l3` | A, current | phase currents |
| `sensor.tuya_<name>_temperature` | °C, temperature | `state.temperature` |
| `sensor.tuya_<name>_charge_duration` | s, duration | `state.charge_seconds` |
| `sensor.tuya_<name>_fault_code` | raw bitfield | `state.fault` |
| `binary_sensor.tuya_<name>_plugged` | plug | `state.plugged` |
| `binary_sensor.tuya_<name>_charging` | running | `state.charging` |
| `binary_sensor.tuya_<name>_fault` | problem | `state.fault > 0` |
| `binary_sensor.tuya_<name>_rfid_active` | occupancy | `state.rfid` |
| `binary_sensor.tuya_<name>_schedule` | — | `state.schedule` |

### Link diagnostics

| Entity | Unit / class | Source topic |
|---|---|---|
| `sensor.tuya_<name>_rssi` | dBm, signal strength | `tuya-lora/charger/link` |
| `sensor.tuya_<name>_snr` | dB | `tuya-lora/charger/link` |
| `sensor.tuya_<name>_link_age` | s, duration | `tuya-lora/charger/link` |
| `sensor.tuya_<name>_last_seen` | timestamp | `tuya-lora/charger/link` |

These entities work independently of the device state. They show whether the
LoRa link is alive even when the Tuya device is unreachable.

### Control

Control entities are published when `ENABLE_COMMANDS` is `1` in
`base-node/config.h`.

| Entity | Type | Topic |
|---|---|---|
| `switch.tuya_<name>_charge_control` | switch | `command/charge` |
| `button.tuya_<name>_charge_enable` | button | `command/start` |
| `button.tuya_<name>_charge_disable` | button | `command/stop` |
| `number.tuya_<name>_current_limit` | number | `command/current_limit` |
| `number.tuya_<name>_charge_delay` | number | `command/delay` |

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
   `tuya-lora/charger/availability` to `offline` if the base disconnects.
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
mosquitto_pub -t tuya-lora/charger/command/query -m query
mosquitto_sub -v -t tuya-lora/charger/dps
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
- Treat `tuya-lora/charger/command/*` as privileged.
