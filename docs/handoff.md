# Project handoff

Date: 2026-10-28  
Repository: `gyenchev/tuya-lora-bridge`  
Branch: `refactor/generic-device-bridge` (based on `main` at `0a79d5e`)  
Firmware version: **0.5.0** on both node sketches

## 1. What the project is

A two-node ESP32 bridge:

```text
Tuya device --WiFi AP--> remote ESP32 --LoRa--> base ESP32 --WiFi/MQTT--> Home Assistant
```

- **Remote node:** hosts the Tuya device AP, discovers the device on TCP 6668,
  proxies raw Tuya DP JSON, and answers base commands.
- **Base node:** owns the active device profile, DP mapping, MQTT/Home Assistant
  discovery, control commands, the web UI and system telemetry.
- The LoRa link is AES-128-GCM authenticated/encrypted with boot IDs, sequence
  numbers and replay protection.

The bridge is generic: the base maps any supported tuya-local-style descriptor
to Home Assistant entities. There is no built-in device-specific profile and no
legacy device-specific state model.

## 2. This branch

This branch removes the original device-specific layer and all backwards
compatibility:

- Removed `firmware/base-node/profiles.*` and `firmware/base-node/entities.*`.
- Removed the hardcoded `StatusData` model and `statusName()` from both copies
  of `lora_protocol.*`.
- Removed the built-in Feyree/F-OBZ2 profiles and the custom-profile parser.
- Removed legacy HA discovery (`status`, `power`, `energy`, voltage/current,
  control buttons and number entities).
- Removed the mapped `state` MQTT topic; the base now publishes the raw DP
  snapshot and profile discovery only.
- Removed the legacy command topics (`charge`, `start`, `stop`,
  `current_limit`, `delay`, `mode`). Only `command/query` and `command/dp`
  remain.
- Removed the `/profile.json` migration path and the legacy hardcoded
  topic-prefix rewrite.
- Default Home Assistant device name is now **`Tuya device`**.
- Docs and web UI wording are generic.

External Tuya protocol literals and upstream tuya-local catalog data are
unchanged. That includes status values such as `charger_free` or `OpenCharging`,
DP names such as `session_energy`, the catalog group `Chargers`, and the
upstream profile IDs/files.

## 3. Current source state

- `main` is at `0a79d5e`.
- Project license: **MIT**, Copyright (c) 2026 Georgi Enchev.
- tuya-local-derived material is documented in `THIRD_PARTY_NOTICES.md`.
- No Si4432 work is present or pending.

Merged feature PRs on `main`:

| PR | Result |
|---|---|
| #3 | Base-local LoRa telemetry, separate **LoRa** tab, profile tile cleanup, System-only factory reset |
| #4 | Coordinated live LoRa configuration, five-minute rollback, PSK generation, duty-cycle limits |
| #5 | MIT relicensing and tuya-local attribution |

## 4. Current hardware/runtime state

### Base

- Board: TTGO LoRa32 V2.1 / T3 V1.6.1
- USB port: `/dev/cu.usbserial-56570272181`
- IP: `192.168.88.96`
- Firmware: 0.5.0 (flashed from this branch)
- Home Assistant device name: `CarCharger` (saved in NVS)
- MQTT: connected
- Web UI: `http://192.168.88.96/`
- LoRa link: online, `have_seen=true`, `have_state=true`
- Current radio: **869.525 MHz, SF9, 125 kHz, CR 4/5, preamble 8, TX 20 dBm**
- Last RSSI/SNR: approximately **-104 dBm / -3.7 dB**
- Current profile: active catalog profile, 22 entities, no profile error
- Duty band: 869.4-869.7 MHz → **10%** limit
- Telemetry at last check: 60-minute duty about **0.66%**, observed about
  **3.94%**, `ok=true`

### Remote

- Board: TTGO LoRa32 V2.1 / T3 V1.6.1
- Firmware: 0.4.0 on the device; this branch builds 0.5.0
- Stored device AP SSID: **`Mimi`**
- Setup AP: `TuyaLoraRemote-Setup`
- USB port is not currently present; reconnect USB before reflashing.
- Tuya credentials are in NVS.
- Remote management UI remains editable for bootstrap/recovery.

### Credentials/settings

- WiFi, MQTT and Home Assistant credentials are in NVS, not in the repository.
- The current LoRa PSK is a generated random key in NVS.
- Factory reset sets both nodes back to the all-zero default PSK; generate a new
  key before deployment.
- Do not print keys, passwords or MQTT credentials in logs or commits.

## 5. Web UI

Top-level tabs:

1. **Status** — profile/link status and profile-driven device telemetry
2. **LoRa** — link transfer telemetry, duty cycle, coordinated LoRa settings
3. **Settings** — WiFi, MQTT, LoRa, Home Assistant, timing, control, profile,
   System

The Status page uses only generic profile telemetry. There are no
device-specific status or power tiles. Factory reset is only visible in
**Settings > System**. The base LoRa tab is the normal place to change radio
settings; normal **Save and reboot** rejects LoRa changes and directs the user
to the coordinated apply action.

## 6. Coordinated LoRa configuration

Implemented in both firmwares:

- Base sends `CONFIG_SET` under the old PHY/PSK.
- Remote validates, stores old/new configs in NVS, sends `CONFIG_ACK` under the
  old PHY/PSK, then reinitializes the radio and codec live without rebooting.
- Base applies the new PHY/PSK only after ACK, then sends probes.
- Remote sends `CONFIG_READY` under the new config.
- Base commits and sends `CONFIG_COMMIT`; remote commits and sends
  `CONFIG_COMMIT_ACK`.
- Old configuration remains in a dedicated NVS trial namespace until commit.
- If communication is not established within five minutes, the node restores
  the old configuration live.
