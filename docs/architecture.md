# Architecture

## Topology

```text
┌──────────────────────────────┐
│ Tuya device                  │
│ 2.4 GHz WiFi                 │
│ local TCP API on :6668       │
└──────────────┬───────────────┘
               │ WiFi: device is a client of the remote AP
               ▼
┌──────────────────────────────┐
│ Remote node — TTGO #1        │
│                              │
│ WiFi AP + DHCP               │
│ Tuya local TCP client        │
│ raw DP snapshot proxy        │
│ DP_SET → Tuya CONTROL        │
│ OLED status                  │
│ LoRa 868 MHz + AES-GCM       │
└──────────────┬───────────────┘
               │ LoRa 868.1 MHz, SF7, sync 0xA6
               │ DP snapshot every 60 s, control on demand
               ▼
┌──────────────────────────────┐
│ Base node — TTGO #2          │
│                              │
│ LoRa query/reassembly        │
│ DP map + control logic       │
│ MQTT client + discovery      │
│ retained state + availability│
│ OLED status                  │
└──────────────┬───────────────┘
               │ WiFi STA → home router → MQTT broker
               ▼
┌──────────────────────────────┐
│ Home Assistant               │
│ MQTT entities + automations  │
└──────────────────────────────┘
```

## Responsibilities

### Tuya device

The device owns its local schedules, RFID/authorization behavior, power
electronics and all protective functions. It exposes a Tuya local API after it
is activated in the Tuya app. The bridge never modifies the device and never
interrupts its mains supply.

### Remote node

- Hosts a 2.4 GHz WPA2 AP for the device.
- Provides DHCP on `192.168.4.0/24`.
- Discovers the device by listing AP clients and probing TCP port `6668`.
- Maintains a Tuya local TCP connection using protocol version 3.1, 3.3, 3.4 or
  3.5.
- Answers `DP_QUERY` by calling `tuya.query()` and returning the raw decrypted
  DP JSON in chunks. It does not map DP numbers.
- Applies `DP_SET` by passing the raw `dps` fragment to `tuya.setDps()` and
  returns an `ACK`.
- Retransmits missing chunks after `DP_ACK`.
- Serves a live status page and a tabbed settings UI on the AP.
- Stores AP credentials, Tuya keys, protocol version, optional static IP, LoRa
  settings and timing values in ESP32 NVS.
- Shows AP clients, device IP, Tuya state, snapshot activity and LoRa status on
  the OLED.

### Base node

- Sends `DP_QUERY` every 60 seconds, verifies AES-GCM and replay protection,
  reassembles chunks, and acknowledges the chunk bitmap.
- Publishes the raw snapshot to retained `tuya-lora/<slug>/dps` and the
  per-DP topics.
- Publishes retained `tuya-lora/<slug>/link` and
  `tuya-lora/<slug>/availability` topics.
- Stores one active browser profile pack. A browser pack is precompiled
  discovery plus command templates; the remote stays a raw DP proxy.
- The browser converts any tuya-local descriptor into a pack using native MQTT
  platforms (light, fan, cover, climate, water heater, humidifier, lock,
  alarm, siren, valve, event, time/datetime, lawn mower, camera, sensor,
  binary sensor, switch, number, select, button, text) and decomposes
  vacuums, media players, remotes and infrared emitters into standard entities.
- Stores packs as metadata plus one small LittleFS file per discovery entity.
  Uploads are streamed as NDJSON, so large descriptors do not require a large
  in-RAM JSON document.
- Publishes a hidden Raw DPs sensor with the complete `dps` map as an attribute
  and one retained per-DP topic under the raw snapshot topic. Every DP remains
  available even when a platform cannot express it natively.
- Subscribes to the generic DP command topic. Command templates emit a JSON
  `dps` fragment, which the base forwards to `DP_SET`. Control is rejected when
  no profile is loaded or no snapshot has been seen.
- Publishes precompiled Home Assistant MQTT discovery configs from the pack and
  clears stale entities when the active profile changes.
- Serves a live status page and settings UI on the home WiFi.
- Stores WiFi, MQTT, LoRa, HA, topic and control settings in ESP32 NVS.
- Marks the remote link unavailable after 10 minutes without a valid frame.
- Does not automatically retry a command after a lost ACK.

### Home Assistant

Home Assistant provides the MQTT broker and consumes the discovery entities. No
custom integration is required. Automations can use the snapshot, availability and
link diagnostics for monitoring, notifications and slow control.

## Data flow

### Snapshot: device → Home Assistant

1. The base sends `DP_QUERY` with a request ID.
2. The remote calls `tuya.query()` and returns the raw JSON in `DP_CHUNK`
   frames.
3. The base reassembles chunks, sends `DP_ACK`, and requests missing chunks.
4. The base publishes the raw snapshot to `tuya-lora/<slug>/dps` (retained).
5. The base resolves the active profile from LittleFS. It publishes the
   profile's precompiled discovery configs, a hidden Raw DPs sensor, and
   retained per-DP topics under the raw snapshot topic. Profile entities read
   the raw snapshot directly.
