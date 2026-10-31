#!/usr/bin/env python3
"""Build a relocatable PaddleOCR + ONNX Runtime worker for packaging."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
import tarfile
import urllib.request
import venv
from pathlib import Path
from urllib.parse import urlsplit


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--work", required=True, type=Path)
    parser.add_argument(
        "--requirements", type=Path,
        default=Path("packaging/paddleocr-worker-requirements.txt"),
    )
    parser.add_argument(
        "--model-manifest", type=Path,
        default=Path("packaging/paddleocr-models.json"),
    )
    parser.add_argument(
        "--worker-script", type=Path,
        default=Path("scripts/paddleocr_worker.py"),
    )
    return parser.parse_args()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def download(url: str, destination: Path, expected_sha256: str) -> None:
    if urlsplit(url).scheme != "https":
        raise SystemExit(f"Model URL must use HTTPS: {url}")
    temporary = destination.with_suffix(destination.suffix + ".part")
    request = urllib.request.Request(
        url, headers={"User-Agent": "MiniTreasureBox-release-builder"}
    )
    digest = hashlib.sha256()
    try:
        with urllib.request.urlopen(request, timeout=180) as response, temporary.open("xb") as output:
            for block in iter(lambda: response.read(1024 * 1024), b""):
                digest.update(block)
                output.write(block)
        actual = digest.hexdigest()
        if actual != expected_sha256.lower():
            raise SystemExit(
                f"Model SHA-256 mismatch for {url}: expected {expected_sha256}, received {actual}"
            )
        temporary.replace(destination)
    finally:
        temporary.unlink(missing_ok=True)


def safe_extract(archive: Path, destination: Path) -> None:
    destination_root = destination.resolve()
    with tarfile.open(archive) as package:
        for member in package.getmembers():
            target = (destination / member.name).resolve()
            if destination_root != target and destination_root not in target.parents:
                raise SystemExit(f"Model archive contains an unsafe path: {member.name}")
        package.extractall(destination, filter="data")


def python_in(venv_root: Path) -> Path:
    return (venv_root / "Scripts/python.exe") if sys.platform == "win32" else (venv_root / "bin/python")


def trim_paddlex_assets(worker_output: Path) -> None:
    configs = worker_output / "_internal/paddlex/configs"
    required_modules = {
        "text_detection", "text_recognition", "textline_orientation",
        "doc_text_orientation", "image_unwarping",
    }
    modules = configs / "modules"
    if modules.is_dir():
        for path in modules.iterdir():
            if path.is_dir() and path.name not in required_modules:
                shutil.rmtree(path)
    pipelines = configs / "pipelines"
    if pipelines.is_dir():
        for path in pipelines.glob("*.yaml"):
            if path.name != "OCR.yaml":
                path.unlink()
    serving = worker_output / "_internal/paddlex/inference/serving"
    if serving.is_dir():
        shutil.rmtree(serving)


def collect_licenses(python: Path, output: Path) -> None:
    code = "import json,site; print(json.dumps(site.getsitepackages()))"
    site_packages = Path(json.loads(subprocess.check_output([python, "-c", code], text=True))[0])
    license_root = output / "licenses"
    license_root.mkdir(parents=True)
    rows: list[tuple[str, str, str]] = []
    metadata_code = (
        "import importlib.metadata,json; "
        "print(json.dumps(sorted([(d.metadata.get('Name',d.name),d.version,"
        "d.metadata.get('License-Expression') or d.metadata.get('License') or 'Not declared') "
        "for d in importlib.metadata.distributions()])))"
    )
    for name, version, license_name in json.loads(
        subprocess.check_output([python, "-c", metadata_code], text=True)
    ):
        rows.append((str(name), str(version), str(license_name).replace("|", "\\|")))

    copied = 0
    for dist_info in site_packages.glob("*.dist-info"):
        candidates = list((dist_info / "licenses").rglob("*")) if (dist_info / "licenses").is_dir() else []
        candidates.extend(dist_info.glob("LICENSE*"))
        candidates.extend(dist_info.glob("COPYING*"))
        files = [path for path in candidates if path.is_file()]
        if not files:
            continue
        target = license_root / dist_info.name
        target.mkdir(parents=True, exist_ok=True)
        for source in files:
            relative = source.relative_to(dist_info / "licenses") if (dist_info / "licenses") in source.parents else Path(source.name)
            destination = target / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, destination)
            copied += 1

    manifest = [
        "# Bundled PaddleOCR runtime manifest",
        "",
        "The installer contains a native PyInstaller worker, PaddleOCR, PaddleX OCR Core, ONNX Runtime, and pinned PP-OCRv5 mobile models.",
        "",
        "| Package | Version | Declared license |",
        "| --- | --- | --- |",
    ]
    manifest.extend(f"| {name} | {version} | {license_name} |" for name, version, license_name in rows)
    manifest.extend(["", f"Copied license files: {copied}", ""])
    (license_root / "THIRD_PARTY_MANIFEST.md").write_text(
        "\n".join(manifest), encoding="utf-8"
    )


def main() -> int:
    arguments = parse_arguments()
    output = arguments.output.resolve()
    work = arguments.work.resolve()
    if output.exists():
        raise SystemExit(f"PaddleOCR runtime output already exists: {output}")
    if work.exists():
        raise SystemExit(f"PaddleOCR work directory already exists: {work}")
    for required in (arguments.requirements, arguments.model_manifest, arguments.worker_script):
        if not required.is_file():
            raise SystemExit(f"Required PaddleOCR build input is missing: {required}")

    work.mkdir(parents=True)
    environment = work / "venv"
    venv.EnvBuilder(with_pip=True, clear=True).create(environment)
    python = python_in(environment)
    subprocess.run([python, "-m", "pip", "install", "--disable-pip-version-check", "--upgrade", "pip"], check=True)
    subprocess.run([python, "-m", "pip", "install", "--disable-pip-version-check", "-r", arguments.requirements.resolve()], check=True)

    model_manifest = json.loads(arguments.model_manifest.read_text(encoding="utf-8"))
    archives = work / "model-archives"
    extracted = work / "extracted-models"
    archives.mkdir()
    extracted.mkdir()
    models = output / "models"
    models.mkdir(parents=True)
    for model_name, record in model_manifest.items():
        url = str(record["url"])
        expected_sha256 = str(record["sha256"]).lower()
        archive = archives / Path(urlsplit(url).path).name
        download(url, archive, expected_sha256)
        safe_extract(archive, extracted)
        source = extracted / str(record["archiveRoot"])
        if not (source / "inference.onnx").is_file() or not (source / "inference.yml").is_file():
            raise SystemExit(f"Extracted PaddleOCR model is incomplete: {source}")
        shutil.copytree(source, models / model_name)

    dist = work / "dist"
    pyinstaller_work = work / "pyinstaller-work"
    spec = work / "spec"
    subprocess.run([
        python, "-m", "PyInstaller", "--noconfirm", "--clean", "--onedir",
        "--name", "paddleocr-worker", "--distpath", dist,
        "--workpath", pyinstaller_work, "--specpath", spec,
        "--collect-all", "paddleocr", "--collect-all", "paddlex",
        "--hidden-import", "onnxruntime", "--collect-all", "cv2",
        "--collect-all", "imagesize", "--collect-all", "pyclipper",
        "--collect-all", "pypdfium2", "--collect-all", "bidi",
        "--collect-all", "shapely", "--exclude-module", "tkinter",
        "--exclude-module", "matplotlib", "--exclude-module", "IPython",
        "--exclude-module", "notebook", "--recursive-copy-metadata", "paddleocr",
        "--recursive-copy-metadata", "paddlex", str(arguments.worker_script.resolve()),
    ], check=True)
    worker_output = dist / "paddleocr-worker"
    if not worker_output.is_dir():
        raise SystemExit(f"PyInstaller did not produce the expected worker: {worker_output}")
    trim_paddlex_assets(worker_output)
    shutil.copytree(worker_output, output / "bin")
    collect_licenses(python, output)
    shutil.copy2(arguments.requirements, output / "licenses/PYTHON_REQUIREMENTS.txt")
    shutil.copy2(arguments.model_manifest, output / "licenses/PADDLEOCR_MODELS.json")

    executable = output / "bin" / ("paddleocr-worker.exe" if sys.platform == "win32" else "paddleocr-worker")
    completed = subprocess.run(
        [executable, "--models", models, "--self-test"],
        text=True, capture_output=True, timeout=180,
    )
    if completed.returncode != 0:
        raise SystemExit(
            "Bundled PaddleOCR worker self-test failed:\n"
            + completed.stdout + "\n" + completed.stderr
        )
    payload = json.loads(completed.stdout.strip().splitlines()[-1])
    if not payload.get("ok") or payload.get("backend") != "PaddleOCR":
        raise SystemExit(f"Unexpected PaddleOCR worker self-test response: {payload}")
    print(f"Staged PaddleOCR runtime at {output}")
    print(f"Worker self-test: {payload}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
