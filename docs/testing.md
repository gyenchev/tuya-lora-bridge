# Testing and validation

Use this checklist when commissioning a new installation or changing the DP map.
The tests assume the device is on the remote AP and the base node is connected
to MQTT and Home Assistant.

## Validation checklist

### Network and Tuya

- [ ] The device joins the remote AP and receives an IP on `192.168.4.0/24`.
- [ ] The remote node finds TCP port `6668` on the device.
- [ ] `tinytuya` or the remote status page can query DPs with the local key.
- [ ] Local control works while the AP has no internet.
- [ ] A device power cycle reconnects automatically.
- [ ] A remote-node power cycle reconnects automatically.

### LoRa link

- [ ] Both nodes use the same frequency, sync word and PSK.
- [ ] The base receives `HELLO` and displays the remote link on the OLED.
- [ ] `tuya-lora/charger/link` publishes RSSI, SNR, age and last-seen values.
- [ ] A full snapshot reassembles without missing chunks.
- [ ] A deliberately dropped chunk is recovered by the retransmit path.
- [ ] Range and RSSI/SNR are stable at the installed positions.
- [ ] The measured frame rate stays within the EU duty-cycle budget.

### MQTT and Home Assistant

- [ ] The base publishes retained state, raw DP and availability topics.
- [ ] Home Assistant creates the expected entities.
- [ ] Entity IDs use the configured device prefix.
- [ ] A Home Assistant restart restores retained state without replaying
  commands.
- [ ] A base reboot republishes discovery and state.
- [ ] A remote or device outage marks availability offline after the timeout.

### Profiles

- [ ] `auto` selects `3-phase` for a three-phase snapshot fixture.
- [ ] `auto` selects `single-phase` for a single-phase snapshot fixture.
- [ ] `auto` selects `F-OBZ2` for an F-OBZ2 snapshot fixture.
- [ ] An unrecognized snapshot selects `fallback` and rejects control.
- [ ] A manually selected built-in profile stays active across reboots.
- [ ] A manual profile that does not match the snapshot reports
  `profile_match: false` and rejects control.
- [ ] Valid custom JSON appears in the Profile dropdown after save and reboot.
- [ ] A custom profile with detection rules is selected by `auto`.
- [ ] A custom profile selected manually uses its state and control maps.
- [ ] Invalid JSON is rejected, the editor keeps the submitted text, and the
  previous valid profiles still load after a reboot.
- [ ] More than four legacy custom profiles is rejected.
- [ ] JSON larger than 8192 bytes is rejected.
- [ ] Duplicate, reserved or unsafe profile IDs are rejected.
- [ ] A generic `entities` profile publishes one discovery config and one state
  topic per entity.
- [ ] Generic sensor scaling, enum mappings, binary-sensor mappings, number
  ranges, select options, buttons and text commands behave as described.
- [ ] Generic discovery and state are removed when switching to a legacy or
  built-in profile.
- [ ] Generic entity suffixes remain stable across reboots for the same
  descriptor order.
- [ ] Factory reset removes custom profiles and returns the selection to `auto`.
- [ ] Built-in profiles still honor the legacy command DP overrides in the
  Control tab.
- [ ] `/profiles` loads the online catalog and converts a light, climate,
  cover, fan, lock, vacuum, valve, event and camera descriptor.
- [ ] Saving an offline charger pack works without internet.
- [ ] A large pack (81 entities / ~61 KB) uploads, reboots, reports the expected
  `profile_entities`, and publishes discovery.
- [ ] The largest catalog pack (181 KB / 63 entities) uploads and reloads.
- [ ] The Raw DPs sensor exposes every DP as a `dps` attribute, and per-DP topics
  are published in pack mode.
- [ ] A pack-selected device shows `profile_match` and rejects commands when the
  snapshot does not contain the pack's required DPs.
- [ ] Switching back to `auto` clears pack discovery and restores the built-in
  profile.
- [ ] A selected pack whose required DP set is missing one non-critical DP still
  reports `profile_match: true` (loose matching) and allows commands.
- [ ] `/api/profile/telemetry` returns the pack's telemetry fields.
- [ ] The status page renders Device telemetry from the selected pack and falls
  back to raw DP rows when a pack has no telemetry metadata.
- [ ] Status-page top tiles show only link/profile information, not charger-only
  state/power/energy tiles.
- [ ] Base OLED shows `Device: available/mismatch/unknown` and `Profile: <name>`,
  not `Charger`/`Power` rows.
- [ ] With MQTT buffer 8192, the Feyree Charge current discovery (pack_e14)
  appears in HA; with a 2048 buffer it is rejected and a stale entity can remain.
- [ ] Setting Charge current to 32 A in HA publishes `{"115":32}` (or the
  active variant DP), returns an `ok` command result, and updates raw DP 115/125.
- [ ] The HA number and base Device telemetry both show 32 A after the command.
- [ ] Problem is OFF while DP 10 is 0 and ON for a non-zero fault.
- [ ] Stale pack discovery topics from previous packs are removed so HA does
  not keep an old entity ID.
