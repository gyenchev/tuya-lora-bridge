import { ESPLoader, Transport } from './vendor/esptool-js.bundle.js';

const FULL_IMAGE_BYTES = 4 * 1024 * 1024;

const boardEl = document.getElementById('board');
const latestEl = document.getElementById('latest');
const releaseEl = document.getElementById('release');
const fileEl = document.getElementById('file');
const firmwareEl = document.getElementById('firmware');
const connectEl = document.getElementById('connect');
const disconnectEl = document.getElementById('disconnect');
const chipEl = document.getElementById('chip');
const confirmEl = document.getElementById('confirm');
const flashEl = document.getElementById('flash');
const progressEl = document.getElementById('progress');
const logEl = document.getElementById('log');
const unsupportedEl = document.getElementById('unsupported');

let firmware = null;
let firmwareLabel = '';
let port = null;
let transport = null;
let loader = null;
let busy = false;

function log(text) {
  logEl.textContent += (logEl.textContent && !logEl.textContent.endsWith('\n') ? '\n' : '') + text;
  logEl.scrollTop = logEl.scrollHeight;
}

function clearLog() {
  logEl.textContent = '';
}

function setBusy(value) {
  busy = value;
  boardEl.disabled = value;
  latestEl.disabled = value;
  fileEl.disabled = value;
  connectEl.disabled = value || !!loader;
  disconnectEl.disabled = value || !loader;
  updateFlashButton();
}

function updateFlashButton() {
  flashEl.disabled = busy || !loader || !firmware || !confirmEl.checked;
}

function setFirmware(data, label) {
  firmware = data;
  firmwareLabel = label;
  if (!data) {
    firmwareEl.textContent = 'No firmware loaded.';
  } else {
    firmwareEl.textContent = `${label} · ${data.byteLength.toLocaleString()} bytes`;
  }
  updateFlashButton();
}

function formatBytes(value) {
  return `${(value / (1024 * 1024)).toFixed(2)} MiB`;
}

async function loadLatest() {
  try {
    setBusy(true);
    clearLog();
    releaseEl.textContent = 'Fetching latest release...';
    log(`Fetching latest ${boardEl.value} full image from the local flasher server...`);
    const response = await fetch(`/api/firmware?board=${encodeURIComponent(boardEl.value)}`, {
      cache: 'no-store',
    });
    if (!response.ok) {
      throw new Error(`server returned HTTP ${response.status}: ${await response.text()}`);
    }
    const data = new Uint8Array(await response.arrayBuffer());
    if (data.byteLength !== FULL_IMAGE_BYTES) {
      throw new Error(`expected a 4 MiB full image, got ${formatBytes(data.byteLength)}`);
    }
    const release = response.headers.get('X-Firmware-Release') || 'latest release';
    const sha = response.headers.get('X-Firmware-SHA256') || '';
    setFirmware(data, `${boardEl.value} full image`);
    releaseEl.textContent = `${release}${sha ? ` · sha256 ${sha.slice(0, 12)}…` : ''}`;
    log(`Loaded ${boardEl.value} full image (${formatBytes(data.byteLength)}) from ${release}.`);
  } catch (error) {
    releaseEl.textContent = 'Could not load latest release';
    setFirmware(null, '');
    log(`Latest release failed: ${error.message}`);
    log('Start the flasher with "npm run flasher" or choose a local full .bin file.');
  } finally {
    setBusy(false);
  }
}

async function loadFile() {
  try {
    const file = fileEl.files && fileEl.files[0];
    if (!file) return;
    const data = new Uint8Array(await file.arrayBuffer());
    if (data.byteLength !== FULL_IMAGE_BYTES) {
      setFirmware(null, '');
      releaseEl.textContent = 'Local file rejected';
      log(`Rejected ${file.name}: expected a 4 MiB full image, got ${formatBytes(data.byteLength)}.`);
      return;
    }
    setFirmware(data, file.name);
    releaseEl.textContent = 'Local file';
    log(`Loaded local image ${file.name} (${formatBytes(data.byteLength)}).`);
  } catch (error) {
    setFirmware(null, '');
    log(`Could not read file: ${error.message}`);
  }
}

async function connect() {
  if (!navigator.serial) {
    log('Web Serial is not available in this browser.');
    return;
  }
  clearLog();
  try {
    setBusy(true);
    log('Requesting USB serial port...');
    port = await navigator.serial.requestPort();
    transport = new Transport(port, true);
    loader = new ESPLoader({
      transport,
      baudrate: 460800,
      terminal: {
        clean() {},
        writeLine(data) { log(data); },
        write(data) { logEl.textContent += data; logEl.scrollTop = logEl.scrollHeight; },
      },
    });
    log('Connecting to the ESP32...');
    const chip = await loader.main();
    chipEl.textContent = chip || 'ESP32 connected';
    log(`Connected: ${chip || 'ESP32'}`);
  } catch (error) {
    log(`Connect failed: ${error.message}`);
    log('If the board does not enter the bootloader, hold BOOT, tap RESET, then release BOOT.');
    await disconnect();
  } finally {
    setBusy(false);
  }
}

async function disconnect() {
  try {
    if (transport) await transport.disconnect();
  } catch (_) {
    // The port may already be closed after a reset.
  }
  transport = null;
  loader = null;
  port = null;
  chipEl.textContent = 'Not connected';
  updateFlashButton();
}

async function flash() {
  if (!loader || !firmware) return;
  if (!confirmEl.checked) {
    log('Tick the confirmation checkbox first.');
    return;
  }
  let lastPercent = -1;
  try {
    setBusy(true);
    clearLog();
    log(`Writing ${firmwareLabel} at 0x0 (full flash, settings will be erased)...`);
    await loader.writeFlash({
      fileArray: [{ data: firmware, address: 0x0 }],
      flashMode: 'dio',
      flashFreq: '40m',
      flashSize: '4MB',
      eraseAll: true,
      compress: true,
      reportProgress: (_fileIndex, written, total) => {
        const percent = total ? Math.round((written * 100) / total) : 0;
        progressEl.value = percent;
        if (percent !== lastPercent && (percent % 5 === 0 || percent === 100)) {
          lastPercent = percent;
          log(`Writing... ${percent}%`);
        }
      },
    });
    log('Flash complete. Resetting the board...');
    await loader.after('hard_reset');
    await disconnect();
    log('Done. The board has been reset.');
  } catch (error) {
    log(`Flash failed: ${error.message}`);
    await disconnect();
  } finally {
    setBusy(false);
  }
}

if (!navigator.serial) {
  unsupportedEl.hidden = false;
  connectEl.disabled = true;
}

latestEl.addEventListener('click', loadLatest);
fileEl.addEventListener('change', loadFile);
connectEl.addEventListener('click', connect);
disconnectEl.addEventListener('click', disconnect);
flashEl.addEventListener('click', flash);
confirmEl.addEventListener('change', updateFlashButton);
boardEl.addEventListener('change', () => {
  setFirmware(null, '');
  releaseEl.textContent = 'No release loaded';
});

updateFlashButton();
