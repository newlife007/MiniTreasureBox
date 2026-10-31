import datetime as dt
import hashlib
import json
import tempfile
import unittest
from pathlib import Path

from ml.exam_restoration.datasets.manifest import grouped_split, validate_manifest
from ml.exam_restoration.export.export_onnx import export_model
from ml.exam_restoration.export.verify_onnx import verify_model
from ml.exam_restoration.models.segmentation import ReferenceShapeModel


class FakeOutput:
    shape = (1, 3, 2, 2)

    def argmax(self, axis):
        if axis != 1:
            raise AssertionError("unexpected axis")
        return FakeComparison()


class FakeComparison:
    def __eq__(self, other):
        return self

    def all(self):
        return True


class FakeSession:
    class Input:
        name = "image"

    def get_inputs(self):
        return [self.Input()]

    def run(self, outputs, inputs):
        return [FakeOutput()]


class MlExamRestorationTest(unittest.TestCase):
    def _record(self, root: Path, identifier: str, template: str):
        image = root / f"{identifier}.image"
        label = root / f"{identifier}.label"
        image.write_bytes(f"generated-{identifier}".encode())
        label.write_bytes(b"generated-label")
        return {
            "id": identifier,
            "template_id": template,
            "image": image.name,
            "label": label.name,
            "source_owner": "generated fixture",
            "license_evidence": "test-only generated fixture",
            "permitted_uses": ["training", "testing"],
            "retention_until": "2999-12-31",
            "withdrawn": False,
            "sha256": hashlib.sha256(image.read_bytes()).hexdigest(),
        }

    def test_license_validation_and_template_isolated_split(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            records = [self._record(root, f"page-{index}", f"template-{index // 2}")
                       for index in range(12)]
            validated = validate_manifest(
                {"version": 1, "records": records}, root, dt.date(2026, 1, 1)
            )
            first = grouped_split(validated, seed=26)
            self.assertEqual(first, grouped_split(list(reversed(validated)), seed=26))
            assignments = {}
            for partition, identifiers in first.items():
                for identifier in identifiers:
                    template = next(record["template_id"] for record in records
                                    if record["id"] == identifier)
                    assignments.setdefault(template, set()).add(partition)
            self.assertTrue(all(len(partitions) == 1 for partitions in assignments.values()))

            records[0]["withdrawn"] = True
            with self.assertRaisesRegex(ValueError, "Withdrawn"):
                validate_manifest({"version": 1, "records": records}, root)

    def test_generated_model_fixture_has_three_class_output(self):
        sample = [[[[0.0] * 5 for _ in range(4)] for _ in range(3)] for _ in range(2)]
        output = ReferenceShapeModel()(sample)
        self.assertEqual(len(output), 2)
        self.assertEqual(len(output[0]), 3)
        self.assertEqual(len(output[0][0]), 4)
        self.assertEqual(len(output[0][0][0]), 5)

    def test_export_and_runtime_verification_hooks_use_generated_fixture(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "generated.onnx"
            digest = export_model(
                object(), object(), output,
                exporter=lambda model, sample, path: path.write_bytes(b"generated-onnx-fixture"),
            )
            self.assertEqual(digest, hashlib.sha256(output.read_bytes()).hexdigest())
            self.assertTrue(verify_model(
                output, "generated-input", FakeComparison(),
                session_factory=lambda path: FakeSession(),
            ))


if __name__ == "__main__":
    unittest.main()
