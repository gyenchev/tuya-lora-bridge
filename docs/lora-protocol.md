# LoRa link protocol

The LoRa link connects the remote node and the base node. It is deliberately
small because LoRa is a low-rate, duty-cycled radio, not a network transport.
The remote node is a raw Tuya DP proxy; the base node owns the DP map, MQTT and
Home Assistant integration.

## Radio settings

| Setting | Default | Notes |
|---|---|---|
| Frequency | `868.1 MHz` | EU 868 band; 1% duty cycle |
| Spreading factor | `SF7` | Lowest airtime; increase only if range requires it |
| Bandwidth | `125 kHz` | |
| Coding rate | `4/5` | |
| CRC | on | Hardware CRC |
| Preamble | 8 symbols | |
| Sync word | `0xA6` | Private network identifier |
| TX power | `14 dBm` | Check local ERP limits |
| Antenna | 868 MHz, vertical | Keep clear of metal and mains wiring |

Changing the spreading factor changes airtime and range. Re-check the duty-cycle
budget before using SF9 or slower with a 60-second snapshot interval.

## Frame format

### Secure frame

```text
offset  size  field
0       1     magic = 0xF1
1       1     version = 0x01
2       1     type
3       1     flags
4       4     seq (32-bit, big-endian)
8       1     ciphertext length (N)
9       4     boot_id (random per boot)
13      N     ciphertext
13+N    16    AES-GCM tag
```

The authenticated additional data (AAD) is bytes 0 through 12. The 12-byte
nonce is:

```text
boot_id (4) || seq (4) || direction (1) || 0x000000
```

- `direction` is `0x01` for remote → base and `0x02` for base → remote.
- `boot_id` keeps nonces unique across reboots even when `seq` restarts.
- The GCM tag provides integrity and authenticity. A wrong key or tampered frame
  is dropped silently.
- The ciphertext length is limited to 96 bytes; the complete frame is kept below
  128 bytes.

## Message types

| Type | Name | Direction | Payload |
|---:|---|---|---|
| `0x01` | `HELLO` | remote → base | role, firmware version, boot ID |
| `0x02` | `ACK` | both | result, acknowledged sequence, detail |
| `0x03` | `LOG` | remote → base | short debug text |
| `0x04` | `DP_QUERY` | base → remote | `req_id` (2), flags (1) |
| `0x05` | `DP_CHUNK` | remote → base | `req_id` (2), index (1), count (1), data (≤80) |
| `0x06` | `DP_ACK` | base → remote | `req_id` (2), bitmap (4) |
| `0x07` | `DP_SET` | base → remote | raw Tuya `dps` JSON fragment |

There are no separate status, time or configuration frames. The only data path is
the DP snapshot proxy. The remote performs `tuya.query()` or `tuya.setDps()` and
does not interpret DP numbers.

## DP snapshot exchange

1. The base sends `DP_QUERY` with a 16-bit `req_id`.
2. The remote calls `tuya.query()` and splits the raw response JSON into
   `DP_CHUNK` frames. Each chunk carries up to 80 bytes; a snapshot is capped at
   2560 bytes or 32 chunks.
3. The base reassembles the chunks, sends `DP_ACK` with a 32-bit bitmap of the
   chunks it has, and asks for missing chunks again if needed.
4. The remote retransmits only the missing chunks after a short timeout.
5. The base parses the raw `dps` object, detects the profile, publishes
   `tuya-lora/<slug>/dps` and `tuya-lora/<slug>/state`, and schedules the next
   query.

A full snapshot is requested every 60 seconds. The base marks the remote link
offline after 10 minutes without a valid frame or chunk. The raw snapshot is
published to MQTT unchanged, so the MQTT buffer must be at least as large as the
snapshot; the default 4096-byte buffer covers the 2560-byte snapshot cap.

## DP_SET payload

`DP_SET` contains the inside of the Tuya `dps` object:

```json
{"124":"OpenCharging"}
{"124":"CloseCharging"}
{"115":16}
{"118":5}
{"104":5,"14":"charge_schedule"}
```

