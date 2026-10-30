#!/usr/bin/env python3

from __future__ import annotations

import hashlib
import importlib.util
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch


SCRIPT_PATH = Path(sys.argv[1]).resolve()
SPEC = importlib.util.spec_from_file_location("fetch_ocr_runtime", SCRIPT_PATH)
assert SPEC and SPEC.loader
FETCHER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(FETCHER)


class FakeResponse(io.BytesIO):
    def __enter__(self) -> "FakeResponse":
        return self

    def __exit__(self, *args: object) -> None:
        self.close()


class FetchOcrRuntimeTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.url = "https://conda.example/win-64/runtime-1.0-test_0.conda"
        self.contents = b"locked executable package"
        self.lock = self.root / "runtime.lock"
        self.lock.write_text(
            f"@EXPLICIT\n{self.url}#{'1' * 32}\n", encoding="utf-8"
        )

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def invoke(self, digest: str) -> tuple[Path, Path]:
        manifest = self.root / "sha256.json"
        manifest.write_text(json.dumps({self.url: digest}), encoding="utf-8")
        cache = self.root / "packages"
        local_lock = self.root / "local.lock"
        arguments = [
            str(SCRIPT_PATH),
            "--lock-file", str(self.lock),
            "--sha256-manifest", str(manifest),
            "--output-cache", str(cache),
            "--output-lock", str(local_lock),
        ]
        with patch.object(sys, "argv", arguments), patch.object(
            FETCHER, "urlopen", return_value=FakeResponse(self.contents)
        ):
            FETCHER.main()
        return cache, local_lock

    def test_writes_local_lock_only_after_sha256_verification(self) -> None:
        digest = hashlib.sha256(self.contents).hexdigest()
        cache, local_lock = self.invoke(digest)
        archive = cache / "runtime-1.0-test_0.conda"
        self.assertEqual(archive.read_bytes(), self.contents)
        lines = local_lock.read_text(encoding="utf-8").splitlines()
        self.assertEqual(lines[0], "@EXPLICIT")
        self.assertEqual(lines[1], f"{archive.resolve().as_uri()}#{'1' * 32}")

    def test_rejects_download_before_writing_local_lock_on_mismatch(self) -> None:
        with self.assertRaisesRegex(SystemExit, "SHA-256 mismatch"):
            self.invoke("f" * 64)
        self.assertFalse((self.root / "local.lock").exists())
        self.assertFalse((self.root / "packages/runtime-1.0-test_0.conda").exists())


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
