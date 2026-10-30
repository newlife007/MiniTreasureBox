#!/usr/bin/env python3
"""Stage and validate the pinned conda-forge Tesseract runtime."""

from __future__ import annotations

import argparse
import json
import shutil
import sys
from pathlib import Path


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--environment", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--package-cache", type=Path)
    parser.add_argument("--platform", choices=("windows", "macos"))
    return parser.parse_args()


def first_existing(root: Path, relative_paths: tuple[str, ...]) -> Path | None:
    for relative_path in relative_paths:
        candidate = root / relative_path
        if candidate.is_file():
            return candidate
    return None


def package_source(meta: dict[str, object], package_cache: Path | None) -> Path | None:
    extracted = meta.get("extracted_package_dir")
    if isinstance(extracted, str):
        extracted_path = Path(extracted)
        if extracted_path.is_dir():
            return extracted_path
    if package_cache is not None:
        candidate = package_cache / (
            f"{meta['name']}-{meta['version']}-{meta['build']}"
        )
        if candidate.is_dir():
            return candidate
    return None


def main() -> int:
    arguments = parse_arguments()
    environment = arguments.environment.resolve()
    output = arguments.output.resolve()
    if not environment.is_dir():
        raise SystemExit(f"OCR environment does not exist: {environment}")
    if output.exists():
        raise SystemExit(f"OCR staging output already exists: {output}")

    metadata_paths = sorted((environment / "conda-meta").glob("*.json"))
    if not metadata_paths:
        raise SystemExit("OCR environment has no conda package metadata")

    shutil.copytree(environment, output, symlinks=True)

    platform = arguments.platform
    if platform is None:
        if sys.platform == "win32":
            platform = "windows"
        elif sys.platform == "darwin":
            platform = "macos"
    if platform == "windows":
        executable = first_existing(
            output, ("Library/bin/tesseract.exe", "bin/tesseract.exe")
        )
        runtime_libraries = list((output / "Library/bin").glob("*.dll"))
    elif platform == "macos":
        executable = first_existing(output, ("bin/tesseract",))
        runtime_libraries = list((output / "lib").glob("*.dylib"))
    else:
        raise SystemExit("Use --platform windows or --platform macos for cross-platform validation")

    if executable is None:
        raise SystemExit("Tesseract executable is missing from the staged environment")
    if not runtime_libraries:
        raise SystemExit("Tesseract transitive runtime libraries are missing")

    tessdata = output / "share/tessdata"
    required_languages = {"eng", "chi_sim"}
    for model in tessdata.glob("*.traineddata"):
        if model.stem not in required_languages:
            model.unlink()
    for language in sorted(required_languages):
        model = tessdata / f"{language}.traineddata"
        if not model.is_file():
            raise SystemExit(f"Required OCR language data is missing: {model}")

    license_root = output / "licenses"
    license_root.mkdir()
    manifest_lines = [
        "# Bundled OCR runtime package manifest",
        "",
        "This directory contains license files supplied by the exact conda-forge packages used to build this runtime.",
        "",
        "| Package | Version | Build | Declared license | Source channel |",
        "| --- | --- | --- | --- | --- |",
    ]
    copied_license_files = 0
    for metadata_path in metadata_paths:
        meta = json.loads(metadata_path.read_text(encoding="utf-8"))
        package_id = f"{meta['name']}-{meta['version']}-{meta['build']}"
        source = package_source(meta, arguments.package_cache)
        source_licenses = source / "info/licenses" if source is not None else None
        if source_licenses is not None and source_licenses.is_dir():
            shutil.copytree(source_licenses, license_root / package_id)
            copied_license_files += sum(
                1 for path in (license_root / package_id).rglob("*") if path.is_file()
            )
        declared_license = str(meta.get("license", "Not declared")).replace("|", "\\|")
        channel_url = str(meta.get("url", meta.get("channel", "conda-forge")))
        manifest_lines.append(
            f"| {meta['name']} | {meta['version']} | {meta['build']} | "
            f"{declared_license} | {channel_url} |"
        )

    if copied_license_files == 0:
        raise SystemExit("No third-party license files were found in the package cache")
    (license_root / "THIRD_PARTY_MANIFEST.md").write_text(
        "\n".join(manifest_lines) + "\n", encoding="utf-8"
    )
    shutil.rmtree(output / "conda-meta")

    print(f"Staged OCR runtime at {output}")
    print(f"Validated executable: {executable.relative_to(output)}")
    print(f"Validated runtime libraries: {len(runtime_libraries)}")
    print("Validated language data: eng, chi_sim")
    print(f"Collected third-party license files: {copied_license_files}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
