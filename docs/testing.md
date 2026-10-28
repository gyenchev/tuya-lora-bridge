# Testing and validation

Use this checklist when commissioning a new installation or changing the active
profile. The tests assume the device is on the remote AP and the base node is
connected to MQTT and Home Assistant.

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
- [ ] `tuya-lora/<slug>/link` publishes RSSI, SNR, age and last-seen values.
- [ ] A full snapshot reassembles without missing chunks.
- [ ] A deliberately dropped chunk is recovered by the retransmit path.
- [ ] Range and RSSI/SNR are stable at the installed positions.
- [ ] The measured frame rate stays within the EU duty-cycle budget.
- [ ] `/api/status` contains `lora_stats` with PHY, totals, RX/TX bursts,
  last-frame speeds, the 60-minute duty window, DP-snapshot and error counters;
  the **LoRa** tab renders them.
- [ ] After a snapshot, the last-frame speed, burst speed, wire bytes and DP
  snapshot speed change; the duty percentages stay against the fixed 3600 s
  window and do not decay while idle.
- [ ] The LoRa statistics are absent from the MQTT link/state/discovery payloads.

### MQTT and Home Assistant

- [ ] The base publishes retained raw DP and availability topics.
- [ ] Topics use `tuya-lora/<slug>/...`, where `<slug>` is the slugified Home
  Assistant device name.
- [ ] Renaming the Home Assistant device moves the topics and republishes
  discovery with resolved profile placeholders.
- [ ] The Settings page has no MQTT Topics tab or topic override fields.
- [ ] Home Assistant creates the expected entities.
- [ ] A Home Assistant restart restores retained state without replaying
  commands.
- [ ] A base reboot republishes discovery and state.
- [ ] A remote or device outage marks availability offline after the timeout.

### Profile editor

- [ ] `/profiles` loads the active profile from `/api/profile/export` and shows
  it as editable JSON.
- [ ] Selecting an online catalog device replaces the textarea with the
  generated profile JSON.
- [ ] Metadata, telemetry and discovery entries can be edited and saved.
- [ ] The saved profile reboots the base and reports the expected
  `profile_entities`.
- [ ] A large pack (81 entities / ~61 KB) uploads and publishes discovery.
- [ ] The largest catalog pack (181 KB / 63 entities) uploads and reloads.
- [ ] Invalid JSON is rejected without replacing the last valid profile.
- [ ] MQTT topic placeholders (`__STATE_TOPIC__`, `__CMD_TOPIC__`,
  `__RAW_ENTITY__`) resolve to the current device name and topics.
- [ ] The Raw DPs sensor exposes every DP as a `dps` attribute, and per-DP
  topics are published.
- [ ] `/api/profile/telemetry` returns the profile's telemetry fields.
- [ ] The status page renders Device telemetry from the active profile and falls
  back to raw DP rows when the profile has no telemetry metadata.
- [ ] Status-page top tiles show only link/profile information, not charger-only
  state/power/energy tiles.
- [ ] Base OLED shows `Device: available/unknown` and `Profile: <name>`, not
  `Charger`/`Power` rows.
- [ ] With MQTT buffer 8192, the Feyree Charge current discovery (`pack_charge_current`)
  appears in HA; with a 2048 buffer it is rejected and a stale entity can remain.
- [ ] Setting Charge current to 32 A in HA publishes `{"115":32}` (or the active
  variant DP), returns an `ok` command result, and updates raw DP 115/125.
- [ ] The HA number and base Device telemetry both show 32 A after the command.
- [ ] Problem is OFF while DP 10 is 0 and ON for a non-zero fault.
- [ ] Stale pack discovery topics from previous profiles are removed so HA does
  not keep an old entity ID.

### Control

- [ ] The profile command returns an ACK and a visible result.
- [ ] The command is rejected while a snapshot is in progress.
- [ ] A duplicate command does not execute twice.
- [ ] A command timeout is reported and is not retried automatically.
- [ ] No command starts charging after a reboot or link recovery.
- [ ] The profile's charge entity reflects the actual session state.
- [ ] Delay/current controls write the DPs from the active profile's command
  templates.

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
| Profile save/reboot | Profile appears after JSON save; stays active after reboot |
| Invalid profile JSON | Rejected; previous profile preserved |
| Topic rename | Discovery republished with resolved topic placeholders |
| Pack command | ACK returned; raw DP updated |
| LoRa packet loss | Missing chunks retried; then timeout result |
| Tampered frame | Dropped; no action |
| Replayed frame | Dropped |
| Duplicate command | ACK repeated; no double action |
| Device offline | Availability offline after 10 minutes |
| Base offline | MQTT LWT offline; device unaffected |
| Home Assistant restart | State restored; no command replay |
| ESP32 reboot | Reconnects; no auto-start |
| Device reboot | Reconnects; state republished |
| 24-hour soak | No resets; stable heap; no stuck TCP |
| Airtime measurement | Below 1% at 868 MHz |
| Final range test | Stable RSSI/SNR at the installed position |

## Acceptance criteria

- Home Assistant shows the active profile's entities and Device telemetry within
  60 seconds.
- The profile's command templates send the expected DP values with an ACK and a
  visible result.
- Link loss marks the bridge unavailable without changing the device's charging
  state.
- No auto-start occurs after any reboot.
- The 24-hour soak completes without resets, memory growth or stuck connections.
- Airtime stays below the 1% EU limit.
- No credentials appear in Git, logs or screenshots.
- The enclosure, power and antenna installation is safe and the final range test
  passes.

## Known limitations

- Profiles are generated from tuya-local descriptors or edited as JSON; there is
  no built-in profile list or offline catalog.
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

- A physical remote-control enable input.
- Network OTA with rollback.
- Multi-device support.
- Extended energy and cost reporting.
- Additional phase-level diagnostics in Home Assistant.