The remote wraps the fragment in the correct Tuya `CONTROL` (3.1/3.3) or
`CONTROL_NEW` (3.4/3.5) envelope. The base owns the model-specific mapping, so it
can change without reflashing the remote node.

Control is accepted only after a profile is loaded and the base has received a
valid snapshot. Without a profile, commands are rejected.

## State mapping

The LoRa link has no dedicated status payload. The base decodes the raw `dps`
object into its local `StatusData` struct and publishes the mapped state to
`tuya-lora/<slug>/state`. DP numbers and meanings are documented in
[`tuya-device.md`](tuya-device.md); published fields are documented in
[`home-assistant.md`](home-assistant.md).

Start and stop are explicit `DP_SET` values, not toggles. A duplicated `DP_SET`
is dropped by replay protection and is not executed twice.

## ACK payload

```c
struct __attribute__((packed)) AckPayload {
  uint8_t  result;      // 0 ok, 1 rejected, 2 invalid, 3 busy, 4 hardware error
  uint32_t acked_seq;   // sequence of the command being acknowledged
  uint16_t detail;      // optional error/status detail
};
```

## HELLO payload

```c
struct __attribute__((packed)) HelloPayload {
  uint8_t  role;        // 1 remote node, 2 base node
  uint8_t  fw_version;  // protocol payload version
  uint32_t boot_id;
};
```

`boot_id` is also present in the secure header. The payload copy is for logging.

## Control DP mapping

The base builds these fragments from the active profile. The DP numbers are
defaults in `base-node/config.h`; confirm the real map in
`tuya-lora/<slug>/dps` before relying on remote control.

| Action | Single-phase | 3-phase | F-OBZ2 |
|---|---|---|---|
| Charge on | `{"112":true}` | `{"124":"OpenCharging"}` | `{"124":0}` |
| Charge off | `{"112":false}` | `{"124":"CloseCharging"}` | `{"124":1}` |
| Current limit | `{"102":<A>}` | `{"115":<A>}` | `{"115":<A>}` |
| Delay | `{"104":<h>,"14":"charge_schedule"}` | `{"118":<h>}` | not mapped |
| Mode | `{"14":"charge_now"}` / `{"14":"charge_schedule"}` | same | not mapped |

`<A>` is clamped to the configured current range; `<h>` is 0–15 hours. For the
single-phase profile, delay `0` sends `charge_now` instead of
`charge_schedule`. The 3-phase profile writes only DP `118`; DP `14` is not part
of its delay path. The F-OBZ2 profile has no public delay or mode DP, so those
commands are rejected.

Other Tuya devices require their own DP map and control fragments.

## Sequence, ACK and retry

- Each direction maintains its own 32-bit `seq`, starting at 0 after boot.
- `DP_QUERY` is acknowledged indirectly by `DP_CHUNK` frames; the base
  acknowledges received chunks with `DP_ACK`, and the remote retransmits missing
  chunks.
- `DP_SET` is acknowledged by a normal `ACK` with `acked_seq` set to the
  `DP_SET` sequence. The base waits up to 8 seconds, then publishes a timeout
  result.
- A lost `DP_SET` ACK is ambiguous because the command may already have been
  applied. The base does not retry automatically. Verify the state before
  sending the command again.
- The receiver keeps the highest `seq` per `boot_id`; duplicate or older frames
  are dropped and not executed again.
- `ACK` frames are not themselves acknowledged.
- The base allows one command at a time. A new command while one is in flight,
  or while a snapshot is in progress, is rejected as `busy`.

## Snapshot scheduling

- The base sends `DP_QUERY` every 60 seconds.
- A successful control command triggers an immediate query after a short delay.
- The remote performs the Tuya query and sends the raw JSON in chunks.
- The base marks the remote link offline after 10 minutes without a valid frame
  or chunk.
- On reconnect, the base sends a fresh query. The remote does not push
  unsolicited snapshots.

## Collision avoidance

LoRa is half-duplex. With two nodes, a simple schedule is sufficient:

1. The base initiates a query on its 60-second timer or after a command.
2. The remote replies with a chunk burst and listens for `DP_ACK`.
3. The base sends `DP_ACK` and either requests missing chunks or waits for the
   next interval.
