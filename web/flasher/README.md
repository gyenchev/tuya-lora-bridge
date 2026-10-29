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

## Hosted use

Open **<https://gyenchev.github.io/tuya-lora-bridge/>** in desktop Chrome or
Edge. The GitHub Pages deployment bundles the firmware from the latest `main`
build plus a manifest, so the page does not need a local helper server.

Web Serial requires a secure context; GitHub Pages serves the page over HTTPS.

## Flash flow

1. Select **Base node** or **Remote node**.
2. Click **Load bundled firmware**.
3. Click **Connect USB** and select the board's serial port.
4. Tick the confirmation checkbox.
5. Click **Flash firmware**.

The board is reset after a successful flash.

## Important

This tool writes a **full 4 MB image at address `0x0`** with `eraseAll: true`.
It erases NVS and LittleFS, including WiFi, MQTT, Home Assistant and profile
settings. Use the SD-card or `/ota` update path when you want to preserve the
current configuration.

Only full images are bundled:

```text
base-node-full.bin
remote-node-full.bin
```

The smaller `base-node.bin` and `remote-node.bin` files are application images
for SD/OTA updates.

If the serial connection fails, hold **BOOT**, tap **RESET**, release **BOOT**,
and try again.

## Deployment

`.github/workflows/firmware.yml` builds both sketches on `main`, copies the full
images and manifest into the flasher site, and deploys it to GitHub Pages.

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
