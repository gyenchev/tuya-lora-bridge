# Tuya device reference

This document covers the Tuya local API used by the remote node and the DP maps
that the base node can use. It is a reference for adapting the bridge to a
specific Tuya device.

The base includes built-in compatibility profiles for Feyree three-phase,
single-phase and F-OBZ2 chargers, plus a validated custom-JSON profile editor.
Generic custom profiles map DPs to Home Assistant entities using a compact
tuya-local-style schema. Tuya products change their DP maps between models and
firmware versions, so always capture the map for the actual device before
enabling control.

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

The tables below are examples from different Tuya EV charger profiles. A DP
number is not portable between models. Use them as a starting point only.

### Single-phase profile

From `tuya-local`'s Feyree single-phase profile:

| DP | Type | Meaning | Scale / notes |
|---:|---|---|---|
| 3 | string | charger status | optional detail |
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
| 3 | string | charger status | `charger_free`, `charger_insert`, `charger_free_fault`, `charger_wait`, `charger_charging`, `charger_pause`, `charger_end`, `charger_fault` |
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

The base owns both directions of the DP map:

- **State decoding** turns the raw snapshot into the stable state model published
  to MQTT and Home Assistant.
- **Control encoding** turns MQTT commands into Tuya `dps` fragments.

The remote node never receives a profile; it only forwards raw DP snapshots and
`DP_SET` fragments.

### Built-in profiles

| ID | Name | Auto priority | Typical detection keys |
|---|---|---:|---|
| `3-phase` | Feyree 3-phase | 30 | DP 103, 104, 105, 106, 107 |
| `single-phase` | Feyree single-phase | 20 | DP 106, 107, 108 |
| `F-OBZ2` | Feyree F-OBZ2 | 10 | DP 102, 105, 109 |
| `fallback` | Internal read-only fallback | 0 | none |

`fallback` is not user-selectable. It maps a few common fields and rejects all
control. Auto-detection ignores profiles with no required DPs. Higher priority
wins when more than one profile matches. Custom profiles default to priority 5.

The **Profile** tab in the base web UI offers `auto`, the three built-in
profiles, and any loaded custom profiles. The selection is stored in NVS.
Saving a profile selection reboots the base so the new active map is applied
cleanly.

The **Control** tab still has DP ID fields for the built-in profiles. These are
legacy outgoing-command overrides: they change the command DP numbers for the
built-in map but do not change its state decoding. Use a custom profile when both
directions need different DPs.

### Custom profiles

Custom profiles are stored as JSON in the base node's NVS under
`tuya-lora-base/profiles_json`. They are not part of the repository, are not
published to MQTT, and are cleared by a factory reset.

Limits:

- at most four custom profiles;
- at most 8192 bytes of JSON;
- profile IDs contain only letters, digits, `_` and `-`, with 1–23 characters;
- IDs `auto`, `fallback`, `unknown` and the built-in IDs are reserved;
- invalid JSON is rejected and the previous valid profile set remains active;
- each profile must define at least one DP in `state` or `entities`;
- a generic profile can define at most 10 entities, 4 DPs per entity and 32
  mapping rules.

The editor accepts either a top-level array or an object with a `profiles`
array.

#### Generic `entities` descriptors

The recommended way to add a new device is a tuya-local-style `entities` list.
The base translates each descriptor into Home Assistant discovery configs and
publishes one state topic per entity. Nothing in the firmware is device-specific.

Supported entity types:

| `entity` | Purpose | Command topic |
|---|---|---|
| `sensor` | read-only numeric or enum value | none |
| `binary_sensor` | read-only boolean, for example a fault | none |
| `switch` | on/off control | yes |
| `number` | numeric control with a range | yes |
| `select` | enumerated control | yes |
| `button` | write-only action | yes |
| `text` | string value or schedule | yes |

Entity object fields:

| Key | Required | Meaning |
|---|---|---|
| `entity` | yes | One of the types above. |
| `name` | no | Friendly name. Derived from `translation_key`, `class` or the entity type if omitted. |
| `class` | no | Home Assistant device class, for example `temperature`, `voltage`, `power`, `energy`, `current`, `enum`, `problem`. |
| `category` | no | `diagnostic` or `config`. |
| `icon` | no | Home Assistant icon, for example `mdi:thermometer`. |
| `hidden` | no | Publish the entity disabled by default. |
| `translation_key` | no | Small name-derivation helper: `status`, `voltage_x`, `current_x`. |
| `translation_placeholders` | no | `{"x": "A"}` for `voltage_x`/`current_x`. |
| `dps` | yes | 1–4 DP definitions. The first DP is the entity's primary value. |

