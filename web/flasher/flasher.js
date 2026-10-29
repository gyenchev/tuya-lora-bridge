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

async function fetchStaticFirmware(board) {
  const manifestResponse = await fetch('./firmware/manifest.json', { cache: 'no-store' });
  if (!manifestResponse.ok) throw new Error(`no bundled manifest (HTTP ${manifestResponse.status})`);
  const manifest = await manifestResponse.json();
  const name = manifest.assets?.[board] ||
    (board === 'base' ? 'base-node-full.bin' : 'remote-node-full.bin');
  const response = await fetch(`./firmware/${encodeURIComponent(name)}`, { cache: 'no-store' });
  if (!response.ok) throw new Error(`bundled image HTTP ${response.status}`);
  return {
    data: new Uint8Array(await response.arrayBuffer()),
    release: manifest.tag || 'bundled build',
    sha: manifest.sha256?.[board] || '',
  };
}

async function fetchServerFirmware(board) {
  const response = await fetch(`/api/firmware?board=${encodeURIComponent(board)}`, {
    cache: 'no-store',
  });
  if (!response.ok) {
    throw new Error(`local server HTTP ${response.status}: ${await response.text()}`);
  }
  return {
    data: new Uint8Array(await response.arrayBuffer()),
    release: response.headers.get('X-Firmware-Release') || 'latest release',
    sha: response.headers.get('X-Firmware-SHA256') || '',
  };
}

async function loadLatest() {
  const board = boardEl.value;
  try {
    setBusy(true);
    clearLog();
    releaseEl.textContent = 'Fetching firmware...';

    let result = null;
    try {
      log('Trying firmware bundled with the hosted flasher...');
      result = await fetchStaticFirmware(board);
    } catch (error) {
      log(`Bundled firmware unavailable: ${error.message}`);
    }

    if (!result) {
      log('Trying the local flasher server...');
      result = await fetchServerFirmware(board);
    }

    if (result.data.byteLength !== FULL_IMAGE_BYTES) {
      throw new Error(`expected a 4 MiB full image, got ${formatBytes(result.data.byteLength)}`);
    }

    setFirmware(result.data, `${board} full image`);
    releaseEl.textContent = `${result.release}${result.sha ? ` · sha256 ${result.sha.slice(0, 12)}…` : ''}`;
    log(`Loaded ${board} full image (${formatBytes(result.data.byteLength)}) from ${result.release}.`);
  } catch (error) {
    releaseEl.textContent = 'Could not load firmware';
    setFirmware(null, '');
    log(`Firmware load failed: ${error.message}`);
    log('Choose a local full .bin file, or run the local flasher helper with "npm run flasher".');
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
