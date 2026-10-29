# Web Serial flasher

A local browser flasher for the ESP32 base and remote nodes. It uses the
[Web Serial API](https://developer.mozilla.org/en-US/docs/Web/API/Web_Serial_API)
and [esptool-js](https://github.com/espressif/esptool-js) to write a full 4 MB
image over USB.

## Requirements

- Desktop Chrome or Edge (Firefox and Safari do not support Web Serial).
- A data-capable USB cable.
- Node.js 18 or newer to run the local helper server.
- The flasher must be opened from `http://localhost`, `http://127.0.0.1`, or an
  HTTPS origin. Web Serial is not available on a plain LAN `http://` address.

## Run

```bash
npm run flasher
```

Then open <http://localhost:8787/>.

The helper server:

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

The page is static. It can be hosted on an HTTPS origin, but the
**Load latest release** button requires the local helper server. Without it, use
the local file input.
