"""Evaluate generated or approved prediction records."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from .metrics import class_recall, confusion_matrix, print_false_deletion


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--predictions", required=True, type=Path)
    arguments = parser.parse_args()
    payload = json.loads(arguments.predictions.read_text(encoding="utf-8"))
    matrix = confusion_matrix(payload["expected"], payload["predicted"])
    print(json.dumps({"confusion_matrix": matrix,
                      "handwriting_recall": class_recall(matrix, 1),
                      "print_false_deletion": print_false_deletion(matrix)}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
