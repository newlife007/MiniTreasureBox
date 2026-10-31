import hashlib
import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
WORKER_PATH = ROOT / "scripts" / "exam_restoration_worker.py"
SPEC = importlib.util.spec_from_file_location("exam_restoration_worker", WORKER_PATH)
worker = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(worker)


class ExamRestorationWorkerTest(unittest.TestCase):
    @staticmethod
    def _tiles(width, height, tile_size, overlap):
        tiles = []
        for x, y, tile_width, tile_height in worker.tile_coordinates(
            width, height, tile_size, overlap
        ):
            values = []
            for local_y in range(tile_height):
                row = []
                for local_x in range(tile_width):
                    global_x, global_y = x + local_x, y + local_y
                    selected = (global_x + 2 * global_y) % 3
                    probability = [0.05, 0.05, 0.05]
                    probability[selected] = 0.90
                    row.append(probability)
                values.append(row)
            tiles.append((x, y, values))
        return tiles

    def test_tile_boundaries_seams_and_order_are_deterministic(self):
        self.assertEqual(worker.tile_origins(1024), [0])
        self.assertEqual(worker.tile_origins(1025), [0, 1])
        self.assertEqual(worker.tile_origins(2176), [0, 896, 1152])
        for width, height in ((7, 5), (8, 8), (13, 9)):
            with self.subTest(size=(width, height)):
                tiles = self._tiles(width, height, 6, 2)
                forward = worker.fuse_probability_tiles(width, height, tiles, overlap=2)
                reverse = worker.fuse_probability_tiles(
                    width, height, reversed(tiles), overlap=2
                )
                self.assertEqual(forward, reverse)
                forward_hash = hashlib.sha256(
                    json.dumps(forward, separators=(",", ":")).encode()
                ).hexdigest()
                reverse_hash = hashlib.sha256(
                    json.dumps(reverse, separators=(",", ":")).encode()
                ).hexdigest()
                self.assertEqual(forward_hash, reverse_hash)

    def test_three_class_mapping_and_conservative_mask(self):
        classes = [
            [worker.CLASS_BACKGROUND] * 6,
            [worker.CLASS_BACKGROUND, worker.CLASS_HANDWRITING,
             worker.CLASS_HANDWRITING, worker.CLASS_PRINT,
             worker.CLASS_BACKGROUND, worker.CLASS_HANDWRITING],
            [worker.CLASS_BACKGROUND] * 6,
        ]
        mask = worker.conservative_mask(classes, minimum_component=2, dilation_radius=1)
        self.assertEqual(mask[1][1], 1)
        self.assertEqual(mask[1][2], 1)
        self.assertEqual(mask[1][3], 0, "print pixels must never be accepted")
        self.assertEqual(mask[1][5], 0, "isolated handwriting noise must be removed")

    def test_hard_composite_preserves_every_pixel_outside_mask(self):
        corrected = [[bytes((x, y, 17, 255)) for x in range(5)] for y in range(4)]
        candidate = [[bytes((255, 255, 255, 255)) for _ in range(5)] for _ in range(4)]
        mask = [[1 if (x, y) in {(1, 1), (2, 2)} else 0 for x in range(5)]
                for y in range(4)]
        restored = worker.hard_composite(corrected, candidate, mask)
        for y in range(4):
            for x in range(5):
                if not mask[y][x]:
                    self.assertEqual(restored[y][x], corrected[y][x])

    def test_missing_packaged_model_returns_versioned_safe_error(self):
        with tempfile.TemporaryDirectory() as temporary:
            completed = subprocess.run(
                [sys.executable, str(WORKER_PATH), "--models", temporary, "--self-test"],
                check=False, capture_output=True, text=True, timeout=10,
            )
        payload = json.loads(completed.stdout)
        self.assertFalse(payload["ok"])
        self.assertEqual(payload["schema_version"], 1)
        self.assertEqual(payload["error"], "model_missing")
        self.assertNotIn(temporary, completed.stdout)


if __name__ == "__main__":
    unittest.main()
