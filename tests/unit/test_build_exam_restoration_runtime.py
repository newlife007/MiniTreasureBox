#!/usr/bin/env python3
"""Regression tests for the gated exam-restoration runtime builder."""

from __future__ import annotations

import hashlib
import importlib.util
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
SCRIPT_PATH = REPOSITORY_ROOT / "scripts/build_exam_restoration_runtime.py"
SPEC = importlib.util.spec_from_file_location("build_exam_restoration_runtime", SCRIPT_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"Could not load runtime builder from {SCRIPT_PATH}")
BUILDER = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = BUILDER
SPEC.loader.exec_module(BUILDER)


class FakeResponse(io.BytesIO):
    pass


def digest(contents: bytes) -> str:
    return hashlib.sha256(contents).hexdigest()


def approved_manifest(model: bytes = b"model", license_text: bytes = b"license\n") -> dict[str, object]:
    revision = "1" * 40
    return {
        "schema_version": 1,
        "artifacts": [{
            "id": "approved-segmentation-v1",
            "filename": "exam-restoration.onnx",
            "revision": revision,
            "url": f"https://models.example/releases/{revision}/exam-restoration.onnx",
            "bytes": len(model),
            "sha256": digest(model),
            "source": f"https://source.example/tree/{revision}",
            "purpose": "Three-class background, handwriting, and print segmentation",
            "license": {
                "name": "Example-1.0",
                "url": f"https://source.example/raw/{revision}/LICENSE",
                "bytes": len(license_text),
                "sha256": digest(license_text),
            },
            "redistribution_approved": True,
            "approval_reference": "LEGAL-123",
        }],
    }


class ManifestValidationTest(unittest.TestCase):
    def test_accepts_complete_approved_manifest(self) -> None:
        records = BUILDER.validate_model_manifest(approved_manifest())
        self.assertEqual(len(records), 1)
        self.assertEqual(records[0]["filename"], "exam-restoration.onnx")

    def test_empty_manifest_keeps_runtime_disabled(self) -> None:
        self.assertEqual(BUILDER.validate_model_manifest({
            "schema_version": 1, "artifacts": [],
        }), [])

    def test_rejects_missing_rights_before_download(self) -> None:
        manifest = approved_manifest()
        del manifest["artifacts"][0]["approval_reference"]
        with tempfile.TemporaryDirectory() as temporary_directory:
            with mock.patch.object(BUILDER.urllib.request, "urlopen") as urlopen:
                with self.assertRaisesRegex(SystemExit, "incomplete rights"):
                    BUILDER.stage_model(
                        manifest,
                        Path(temporary_directory) / "models",
                        Path(temporary_directory) / "licenses",
                    )
            urlopen.assert_not_called()

    def test_rejects_non_https_before_download(self) -> None:
        manifest = approved_manifest()
        manifest["artifacts"][0]["url"] = (
            "http://models.example/releases/" + "1" * 40 + "/exam-restoration.onnx"
        )
        with tempfile.TemporaryDirectory() as temporary_directory:
            with mock.patch.object(BUILDER.urllib.request, "urlopen") as urlopen:
                with self.assertRaisesRegex(SystemExit, "clean HTTPS"):
                    BUILDER.stage_model(
                        manifest,
                        Path(temporary_directory) / "models",
                        Path(temporary_directory) / "licenses",
                    )
            urlopen.assert_not_called()


class ArtifactStagingTest(unittest.TestCase):
    def test_stages_verified_model_and_exact_license_text(self) -> None:
        model = b"verified model"
        license_text = b"Exact license text\n"
        manifest = approved_manifest(model, license_text)
        responses = {
            manifest["artifacts"][0]["url"]: model,
            manifest["artifacts"][0]["license"]["url"]: license_text,
        }

        def open_response(request: object, timeout: int) -> FakeResponse:
            self.assertEqual(timeout, 180)
            return FakeResponse(responses[request.full_url])

        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            with mock.patch.object(BUILDER.urllib.request, "urlopen", side_effect=open_response):
                BUILDER.stage_model(manifest, root / "models", root / "licenses")
            self.assertEqual((root / "models/exam-restoration.onnx").read_bytes(), model)
            self.assertEqual((root / "licenses/MODEL_LICENSE.txt").read_bytes(), license_text)

    def test_sha_mismatch_removes_partial_and_destination(self) -> None:
        manifest = approved_manifest(b"expected")
        model_url = manifest["artifacts"][0]["url"]
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            with mock.patch.object(
                BUILDER.urllib.request, "urlopen",
                side_effect=lambda request, timeout: FakeResponse(
                    b"corrupt!" if request.full_url == model_url else b"license\n"
                ),
            ):
                with self.assertRaisesRegex(SystemExit, "SHA-256 mismatch"):
                    BUILDER.stage_model(manifest, root / "models", root / "licenses")
            destination = root / "models/exam-restoration.onnx"
            self.assertFalse(destination.exists())
            self.assertFalse(destination.with_suffix(".onnx.part").exists())

    def test_license_sha_mismatch_cleans_entire_staging_area(self) -> None:
        model = b"verified"
        manifest = approved_manifest(model, b"license\n")
        model_url = manifest["artifacts"][0]["url"]
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            with mock.patch.object(
                BUILDER.urllib.request, "urlopen",
                side_effect=lambda request, timeout: FakeResponse(
                    model if request.full_url == model_url else b"corrupt\n"
                ),
            ):
                with self.assertRaisesRegex(SystemExit, "SHA-256 mismatch"):
                    BUILDER.stage_model(manifest, root / "models", root / "licenses")
            self.assertFalse((root / "models").exists())
            self.assertFalse((root / "licenses").exists())

    def test_byte_mismatch_removes_partial_file(self) -> None:
        manifest = approved_manifest(b"expected")
        manifest["artifacts"][0]["bytes"] += 1
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            with mock.patch.object(
                BUILDER.urllib.request, "urlopen", return_value=FakeResponse(b"expected")
            ):
                with self.assertRaisesRegex(SystemExit, "byte size mismatch"):
                    BUILDER.stage_model(manifest, root / "models", root / "licenses")
            self.assertFalse((root / "models/exam-restoration.onnx.part").exists())


class SelfTestValidationTest(unittest.TestCase):
    def test_accepts_expected_self_test(self) -> None:
        payload = {
            "ok": True, "schema_version": 1,
            "backend": "onnxruntime", "model": "exam-restoration.onnx",
        }
        completed = mock.Mock(returncode=0, stdout=json.dumps(payload) + "\n", stderr="")
        with mock.patch.object(BUILDER.subprocess, "run", return_value=completed):
            self.assertEqual(
                BUILDER.run_self_test(Path("worker"), Path("models")), payload
            )

    def test_rejects_unexpected_self_test_backend(self) -> None:
        payload = {
            "ok": True, "schema_version": 1,
            "backend": "unapproved", "model": "exam-restoration.onnx",
        }
        completed = mock.Mock(returncode=0, stdout=json.dumps(payload) + "\n", stderr="")
        with mock.patch.object(BUILDER.subprocess, "run", return_value=completed):
            with self.assertRaisesRegex(SystemExit, "Unexpected"):
                BUILDER.run_self_test(Path("worker"), Path("models"))


if __name__ == "__main__":
    unittest.main()