- [ ] Home Assistant entity IDs are unchanged after adding profile fields.

### Control

- [ ] Start and stop commands return an ACK and a visible result.
- [ ] Current-limit changes stay within the configured range.
- [ ] Out-of-range current values are rejected.
- [ ] A command is rejected while a snapshot is in progress.
- [ ] A duplicate command does not execute twice.
- [ ] A command timeout is reported and is not retried automatically.
- [ ] No command starts charging after a reboot or link recovery.
- [ ] The charge switch reflects the actual session state.
- [ ] Delay control writes the correct DP for the detected profile.

### Safety

- [ ] The device's local RCD/RCBO and control pilot remain functional.
- [ ] The bridge does not interrupt mains power.
- [ ] Loss of LoRa, WiFi or MQTT does not change the device's charging state.
- [ ] MQTT command topics are accessible only to trusted clients.
- [ ] The enclosure, cable gland and strain relief are installed correctly.
- [ ] A qualified person has checked any mains-adjacent work.

## Test matrix

| Test | Expected result |
|---|---|
| Device joins remote AP | Station appears; IP assigned |
| TCP `:6668` connect | Success; Tuya handshake completes |
| DP query | State matches the device display/app |
| Start command | Device starts; ACK ok; state changes |
| Stop command | Device stops; ACK ok |
| Set current 6/10/16 A | Device accepts; feedback matches |
| Out-of-range current | Rejected; no change |
| Start with no vehicle | Rejected |
| Start with fault | Rejected |
| Duplicate command | ACK repeated; no double action |
| LoRa packet loss | Missing chunks retried; then timeout result |
| Tampered frame | Dropped; no action |
| Replayed frame | Dropped |
| Three-phase fixture | `auto` selects `3-phase`; control allowed |
| Single-phase fixture | `auto` selects `single-phase`; control allowed |
| F-OBZ2 fixture | `auto` selects `F-OBZ2`; control allowed |
| Unknown fixture | `fallback` selected; control rejected |
| Manual profile mismatch | `profile_match: false`; control rejected |
| Custom profile save/reboot | Profile appears after JSON save; stays selected after the second save |
| Generic `entities` profile | Discovery and per-entity state are published for the descriptor |
| Generic switch/number/select/button/text | Command topics write the mapped DP values |
| Generic profile switch to built-in | Generic discovery/state cleared; legacy entities return |
| Invalid custom JSON | Rejected; previous profiles preserved |
| Custom profile factory reset | Custom JSON and selection cleared |
| Device offline | Availability offline after 10 minutes |
| Base offline | MQTT LWT offline; device unaffected |
| Home Assistant restart | State restored; no command replay |
| ESP32 reboot | Reconnects; no auto-start |
| Device reboot | Reconnects; state republished |
| 24-hour soak | No resets; stable heap; no stuck TCP |
| Airtime measurement | Below 1% at 868 MHz |
| Final range test | Stable RSSI/SNR at the installed position |

## Acceptance criteria

- Home Assistant shows status, power, current, voltage, energy, temperature and
  fault information within 60 seconds for a built-in charger profile.
- A generic `entities` profile publishes the expected discovery configs and
  per-entity state, and writable entities send the mapped DP values.
- Start, stop and current limit work with an ACK and a visible result for the
  built-in charger profiles.
- Link loss marks the bridge unavailable without changing the device's charging
  state.
- No auto-start occurs after any reboot.
- The 24-hour soak completes without resets, memory growth or stuck
  connections.
- Airtime stays below the 1% EU limit.
- No credentials appear in Git, logs or screenshots.
- The enclosure, power and antenna installation is safe and the final range test
  passes.

## Known limitations

- The built-in profiles target Feyree/F-OBZ2 chargers. Other devices need a
  custom generic `entities` profile or a compact legacy state/control profile.
- The generic descriptor subset supports sensor, binary_sensor, switch, number,
  select, button and text entities. Climate, light, cover, fan, lock, vacuum and
  advanced tuya-local mapping rules are not implemented.
- One Tuya device is supported per bridge. Multi-device support is not
  implemented.
- Raw DP snapshots are capped at 2560 bytes. Devices with larger snapshots are
  not supported.
- Snapshots run every 60 seconds by default. The bridge is not suitable for fast
  dynamic load balancing.
- Most Tuya devices accept one local TCP client at a time.
- The firmware has no physical arm-switch input.
- Updates use the SD card; network OTA is not included.
- The web UI is unauthenticated and must stay on a trusted network.
- LoRa range and duty cycle depend on the antenna, enclosure and local
  interference.

## Possible extensions

- More generic entity types and tuya-local mapping features (climate, light,
  cover, fan, lock, vacuum, value redirects and conditions).
- Additional built-in Tuya device profiles and DP maps.
- A physical remote-control enable input.
- Network OTA with rollback.
- Multi-device support.
- Extended energy and cost reporting.
- Additional phase-level diagnostics in Home Assistant.