6. The base publishes `tuya-lora/<slug>/link` diagnostics on every valid LoRa
   frame, independent of the device snapshot.
7. The base publishes availability `online`; Home Assistant updates the
   entities.

### Command: Home Assistant → device

1. Home Assistant publishes a profile command template to
   `tuya-lora/<slug>/command/dp`, or a snapshot request to
   `tuya-lora/<slug>/command/query`.
2. The base validates the command and active profile. The profile command is
   already a JSON `dps` fragment. The base sends `DP_SET` with a new sequence
   number.
3. The remote verifies the frame and passes the fragment to `tuya.setDps()`.
4. The remote replies with `ACK` and a result code.
5. The base publishes the result to `tuya-lora/<slug>/command/result` and
   schedules a fresh snapshot.
6. A command timeout is reported to MQTT and is not retried automatically.

### Time

The remote node has no reliable wall clock. It uses `millis()` for durations.
The device keeps its own schedules. The bridge does not send time frames and
does not depend on wall-clock time for safety.

## Sequence diagrams

### Boot and first snapshot

```text
Remote node                         Base node
     │                                  │
     │ boot, start AP, OLED splash      │
     │ scan AP for :6668                │
     │ connect Tuya TCP                 │
     │──── HELLO (boot_id, fw) ────────►│
     │◄─── DP_QUERY req=7 ──────────────│
     │──── DP_CHUNK 0..n ──────────────►│
     │◄─── DP_ACK bitmap ───────────────│
     │                                  │──► MQTT dps + availability
```

### Command

```text
HA          Base node                    Remote node
 │  dp command    │                            │
 │───────────────►│                            │
 │                │──── DP_SET seq=41 ────────►│
 │                │      {"1":true}            │
 │                │◄─── ACK seq=41 ok ─────────│
 │                │──► MQTT result             │
 │                │──── DP_QUERY (after 500ms) ►│
 │                │◄─── DP_CHUNK ... ──────────│
 │                │──► MQTT dps refresh        │
```

### Link loss

```text
Remote node                         Base node
     │                                  │
     │  (no valid frame for 10 min)     │
     │                                  │──► availability offline
     │                                  │    link diagnostics stale
     │                                  │
     │  device keeps its local behavior │
     │  no automatic stop is issued     │
```

## Design constraints

### Bandwidth and latency

LoRa is a low-rate packet link, not an IP transport. The bridge carries a raw DP
snapshot of up to 2560 bytes in 80-byte chunks, plus short control fragments.

- A full snapshot is requested every 60 seconds.
- A successful command triggers a fresh snapshot after a short delay.
- Expected command latency is a few seconds.
- The bridge does not carry streaming data, OCPP or the Tuya cloud protocol.
- It is not suitable for fast dynamic load balancing. Use a local energy meter
  and the device's own load-balancing input for that.

### Reliability

- Chunk bitmap ACK and missing-chunk retransmission for snapshots.
- Stop-and-wait ACK for control.
- Sequence numbers and duplicate suppression.
- Retained state and raw snapshot; availability is separate from state.
- WiFi, TCP and MQTT reconnect handling.
- No command replay after a reboot.
- No automatic start after a reboot or link recovery.

### Security

- LoRa frames are encrypted and authenticated with a pre-shared AES-128 key.
- Replay protection uses `boot_id` plus a monotonic sequence per direction.
- The device AP uses WPA2 with a strong password.
- MQTT uses a dedicated account limited to the bridge topics.
- Do not expose MQTT or Home Assistant to the internet without TLS/VPN.
- Runtime settings live in ESP32 NVS and are edited through the web UI.
- The configuration and status web UIs are unauthenticated. Keep them on a
  trusted network and behind the AP's WPA2 password.
- Command topics are privileged: any MQTT client that can publish there can
  control the device.

### Safety

- The device owns safety. The bridge cannot bypass RCD/RCBO, control-pilot
  checks or fault handling.
- Every command is validated against the active profile and a fresh DP snapshot.
- Current limits are clamped to the configured range.
- A command is rejected while another command or snapshot is in flight.
- Link loss causes no automatic action. The device continues its local
  behavior.
- The bridge never auto-starts after boot, Home Assistant restart or link
  recovery.

### Power and enclosure

- The remote node uses a separate 5 V USB adapter, not the device's internal
  electronics.
- Install the remote node in its own IP65 enclosure with a cable gland and
  strain relief.
- Keep the LoRa antenna vertical and away from mains wiring, inverters and the
  WiFi antenna.
- The base node can use any reliable USB port near the home router and MQTT
  broker.

## Scope

The bridge does not:

- extend WiFi or Ethernet to the device;
- run the Tuya cloud protocol or OCPP over LoRa;
- replace the device's local schedules or safety systems;
- provide real-time power control;
- modify the device hardware.

If the device cannot operate on a local AP without internet, a conventional
WiFi bridge, directional CPE or powerline link is the appropriate alternative.
