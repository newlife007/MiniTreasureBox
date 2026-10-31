#!/usr/bin/env python3
"""Regression tests for verified PaddleOCR model staging."""

from __future__ import annotations

import hashlib
import importlib.util
import io
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
SCRIPT_PATH = REPOSITORY_ROOT / "scripts/build_paddleocr_runtime.py"
SPEC = importlib.util.spec_from_file_location("build_paddleocr_runtime", SCRIPT_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"Could not load runtime builder from {SCRIPT_PATH}")
BUILDER = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = BUILDER
SPEC.loader.exec_module(BUILDER)


class FakeResponse(io.BytesIO):
    pass


def sha256(contents: bytes) -> str:
    return hashlib.sha256(contents).hexdigest()


def model_record(
    onnx_contents: bytes,
    yml_contents: bytes,
    *,
    onnx_url: str = "https://models.example/revision/inference.onnx",
) -> dict[str, object]:
    return {
        "files": {
            "inference.onnx": {
                "url": onnx_url,
                "sha256": sha256(onnx_contents),
            },
            "inference.yml": {
                "url": "https://models.example/revision/inference.yml",
                "sha256": sha256(yml_contents),
            },
        }
    }


class StageModelsTest(unittest.TestCase):
    def test_stages_verified_model_files(self) -> None:
        onnx_contents = b"verified ONNX model bytes"
        yml_contents = b"Global:\n  model_name: test\n"
        manifest = {"test_model": model_record(onnx_contents, yml_contents)}
        responses = {
            "https://models.example/revision/inference.onnx": onnx_contents,
            "https://models.example/revision/inference.yml": yml_contents,
        }

        def open_response(request: object, timeout: int) -> FakeResponse:
            self.assertEqual(timeout, 180)
            return FakeResponse(responses[request.full_url])

        with tempfile.TemporaryDirectory() as temporary_directory:
            models = Path(temporary_directory) / "models"
            with mock.patch.object(BUILDER.urllib.request, "urlopen", side_effect=open_response):
                BUILDER.stage_models(manifest, models)

            model = models / "test_model"
            self.assertEqual((model / "inference.onnx").read_bytes(), onnx_contents)
            self.assertEqual((model / "inference.yml").read_bytes(), yml_contents)
            self.assertFalse((model / "inference.onnx.part").exists())
            self.assertFalse((model / "inference.yml.part").exists())

    def test_sha256_mismatch_removes_destination_and_partial_file(self) -> None:
        expected_contents = b"expected model"
        received_contents = b"corrupt model"
        manifest = {"test_model": model_record(expected_contents, b"configuration")}

        with tempfile.TemporaryDirectory() as temporary_directory:
            models = Path(temporary_directory) / "models"
            with mock.patch.object(
                BUILDER.urllib.request,
                "urlopen",
                return_value=FakeResponse(received_contents),
            ):
                with self.assertRaisesRegex(SystemExit, "SHA-256 mismatch"):
                    BUILDER.stage_models(manifest, models)

            destination = models / "test_model/inference.onnx"
            self.assertFalse(destination.exists())
            self.assertFalse(destination.with_suffix(".onnx.part").exists())

    def test_rejects_non_https_url_before_downloading(self) -> None:
        manifest = {
            "test_model": model_record(
                b"model",
                b"configuration",
                onnx_url="http://models.example/revision/inference.onnx",
            )
        }

        with tempfile.TemporaryDirectory() as temporary_directory:
            models = Path(temporary_directory) / "models"
            with mock.patch.object(BUILDER.urllib.request, "urlopen") as urlopen:
                with self.assertRaisesRegex(SystemExit, "must use HTTPS"):
                    BUILDER.stage_models(manifest, models)
            urlopen.assert_not_called()
            self.assertFalse(models.exists())

    def test_rejects_incomplete_model_record_before_staging(self) -> None:
        manifest = {
            "test_model": {
                "files": {
                    "inference.onnx": {
                        "url": "https://models.example/revision/inference.onnx",
                        "sha256": sha256(b"model"),
                    }
                }
            }
        }

        with tempfile.TemporaryDirectory() as temporary_directory:
            models = Path(temporary_directory) / "models"
            with self.assertRaisesRegex(
                SystemExit,
                "missing required files: inference.yml",
            ):
                BUILDER.stage_models(manifest, models)
            self.assertFalse(models.exists())


if __name__ == "__main__":
    unittest.main()
