# Browser profile catalog and converter

The base node no longer stores the full tuya-local catalog. The browser fetches
the catalog and descriptor YAML, converts the selected descriptor into
precompiled Home Assistant MQTT discovery configs, and streams the result to the
base. The base stores only the selected pack.

## What is generated

The catalog is generated from a pinned tuya-local checkout:

- [`catalog.json`](catalog.json) — all descriptors, grouped by device type,
  with the pinned tuya-local commit and raw YAML base URL.
- [`offline_chargers.json`](offline_chargers.json) — pre-converted charger packs
  served by the base for offline use.
- `firmware/base-node/web_assets.cpp/.h` — the builder page, converter JS,
  mapping JS and offline charger JSON embedded in the base firmware.

At the current pin there are **1,779 descriptors and all 1,779 convert**. There
is no “milestone”/unsupported gate in the builder.

## Browser flow

Open `http://<base-ip>/profiles` from the base UI. The page:

1. loads the offline charger catalog from the base;
2. optionally loads the online catalog from GitHub raw and the descriptor YAML
   from the pinned tuya-local commit;
3. converts the descriptor in the browser;
4. previews the generated discovery entities;
5. uploads the pack to `/api/profile/save` as NDJSON.

The online catalog URL in the builder is a branch/raw URL. For a branch whose
name contains `/`, use the `refs/heads/` form, for example:

```text
https://raw.githubusercontent.com/gyenchev/tuya-lora-bridge/refs/heads/feature/dp-profiles/web/catalog.json
```

After merging to `main`, the `refs/heads/main` form can be used, or the branch
can be pinned to a commit for reproducibility.

The converter loads `js-yaml` from jsDelivr for online conversion. Offline
charger packs are already converted and do not need js-yaml.

## Conversion output

Each pack is a list of Home Assistant MQTT discovery entries plus metadata:

```json
{
  "format": 1,
  "id": "feyree_ev_charger",
  "name": "EV charger",
  "device_type": "Chargers",
  "source": "tuya-local <commit>/feyree_ev_charger.yaml",
  "detect": { "require": [101, 10, 106], "priority": 5 },
  "discovery": [
    {
      "component": "sensor",
      "object_id": "e0",
      "config": {
        "name": "Status",
        "state_topic": "tuya-lora/charger/dps",
        "value_template": "{{ ... }}"
      }
    }
  ]
}
```

The ESP32 adds the device, availability and origin blocks at publish time. It
also publishes a hidden **Raw DPs** sensor whose `dps` attribute contains the
complete raw DP snapshot. Command templates use that sensor to perform
conditional mappings, bit masks and packed-value read-modify-write.

### Native platforms

The converter generates native MQTT discovery for:

`light`, `fan`, `cover`, `climate`, `water_heater`, `humidifier`, `lock`,
`vacuum`, `alarm_control_panel`, `siren`, `valve`, `event`, `time`,
`datetime`, `lawn_mower`, `camera`, `sensor`, `binary_sensor`, `switch`,
`number`, `select`, `button` and `text`.

Media players, Tuya remotes and Tuya infrared emitters have no single standard
MQTT platform; those descriptors are decomposed into switches, numbers,
selects, buttons, text entities and sensors. All leftover DPs remain visible in
the Raw DPs sensor.

### Raw-DP sensor

The base always publishes, in pack mode:

- the retained raw snapshot at `state_topic`;
- one retained per-DP topic at `<state_topic>/<dp-id>` (used by the camera
  image entity and available for other automations);
- a `sensor.<ha-name>_pack_raw` entity whose `dps` attribute is the complete DP
  map.

## Storage and limits

Packs are uploaded as newline-delimited JSON:

- first line: metadata (`id`, `name`, `device_type`, `detect`,
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
- writes `catalog.json` and `offline_chargers.json`;
- generates `firmware/base-node/web_assets.cpp/.h` with the builder HTML,
  converter JS, mapping JS and offline catalog.

The offline charger catalog is capped at a total of about 200 KB so the base
firmware stays within the 1.75 MB app partition. The online catalog still
contains every charger.

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
