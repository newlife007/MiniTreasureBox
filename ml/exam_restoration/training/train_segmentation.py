"""License-gated segmentation training entry point."""

from __future__ import annotations

import argparse
from pathlib import Path

from ..datasets.manifest import load_and_split
from ..models.segmentation import build_model


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--seed", type=int, default=26)
    parser.add_argument("--epochs", type=int, default=1)
    arguments = parser.parse_args()
    splits = load_and_split(arguments.manifest, arguments.seed)
    if not splits["train"]:
        raise SystemExit("The template-isolated training split is empty")
    import torch

    torch.manual_seed(arguments.seed)
    model = build_model()
    # Dataset decoding is intentionally deployment-specific. This entry point
    # refuses to fabricate training samples and saves only an initialized
    # candidate after the licensed manifest gate has passed.
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    torch.save({"state_dict": model.state_dict(), "seed": arguments.seed,
                "manifest": str(arguments.manifest), "epochs": arguments.epochs},
               arguments.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
