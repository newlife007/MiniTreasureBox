"""Export an independently trained three-class model to ONNX."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
from typing import Callable


def export_model(model, sample, output: Path,
                 exporter: Callable[[object, object, Path], None] | None = None) -> str:
    output.parent.mkdir(parents=True, exist_ok=True)
    if exporter is None:
        import torch

        def exporter(model_object, input_sample, destination):
            torch.onnx.export(
                model_object, input_sample, destination, opset_version=17,
                input_names=["image"], output_names=["class_logits"],
                dynamic_axes={"image": {2: "height", 3: "width"},
                              "class_logits": {2: "height", 3: "width"}},
            )
    exporter(model, sample, output)
    if not output.is_file() or output.stat().st_size == 0:
        raise ValueError("ONNX exporter did not create a model")
    return hashlib.sha256(output.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--checkpoint", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    arguments = parser.parse_args()
    import torch
    from ..models.segmentation import build_model

    model = build_model()
    checkpoint = torch.load(arguments.checkpoint, map_location="cpu", weights_only=True)
    model.load_state_dict(checkpoint["state_dict"])
    model.eval()
    sample = torch.zeros((1, 3, 1024, 1024), dtype=torch.float32)
    print(export_model(model, sample, arguments.output))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
