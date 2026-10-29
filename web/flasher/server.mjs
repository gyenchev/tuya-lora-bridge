import { createHash } from 'node:crypto';
import { readFile } from 'node:fs/promises';
import { createServer } from 'node:http';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const REPO = 'gyenchev/tuya-lora-bridge';
const ASSET_NAMES = {
  base: 'base-node-full.bin',
  remote: 'remote-node-full.bin',
};
const PORT = Number(process.env.PORT || 8787);
const HOST = process.env.HOST || '127.0.0.1';
const RELEASE_CACHE_MS = 5 * 60 * 1000;

let releaseCache = { at: 0, release: null };

function send(res, status, body, headers = {}) {
  res.writeHead(status, {
    'Content-Type': 'text/plain; charset=utf-8',
    'Cache-Control': 'no-store',
    ...headers,
  });
  res.end(body);
}

async function latestRelease() {
  const now = Date.now();
  if (releaseCache.release && now - releaseCache.at < RELEASE_CACHE_MS) {
    return releaseCache.release;
  }
  const response = await fetch(`https://api.github.com/repos/${REPO}/releases/latest`, {
    headers: {
      Accept: 'application/vnd.github+json',
      'User-Agent': 'tuya-lora-webserial-flasher',
    },
  });
  if (!response.ok) {
    throw new Error(`GitHub release API returned HTTP ${response.status}`);
  }
  const release = await response.json();
  releaseCache = { at: now, release };
  return release;
}

async function firmwareAsset(board) {
  const name = ASSET_NAMES[board];
  if (!name) throw new Error('unknown board');
  const release = await latestRelease();
  const asset = (release.assets || []).find((item) => item.name === name);
  if (!asset) throw new Error(`${name} is not present in ${release.tag_name}`);
  return { asset, release };
}

async function serveFirmware(res, board) {
  const { asset, release } = await firmwareAsset(board);
  const response = await fetch(asset.browser_download_url, {
    headers: { 'User-Agent': 'tuya-lora-webserial-flasher' },
  });
  if (!response.ok) {
    throw new Error(`download returned HTTP ${response.status}`);
  }
  const bytes = Buffer.from(await response.arrayBuffer());
  const sha256 = createHash('sha256').update(bytes).digest('hex');
  res.writeHead(200, {
    'Content-Type': 'application/octet-stream',
    'Content-Length': bytes.length,
    'Cache-Control': 'no-store',
    'X-Firmware-Release': release.tag_name,
    'X-Firmware-SHA256': sha256,
  });
  res.end(bytes);
}

const staticFiles = new Map([
  ['/', ['index.html', 'text/html; charset=utf-8']],
  ['/index.html', ['index.html', 'text/html; charset=utf-8']],
  ['/flasher.js', ['flasher.js', 'text/javascript; charset=utf-8']],
  ['/vendor/esptool-js.bundle.js', ['vendor/esptool-js.bundle.js', 'text/javascript; charset=utf-8']],
]);

const server = createServer(async (req, res) => {
  try {
    const url = new URL(req.url || '/', `http://${req.headers.host || 'localhost'}`);

    if (req.method === 'GET' && staticFiles.has(url.pathname)) {
      const [file, type] = staticFiles.get(url.pathname);
      const body = await readFile(path.join(__dirname, file));
      res.writeHead(200, {
        'Content-Type': type,
        'Cache-Control': 'no-store',
      });
      res.end(body);
      return;
    }

    if (req.method === 'GET' && url.pathname === '/api/release') {
      const release = await latestRelease();
      const assets = (release.assets || [])
        .filter((asset) => Object.values(ASSET_NAMES).includes(asset.name))
        .map((asset) => ({ name: asset.name, size: asset.size }));
      res.writeHead(200, {
        'Content-Type': 'application/json; charset=utf-8',
        'Cache-Control': 'no-store',
      });
      res.end(JSON.stringify({ tag: release.tag_name, name: release.name, assets }));
      return;
    }

    if (req.method === 'GET' && url.pathname === '/api/firmware') {
      await serveFirmware(res, url.searchParams.get('board') || '');
      return;
    }

    if (req.method === 'GET' && url.pathname === '/favicon.ico') {
      res.writeHead(204);
      res.end();
      return;
    }

    send(res, 404, 'Not found\n');
  } catch (error) {
    console.error(error);
    send(res, 502, `Flasher server error: ${error.message}\n`);
  }
});

server.listen(PORT, HOST, () => {
  console.log(`Web Serial flasher: http://localhost:${PORT}/`);
  console.log('Use desktop Chrome or Edge; Web Serial needs a secure context (localhost is fine).');
  console.log('Press Ctrl+C to stop.');
});
