"""Generate deterministic overlay metadata from licensed asset IDs."""

from __future__ import annotations

import hashlib
import random


def overlay_plan(page_id: str, asset_ids: list[str], seed: int = 26) -> list[dict[str, object]]:
    if not asset_ids:
        raise ValueError("At least one licensed asset ID is required")
    stable = int.from_bytes(hashlib.sha256(f"{seed}:{page_id}".encode()).digest()[:8], "big")
    generator = random.Random(stable)
    return [{"asset_id": generator.choice(sorted(asset_ids)),
             "x": generator.random(), "y": generator.random(),
             "opacity": generator.uniform(0.65, 1.0)} for _ in range(4)]
