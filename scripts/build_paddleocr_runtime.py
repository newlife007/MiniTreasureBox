#!/usr/bin/env python3
"""Build a relocatable PaddleOCR + ONNX Runtime worker for packaging."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
import urllib.request
import venv
from pathlib import Path
from urllib.parse import urlsplit


REQUIRED_MODEL_FILES = ("inference.onnx", "inference.yml")
ModelFileRecord = dict[str, str]
ModelFiles = dict[str, ModelFileRecord]
ValidatedModelManifest = dict[str, dict[str, ModelFiles]]


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


def validate_model_manifest(model_manifest: object) -> ValidatedModelManifest:
    if not isinstance(model_manifest, dict) or not model_manifest:
        raise SystemExit("PaddleOCR model manifest must contain at least one model record")

    validated: ValidatedModelManifest = {}
    for model_name, record in model_manifest.items():
        if (
            not isinstance(model_name, str)
            or not model_name
            or Path(model_name).name != model_name
            or model_name in {".", ".."}
        ):
            raise SystemExit(f"PaddleOCR model name must be a safe directory name: {model_name!r}")
        if not isinstance(record, dict):
            raise SystemExit(f"PaddleOCR model record must be an object: {model_name}")

        files = record.get("files")
        if not isinstance(files, dict):
            raise SystemExit(f"PaddleOCR model record must define a files object: {model_name}")
        missing = [filename for filename in REQUIRED_MODEL_FILES if filename not in files]
        if missing:
            raise SystemExit(
                f"PaddleOCR model record is incomplete for {model_name}; "
                f"missing required files: {', '.join(missing)}"
            )
        unsupported = sorted(set(files) - set(REQUIRED_MODEL_FILES))
        if unsupported:
            raise SystemExit(
                f"PaddleOCR model record contains unsupported files for {model_name}: "
                f"{', '.join(unsupported)}"
            )

        validated_files: dict[str, dict[str, str]] = {}
        for filename in REQUIRED_MODEL_FILES:
            file_record = files[filename]
            if not isinstance(file_record, dict):
                raise SystemExit(
                    f"PaddleOCR model file record must be an object: {model_name}/{filename}"
                )
            url = file_record.get("url")
            if not isinstance(url, str) or not url:
                raise SystemExit(
                    f"PaddleOCR model file URL is missing: {model_name}/{filename}"
                )
            if urlsplit(url).scheme != "https":
                raise SystemExit(f"Model URL must use HTTPS: {url}")
            expected_sha256 = file_record.get("sha256")
            if (
                not isinstance(expected_sha256, str)
                or len(expected_sha256) != 64
                or any(character not in "0123456789abcdefABCDEF" for character in expected_sha256)
            ):
                raise SystemExit(
                    f"PaddleOCR model file SHA-256 is invalid: {model_name}/{filename}"
                )
            validated_files[filename] = {
                "url": url,
                "sha256": expected_sha256.lower(),
            }
        validated[model_name] = {"files": validated_files}
    return validated


def stage_models(model_manifest: object, models: Path) -> None:
    validated = validate_model_manifest(model_manifest)
    models.mkdir(parents=True)
    for model_name, record in validated.items():
        model_destination = models / model_name
        model_destination.mkdir()
        for filename in REQUIRED_MODEL_FILES:
            file_record = record["files"][filename]
            download(
                file_record["url"],
                model_destination / filename,
                file_record["sha256"],
            )


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
    models = output / "models"
    stage_models(model_manifest, models)

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
