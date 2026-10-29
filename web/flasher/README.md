# Web Serial flasher

A browser flasher for the ESP32 base and remote nodes. It uses the
[Web Serial API](https://developer.mozilla.org/en-US/docs/Web/API/Web_Serial_API)
and [esptool-js](https://github.com/espressif/esptool-js) to write a full 4 MB
image over USB.

**Hosted:** <https://gyenchev.github.io/tuya-lora-bridge/>  
The GitHub Pages build bundles the latest `main` full images and a manifest, so
the hosted page works without a local server.

The same page can also run locally for offline use or when you want to fetch the
latest GitHub release instead.

## Requirements

- Desktop Chrome or Edge (Firefox and Safari do not support Web Serial).
- A data-capable USB cable.
- Node.js 18 or newer to run the local helper server.
- The flasher must be opened from `http://localhost`, `http://127.0.0.1`, or an
  HTTPS origin. Web Serial is not available on a plain LAN `http://` address.

## Run locally

```bash
npm run flasher
```

Then open <http://localhost:8787/>.

The page first tries firmware bundled next to it under `firmware/`. This is how
the GitHub Pages deployment works.

The local helper server:

- serves the flasher page and the vendored esptool-js bundle;
- fetches the latest GitHub release metadata server-side, avoiding browser CORS
  restrictions;
- proxies `base-node-full.bin` and `remote-node-full.bin` from that release.

You can also choose a local full image. Images downloaded manually from a
GitHub release work without the helper server.

## Flash flow

1. Select **Base node** or **Remote node**.
2. Click **Load latest release**, or choose a local `*-full.bin`.
3. Click **Connect USB** and select the board's serial port.
4. Tick the confirmation checkbox.
5. Click **Flash firmware**.

The board is reset after a successful flash.

## Important

This tool writes a **full 4 MB image at address `0x0`** with `eraseAll: true`.
It erases NVS and LittleFS, including WiFi, MQTT, Home Assistant and profile
settings. Use the SD-card or `/ota` update path when you want to preserve the
current configuration.

Only full images are accepted:

```text
base-node-full.bin
remote-node-full.bin
```

The smaller `base-node.bin` and `remote-node.bin` files are application images
for SD/OTA updates.

If the serial connection fails, hold **BOOT**, tap **RESET**, release **BOOT**,
and try again.

## Hosting

The page is static and is deployed to GitHub Pages by
`.github/workflows/firmware.yml`. The Pages job copies the full images built from
the same commit and writes `firmware/manifest.json`.

For another HTTPS host, copy `index.html`, `flasher.js`, `vendor/`, and a
`firmware/` directory containing the two full images plus `manifest.json`. The
local file input works even without a firmware directory.