DP object fields:

| Key | Required | Meaning |
|---|---|---|
| `id` | yes | Tuya DP number, 1–999. |
| `type` | yes | `string`, `boolean`, `integer`, `bitfield` or `float`. |
| `name` | yes | Role or attribute name, letters/digits/`_` only and starting with a letter or `_`. The primary DP name depends on the entity type: `sensor` (sensor/binary_sensor), `switch`, `value` (number/text), `option` (select), `button`. |
| `mapping` | no | Value mapping and scale rules. |
| `range` | no | `{"min": ..., "max": ..., "step": ...}` for number/text controls. |
| `unit` | no | Unit shown by Home Assistant, for example `C`, `V`, `A`, `kWh`. |
| `class` | no | Sensor state class: `measurement`, `total` or `total_increasing`. |
| `precision` | no | Suggested display precision, 0–10. |
| `scale` | no | Divisor applied to numeric values (tuya-local semantics). |
| `optional` | no | Ignore this DP when deriving auto-detection keys. |
| `readonly` | no | Not valid for the primary DP of a writable entity. |
| `hidden` | no | Do not publish this DP as an attribute. Hidden is not allowed on the primary DP. |

Mapping rules are a list. Each rule can contain:

- `dps_val` — the raw Tuya value. Omit it for a default rule. `null` also means
  default.
- `value` — the Home Assistant value. If omitted with `dps_val`, the raw value
  is passed through.
- `scale` — a standalone rule (`{"scale": 10}`) that divides numeric raw values.
  It cannot be combined with `dps_val`/`value` in the same rule.
- `hidden` — keep the value out of a select/enum option list while still mapping
  incoming state.

`scale` in a generic `entities` descriptor is a **divisor**, matching
tuya-local. A raw voltage of `2301` with `"scale": 10` becomes `230.1`. The
legacy `state` map below uses a multiplier instead; do not mix the two.

Number entities require a `range`. Select entities require a `mapping` with at
least one `dps_val` option. Text entities require a `string` primary DP. Switch
mappings must include both on and off values if a mapping is used. A numeric
switch DP can omit the mapping and uses `1`/`0`; a boolean switch uses
`true`/`false`.

A complete generic example:

```json
{
  "profiles": [
    {
      "id": "generic-demo",
      "name": "Generic demo device",
      "description": "Temperature, relay, target, mode and fault",
      "detect": {"require": [1, 2, 3], "priority": 12},
      "entities": [
        {
          "entity": "sensor",
          "name": "Temperature",
          "class": "temperature",
          "category": "diagnostic",
          "dps": [
            {"id": 1, "type": "integer", "name": "sensor", "unit": "C",
             "scale": 10, "class": "measurement", "precision": 1}
          ]
        },
        {
          "entity": "switch",
          "name": "Relay",
          "dps": [{"id": 2, "type": "boolean", "name": "switch"}]
        },
        {
          "entity": "number",
          "name": "Target",
          "class": "temperature",
          "dps": [
            {"id": 3, "type": "integer", "name": "value", "unit": "C",
             "scale": 10, "range": {"min": 5, "max": 35, "step": 0.5}}
          ]
        },
        {
          "entity": "select",
          "name": "Mode",
          "dps": [
            {"id": 4, "type": "string", "name": "option",
             "mapping": [
               {"dps_val": "auto", "value": "Auto"},
               {"dps_val": "manual", "value": "Manual"}
             ]}
          ]
        },
        {
          "entity": "binary_sensor",
          "name": "Fault",
          "class": "problem",
          "dps": [
            {"id": 5, "type": "bitfield", "name": "sensor",
             "mapping": [
               {"dps_val": 0, "value": false},
               {"value": true}
             ]}
          ]
        },
        {
          "entity": "text",
          "name": "Schedule",
          "dps": [{"id": 7, "type": "string", "name": "value"}]
        }
      ]
    }
  ]
}
```

