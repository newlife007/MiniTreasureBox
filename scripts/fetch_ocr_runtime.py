#!/usr/bin/env python3
"""Download locked OCR packages and verify SHA-256 before extraction."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
from urllib.parse import urldefrag, urlsplit
from urllib.request import Request, urlopen


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--lock-file", required=True, type=Path)
    parser.add_argument("--sha256-manifest", required=True, type=Path)
    parser.add_argument("--output-cache", required=True, type=Path)
    parser.add_argument("--output-lock", required=True, type=Path)
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    if arguments.output_cache.exists():
        raise SystemExit(f"Verified package output already exists: {arguments.output_cache}")
    if arguments.output_lock.exists():
        raise SystemExit(f"Local OCR lock output already exists: {arguments.output_lock}")

    lines = [
        line.strip()
        for line in arguments.lock_file.read_text(encoding="utf-8").splitlines()
    ]
    if not lines or lines[0] != "@EXPLICIT":
        raise SystemExit("OCR package lock must use the @EXPLICIT format")
    entries = [line for line in lines[1:] if line and not line.startswith("#")]
    manifest = json.loads(arguments.sha256_manifest.read_text(encoding="utf-8"))
    expected_urls = {urldefrag(entry)[0] for entry in entries}
    missing = sorted(expected_urls - set(manifest))
    if missing:
        raise SystemExit(
            f"SHA-256 manifest is missing locked packages: {missing}"
        )

    arguments.output_cache.mkdir(parents=True)
    local_entries: list[str] = []
    for entry in entries:
        url, md5_fragment = urldefrag(entry)
        if urlsplit(url).scheme != "https":
            raise SystemExit(f"Locked OCR package URL must use HTTPS: {url}")
        if len(md5_fragment) != 32:
            raise SystemExit(f"Locked OCR package is missing its package-manager checksum: {url}")
        expected_sha256 = str(manifest[url]).lower()
        if len(expected_sha256) != 64:
            raise SystemExit(f"Invalid SHA-256 digest for locked package: {url}")
        try:
            int(expected_sha256, 16)
        except ValueError as error:
            raise SystemExit(f"Invalid SHA-256 digest for locked package: {url}") from error

        filename = Path(urlsplit(url).path).name
        if not filename or filename != Path(filename).name:
            raise SystemExit(f"Locked OCR package URL has an invalid filename: {url}")
        destination = arguments.output_cache / filename
        temporary = destination.with_suffix(destination.suffix + ".part")
        digest = hashlib.sha256()
        request = Request(url, headers={"User-Agent": "MiniTreasureBox-release-builder"})
        try:
            with urlopen(request, timeout=120) as response, temporary.open("xb") as output:
                for block in iter(lambda: response.read(1024 * 1024), b""):
                    digest.update(block)
                    output.write(block)
            actual_sha256 = digest.hexdigest()
            if actual_sha256 != expected_sha256:
                raise SystemExit(
                    f"SHA-256 mismatch for {filename}: expected {expected_sha256}, "
                    f"received {actual_sha256}"
                )
            os.replace(temporary, destination)
        finally:
            temporary.unlink(missing_ok=True)
        local_entries.append(f"{destination.resolve().as_uri()}#{md5_fragment}")
        print(f"Verified {filename}: {expected_sha256}")

    arguments.output_lock.write_text(
        "@EXPLICIT\n" + "\n".join(local_entries) + "\n", encoding="utf-8"
    )
    print(f"Wrote verified local OCR lock: {arguments.output_lock}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
