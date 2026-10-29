# Browser profile catalog and converter

## Web Serial flasher

`flasher/` contains a local Web Serial USB flasher for both boards:

```bash
npm run flasher
```

Open <http://localhost:8787/> in desktop Chrome or Edge. It can fetch the latest
full release images through the local helper server or accept a local
`*-full.bin`. Full flashing erases NVS and LittleFS; use SD/OTA when settings must
be preserved. See [`flasher/README.md`](flasher/README.md).

The base node stores only the selected profile pack. The profile editor in the
browser browses the upstream [tuya-local](https://github.com/make-all/tuya-local)
repository directly, converts a selected YAML descriptor into precompiled Home
Assistant MQTT discovery configs, and streams the result to the base. There is no
catalog file to generate or commit.

## Browser flow

Open `http://<base-ip>/profiles` from the base UI. The page:

1. loads the active profile from `/api/profile/export` and shows it as editable
   JSON;
2. fetches the tuya-local device tree from the GitHub API for the chosen
   repository and ref;
3. shows a flat, searchable file list immediately;
4. builds a grouped catalog in a Web Worker by downloading the descriptor YAML
   files in parallel, then caches the compact result in IndexedDB;
5. converts the selected descriptor in the browser into the profile JSON;
6. lets you fine-tune the metadata, telemetry and discovery entries in the
   textarea;
7. uploads the edited profile to `/api/profile/save` as NDJSON.

The repository defaults to `make-all/tuya-local` and the ref to `main`. Both are
editable, so a tag or commit SHA can be used for a reproducible catalog. The
resolved commit SHA is shown in the UI and recorded in each pack's `source`
field.

### Grouped index

Upstream has no machine-readable grouped catalog, so grouping requires parsing
the YAML descriptors. The first visit downloads the device YAML files (about
1,779 files / 6.8 MB at the time of writing) with limited concurrency and builds
a compact catalog in the browser. This can take tens of seconds. The result is
cached in IndexedDB by repository and commit SHA; later visits load the grouped
list instantly. The **Build grouped catalog** button rebuilds it, and **Cancel**
leaves the flat file list usable.

YAML files are fetched from jsDelivr first and raw.githubusercontent.com as a
fallback. The GitHub API is used only for the tree and commit SHA, so the
unauthenticated API rate limit is not a concern for normal use.

## Conversion output

Each pack is a list of Home Assistant MQTT discovery entries plus metadata:

```json
{
  "format": 1,
  "id": "generic_switch",
  "name": "Tuya device",
  "device_type": "Switches & controls",
  "source": "tuya-local <commit>/example.yaml",
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
example `Status` → `status`, `Battery level` → `battery_level`), lowercased and
separated with underscores. The base prefixes them with `pack_` when publishing.

The ESP32 resolves the `__STATE_TOPIC__`, `__CMD_TOPIC__` and `__RAW_ENTITY__`
tokens at publish time, then adds the device, availability and origin blocks. It
also publishes a hidden **Raw DPs** sensor whose `dps` attribute contains the
complete raw DP snapshot. Command templates use that sensor to perform
conditional mappings, bit masks and packed-value read-modify-write.

The pack metadata also contains a small `telemetry` array: labels, DP IDs, enum
maps and scaling for the status page. `/api/profile/telemetry` serves it and the
status page renders the values from the current raw snapshot. Packs without
telemetry metadata fall back to a table of raw DPs.

### Native platforms

The converter generates native MQTT discovery for:

`light`, `fan`, `cover`, `climate`, `water_heater`, `humidifier`, `lock`,
`alarm_control_panel`, `siren`, `valve`, `event`, `time`, `datetime`,
`lawn_mower`, `camera`, `sensor`, `binary_sensor`, `switch`, `number`, `select`,
`button` and `text`.

Vacuums, media players, Tuya remotes and Tuya infrared emitters have no single
standard MQTT platform; those descriptors are decomposed into switches, numbers,
selects, buttons, text entities and sensors. All leftover DPs remain visible in
the Raw DPs sensor.

### Raw-DP sensor

The base always publishes, with an active profile:

- the retained raw snapshot at `state_topic`;
- one retained per-DP topic at `<state_topic>/<dp-id>` (used by the camera image
  entity and available for other automations);
- a `sensor.<ha-name>_pack_raw` entity whose `dps` attribute is the complete DP
  map.

## Storage and limits

Packs are uploaded as newline-delimited JSON:

- first line: metadata (`format`, `id`, `name`, `device_type`, `source`,
  `telemetry`, `discovery_count`);
- following lines: one discovery item per line.

The base writes each item to a small LittleFS file and commits the metadata
last. This avoids the heap cost of holding a large pack and allows the largest
tuya-local descriptors to be stored and published.

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
**MQTT buffer size** to at least 8192 bytes; a 2048-byte buffer silently rejects
large discovery configs, leaving HA on a stale/old entity. After changing the
buffer, reboot the base and reload the HA MQTT integration if needed.

## Embedding the editor in firmware

The editor, converter, mapping engine and Web Worker are embedded in
`firmware/base-node/web_assets.cpp`:

```bash
node web/build_assets.mjs
```

No `npm install`, local tuya-local checkout or catalog generation is required.
The script only reads `profile_builder.html`, `profile_converter.mjs`,
`profile_mapping.mjs` and `profile_worker.mjs` and regenerates
`web_assets.cpp/.h`.

## Adding platform coverage

Platform builders live in `profile_converter.mjs`; the shared Jinja mapping
engine is in `profile_mapping.mjs`. If a native builder cannot represent a
descriptor, it falls back to generic DP sensors rather than rejecting the
profile. The raw-DP sensor remains the complete view of every DP.

## License

The converter, mapping engine and catalog-building logic include material
derived from tuya-local under the MIT License. See
[`../THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md). The project is MIT
licensed; see [`../LICENSE`](../LICENSE).