If `detect.require` is omitted, the base derives up to eight required DPs from
the non-optional primary DPs of the entities. Add an explicit `detect.require`
list when the device does not report all primary DPs in one snapshot, or set it
to an empty array to make the profile manual-only. A profile with no required DPs
is never selected automatically but can be selected manually.

For each generic entity the base publishes:

- discovery under `<discovery-prefix>/<component>/<ha-device-id>/profile_eN/config`;
- state under `<state-topic>/entities/eN` as a JSON object keyed by DP name;
- commands under `<command-base>/generic/eN`.

The entity suffix `eN` is stable for a descriptor's entity order. Existing
built-in profiles keep their original entity IDs; generic profiles use
`tuya_<device-name>_profile_eN` suggested IDs. When commands are disabled in the
web UI, writable generic entities are published as read-only sensors.

Limitations of the generic subset: no `value_redirect`, `constraint`/
`conditions`, bitfield sub-field decoding, `base64`/`hex`/`json`/`unixtime` DP
types, `force`/`persist` handling, `product`-ID matching, or
climate/light/cover/fan/lock/vacuum entities. Those require richer handling than
the ESP32 base currently implements. The compact subset is intentionally focused
on the common sensor/switch/number/select/button/text patterns. A generic state
object is built from the latest snapshot; DPs missing from that snapshot are not
persisted. If a descriptor has many enum options, raise the MQTT buffer in the
base web UI (the default for new settings is 4096 bytes).

A generated library of 104 tuya-local-compatible profiles is in
[`profiles/`](../profiles/README.md); use
[`tools/tuya_local_to_profile.py`](../tools/README.md) to regenerate it or try
more descriptors. The full tuya-local collection contains 1779 descriptors, but
many use unsupported entity types or exceed the compact limits.

#### Legacy `state`/`control` descriptors

The built-in Feyree and F-OBZ2 profiles, and existing compact custom profiles,
use the legacy state/control map. New generic devices should use `entities`
instead. The legacy example below is still useful for EV chargers because it
maps directly to the stable charger state model.

```json
{
  "profiles": [
    {
      "id": "my-obz2",
      "name": "My OBZ2 clone",
      "description": "Example single-phase TuyaMCU-style map",
      "phases": 1,
      "detect": {
        "require": [102, 105, 109],
        "exclude": [103],
        "priority": 15
      },
      "state": {
        "status": {"dp": 101, "type": "text", "fallback": 3},
        "auth": {"dp": 101, "type": "text"},
        "fault": {"dp": 10},
        "voltage_l1": {"dp": 102, "scale": 0.1},
        "current_l1": {"dp": 105, "scale": 0.1},
        "power": {"dp": 109, "scale": 100},
        "session_energy": {"dp": 112, "scale": 10},
        "temperature": {"dp": 110, "scale": 0.1},
        "current_limit": {"dp": 115},
        "charge_seconds": {"dp": 120, "type": "duration_hms"},
        "charge_enable": {"dp": 124, "type": "int", "on": "0"},
        "schedule": {"dp": 14, "type": "text_contains", "match": "schedule"}
      },
      "control": {
        "charge": {"dp": 124, "type": "int", "on": 0, "off": 1},
        "current": {"dp": 115}
      }
    }
  ]
}
```

#### `state` fields

All fields are optional, but at least one must be present. Each field is an
object with a DP number and an optional type and scale.

| Key | Published state | Expected base unit |
|---|---|---|
| `status` | `status` | text; use `fallback` for a secondary DP |
| `auth` | `waiting_for_authorization` | `wait_rfid` when true |
| `fault` | `fault` | raw bitfield |
| `voltage_l1/l2/l3` | phase voltage | volts |
| `current_l1/l2/l3` | phase current | amps |
| `power` | `power` | watts |
| `energy` | total energy | Wh |
| `session_energy` | session energy | Wh |
| `temperature` | temperature | °C |
| `current_limit` | current limit | amps |
| `current_limit_alt` | fallback current limit | amps |
| `charge_seconds` | session duration | seconds |
| `charge_seconds_alt` | fallback duration | seconds |
| `charge_enable` | `charge_control` | boolean state |
| `rfid` | `rfid` | boolean state |
| `schedule` | `schedule` | boolean state |
| `delay` | `delay_hours` | hours |

`scale` converts the raw DP value to the unit in this table. For example, a raw
power value of `44` in units of 100 W uses `"scale": 100`; a raw temperature of
`294` in units of 0.1 °C uses `"scale": 0.1`.

