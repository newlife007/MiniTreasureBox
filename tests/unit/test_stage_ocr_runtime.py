#!/usr/bin/env python3

from __future__ import annotations

import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


class StageOcrRuntimeTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.environment = self.root / "environment"
        self.cache = self.root / "cache"
        self.environment.mkdir()
        self.cache.mkdir()
        (self.environment / "conda-meta").mkdir()
        (self.environment / "Library/bin").mkdir(parents=True)
        (self.environment / "Library/include").mkdir(parents=True)
        (self.environment / "share/tessdata").mkdir(parents=True)
        (self.environment / "Library/bin/tesseract.exe").write_bytes(b"executable")
        (self.environment / "Library/bin/tesseract55.dll").write_bytes(b"runtime")
        (self.environment / "Library/bin/unrelated.exe").write_bytes(b"tool")
        (self.environment / "Library/include/tesseract.h").write_text("header")
        (self.environment / "share/tessdata/eng.traineddata").write_bytes(b"eng")
        (self.environment / "share/tessdata/chi_sim.traineddata").write_bytes(b"chi")
        (self.environment / "share/tessdata/deu.traineddata").write_bytes(b"deu")

        self.urls = [
            "https://conda.example/win-64/tesseract-5.5.3-test_0.conda",
            "https://conda.example/win-64/vc-14.5-test_0.conda",
        ]
        self.digests: dict[str, str] = {}
        packages = (
            ("tesseract", "5.5.3", "test_0", "Apache-2.0", True),
            ("vc", "14.5", "test_0", "BSD-3-Clause", False),
        )
        for url, package in zip(self.urls, packages, strict=True):
            name, version, build, license_name, supplies_license = package
            filename = Path(url).name
            archive = self.cache / filename
            archive.write_bytes(filename.encode("utf-8"))
            self.digests[url] = hashlib.sha256(archive.read_bytes()).hexdigest()
            source = self.cache / f"{name}-{version}-{build}"
            source.mkdir()
            if supplies_license:
                (source / "info/licenses").mkdir(parents=True)
                (source / "info/licenses/LICENSE.txt").write_text("license")
            metadata = {
                "name": name,
                "version": version,
                "build": build,
                "fn": filename,
                "license": license_name,
                "url": url,
                "extracted_package_dir": str(source),
            }
            (self.environment / "conda-meta" / f"{name}.json").write_text(
                json.dumps(metadata), encoding="utf-8"
            )

        self.lock_file = self.root / "runtime.lock"
        self.lock_file.write_text(
            "@EXPLICIT\n" + "\n".join(f"{url}#{'0' * 32}" for url in self.urls) + "\n",
            encoding="utf-8",
        )
        self.manifest = self.root / "sha256.json"
        self.manifest.write_text(json.dumps(self.digests), encoding="utf-8")

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def run_stager(self, output: Path, manifest: Path | None = None) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [
                sys.executable,
                sys.argv[1],
                "--environment", str(self.environment),
                "--output", str(output),
                "--package-cache", str(self.cache),
                "--lock-file", str(self.lock_file),
                "--sha256-manifest", str(manifest or self.manifest),
                "--platform", "windows",
            ],
            check=False,
            capture_output=True,
            text=True,
        )

    def test_verifies_archives_prunes_payload_and_accounts_for_licenses(self) -> None:
        output = self.root / "output"
        result = self.run_stager(output)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue((output / "Library/bin/tesseract.exe").is_file())
        self.assertTrue((output / "Library/bin/tesseract55.dll").is_file())
        self.assertFalse((output / "Library/bin/unrelated.exe").exists())
        self.assertFalse((output / "Library/include").exists())
        self.assertFalse((output / "share/tessdata/deu.traineddata").exists())
        manifest = (output / "licenses/THIRD_PARTY_MANIFEST.md").read_text(encoding="utf-8")
        self.assertIn("1 supplied file(s) copied", manifest)
        self.assertIn("No separate info/licenses files supplied", manifest)
        for digest in self.digests.values():
            self.assertIn(digest, manifest)

    def test_rejects_a_package_with_the_wrong_sha256(self) -> None:
        invalid_manifest = self.root / "invalid-sha256.json"
        invalid = dict(self.digests)
        invalid[self.urls[0]] = "f" * 64
        invalid_manifest.write_text(json.dumps(invalid), encoding="utf-8")
        result = self.run_stager(self.root / "invalid-output", invalid_manifest)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("SHA-256 mismatch", result.stderr)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