- ACKs are repeated and the base retries for 60 seconds to tolerate a busy
  remote.
- Remote skips the full subnet scan when no AP client is connected and services
  LoRa during scans.
- Remote sends HELLO periodically so the base can track protocol capability.
- `HELLO` protocol capability is 2; the base refuses a known older remote.

### Relevant state fields

`/api/status` exposes `lora_config` with:

- `state`: `idle`, `sending`, `waiting_ack`, `waiting_ready`,
  `waiting_commit_ack`, `error`
- `busy`
- `config_id`
- `attempts`
- `remote_fw_version`
- `remaining_ms`
- `last_contact_age_ms`
- `error`

Remote `/api/status` exposes the pending/committed/rollback state.

## 7. Duty-cycle telemetry

The LoRa tab shows:

- per-frame and burst speeds;
- wire/payload bytes;
- DP snapshot payload speed;
- message mix;
- errors;
- fixed 60-minute duty budget and observed duty;
- band-specific duty limit.

Limit mapping currently implemented in `lora_stats.cpp`:

| Frequency | Limit |
|---|---:|
| 868.0-868.7 MHz | 1% |
| 868.7-869.2 MHz | 0.1% |
| 869.25-869.3 MHz | 0.1% |
| 869.3-869.4 MHz | 1% |
| 869.4-869.7 MHz | 10% |
| 869.7-870.0 MHz | 1% |
| 433.05-434.79 MHz | 10% |

The 60-minute row uses a fixed 3600-second denominator and the selected band
limit. The “observed” value uses airtime divided by uptime.

## 8. Key files

```text
firmware/base-node/base-node.ino          base loop, config state machine, MQTT
firmware/base-node/web_ui.cpp             base settings/status/UI logic
firmware/base-node/browser_profile.cpp    profile pack storage and discovery
firmware/base-node/config_store.*         base NVS settings and zero-PSK default
firmware/base-node/lora_protocol.*        frame codec and CONFIG_* messages
firmware/base-node/lora_trial.*           trial/rollback NVS state
firmware/base-node/lora_stats.*           airtime, rates, duty, message counters
firmware/remote-node/remote-node.ino      remote loop, discovery, config apply
firmware/remote-node/web_ui.cpp           remote settings/status UI
firmware/remote-node/config_store.*       remote NVS settings
firmware/remote-node/lora_protocol.*      duplicated protocol copy
firmware/remote-node/lora_trial.*         duplicated trial state
docs/lora-protocol.md                     wire protocol and coordinated config
docs/home-assistant.md                    MQTT/HA behavior
docs/testing.md                           validation checklist
README.md                                 setup, API, license
THIRD_PARTY_NOTICES.md                    tuya-local MIT attribution
```

## 9. Build/flash commands

Arduino CLI path used in this environment:

```bash
CLI="/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"
FQBN="esp32:esp32:esp32:FlashSize=4M,PartitionScheme=min_spiffs,UploadSpeed=460800"
LIBS="$HOME/Documents/Arduino/libraries"
```

Compile:

```bash
"$CLI" compile --fqbn "$FQBN" --libraries "$LIBS" \
  --output-dir /tmp/base-build firmware/base-node

"$CLI" compile --fqbn "$FQBN" --libraries "$LIBS" \
  --output-dir /tmp/remote-build firmware/remote-node
```

Flash:

```bash
"$CLI" upload -p /dev/cu.usbserial-56570272181 \
  --fqbn "$FQBN" --input-dir /tmp/base-build firmware/base-node

# reconnect the remote USB first
"$CLI" upload -p /dev/cu.usbserial-... \
  --fqbn "$FQBN" --input-dir /tmp/remote-build firmware/remote-node
```

## 10. Validation already performed

- Both nodes compile with 0.5.0 on this branch.
- The base was flashed with 0.5.0 and reconnects to WiFi, MQTT and the LoRa link.
- Retained-topic cleanup was performed against the broker:
  - removed `tuya-lora/charger/#` (old device name);
  - removed `tuya-lora/garage_charger/#` (old device name);
  - removed `tuya-lora/carcharger/state` (legacy mapped state).
- No stale Home Assistant discovery topics remained under `tuya_lora_bridge`;
  the active profile publishes exactly its 22 pack entities plus `pack_raw` and
  the four link entities.
- Coordinated frequency changes, PSK-only changes and remote rollback were
  validated on 0.4.0; re-test after flashing 0.5.0 to the remote.
- Duty-limit mapping and the 60-minute budget were validated on 0.4.0.
- The profile editor/converter were validated on 0.4.0.

## 11. Known issues / next steps

1. Flash 0.5.0 to the remote and re-run the checklist in `docs/testing.md`.
2. The current LoRa link is weak at approximately **RSSI -104 dBm / SNR -3.7 dB**.
   Check antenna orientation, cable loss, enclosure and distance. SF9 at
   869.525 MHz is currently in use; a higher SF may help range but increases
   airtime. Keep the 10% band budget in mind.
3. The remote USB port is not present. Reconnect it before the next remote
   flash.
4. Base post-apply rollback (no contact after applying) has not been separately
   fault-injected.
5. Existing NVS keeps the saved Home Assistant device name `CarCharger`, so
   topics and entity IDs stay `tuya-lora/carcharger/...` and
   `tuya_carcharger_*`. Change the name in **Settings > Home Assistant** to
   adopt a new prefix.
6. The 0.5.0 cleanup of old retained topics was performed manually. This branch
   intentionally has no automatic migration for future installs.
7. Factory reset returns to the all-zero PSK. Regenerate and apply before
   deployment.
8. Keep `THIRD_PARTY_NOTICES.md` with separately distributed binaries or
   profile packs that include tuya-local-derived material.
