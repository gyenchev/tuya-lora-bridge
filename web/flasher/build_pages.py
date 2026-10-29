#!/usr/bin/env python3
"""Assemble the firmware version directories and manifest for GitHub Pages."""

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def gh_json(*args: str):
    result = subprocess.run(
        ["gh", *args],
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    return json.loads(result.stdout)


def download_release(repo: str, tag: str, target: Path) -> bool:
    target.mkdir(parents=True, exist_ok=True)
    result = subprocess.run(
        [
            "gh",
            "release",
            "download",
            tag,
            "--repo",
            repo,
            "--pattern",
            "*-full.bin",
            "--dir",
            str(target),
            "--clobber",
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if result.returncode != 0:
        print(f"Skipping {tag}: {result.stderr.strip()}", file=sys.stderr)
        return False
    return True


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--site", required=True)
    parser.add_argument("--dist", required=True)
    parser.add_argument("--current-tag", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--built", required=True)
    parser.add_argument("--limit", type=int, default=10)
    args = parser.parse_args()

    repo = os.environ.get("GITHUB_REPOSITORY", "")
    if not repo:
        raise SystemExit("GITHUB_REPOSITORY is required")

    site = Path(args.site)
    firmware_root = site / "firmware"
    firmware_root.mkdir(parents=True, exist_ok=True)

    current_dir = firmware_root / args.current_tag
    current_dir.mkdir(parents=True, exist_ok=True)
    for filename in ("base-node-full.bin", "remote-node-full.bin", "README.txt", "SHA256SUMS.txt"):
        source = Path(args.dist) / filename
        if source.exists():
            shutil.copy2(source, current_dir / filename)

    versions = []

    def add_entry(tag: str, name: str, published_at: str, directory: Path):
        base = directory / "base-node-full.bin"
        remote = directory / "remote-node-full.bin"
        if not base.exists() or not remote.exists():
            print(f"Skipping {tag}: full images are missing", file=sys.stderr)
            return False
        versions.append(
            {
                "tag": tag,
                "name": name,
                "published_at": published_at,
                "assets": {
                    "base": f"{tag}/base-node-full.bin",
                    "remote": f"{tag}/remote-node-full.bin",
                },
                "sha256": {
                    "base": sha256(base),
                    "remote": sha256(remote),
                },
            }
        )
        return True

    add_entry(
        args.current_tag,
        f"Tuya LoRa Bridge {args.version}",
        args.built,
        current_dir,
    )

    releases = gh_json(
        "release",
        "list",
        "--repo",
        repo,
        "--limit",
        str(max(args.limit, 10)),
        "--json",
        "tagName,name,publishedAt,isDraft,isPrerelease",
    )

    for release in releases:
        if len(versions) >= args.limit:
            break
        tag = release["tagName"]
        if tag == args.current_tag or release.get("isDraft"):
            continue
        target = firmware_root / tag
        if not download_release(repo, tag, target):
            continue
        add_entry(tag, release.get("name") or tag, release.get("publishedAt") or "", target)

    manifest = {
        "generated": args.built,
        "current": args.current_tag,
        "versions": versions,
    }
    (firmware_root / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps(manifest, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
