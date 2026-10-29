import { ESPLoader, Transport } from './vendor/esptool-js.bundle.js';

const FULL_IMAGE_BYTES = 4 * 1024 * 1024;

const boardEl = document.getElementById('board');
const latestEl = document.getElementById('latest');
const releaseEl = document.getElementById('release');
const versionEl = document.getElementById('version');
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
let manifest = null;

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
  versionEl.disabled = value || !manifest;
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

async function loadManifest() {
  const response = await fetch('./firmware/manifest.json', { cache: 'no-store' });
  if (!response.ok) throw new Error(`manifest HTTP ${response.status}`);
  const data = await response.json();
  const versions = Array.isArray(data.versions) ? data.versions : [];
  if (versions.length === 0) throw new Error('manifest has no firmware versions');

  manifest = data;
  versionEl.innerHTML = '';
  for (const version of versions) {
    const option = document.createElement('option');
    option.value = version.tag;
    option.textContent = version.tag + (version.name ? ` — ${version.name}` : '');
    versionEl.appendChild(option);
  }
  versionEl.value = versions[0].tag;
  versionEl.disabled = false;
  latestEl.disabled = false;
  releaseEl.textContent = `${versions.length} version${versions.length === 1 ? '' : 's'} available`;
  return versions[0];
}

async function fetchStaticFirmware(board, versionTag) {
  if (!manifest) await loadManifest();
  const version = manifest.versions.find((item) => item.tag === versionTag);
  if (!version) throw new Error(`version ${versionTag} is not available`);
  const name = version.assets?.[board];
  if (!name) throw new Error(`no ${board} image for ${versionTag}`);

  const response = await fetch(`./firmware/${name}`, { cache: 'no-store' });
  if (!response.ok) throw new Error(`bundled image HTTP ${response.status}`);
  return {
    data: new Uint8Array(await response.arrayBuffer()),
    release: version.tag,
    name: version.name || version.tag,
    sha: version.sha256?.[board] || '',
  };
}

async function loadFirmware() {
  const board = boardEl.value;
  const versionTag = versionEl.value;
  try {
    setBusy(true);
    clearLog();
    releaseEl.textContent = 'Fetching selected firmware...';
    log(`Loading ${board} full image for ${versionTag} from GitHub Pages...`);

    const result = await fetchStaticFirmware(board, versionTag);
    if (result.data.byteLength !== FULL_IMAGE_BYTES) {
      throw new Error(`expected a 4 MiB full image, got ${formatBytes(result.data.byteLength)}`);
    }

    setFirmware(result.data, `${result.release} ${board} full image`);
    releaseEl.textContent = `${result.release}${result.sha ? ` · sha256 ${result.sha.slice(0, 12)}…` : ''}`;
    log(`Loaded ${result.release} ${board} full image (${formatBytes(result.data.byteLength)}).`);
  } catch (error) {
    releaseEl.textContent = 'Firmware unavailable';
    setFirmware(null, '');
    log(`Firmware load failed: ${error.message}`);
    log('Open the flasher from https://gyenchev.github.io/tuya-lora-bridge/ so the bundled firmware is available.');
  } finally {
    setBusy(false);
  }
}

async function init() {
  if (!navigator.serial) {
    unsupportedEl.hidden = false;
    connectEl.disabled = true;
    return;
  }
  try {
    await loadManifest();
  } catch (error) {
    releaseEl.textContent = 'Could not load release list';
    versionEl.innerHTML = '<option>Unavailable</option>';
    log(`Release list failed: ${error.message}`);
    log('Open the flasher from https://gyenchev.github.io/tuya-lora-bridge/.');
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

latestEl.addEventListener('click', loadFirmware);
connectEl.addEventListener('click', connect);
disconnectEl.addEventListener('click', disconnect);
flashEl.addEventListener('click', flash);
confirmEl.addEventListener('change', updateFlashButton);
boardEl.addEventListener('change', () => {
  setFirmware(null, '');
  releaseEl.textContent = 'No firmware loaded';
});
versionEl.addEventListener('change', () => {
  setFirmware(null, '');
  releaseEl.textContent = versionEl.value || 'No firmware loaded';
});

latestEl.disabled = true;
updateFlashButton();
init();