Field types:

| Type | Meaning |
|---|---|
| `int` | default; integer or numeric string, multiplied by `scale` |
| `bool` | Tuya boolean |
| `text` | string; compare against the comma-separated `on`/`match` list |
| `text_contains` | string; true when it contains `match` |
| `duration_hms` | `HH:MM:SS` text |
| `duration_hours` | integer hours, multiplied by 3600 and `scale` |
| `duration_tenth_hours` | integer tenths of an hour, multiplied by 360 and `scale` |
| `voltage_auto` | values above 500 are treated as 0.1 V; lower values as volts |

For `charge_enable`, an integer field with `"on": "0"` means raw `0` is enabled.
A text field accepts a comma-separated list, for example
`"on": "opencharging,charge,on"`. A boolean field tests the boolean value.

For `schedule`, the default `text_contains` match is `schedule`.

#### `control` fields

`control` is optional. Missing control fields reject the corresponding MQTT
command for that profile; the rest of the state map still works.

| Key | Fields | Meaning |
|---|---|---|
| `charge` | `dp`, `type`, `on`, `off` | Charge/stop command |
| `current` | `dp` | Current-limit command, value in amps |
| `delay` | `dp`, `sets_mode` | Delay in hours; optionally also writes a mode |
| `mode` | `dp`, `now`, `schedule` | Mode command strings |

`charge.type` is one of:

- `bool` — write JSON `true`/`false`. `on`/`off` default to `true`/`false`;
  `1`/`0` are accepted and normalized;
- `int` — write the numeric `on`/`off` values;
- `string` — write the `on`/`off` strings, for example `OpenCharging` and
  `CloseCharging`.

If `charge.type` is omitted, it defaults to `string`.

The `mode` values are the wire strings used by the MQTT mode command. An MQTT
`charge_now` (or `now`) command writes `mode.now`; `charge_schedule` (or
`schedule`) writes `mode.schedule`. This lets a custom profile use different
Tuya values. `delay.sets_mode` writes `mode.schedule` when the requested delay
is greater than zero and `mode.now` when it is zero. It requires a `mode` object
in the same profile.

For the single-phase built-in profile, `delay.sets_mode` is true and
`mode.now`/`mode.schedule` default to `charge_now`/`charge_schedule`. The
three-phase built-in profile writes only DP 118 for delay; it does not touch the
mode DP. F-OBZ2 has no delay or mode control.

#### `detect` fields

`detect.require` lists DP IDs that must be present in the snapshot.
`detect.exclude` lists DP IDs that must be absent. `detect.priority` is 0–255.

A custom profile with no required DPs is never chosen by auto-detection. For a
generic `entities` profile, omitting `detect.require` derives up to eight
required DPs from the non-optional primary DPs. An explicit empty `require`
array makes the profile manual-only. It can still be selected manually; in that
case it matches any snapshot and the user is responsible for selecting the right
map. Keep `require` specific if several profiles are loaded.

#### Selection and mismatch behavior

- `auto` selects the highest-priority matching profile and falls back to the
  read-only fallback when none match.
- A manually selected profile is used for state decoding even if it does not
  match the snapshot.
- `profile_match` is `false` for a mismatch or for the fallback profile.
- Control is rejected when `profile_match` is false.
- `profile_detected` always shows what auto-detection would choose, even when a
  manual profile is active.
- To delete a custom profile, switch the selection to `auto` or a built-in
  profile in the same save.

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
   - idle, cable unplugged;
   - cable plugged in, not charging;
   - start charging;
   - change current limit;
   - stop charging;
   - charging complete;
   - a safe fault or RFID state if available.
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

Keep the DP numbers and scales in the base node's profile selection or custom
JSON. Keep the device ID and local key in the remote node's NVS-backed web UI.

## Offline local control

Tuya devices normally keep a local TCP server available after activation, even
when the cloud is unreachable. Test it before installation:

1. Pair and retrieve the key on a network with internet.
2. Move the device to an AP with no internet.
3. Query status and test start/stop/current control from a laptop on that AP.

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
The generic `entities` profile schema is deliberately modelled on tuya-local's
device descriptors, so an existing descriptor can usually be reduced to its
`entities` list and given an explicit `detect.require` list. Unsupported
tuya-local features are listed in the generic-profile section above.

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
