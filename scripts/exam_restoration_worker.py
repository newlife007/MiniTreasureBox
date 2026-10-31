#!/usr/bin/env python3
"""Offline three-class exam restoration worker.

The worker never downloads models. It accepts only the fixed ONNX file under
its supplied packaged model root and reserves stdout for one bounded JSON
response.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import sys
from pathlib import Path
from typing import Iterable, Sequence

SCHEMA_VERSION = 1
MODEL_FILENAME = "exam-restoration.onnx"
TILE_SIZE = 1024
TILE_OVERLAP = 128
MAX_PIXELS = 40_000_000
CLASS_BACKGROUND = 0
CLASS_HANDWRITING = 1
CLASS_PRINT = 2
PROBABILITY_SCALE = 65_535


def tile_origins(length: int, tile_size: int = TILE_SIZE,
                 overlap: int = TILE_OVERLAP) -> list[int]:
    """Return fixed-stride origins, leaving the terminal tile to be padded."""
    if length <= 0 or tile_size <= 0 or overlap < 0 or overlap >= tile_size:
        raise ValueError("Invalid image or tile dimensions")
    if length <= tile_size:
        return [0]
    stride = tile_size - overlap
    origins = [0]
    while origins[-1] + tile_size < length:
        origins.append(origins[-1] + stride)
    return origins


def tile_coordinates(width: int, height: int, tile_size: int = TILE_SIZE,
                     overlap: int = TILE_OVERLAP,
                     reverse: bool = False) -> list[tuple[int, int, int, int]]:
    coordinates = [
        (x, y, min(tile_size, width - x), min(tile_size, height - y))
        for y in tile_origins(height, tile_size, overlap)
        for x in tile_origins(width, tile_size, overlap)
    ]
    return list(reversed(coordinates)) if reverse else coordinates


def _axis_weight(index: int, length: int, overlap: int,
                 touches_start: bool = False, touches_end: bool = False) -> int:
    if overlap <= 0:
        return 1
    start_weight = overlap + 1 if touches_start else min(index + 1, overlap + 1)
    end_weight = overlap + 1 if touches_end else min(length - index, overlap + 1)
    return min(start_weight, end_weight)


def fuse_probability_tiles(
    width: int,
    height: int,
    tiles: Iterable[tuple[int, int, Sequence[Sequence[Sequence[float]]]]],
    overlap: int = TILE_OVERLAP,
) -> tuple[list[list[list[float]]], list[list[int]]]:
    """Fuse small fixture tiles with exact order-independent integer sums."""
    if width <= 0 or height <= 0:
        raise ValueError("Invalid output dimensions")
    sums = [[[[0, 0, 0], 0] for _ in range(width)] for _ in range(height)]
    for origin_x, origin_y, probabilities in tiles:
        tile_height = len(probabilities)
        tile_width = len(probabilities[0]) if tile_height else 0
        if tile_width <= 0 or origin_x < 0 or origin_y < 0:
            raise ValueError("Invalid tile")
        for local_y, row in enumerate(probabilities):
            if len(row) != tile_width or origin_y + local_y >= height:
                raise ValueError("Tile is outside the output")
            weight_y = _axis_weight(
                local_y, tile_height, overlap,
                touches_start=origin_y == 0,
                touches_end=origin_y + tile_height == height,
            )
            for local_x, probability in enumerate(row):
                if len(probability) != 3 or origin_x + local_x >= width:
                    raise ValueError("Tile probability shape must end in three classes")
                weight = weight_y * _axis_weight(
                    local_x, tile_width, overlap,
                    touches_start=origin_x == 0,
                    touches_end=origin_x + tile_width == width,
                )
                cell = sums[origin_y + local_y][origin_x + local_x]
                for class_index in range(3):
                    value = float(probability[class_index])
                    if not math.isfinite(value) or value < 0.0 or value > 1.0:
                        raise ValueError("Invalid class probability")
                    cell[0][class_index] += int(round(value * PROBABILITY_SCALE)) * weight
                cell[1] += weight
    fused: list[list[list[float]]] = []
    classes: list[list[int]] = []
    for row in sums:
        fused_row: list[list[float]] = []
        class_row: list[int] = []
        for class_sums, total_weight in row:
            if total_weight == 0:
                raise ValueError("Tiles do not cover the output")
            values = [value / (total_weight * PROBABILITY_SCALE) for value in class_sums]
            fused_row.append(values)
            class_row.append(max(range(3), key=lambda index: (class_sums[index], -index)))
        fused.append(fused_row)
        classes.append(class_row)
    return fused, classes


def conservative_mask(classes: Sequence[Sequence[int]], minimum_component: int = 2,
                      dilation_radius: int = 1) -> list[list[int]]:
    """Keep handwriting components and never expand through predicted print."""
    height = len(classes)
    width = len(classes[0]) if height else 0
    if width == 0 or any(len(row) != width for row in classes):
        raise ValueError("Invalid class map")
    accepted = [[0] * width for _ in range(height)]
    visited: set[tuple[int, int]] = set()
    for y in range(height):
        for x in range(width):
            if classes[y][x] != CLASS_HANDWRITING or (x, y) in visited:
                continue
            component: list[tuple[int, int]] = []
            stack = [(x, y)]
            visited.add((x, y))
            while stack:
                current_x, current_y = stack.pop()
                component.append((current_x, current_y))
                for next_x, next_y in ((current_x - 1, current_y),
                                       (current_x + 1, current_y),
                                       (current_x, current_y - 1),
                                       (current_x, current_y + 1)):
                    if (0 <= next_x < width and 0 <= next_y < height
                            and classes[next_y][next_x] == CLASS_HANDWRITING
                            and (next_x, next_y) not in visited):
                        visited.add((next_x, next_y))
                        stack.append((next_x, next_y))
            if len(component) >= minimum_component:
                for accepted_x, accepted_y in component:
                    accepted[accepted_y][accepted_x] = 1
    if dilation_radius <= 0:
        return accepted
    expanded = [row[:] for row in accepted]
    selected = [(x, y) for y in range(height) for x in range(width) if accepted[y][x]]
    for x, y in selected:
        for offset_y in range(-dilation_radius, dilation_radius + 1):
            for offset_x in range(-dilation_radius, dilation_radius + 1):
                target_x, target_y = x + offset_x, y + offset_y
                if (0 <= target_x < width and 0 <= target_y < height
                        and classes[target_y][target_x] != CLASS_PRINT):
                    expanded[target_y][target_x] = 1
    return expanded


def hard_composite(corrected: Sequence[Sequence[object]],
                   candidate: Sequence[Sequence[object]],
                   mask: Sequence[Sequence[int]]) -> list[list[object]]:
    """Copy candidate values only inside the accepted Mask."""
    if len(corrected) != len(candidate) or len(corrected) != len(mask):
        raise ValueError("Composite dimensions differ")
    output: list[list[object]] = []
    for source_row, candidate_row, mask_row in zip(corrected, candidate, mask):
        if len(source_row) != len(candidate_row) or len(source_row) != len(mask_row):
            raise ValueError("Composite dimensions differ")
        output.append([
            candidate_value if selected else source_value
            for source_value, candidate_value, selected
            in zip(source_row, candidate_row, mask_row)
        ])
    return output


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--models", required=True, type=Path)
    parser.add_argument("--image", type=Path)
    parser.add_argument("--mask", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--request-id", default="")
    parser.add_argument("--tile-order", choices=("forward", "reverse"), default="forward")
    parser.add_argument("--self-test", action="store_true")
    return parser.parse_args()


def reserve_protocol_channel() -> int:
    sys.stdout.flush()
    return os.dup(sys.stdout.fileno())


def silence_dependency_output() -> None:
    sys.stdout.flush()
    sys.stderr.flush()
    null_fd = os.open(os.devnull, os.O_WRONLY)
    try:
        os.dup2(null_fd, sys.stdout.fileno())
        os.dup2(null_fd, sys.stderr.fileno())
    finally:
        os.close(null_fd)


def emit(protocol_fd: int, payload: dict[str, object]) -> int:
    encoded = (json.dumps(payload, ensure_ascii=False, separators=(",", ":")) + "\n").encode()
    response_ok = bool(payload.get("ok"))
    if len(encoded) > 64 * 1024:
        encoded = b'{"ok":false,"schema_version":1,"error":"protocol_error"}\n'
        response_ok = False
    remaining = memoryview(encoded)
    try:
        while remaining:
            written = os.write(protocol_fd, remaining)
            if written <= 0:
                raise OSError("Unable to write protocol response")
            remaining = remaining[written:]
    finally:
        os.close(protocol_fd)
    return 0 if response_ok else 1


def fixed_model_path(root: Path) -> Path:
    resolved_root = root.resolve(strict=True)
    model = (resolved_root / MODEL_FILENAME).resolve(strict=True)
    if model.parent != resolved_root or not model.is_file():
        raise FileNotFoundError("The packaged restoration model is unavailable")
    return model


def create_session(model_path: Path):
    import onnxruntime as ort

    return ort.InferenceSession(
        str(model_path), providers=["CPUExecutionProvider"],
        sess_options=ort.SessionOptions(),
    )


def _softmax(logits):
    import numpy as np

    shifted = logits - np.max(logits, axis=1, keepdims=True)
    exponent = np.exp(shifted)
    return exponent / np.sum(exponent, axis=1, keepdims=True)


def conservative_mask_array(classes, dilation_radius: int = 1):
    """Apply the production Mask filter with bounded NumPy byte arrays."""
    import numpy as np

    if classes.ndim != 2 or classes.size == 0:
        raise ValueError("Invalid class map")
    handwriting = classes == CLASS_HANDWRITING
    adjacent = np.zeros(classes.shape, dtype=np.bool_)
    adjacent[1:, :] |= handwriting[:-1, :]
    adjacent[:-1, :] |= handwriting[1:, :]
    adjacent[:, 1:] |= handwriting[:, :-1]
    adjacent[:, :-1] |= handwriting[:, 1:]
    accepted = handwriting & adjacent
    if dilation_radius <= 0:
        return accepted.astype(np.uint8)
    if dilation_radius != 1:
        raise ValueError("Production dilation radius must be one")
    expanded = accepted.copy()
    printable = classes != CLASS_PRINT
    height, width = classes.shape
    for offset_y in range(-1, 2):
        source_y = slice(max(0, -offset_y), min(height, height - offset_y))
        target_y = slice(max(0, offset_y), min(height, height + offset_y))
        for offset_x in range(-1, 2):
            source_x = slice(max(0, -offset_x), min(width, width - offset_x))
            target_x = slice(max(0, offset_x), min(width, width + offset_x))
            expanded[target_y, target_x] |= (
                accepted[source_y, source_x] & printable[target_y, target_x]
            )
    return expanded.astype(np.uint8)


def fusion_accumulator_bytes(width: int, height: int) -> int:
    """Return the fixed uint32 fusion allocation, excluding model tile tensors."""
    if width <= 0 or height <= 0 or width > MAX_PIXELS // height:
        raise ValueError("Invalid image dimensions")
    return width * height * 4 * 4


def infer_page(session, rgb, reverse: bool = False):
    """Infer classes with exact uint32 fusion and a 16-byte-per-pixel accumulator."""
    import numpy as np

    height, width, channels = rgb.shape
    if channels != 3:
        raise ValueError("Expected RGB input")
    sums = np.zeros((height, width, 3), dtype=np.uint32)
    weights = np.zeros((height, width), dtype=np.uint32)
    input_name = session.get_inputs()[0].name
    coordinates = tile_coordinates(width, height, reverse=reverse)
    for origin_x, origin_y, tile_width, tile_height in coordinates:
        tile = rgb[origin_y:origin_y + tile_height, origin_x:origin_x + tile_width]
        padded = np.pad(
            tile, ((0, TILE_SIZE - tile_height), (0, TILE_SIZE - tile_width), (0, 0)),
            mode="edge",
        )
        tensor = ((padded.astype(np.float32) / 255.0) - 0.5) / 0.5
        tensor = np.transpose(tensor, (2, 0, 1))[None, ...]
        logits = session.run(None, {input_name: tensor})[0]
        if logits.ndim != 4 or logits.shape[1] != 3:
            raise ValueError("Model output must be NCHW with three classes")
        probability = _softmax(logits)[0, :, :tile_height, :tile_width]
        quantized = np.rint(probability * PROBABILITY_SCALE).astype(np.uint32)
        axis_x = np.array([
            _axis_weight(
                x, tile_width, TILE_OVERLAP,
                touches_start=origin_x == 0,
                touches_end=origin_x + tile_width == width,
            ) for x in range(tile_width)
        ], dtype=np.uint32)
        axis_y = np.array([
            _axis_weight(
                y, tile_height, TILE_OVERLAP,
                touches_start=origin_y == 0,
                touches_end=origin_y + tile_height == height,
            ) for y in range(tile_height)
        ], dtype=np.uint32)
        tile_weights = axis_y[:, None] * axis_x[None, :]
        target = sums[origin_y:origin_y + tile_height, origin_x:origin_x + tile_width]
        target += np.transpose(quantized, (1, 2, 0)) * tile_weights[:, :, None]
        weights[origin_y:origin_y + tile_height, origin_x:origin_x + tile_width] += tile_weights
    if np.any(weights == 0):
        raise ValueError("Tiles do not cover the output")
    classes = (sums[:, :, CLASS_HANDWRITING]
               > sums[:, :, CLASS_BACKGROUND]).astype(np.uint8)
    np.maximum(sums[:, :, CLASS_BACKGROUND], sums[:, :, CLASS_HANDWRITING],
               out=weights)
    classes[sums[:, :, CLASS_PRINT] > weights] = CLASS_PRINT
    return classes


def process(arguments: argparse.Namespace) -> dict[str, object]:
    try:
        model = fixed_model_path(arguments.models)
        session = create_session(model)
        if arguments.self_test:
            outputs = session.get_outputs()
            if not outputs:
                raise ValueError("The model has no output")
            return {"ok": True, "schema_version": SCHEMA_VERSION,
                    "backend": "onnxruntime", "model": MODEL_FILENAME}
        if not arguments.request_id or not all((arguments.image, arguments.mask, arguments.output)):
            return {"ok": False, "schema_version": SCHEMA_VERSION,
                    "request_id": arguments.request_id, "error": "invalid_input"}
        from PIL import Image
        import numpy as np

        with Image.open(arguments.image) as opened:
            width, height = opened.size
            if width <= 0 or height <= 0 or width > MAX_PIXELS // height:
                return {"ok": False, "schema_version": SCHEMA_VERSION,
                        "request_id": arguments.request_id, "error": "resource_limit"}
            rgb = np.array(opened.convert("RGB"), dtype=np.uint8, copy=True)
        classes = infer_page(session, rgb, arguments.tile_order == "reverse")
        mask = conservative_mask_array(classes)
        del classes
        sample = rgb[::8, ::8]
        sample_brightness = sample.astype(np.uint16).sum(axis=2)
        light_pixels = sample[(mask[::8, ::8] == 0) & (sample_brightness >= 3 * 170)]
        fill = (np.median(light_pixels, axis=0).astype(np.uint8)
                if light_pixels.size else np.array([255, 255, 255], dtype=np.uint8))
        restored = rgb.copy()
        restored[mask != 0] = fill
        Image.fromarray(mask * 255, mode="L").save(arguments.mask, format="PNG")
        Image.fromarray(restored, mode="RGB").save(arguments.output, format="PNG")
        ratio = float(np.count_nonzero(mask)) / float(width * height)
        confidence = "low" if ratio > 0.20 else ("medium" if ratio > 0.10 else "high")
        reasons = ["accepted Mask exceeds 20% of the page"] if ratio > 0.20 else []
        return {"ok": True, "schema_version": SCHEMA_VERSION,
                "request_id": arguments.request_id, "confidence": confidence,
                "confidence_reasons": reasons, "model_version": "manifest-defined"}
    except FileNotFoundError:
        return {"ok": False, "schema_version": SCHEMA_VERSION,
                "request_id": arguments.request_id, "error": "model_missing"}
    except Exception:
        return {"ok": False, "schema_version": SCHEMA_VERSION,
                "request_id": arguments.request_id, "error": "processing_failed"}


def main() -> int:
    arguments = parse_arguments()
    protocol_fd = reserve_protocol_channel()
    silence_dependency_output()
    return emit(protocol_fd, process(arguments))


if __name__ == "__main__":
    os._exit(main())
