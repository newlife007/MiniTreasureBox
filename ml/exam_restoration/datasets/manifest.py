"""Strict rights validation and template-isolated deterministic splits."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
from pathlib import Path

REQUIRED = {
    "id", "template_id", "image", "label", "source_owner",
    "license_evidence", "permitted_uses", "retention_until", "withdrawn", "sha256",
}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def validate_manifest(payload: object, root: Path,
                      today: dt.date | None = None) -> list[dict[str, object]]:
    if not isinstance(payload, dict) or payload.get("version") != 1:
        raise ValueError("Manifest version must be 1")
    records = payload.get("records")
    if not isinstance(records, list) or not records:
        raise ValueError("Manifest must contain records")
    today = today or dt.date.today()
    validated: list[dict[str, object]] = []
    identifiers: set[str] = set()
    for record in records:
        if not isinstance(record, dict) or set(record) != REQUIRED:
            raise ValueError("Every record must contain exactly the licensed-data fields")
        if any(not isinstance(record[field], str) or not record[field].strip()
               for field in ("id", "template_id", "image", "label", "source_owner",
                             "license_evidence", "retention_until", "sha256")):
            raise ValueError("Manifest text fields cannot be empty")
        if record["id"] in identifiers:
            raise ValueError("Manifest IDs must be unique")
        identifiers.add(str(record["id"]))
        uses = record["permitted_uses"]
        if not isinstance(uses, list) or "training" not in uses:
            raise ValueError("Training permission must be explicit")
        if record["withdrawn"] is not False:
            raise ValueError("Withdrawn data cannot be processed")
        try:
            retention = dt.date.fromisoformat(str(record["retention_until"]))
        except ValueError as error:
            raise ValueError("Retention date must use YYYY-MM-DD") from error
        if retention < today:
            raise ValueError("Expired data cannot be processed")
        digest = str(record["sha256"])
        if len(digest) != 64 or any(character not in "0123456789abcdef" for character in digest):
            raise ValueError("SHA-256 must be lowercase hexadecimal")
        image = (root / str(record["image"])).resolve()
        label = (root / str(record["label"])).resolve()
        if not image.is_file() or not label.is_file():
            raise ValueError("Manifest image and label must exist")
        if sha256_file(image) != digest:
            raise ValueError("Manifest image SHA-256 does not match")
        validated.append(dict(record))
    return sorted(validated, key=lambda item: str(item["id"]))


def grouped_split(records: list[dict[str, object]], seed: int = 26) -> dict[str, list[str]]:
    groups: dict[str, list[str]] = {}
    for record in records:
        groups.setdefault(str(record["template_id"]), []).append(str(record["id"]))
    result: dict[str, list[str]] = {"train": [], "validation": [], "test": []}
    for template_id in sorted(groups):
        digest = hashlib.sha256(f"{seed}:{template_id}".encode()).digest()
        bucket = int.from_bytes(digest[:8], "big") / float(2**64)
        partition = "train" if bucket < 0.60 else ("validation" if bucket < 0.80 else "test")
        result[partition].extend(sorted(groups[template_id]))
    return result


def load_and_split(path: Path, seed: int = 26) -> dict[str, list[str]]:
    payload = json.loads(path.read_text(encoding="utf-8"))
    return grouped_split(validate_manifest(payload, path.parent), seed)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--seed", type=int, default=26)
    arguments = parser.parse_args()
    print(json.dumps(load_and_split(arguments.manifest, arguments.seed), sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
