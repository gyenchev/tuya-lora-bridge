# Generated tuya-local profiles

These profiles were generated from the device descriptors in
[`make-all/tuya-local`](https://github.com/make-all/tuya-local) using
`tools/tuya_local_to_profile.py`.

The base firmware supports a compact subset of the tuya-local schema:
seven entity types, five DP types, value mappings, scaling, ranges,
units, device/state classes and enum options. Descriptors that use
unsupported entity types, DP types, mapping features or exceed the
firmware limits are skipped.

## How to use

Each `profiles/<id>.json` file is a complete custom-profile document.
Paste one into the **Profile** tab, save with `auto` selected, reboot,
then select the profile and save again.

The current firmware stores at most four custom profiles in NVS. The
`pack.jsonl` file contains all generated profiles, one JSON object per
line, for a future SD/SPIFFS profile-pack loader.

## Coverage

- Source descriptors: 1779
- Generated profiles: 104
- Skipped: 1675

### Skipped by reason

- needs 1-10 entities: 640
- unsupported entity type 'climate': 305
- unsupported entity type 'fan': 208
- unsupported entity type 'light': 138
- unsupported entity type 'humidifier': 89
- unsupported entity type 'cover': 64
- unsupported entity type 'time': 54
- unsupported entity type 'water_heater': 34
- unsupported DP type 'base64': 23
- unsupported entity type 'lock': 19
- unsupported entity type 'valve': 19
- unsupported entity type 'siren': 14
- needs 1-4 DPs: 12
- unsupported entity type 'vacuum': 12
- unsupported entity type 'event': 8
- unsupported entity type 'remote': 7
- unsupported mapping feature: 6
- device class is too long: 4
- empty mapping rule: 4
- unsupported mapping value type: 3
- first DP must be named sensor: 2
- switch mapping needs on and off: 2
- unsupported entity type 'alarm_control_panel': 2
- uses 38 mapping rules (limit 32): 2
- unsupported entity type 'media_player': 1
- unsupported DP type 'json': 1
- uses 35 mapping rules (limit 32): 1
- number entity has no range: 1

## Attribution

The source device descriptors are from tuya-local, Copyright 2018
Nik Rolls, 2026 Jason Rumney, 2022-2026 other contributors, and are
distributed under the MIT License. See `LICENSE.tuya-local`.
