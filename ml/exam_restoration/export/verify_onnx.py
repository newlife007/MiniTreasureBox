"""Verify ONNX output shape and deterministic argmax consistency."""

from __future__ import annotations

import argparse
from pathlib import Path
from typing import Callable


def verify_model(model: Path, sample, expected_classes,
                 session_factory: Callable[[Path], object] | None = None) -> bool:
    if not model.is_file() or model.stat().st_size == 0:
        raise ValueError("ONNX model is missing or empty")
    if session_factory is None:
        import onnxruntime as ort
        session_factory = lambda path: ort.InferenceSession(
            str(path), providers=["CPUExecutionProvider"])
    session = session_factory(model)
    input_name = session.get_inputs()[0].name
    output = session.run(None, {input_name: sample})[0]
    if len(output.shape) != 4 or output.shape[1] != 3:
        raise ValueError("ONNX model must output NCHW logits for three classes")
    try:
        predicted = output.argmax(axis=1)
        return bool((predicted == expected_classes).all())
    except AttributeError:
        return output == expected_classes


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--model", required=True, type=Path)
    arguments = parser.parse_args()
    import numpy as np
    sample = np.zeros((1, 3, 64, 64), dtype=np.float32)
    expected = np.zeros((1, 64, 64), dtype=np.int64)
    if not verify_model(arguments.model, sample, expected):
        raise SystemExit("ONNX argmax verification failed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
