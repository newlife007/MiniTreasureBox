# Independent exam-restoration training workspace

This workspace is used only if the approved H4 decision is `retrain`. It implements the background (`0`), handwriting (`1`), and print (`2`) task independently. It does not contain or fetch HandWritingEraser data, checkpoints, generated weights, or implementation artifacts.

## Data gate

Place private data outside Git and create a JSON manifest that follows `datasets/manifest.schema.json`. Every record must identify its owner, consent or license evidence, permitted uses, retention date, withdrawal state, template group, and SHA-256. `training` must be explicitly permitted. The loader rejects incomplete, withdrawn, expired, or incorrectly hashed records. Splits are deterministic and keep each template in exactly one partition.

## Entry points

- `python -m ml.exam_restoration.datasets.manifest MANIFEST --seed 26`
- `python -m ml.exam_restoration.training.train_segmentation --manifest MANIFEST --output OUTPUT`
- `python -m ml.exam_restoration.evaluation.evaluate --predictions PREDICTIONS`
- `python -m ml.exam_restoration.export.export_onnx --checkpoint CHECKPOINT --output MODEL.onnx`
- `python -m ml.exam_restoration.export.verify_onnx --model MODEL.onnx`

Training and export require an explicitly provisioned PyTorch/ONNX Runtime environment. Exported ONNX files are ignored and cannot enter a release until the legal, quality, CPU, package-size, and ONNX consistency gates in `docs/technical/exam-restoration-solution.md` pass. The dependency-free reference model exists only to test the three-class shape contract with generated fixtures. It is not a production model.
