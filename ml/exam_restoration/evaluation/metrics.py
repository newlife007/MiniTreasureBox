"""Dependency-free three-class segmentation metrics."""

from __future__ import annotations


def confusion_matrix(expected: list[int], predicted: list[int], classes: int = 3) -> list[list[int]]:
    if len(expected) != len(predicted) or classes <= 0:
        raise ValueError("Metric inputs are invalid")
    matrix = [[0] * classes for _ in range(classes)]
    for truth, prediction in zip(expected, predicted):
        if not 0 <= truth < classes or not 0 <= prediction < classes:
            raise ValueError("Class index is outside the configured range")
        matrix[truth][prediction] += 1
    return matrix


def class_recall(matrix: list[list[int]], class_index: int) -> float:
    denominator = sum(matrix[class_index])
    return matrix[class_index][class_index] / denominator if denominator else 0.0


def print_false_deletion(matrix: list[list[int]]) -> float:
    print_total = sum(matrix[2])
    return matrix[2][1] / print_total if print_total else 0.0


def outside_mask_equal(corrected: bytes, restored: bytes, mask: bytes,
                       channels: int = 4) -> bool:
    if len(corrected) != len(restored) or len(corrected) != len(mask) * channels:
        raise ValueError("Image and Mask sizes differ")
    return all(mask[index] or corrected[index * channels:(index + 1) * channels]
               == restored[index * channels:(index + 1) * channels]
               for index in range(len(mask)))
