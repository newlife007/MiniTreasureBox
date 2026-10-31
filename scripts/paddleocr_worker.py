#!/usr/bin/env python3
"""Offline PaddleOCR worker used by the desktop application.

A duplicated stdout descriptor is reserved for one UTF-8 JSON response. All
Python and native dependency output is discarded so it cannot block or corrupt
the protocol channel.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--models", required=True, type=Path)
    parser.add_argument("--image", type=Path)
    parser.add_argument("--language", choices=("ch", "en"), default="ch")
    parser.add_argument("--self-test", action="store_true")
    return parser.parse_args()


def reserve_protocol_channel() -> int:
    sys.stdout.flush()
    return os.dup(sys.stdout.fileno())


def silence_dependency_output() -> None:
    """Redirect process stdout/stderr to the null device for native libraries."""
    sys.stdout.flush()
    sys.stderr.flush()
    null_fd = os.open(os.devnull, os.O_WRONLY)
    try:
        os.dup2(null_fd, sys.stdout.fileno())
        os.dup2(null_fd, sys.stderr.fileno())
    finally:
        os.close(null_fd)


def emit(protocol_fd: int, payload: dict[str, object]) -> int:
    encoded = (json.dumps(payload, ensure_ascii=False, separators=(",", ":")) + "\n").encode("utf-8")
    remaining = memoryview(encoded)
    try:
        while remaining:
            written = os.write(protocol_fd, remaining)
            if written <= 0:
                raise OSError("Unable to write the PaddleOCR protocol response.")
            remaining = remaining[written:]
    finally:
        os.close(protocol_fd)
    return 0 if payload.get("ok") else 1


def model_paths(root: Path) -> tuple[Path, Path]:
    return (
        root / "PP-OCRv5_mobile_det_onnx",
        root / "PP-OCRv5_mobile_rec_onnx",
    )


def validate_models(root: Path) -> tuple[Path, Path]:
    detection, recognition = model_paths(root)
    for model in (detection, recognition):
        if not (model / "inference.onnx").is_file() or not (model / "inference.yml").is_file():
            raise FileNotFoundError(f"Incomplete PaddleOCR model directory: {model}")
    return detection, recognition


def create_pipeline(models: Path):
    detection, recognition = validate_models(models)
    os.environ["PADDLE_PDX_DISABLE_MODEL_SOURCE_CHECK"] = "True"
    os.environ["PADDLE_PDX_MODEL_SOURCE"] = "bos"
    from paddleocr import PaddleOCR

    return PaddleOCR(
        text_detection_model_name="PP-OCRv5_mobile_det",
        text_detection_model_dir=str(detection),
        text_recognition_model_name="PP-OCRv5_mobile_rec",
        text_recognition_model_dir=str(recognition),
        use_doc_orientation_classify=False,
        use_doc_unwarping=False,
        use_textline_orientation=False,
        engine="onnxruntime",
        device="cpu",
    )


def recognize(pipeline, image: Path) -> tuple[str, list[float]]:
    results = list(pipeline.predict(str(image)))
    lines: list[str] = []
    scores: list[float] = []
    for result in results:
        payload = result.json.get("res", {})
        texts = payload.get("rec_texts", [])
        confidences = payload.get("rec_scores", [])
        for index, text in enumerate(texts):
            normalized = str(text).strip()
            if not normalized:
                continue
            lines.append(normalized)
            if index < len(confidences):
                scores.append(float(confidences[index]))
    return "\n".join(lines), scores


def run(arguments: argparse.Namespace) -> dict[str, object]:
    try:
        models = arguments.models.resolve(strict=True)
        pipeline = create_pipeline(models)
        if arguments.self_test:
            return {
                "ok": True,
                "backend": "PaddleOCR",
                "engine": "onnxruntime",
                "models": [path.name for path in model_paths(models)],
            }
        if arguments.image is None or not arguments.image.is_file():
            return {"ok": False, "error": "invalid_input", "message": "Image is unavailable."}
        text, scores = recognize(pipeline, arguments.image.resolve())
        if not text:
            return {"ok": False, "error": "empty_output", "message": "No text was detected."}
        confidence = sum(scores) / len(scores) if scores else None
        return {"ok": True, "text": text, "confidence": confidence}
    except FileNotFoundError as error:
        return {"ok": False, "error": "model_missing", "message": str(error)}
    except Exception:
        return {"ok": False, "error": "process_failed", "message": "PaddleOCR processing failed."}


def main() -> int:
    arguments = parse_arguments()
    protocol_fd = reserve_protocol_channel()
    silence_dependency_output()
    return emit(protocol_fd, run(arguments))


if __name__ == "__main__":
    os._exit(main())
