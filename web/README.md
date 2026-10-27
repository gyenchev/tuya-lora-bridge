# Browser profile catalog and converter

The base node no longer stores the full tuya-local catalog. The browser fetches
the catalog and descriptor YAML, converts the selected descriptor into
precompiled Home Assistant MQTT discovery configs, and streams the result to the
base. The base stores only the selected pack.

## What is generated

The catalog is generated from a pinned tuya-local checkout:

- [`catalog.json`](catalog.json) — all descriptors, grouped by device type,
  with the pinned tuya-local commit and raw YAML base URL.
- `firmware/base-node/web_assets.cpp/.h` — the profile editor page, converter
  JS and mapping JS embedded in the base firmware.

At the current pin there are **1,779 descriptors and all 1,779 convert**. There
is no “milestone”/unsupported gate in the builder.

## Browser flow

Open `http://<base-ip>/profiles` from the base UI. The page:

1. loads the active profile from `/api/profile/export` and shows it as editable
   JSON;
2. optionally loads the online catalog from GitHub raw and the descriptor YAML
   from the pinned tuya-local commit;
3. converts the selected descriptor in the browser into the same profile JSON;
4. lets you fine-tune the metadata, telemetry and discovery entries in the
   textarea;
5. uploads the edited profile to `/api/profile/save` as NDJSON.

The online catalog URL in the builder is a branch/raw URL. For a branch whose
name contains `/`, use the `refs/heads/` form, for example:

```text
https://raw.githubusercontent.com/gyenchev/tuya-lora-bridge/refs/heads/feature/dp-profiles/web/catalog.json
```

The builder default now uses `refs/heads/main`; the catalog can also be pinned
to a commit for reproducibility.

The converter loads `js-yaml` from jsDelivr for online conversion. There is no
offline catalog; the base stores only the saved profile.

## Conversion output

Each pack is a list of Home Assistant MQTT discovery entries plus metadata:

```json
{
  "format": 1,
  "id": "feyree_ev_charger",
  "name": "EV charger",
  "device_type": "Chargers",
  "source": "tuya-local <commit>/feyree_ev_charger.yaml",
  "discovery": [
    {
      "component": "sensor",
      "object_id": "status",
      "config": {
        "name": "Status",
        "state_topic": "__STATE_TOPIC__",
        "value_template": "{{ ... }}"
      }
    }
  ]
}
```

Discovery object IDs are semantic slugs derived from the entity name (for
example `Status` → `status`, `Charge current` → `charge_current`), lowercased
and separated with underscores. The base prefixes them with `pack_` when
publishing.

The ESP32 resolves the `__STATE_TOPIC__`, `__CMD_TOPIC__` and `__RAW_ENTITY__`
tokens at publish time, then adds the device, availability and origin blocks. It
also publishes a hidden **Raw DPs** sensor whose `dps` attribute contains the
complete raw DP snapshot. Command templates use that sensor to perform
conditional mappings, bit masks and packed-value read-modify-write.

The pack metadata also contains a small `telemetry` array: labels, DP IDs,
enum maps and scaling for the status page. `/api/profile/telemetry` serves it and
the status page renders the values from the current raw snapshot. Packs without
telemetry metadata fall back to a table of raw DPs.

### Native platforms

The converter generates native MQTT discovery for:

`light`, `fan`, `cover`, `climate`, `water_heater`, `humidifier`, `lock`,
`alarm_control_panel`, `siren`, `valve`, `event`, `time`, `datetime`,
`lawn_mower`, `camera`, `sensor`, `binary_sensor`, `switch`, `number`, `select`,
`button` and `text`.

Vacuums, media players, Tuya remotes and Tuya infrared emitters have no single
standard MQTT platform; those descriptors are decomposed into switches,
numbers, selects, buttons, text entities and sensors. All leftover DPs remain
visible in the Raw DPs sensor.

### Raw-DP sensor

The base always publishes, with an active profile:

- the retained raw snapshot at `state_topic`;
- one retained per-DP topic at `<state_topic>/<dp-id>` (used by the camera
  image entity and available for other automations);
- a `sensor.<ha-name>_pack_raw` entity whose `dps` attribute is the complete DP
  map.

## Storage and limits

Packs are uploaded as newline-delimited JSON:

- first line: metadata (`id`, `name`, `device_type`, `source`, `telemetry`,
  `discovery_count`);
- following lines: one discovery item per line.

The base writes each item to a small LittleFS file and commits the metadata
last. This avoids the heap cost of holding a large pack and allows the largest
tuya-local descriptors (181 KB, 81 entities at the current catalog pin) to be
stored and published.

Limits:

| Limit | Value |
|---|---:|
| Upload body | 250 KB |
| Discovery entities | 256 |
| Discovery item line | 24 KB |
| Active pack storage | 384 KB LittleFS partition |
| Pack mode | one active browser pack |
| MQTT buffer | 8192 bytes recommended for browser packs |

Large discovery configs are published as one MQTT message. Set the base
**MQTT buffer size** to at least 8192 bytes; a 2048-byte buffer silently
rejects configs such as the Feyree Charge current entity, leaving HA on a
stale/old entity. After changing the buffer, reboot the base and reload the HA
MQTT integration if needed.

The base sketch contains a custom `partitions.csv`:

```text
nvs       0x9000   20K
otadata   0xe000    8K
app0      0x10000  1792K
app1      0x1d0000 1792K
spiffs    0x390000 384K
coredump  0x3f0000 64K
```

## Regenerating the catalog and assets

Requirements: Node 20+ and `npm`.

```bash
cd web
npm install
node build_assets.mjs \
  --source /path/to/tuya-local/custom_components/tuya_local/devices
```

The script:

- parses all YAML descriptors with `js-yaml`;
- classifies device types;
- builds every profile to compute entity counts and pack sizes;
- writes `catalog.json`;
- generates `firmware/base-node/web_assets.cpp/.h` with the profile editor HTML,
  converter JS and mapping JS.

## Building the base

The custom partition table is picked up automatically from the sketch folder:

```bash
LIBS="$HOME/Documents/Arduino/libraries"
BASE_FQBN=esp32:esp32:ttgo-lora32:PartitionScheme=min_spiffs,UploadSpeed=460800
arduino-cli compile --fqbn "$BASE_FQBN" --libraries "$LIBS" firmware/base-node
arduino-cli upload  --fqbn "$BASE_FQBN" --libraries "$LIBS" -p /dev/cu.usbserial-... firmware/base-node
```

The app binary is about 1.64 MB; the custom app partition is 1.75 MB. Back up
NVS (`0x9000`, `0x5000`) before the first switch to this partition table.

## Adding platform coverage

Platform builders live in `profile_converter.mjs`; the shared Jinja mapping
engine is in `profile_mapping.mjs`. If a native builder cannot represent a
descriptor, it falls back to generic DP sensors rather than rejecting the
profile. The raw-DP sensor remains the complete view of every DP.
