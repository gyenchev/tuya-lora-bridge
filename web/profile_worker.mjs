// Builds the tuya-local catalog inside a Web Worker.
//
// The main thread supplies the filtered device file list and a resolved commit
// SHA. This worker downloads each YAML, converts it with the shared converter,
// and reports progress. The compact result is cached by the page in IndexedDB.

import {
  classifyGroup,
  displayName,
  supportInfo,
} from "./profile_converter.mjs";
import {
  DEFAULT_SCHEMA,
  load,
} from "https://cdn.jsdelivr.net/npm/js-yaml@4/dist/js-yaml.mjs";

const FETCH_TIMEOUT_MS = 20000;
const DEFAULT_CONCURRENCY = 24;

async function fetchText(url) {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), FETCH_TIMEOUT_MS);
  try {
    const response = await fetch(url, {
      cache: "force-cache",
      signal: controller.signal,
    });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return await response.text();
  } finally {
    clearTimeout(timer);
  }
}

async function fetchYaml(repo, commit, path) {
  const errors = [];
  const urls = [
    `https://cdn.jsdelivr.net/gh/${repo}@${commit}/${path}`,
    `https://raw.githubusercontent.com/${repo}/${commit}/${path}`,
  ];
  for (const url of urls) {
    try {
      return await fetchText(url);
    } catch (error) {
      errors.push(error.message);
    }
  }
  throw new Error(errors.join("; "));
}

function prettyFile(file) {
  return file
    .replace(/\.yaml$/i, "")
    .replace(/[_-]+/g, " ")
    .replace(/\b\w/g, (letter) => letter.toUpperCase());
}

async function buildCatalog({ repo, commit, files, concurrency }) {
  const total = files.length;
  const results = new Array(total);
  const workers = Math.max(
    1,
    Math.min(concurrency || DEFAULT_CONCURRENCY, total || 1),
  );
  let next = 0;
  let done = 0;

  async function consume() {
    while (true) {
      const index = next;
      next += 1;
      if (index >= total) return;

      const item = files[index];
      const file = item.file || item.path.split("/").pop();
      const source = `tuya-local ${commit.slice(0, 12)}/${file}`;
      try {
        const text = await fetchYaml(repo, commit, item.path);
        const yaml = load(text, { schema: DEFAULT_SCHEMA });
        const name = displayName(yaml);
        const support = supportInfo(yaml, {
          id: item.id,
          name,
          file,
          source,
        });
        results[index] = {
          id: item.id,
          name,
          group: support.group || classifyGroup(yaml, file),
          file,
          path: item.path,
          supported: Boolean(support.supported),
          discovery: support.discovery || 0,
          bytes: support.bytes || 0,
          reason: support.reason || "",
          source,
        };
      } catch (error) {
        results[index] = {
          id: item.id,
          name: prettyFile(file),
          group: "Unknown",
          file,
          path: item.path,
          supported: false,
          discovery: 0,
          bytes: 0,
          reason: error.message,
          source,
        };
      }

      done += 1;
      if (done === total || done % 10 === 0) {
        postMessage({ type: "progress", done, total, file });
      }
    }
  }

  await Promise.all(Array.from({ length: workers }, consume));
  postMessage({ type: "done", profiles: results });
}

self.addEventListener("message", (event) => {
  const data = event.data || {};
  if (data.type !== "build") return;
  buildCatalog(data).catch((error) => {
    postMessage({ type: "error", error: error.message });
  });
});