4. The base rejects new control commands while a snapshot is in progress.

## Encryption and authentication

### Key

- 16-byte pre-shared key (AES-128), stored in ESP32 NVS on both nodes and
  entered through each node's configuration web UI.
- Generate it with a cryptographic random source, for example:

  ```bash
  openssl rand -hex 16
  ```

- The same value must be configured on both nodes. Rotate it by updating both
  web UIs and rebooting both nodes.

### Nonce and replay protection

```text
nonce = boot_id (4) || seq (4) || direction (1) || 0x000000
```

- The base tracks the highest `seq` seen for each `boot_id` and rejects older
  frames, allowing a small window for retransmission.
- A reboot generates a new `boot_id`, so old frames cannot be replayed after a
  reboot.
- ACKs use the same scheme; a duplicated ACK is harmless.

### What is not protected

- Radio metadata such as frequency, timing and RSSI is visible.
- Traffic analysis can reveal when the device is active.
- The WiFi AP and MQTT have their own security and must be secured separately.
- The configuration web UI is unauthenticated.

## Airtime and duty cycle

The EU 868.0–868.6 MHz band allows a 1% duty cycle. Estimate airtime with the
Semtech LoRa calculator or the LoRa library formula.

Approximate airtime for a 60-byte frame, preamble 8, explicit header and CRC on:

| SF | BW | Airtime | 1 frame / 60 s | Notes |
|---:|---:|---:|---:|---|
| 7 | 125 kHz | ~120 ms | 0.20% | Default |
| 9 | 125 kHz | ~400 ms | 0.67% | Better sensitivity; still legal at 60 s |
| 10 | 125 kHz | ~800 ms | 1.33% | Too high at 60 s; use a slower interval |

Add ACKs and retries and keep the total below 1%. At SF9 with a 60-second
snapshot plus ACKs, the link is close to the limit. Use a 90–120 second snapshot
interval when using SF9 or slower.

## Link diagnostics

The base publishes `tuya-lora/<slug>/link` on every valid LoRa frame and every
10 seconds while the remote link is online. The payload includes:

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

The diagnostics are independent of the device state. RSSI and SNR are available
before the first DP snapshot succeeds. The device state remains `unknown` until a
snapshot is received.

## Local transfer telemetry

The base also keeps local link counters for the web UI. They are exposed only in
`/api/status` under `lora_stats` and on the status page; they are **not**
published to MQTT or Home Assistant, and they require no remote-node update.

The counters include:

- configured PHY: frequency, spreading factor, bandwidth, coding rate, preamble,
  TX power and the nominal LoRa bitrate;
- RX/TX frame counts and wire bytes (complete encrypted frames);
- RX/TX decoded payload bytes;
- active RX/TX burst rates: frame count, wire/payload bytes and duration for
  consecutive frames, with wire and payload bytes per second. The last burst is
  held after it ends, so a once-a-minute snapshot does not decay to zero while
  idle;
- the last RX/TX frame size, payload size, airtime, instantaneous wire/payload
  speed and RSSI/SNR;
- a rolling 60-minute duty-cycle window: base TX airtime, observed remote TX
  airtime, combined airtime, percentages against the fixed 3600-second window,
  and remaining airtime against the 1% reference budget;
- completed/failed DP snapshots, the last snapshot's payload bytes, chunks,
  duration and effective payload bytes per second;
- per-message RX/TX counts;
- decode failures, replay drops and failed transmissions.

The airtime and rate values are estimates based on the base's radio settings;
both nodes must use the same PHY configuration, as required by the link. Counters
reset when the base boots or reinitializes the radio.

The duty percentages use a fixed one-hour denominator, not the time since boot,
so a short boot-time snapshot does not falsely appear to exceed the limit. The
`rx_percent` value is the remote's transmit duty as observed by the base; the
base's own transmitter is `tx_percent`. The 1% limit shown in the UI is the EU
868.0–868.6 MHz reference; if you use another band, compare the displayed
percentages with that band's limit.
