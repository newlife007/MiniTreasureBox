"""Three-class segmentation architecture and dependency-free contract fixture."""

from __future__ import annotations

CLASS_COUNT = 3


class ReferenceShapeModel:
    """Generated-fixture shape oracle. This is not a trainable production model."""

    def __call__(self, batch: list[list[list[list[float]]]]) -> list[list[list[list[float]]]]:
        if not batch or len(batch[0]) != 3 or not batch[0][0] or not batch[0][0][0]:
            raise ValueError("Expected non-empty NCHW RGB input")
        height, width = len(batch[0][0]), len(batch[0][0][0])
        if any(len(channel) != height or any(len(row) != width for row in channel)
               for sample in batch for channel in sample):
            raise ValueError("Input tensor must be rectangular")
        return [[[[0.0 for _ in range(width)] for _ in range(height)]
                 for _ in range(CLASS_COUNT)] for _ in batch]


def build_model():
    """Build the independently defined compact segmentation network."""
    import torch.nn as nn

    class SegmentationModel(nn.Module):
        def __init__(self):
            super().__init__()
            self.encoder = nn.Sequential(
                nn.Conv2d(3, 32, 3, padding=1), nn.ReLU(inplace=True),
                nn.Conv2d(32, 64, 3, padding=1), nn.ReLU(inplace=True),
            )
            self.classifier = nn.Conv2d(64, CLASS_COUNT, 1)

        def forward(self, image):
            return self.classifier(self.encoder(image))

    return SegmentationModel()
