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
  fault information within 60 seconds.
- Start, stop and current limit work with an ACK and a visible result.
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

- The included DP map targets a Feyree three-phase charger. Other devices need
  their own map and control profile.
- Snapshots run every 60 seconds by default. The bridge is not suitable for fast
  dynamic load balancing.
- Most Tuya devices accept one local TCP client at a time.
- The firmware has no physical arm-switch input.
- Updates use the SD card; network OTA is not included.
- The web UI is unauthenticated and must stay on a trusted network.
- LoRa range and duty cycle depend on the antenna, enclosure and local
  interference.

## Possible extensions

- Additional Tuya device profiles and DP maps.
- A physical remote-control enable input.
- Network OTA with rollback.
- Multi-device or multi-charger support.
- Extended energy and cost reporting.
- Additional phase-level diagnostics in Home Assistant.
