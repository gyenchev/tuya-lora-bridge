# Web Serial flasher

A hosted browser flasher for the ESP32 base and remote nodes. It uses the
[Web Serial API](https://developer.mozilla.org/en-US/docs/Web/API/Web_Serial_API)
and [esptool-js](https://github.com/espressif/esptool-js) to write a full 4 MB
image over USB.

**Open:** <https://gyenchev.github.io/tuya-lora-bridge/>

The GitHub Pages build bundles the full images from the latest `main` build and
a manifest.

## Requirements

- Desktop Chrome or Edge. Firefox and Safari do not support Web Serial.
- A data-capable USB cable.
- HTTPS or a secure context; the GitHub Pages URL is HTTPS.

## Flash flow

1. Select **Base node** or **Remote node**.
2. Choose a release from the latest 10 releases.
3. Click **Load selected firmware**.
4. Click **Connect USB** and select the board's serial port.
5. Tick the confirmation checkbox.
6. Click **Flash firmware**.

The board is reset after a successful flash.

## Important

This tool writes a **full 4 MB image at address `0x0`** with `eraseAll: true`.
It erases NVS and LittleFS, including WiFi, MQTT, Home Assistant and profile
settings. Use the SD-card or `/ota` update path when you want to preserve the
current configuration.

The hosted flasher bundles only full images:

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

For another HTTPS host, copy `index.html`, `flasher.js`, `vendor/`, and a
`firmware/` directory containing the two full images plus `manifest.json`.
