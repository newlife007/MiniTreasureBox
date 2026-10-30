#!/usr/bin/env python3
"""Stage and validate the pinned conda-forge Tesseract runtime."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import sys
from pathlib import Path
from urllib.parse import urldefrag, urlsplit


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--environment", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--package-cache", required=True, type=Path)
    parser.add_argument("--lock-file", required=True, type=Path)
    parser.add_argument("--sha256-manifest", required=True, type=Path)
    parser.add_argument("--platform", choices=("windows", "macos"))
    return parser.parse_args()


def first_existing(root: Path, relative_paths: tuple[str, ...]) -> Path | None:
    for relative_path in relative_paths:
        candidate = root / relative_path
        if candidate.is_file():
            return candidate
    return None


def package_source(meta: dict[str, object], package_cache: Path) -> Path | None:
    extracted = meta.get("extracted_package_dir")
    if isinstance(extracted, str):
        extracted_path = Path(extracted)
        if extracted_path.is_dir():
            return extracted_path
    candidate = package_cache / f"{meta['name']}-{meta['version']}-{meta['build']}"
    return candidate if candidate.is_dir() else None


def normalized_url(value: object) -> str:
    return urldefrag(str(value))[0]


def locked_urls(lock_file: Path) -> list[str]:
    if not lock_file.is_file():
        raise SystemExit(f"OCR lock file does not exist: {lock_file}")
    lines = [line.strip() for line in lock_file.read_text(encoding="utf-8").splitlines()]
    if not lines or lines[0] != "@EXPLICIT":
        raise SystemExit(f"OCR lock file must use the @EXPLICIT format: {lock_file}")
    urls = [normalized_url(line) for line in lines[1:] if line and not line.startswith("#")]
    if not urls or any(not url.startswith("https://") for url in urls):
        raise SystemExit(f"OCR lock file contains an invalid package URL: {lock_file}")
    if len(urls) != len(set(urls)):
        raise SystemExit(f"OCR lock file contains duplicate package URLs: {lock_file}")
    return urls


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as package:
        for block in iter(lambda: package.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def verify_package_archives(
    metadata: list[dict[str, object]], package_cache: Path, lock_file: Path,
    sha256_manifest: Path,
) -> dict[str, tuple[str, str]]:
    if not sha256_manifest.is_file():
        raise SystemExit(f"OCR SHA-256 manifest does not exist: {sha256_manifest}")
    manifest = json.loads(sha256_manifest.read_text(encoding="utf-8"))
    if not isinstance(manifest, dict):
        raise SystemExit("OCR SHA-256 manifest must contain a URL-to-digest object")

    urls = locked_urls(lock_file)
    expected_filenames = {Path(urlsplit(url).path).name: url for url in urls}
    metadata_filenames = {str(meta.get("fn", "")) for meta in metadata}
    if metadata_filenames != set(expected_filenames):
        missing = sorted(set(expected_filenames) - metadata_filenames)
        unexpected = sorted(metadata_filenames - set(expected_filenames))
        raise SystemExit(
            "OCR environment does not exactly match its lock file; "
            f"missing={missing}, unexpected={unexpected}"
        )

    verified: dict[str, tuple[str, str]] = {}
    for filename, url in sorted(expected_filenames.items()):
        expected = manifest.get(url)
        if not isinstance(expected, str) or len(expected) != 64:
            raise SystemExit(f"Missing valid SHA-256 digest for locked package: {url}")
        try:
            int(expected, 16)
        except ValueError as error:
            raise SystemExit(f"Invalid SHA-256 digest for locked package: {url}") from error
        archive = package_cache / filename
        if not archive.is_file():
            raise SystemExit(f"Locked package archive is unavailable for SHA-256 verification: {archive}")
        actual = sha256_file(archive)
        if actual.lower() != expected.lower():
            raise SystemExit(
                f"SHA-256 mismatch for {filename}: expected {expected}, received {actual}"
            )
        verified[filename] = (url, actual.lower())
    return verified


def copy_file(source: Path, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    if source.is_symlink():
        destination.symlink_to(os.readlink(source))
    else:
        shutil.copy2(source, destination)


def copy_runtime(environment: Path, output: Path, platform: str) -> tuple[Path, int]:
    if platform == "windows":
        executable = first_existing(
            environment, ("Library/bin/tesseract.exe", "bin/tesseract.exe")
        )
        library_paths = sorted(
            set((environment / "Library/bin").glob("*.dll"))
            | set((environment / "bin").glob("*.dll"))
        )
    elif platform == "macos":
        executable = first_existing(environment, ("bin/tesseract",))
        library_paths = sorted((environment / "lib").rglob("*.dylib"))
    else:
        raise SystemExit("Use --platform windows or --platform macos for cross-platform validation")

    if executable is None:
        raise SystemExit("Tesseract executable is missing from the OCR environment")
    if not library_paths:
        raise SystemExit("Tesseract transitive runtime libraries are missing")

    copy_file(executable, output / executable.relative_to(environment))
    for library in library_paths:
        copy_file(library, output / library.relative_to(environment))

    tessdata = environment / "share/tessdata"
    for language in ("eng", "chi_sim"):
        model = tessdata / f"{language}.traineddata"
        if not model.is_file():
            raise SystemExit(f"Required OCR language data is missing: {model}")
        copy_file(model, output / model.relative_to(environment))
    return output / executable.relative_to(environment), len(library_paths)


def collect_licenses(
    metadata: list[dict[str, object]], package_cache: Path, output: Path,
    verified_archives: dict[str, tuple[str, str]],
) -> int:
    license_root = output / "licenses"
    license_root.mkdir(parents=True)
    manifest_lines = [
        "# Bundled OCR runtime package manifest",
        "",
        "Every package below was verified against the checked-in SHA-256 manifest. Notice status records whether the exact conda package supplied separate files under info/licenses.",
        "",
        "| Package | Version | Build | Declared license | SHA-256 | Notice status | Source channel |",
        "| --- | --- | --- | --- | --- | --- | --- |",
    ]
    copied_license_files = 0
    for meta in sorted(metadata, key=lambda item: str(item["name"])):
        package_id = f"{meta['name']}-{meta['version']}-{meta['build']}"
        source = package_source(meta, package_cache)
        if source is None:
            raise SystemExit(f"Extracted package source is unavailable for license audit: {package_id}")
        source_licenses = source / "info/licenses"
        if source_licenses.exists() and not source_licenses.is_dir():
            raise SystemExit(f"Package license path is not a directory: {source_licenses}")
        if source_licenses.is_dir():
            license_files = sorted(path for path in source_licenses.rglob("*") if path.is_file())
            if not license_files:
                raise SystemExit(f"Package has an empty info/licenses directory: {package_id}")
            shutil.copytree(source_licenses, license_root / package_id, symlinks=True)
            copied_license_files += len(license_files)
            notice_status = f"{len(license_files)} supplied file(s) copied"
        else:
            notice_status = "No separate info/licenses files supplied"

        archive_record = verified_archives.get(str(meta.get("fn", "")))
        if archive_record is None:
            raise SystemExit(f"Package archive is not covered by the lock: {package_id}")
        source_url, sha256 = archive_record
        declared_license = str(meta.get("license", "Not declared")).replace("|", "\\|")
        channel_url = source_url.replace("|", "%7C")
        manifest_lines.append(
            f"| {meta['name']} | {meta['version']} | {meta['build']} | "
            f"{declared_license} | `{sha256}` | {notice_status} | {channel_url} |"
        )

    if copied_license_files == 0:
        raise SystemExit("No third-party license files were found in the package cache")
    (license_root / "THIRD_PARTY_MANIFEST.md").write_text(
        "\n".join(manifest_lines) + "\n", encoding="utf-8"
    )
    return copied_license_files


def main() -> int:
    arguments = parse_arguments()
    environment = arguments.environment.resolve()
    output = arguments.output.resolve()
    package_cache = arguments.package_cache.resolve()
    if not environment.is_dir():
        raise SystemExit(f"OCR environment does not exist: {environment}")
    if not package_cache.is_dir():
        raise SystemExit(f"OCR package cache does not exist: {package_cache}")
    if output.exists():
        raise SystemExit(f"OCR staging output already exists: {output}")

    metadata_paths = sorted((environment / "conda-meta").glob("*.json"))
    if not metadata_paths:
        raise SystemExit("OCR environment has no conda package metadata")
    metadata = [json.loads(path.read_text(encoding="utf-8")) for path in metadata_paths]

    platform = arguments.platform
    if platform is None:
        if sys.platform == "win32":
            platform = "windows"
        elif sys.platform == "darwin":
            platform = "macos"
    if platform is None:
        raise SystemExit("Use --platform windows or --platform macos for cross-platform validation")

    verified_archives = verify_package_archives(
        metadata, package_cache, arguments.lock_file.resolve(),
        arguments.sha256_manifest.resolve(),
    )
    output.mkdir(parents=True)
    executable, runtime_library_count = copy_runtime(environment, output, platform)
    copied_license_files = collect_licenses(
        metadata, package_cache, output, verified_archives
    )

    forbidden_directories = ("conda-meta", "include", "Library/include")
    for relative_path in forbidden_directories:
        if (output / relative_path).exists():
            raise SystemExit(f"Development-only path was staged: {relative_path}")

    print(f"Staged OCR runtime at {output}")
    print(f"Verified package archives with SHA-256: {len(verified_archives)}")
    print(f"Validated executable: {executable.relative_to(output)}")
    print(f"Validated runtime libraries: {runtime_library_count}")
    print("Validated language data: eng, chi_sim")
    print(f"Collected third-party license files: {copied_license_files}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
