#!/usr/bin/env python3
"""Build a verified, relocatable exam-restoration worker runtime."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
import urllib.request
import venv
from pathlib import Path
from urllib.parse import urlsplit

MODEL_FILENAME = "exam-restoration.onnx"
REQUIRED_ROOT_FIELDS = {"schema_version", "artifacts"}
REQUIRED_ARTIFACT_FIELDS = {
    "id", "filename", "revision", "url", "bytes", "sha256", "source",
    "purpose", "license", "redistribution_approved", "approval_reference",
}
REQUIRED_LICENSE_FIELDS = {"name", "url", "bytes", "sha256"}
DIGEST_PATTERN = re.compile(r"^[0-9a-fA-F]{64}$")
REVISION_PATTERN = re.compile(r"^[0-9a-fA-F]{40}(?:[0-9a-fA-F]{24})?$")


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path)
    parser.add_argument("--work", type=Path)
    parser.add_argument(
        "--requirements", type=Path,
        default=Path("packaging/exam-restoration-worker-requirements.txt"),
    )
    parser.add_argument(
        "--model-manifest", type=Path,
        default=Path("packaging/exam-restoration-models.json"),
    )
    parser.add_argument(
        "--worker-script", type=Path,
        default=Path("scripts/exam_restoration_worker.py"),
    )
    parser.add_argument(
        "--manifest-status", action="store_true",
        help="print enabled or disabled after strict manifest validation",
    )
    return parser.parse_args()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _safe_name(value: object, description: str) -> str:
    if (not isinstance(value, str) or not value
            or Path(value).name != value or value in {".", ".."}):
        raise SystemExit(f"{description} must be a safe file name")
    return value


def _text(value: object, description: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise SystemExit(f"{description} is missing")
    return value.strip()


def _positive_bytes(value: object, description: str) -> int:
    if not isinstance(value, int) or isinstance(value, bool) or value <= 0:
        raise SystemExit(f"{description} bytes must be a positive integer")
    return value


def _digest(value: object, description: str) -> str:
    if not isinstance(value, str) or DIGEST_PATTERN.fullmatch(value) is None:
        raise SystemExit(f"{description} SHA-256 is invalid")
    return value.lower()


def _immutable_https_url(value: object, revision: str, description: str) -> str:
    url = _text(value, f"{description} URL")
    parsed = urlsplit(url)
    if (parsed.scheme != "https" or not parsed.hostname or parsed.username
            or parsed.password or parsed.query or parsed.fragment):
        raise SystemExit(f"{description} URL must be a clean HTTPS URL")
    path_segments = [segment.lower() for segment in parsed.path.split("/") if segment]
    if revision.lower() not in path_segments:
        raise SystemExit(
            f"{description} URL must contain the immutable revision as a path segment"
        )
    return url


def validate_model_manifest(document: object) -> list[dict[str, object]]:
    if not isinstance(document, dict):
        raise SystemExit("Exam-restoration model manifest must be an object")
    if set(document) != REQUIRED_ROOT_FIELDS or document.get("schema_version") != 1:
        raise SystemExit("Exam-restoration model manifest schema is invalid")
    artifacts = document.get("artifacts")
    if not isinstance(artifacts, list):
        raise SystemExit("Exam-restoration model manifest artifacts must be an array")
    if len(artifacts) > 1:
        raise SystemExit("Exam-restoration runtime accepts exactly one production model")

    validated: list[dict[str, object]] = []
    for index, artifact in enumerate(artifacts):
        label = f"artifact {index}"
        if not isinstance(artifact, dict) or set(artifact) != REQUIRED_ARTIFACT_FIELDS:
            raise SystemExit(f"{label} has incomplete rights or artifact metadata")
        artifact_id = _safe_name(artifact.get("id"), f"{label} id")
        filename = _safe_name(artifact.get("filename"), f"{label} filename")
        if filename != MODEL_FILENAME:
            raise SystemExit(f"{label} filename must be {MODEL_FILENAME}")
        revision = _text(artifact.get("revision"), f"{label} revision")
        if REVISION_PATTERN.fullmatch(revision) is None:
            raise SystemExit(f"{label} revision must be a 40 or 64 character commit or digest")
        url = _immutable_https_url(artifact.get("url"), revision, label)
        source = _immutable_https_url(artifact.get("source"), revision, f"{label} source")
        purpose = _text(artifact.get("purpose"), f"{label} purpose")
        artifact_bytes = _positive_bytes(artifact.get("bytes"), label)
        artifact_sha256 = _digest(artifact.get("sha256"), label)
        if artifact.get("redistribution_approved") is not True:
            raise SystemExit(f"{label} redistribution approval is required")
        approval_reference = _text(
            artifact.get("approval_reference"), f"{label} approval reference"
        )

        license_record = artifact.get("license")
        if (not isinstance(license_record, dict)
                or set(license_record) != REQUIRED_LICENSE_FIELDS):
            raise SystemExit(f"{label} license metadata is incomplete")
        license_name = _text(license_record.get("name"), f"{label} license name")
        license_url = _immutable_https_url(
            license_record.get("url"), revision, f"{label} license"
        )
        license_bytes = _positive_bytes(license_record.get("bytes"), f"{label} license")
        license_sha256 = _digest(license_record.get("sha256"), f"{label} license")
        validated.append({
            "id": artifact_id,
            "filename": filename,
            "revision": revision.lower(),
            "url": url,
            "bytes": artifact_bytes,
            "sha256": artifact_sha256,
            "source": source,
            "purpose": purpose,
            "license": {
                "name": license_name,
                "url": license_url,
                "bytes": license_bytes,
                "sha256": license_sha256,
            },
            "redistribution_approved": True,
            "approval_reference": approval_reference,
        })
    return validated


def download(url: str, destination: Path, expected_bytes: int,
             expected_sha256: str) -> None:
    parsed = urlsplit(url)
    if parsed.scheme != "https":
        raise SystemExit(f"Artifact URL must use HTTPS: {url}")
    temporary = destination.with_suffix(destination.suffix + ".part")
    request = urllib.request.Request(
        url, headers={"User-Agent": "MiniTreasureBox-release-builder"}
    )
    digest = hashlib.sha256()
    received_bytes = 0
    try:
        with urllib.request.urlopen(request, timeout=180) as response, temporary.open("xb") as output:
            for block in iter(lambda: response.read(1024 * 1024), b""):
                received_bytes += len(block)
                if received_bytes > expected_bytes:
                    raise SystemExit(
                        f"Artifact byte size mismatch for {url}: expected {expected_bytes}, "
                        f"received more than expected"
                    )
                digest.update(block)
                output.write(block)
        if received_bytes != expected_bytes:
            raise SystemExit(
                f"Artifact byte size mismatch for {url}: expected {expected_bytes}, "
                f"received {received_bytes}"
            )
        actual_sha256 = digest.hexdigest()
        if actual_sha256 != expected_sha256.lower():
            raise SystemExit(
                f"Artifact SHA-256 mismatch for {url}: expected {expected_sha256}, "
                f"received {actual_sha256}"
            )
        temporary.replace(destination)
    finally:
        temporary.unlink(missing_ok=True)


def stage_model(document: object, models: Path, licenses: Path) -> dict[str, object]:
    artifacts = validate_model_manifest(document)
    if not artifacts:
        raise SystemExit(
            "No approved exam-restoration artifact is present; production runtime is disabled"
        )
    artifact = artifacts[0]
    models.mkdir(parents=True)
    licenses.mkdir(parents=True)
    model_path = models / MODEL_FILENAME
    license_path = licenses / "MODEL_LICENSE.txt"
    try:
        download(
            str(artifact["url"]), model_path,
            int(artifact["bytes"]), str(artifact["sha256"]),
        )
        license_record = artifact["license"]
        assert isinstance(license_record, dict)
        download(
            str(license_record["url"]), license_path,
            int(license_record["bytes"]), str(license_record["sha256"]),
        )
    except BaseException:
        shutil.rmtree(models, ignore_errors=True)
        shutil.rmtree(licenses, ignore_errors=True)
        raise
    return artifact


def python_in(venv_root: Path) -> Path:
    return ((venv_root / "Scripts/python.exe") if sys.platform == "win32"
            else (venv_root / "bin/python"))


def collect_licenses(python: Path, output: Path, artifact: dict[str, object]) -> None:
    code = "import json,site; print(json.dumps(site.getsitepackages()))"
    site_packages = Path(json.loads(
        subprocess.check_output([python, "-c", code], text=True)
    )[0])
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
        candidates = (list((dist_info / "licenses").rglob("*"))
                      if (dist_info / "licenses").is_dir() else [])
        candidates.extend(dist_info.glob("LICENSE*"))
        candidates.extend(dist_info.glob("COPYING*"))
        files = [path for path in candidates if path.is_file()]
        if not files:
            continue
        target = output / dist_info.name
        target.mkdir(parents=True, exist_ok=True)
        for source in files:
            relative = (source.relative_to(dist_info / "licenses")
                        if (dist_info / "licenses") in source.parents
                        else Path(source.name))
            destination = target / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, destination)
            copied += 1

    manifest = [
        "# Bundled exam-restoration runtime manifest", "",
        f"Model artifact: {artifact['id']}",
        f"Immutable revision: {artifact['revision']}",
        f"Purpose: {artifact['purpose']}",
        f"Source: {artifact['source']}",
        f"Redistribution approval: {artifact['approval_reference']}", "",
        "| Package | Version | Declared license |",
        "| --- | --- | --- |",
    ]
    manifest.extend(f"| {name} | {version} | {license_name} |"
                    for name, version, license_name in rows)
    manifest.extend(["", f"Copied dependency license files: {copied}", ""])
    (output / "THIRD_PARTY_MANIFEST.md").write_text(
        "\n".join(manifest), encoding="utf-8"
    )


def validate_self_test_payload(payload: object) -> dict[str, object]:
    if (not isinstance(payload, dict) or payload.get("ok") is not True
            or payload.get("schema_version") != 1
            or payload.get("backend") != "onnxruntime"
            or payload.get("model") != MODEL_FILENAME):
        raise SystemExit(f"Unexpected exam-restoration worker self-test response: {payload}")
    return payload


def run_self_test(executable: Path, models: Path) -> dict[str, object]:
    completed = subprocess.run(
        [executable, "--models", models, "--self-test"],
        text=True, capture_output=True, timeout=180,
    )
    if completed.returncode != 0:
        raise SystemExit(
            "Bundled exam-restoration worker self-test failed:\n"
            + completed.stdout + "\n" + completed.stderr
        )
    try:
        payload = json.loads(completed.stdout.strip().splitlines()[-1])
    except (IndexError, json.JSONDecodeError) as error:
        raise SystemExit("Bundled exam-restoration worker returned invalid self-test JSON") from error
    return validate_self_test_payload(payload)


def main() -> int:
    arguments = parse_arguments()
    if not arguments.model_manifest.is_file():
        raise SystemExit(
            f"Required exam-restoration model manifest is missing: {arguments.model_manifest}"
        )
    document = json.loads(arguments.model_manifest.read_text(encoding="utf-8"))
    artifacts = validate_model_manifest(document)
    if arguments.manifest_status:
        print("enabled" if artifacts else "disabled")
        return 0
    if arguments.output is None or arguments.work is None:
        raise SystemExit("--output and --work are required when building a runtime")
    if not artifacts:
        raise SystemExit(
            "No approved exam-restoration artifact is present; production runtime is disabled"
        )

    output = arguments.output.resolve()
    work = arguments.work.resolve()
    if output.exists():
        raise SystemExit(f"Exam-restoration runtime output already exists: {output}")
    if work.exists():
        raise SystemExit(f"Exam-restoration work directory already exists: {work}")
    for required in (arguments.requirements, arguments.worker_script):
        if not required.is_file():
            raise SystemExit(f"Required exam-restoration build input is missing: {required}")

    work.mkdir(parents=True)
    environment = work / "venv"
    venv.EnvBuilder(with_pip=True, clear=True).create(environment)
    python = python_in(environment)
    subprocess.run(
        [python, "-m", "pip", "install", "--disable-pip-version-check",
         "--upgrade", "pip"], check=True,
    )
    subprocess.run(
        [python, "-m", "pip", "install", "--disable-pip-version-check", "-r",
         arguments.requirements.resolve()], check=True,
    )

    licenses = output / "licenses"
    artifact = stage_model(document, output / "models", licenses)
    dist = work / "dist"
    subprocess.run([
        python, "-m", "PyInstaller", "--noconfirm", "--clean", "--onedir",
        "--name", "exam-restoration-worker", "--distpath", dist,
        "--workpath", work / "pyinstaller-work", "--specpath", work / "spec",
        "--hidden-import", "onnxruntime", "--collect-all", "PIL",
        "--collect-all", "numpy", "--exclude-module", "tkinter",
        "--exclude-module", "matplotlib", str(arguments.worker_script.resolve()),
    ], check=True)
    worker_output = dist / "exam-restoration-worker"
    if not worker_output.is_dir():
        raise SystemExit(f"PyInstaller did not produce the expected worker: {worker_output}")
    shutil.copytree(worker_output, output / "bin")
    collect_licenses(python, licenses, artifact)
    shutil.copy2(arguments.requirements, licenses / "PYTHON_REQUIREMENTS.txt")
    shutil.copy2(arguments.model_manifest, licenses / "EXAM_RESTORATION_MODELS.json")

    executable = output / "bin" / (
        "exam-restoration-worker.exe" if sys.platform == "win32"
        else "exam-restoration-worker"
    )
    payload = run_self_test(executable, output / "models")
    self_test = dict(payload)
    self_test["model_bytes"] = (output / "models" / MODEL_FILENAME).stat().st_size
    self_test["model_sha256"] = sha256_file(output / "models" / MODEL_FILENAME)
    (output / "SELF_TEST.json").write_text(
        json.dumps(self_test, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(f"Staged exam-restoration runtime at {output}")
    print(f"Worker self-test: {payload}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
