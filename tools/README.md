# Tools

## tuya-local profile converter

`tuya_local_to_profile.py` converts tuya-local device YAML descriptors into the
base node's compact generic profile JSON.

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r tools/requirements.txt

python3 tools/tuya_local_to_profile.py \
  --source /path/to/tuya-local/custom_components/tuya_local/devices \
  --out profiles
```

The converter skips descriptors that use unsupported entity types, DP types,
mapping features, or that exceed the firmware limits. It writes:

- `profiles/<id>.json` — one complete custom-profile document per device;
- `profiles/pack.jsonl` — one JSON profile object per line, for a future
  SD/SPIFFS profile-pack loader;
- `profiles/INDEX.md` — generated profile index;
- `profiles/README.md` — coverage report and skip reasons.

The base firmware stores at most four custom profiles in NVS. Paste one
generated profile into the Profile tab, save with `auto`, reboot, then select the
profile and save again.

The source descriptors are from
[`make-all/tuya-local`](https://github.com/make-all/tuya-local) and are used
under the MIT License; see `profiles/LICENSE.tuya-local`.
