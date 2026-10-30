# Third-party notices

MiniTreasureBox release packages include a local Tesseract OCR runtime and the `eng` and `chi_sim` language models. The application does not download OCR programs or language data at runtime.

## Tesseract OCR 5.5.3

- Project: [tesseract-ocr/tesseract](https://github.com/tesseract-ocr/tesseract)
- License: Apache License 2.0
- Upstream source archive SHA-256: `9218e62793116d42a9f6d14cd9348518b27f382096eea3d0f2d1a24616bb5884`
- License text: [`licenses/Apache-2.0.txt`](licenses/Apache-2.0.txt)

## tessdata_fast 4.1.0

- Project: [tesseract-ocr/tessdata_fast](https://github.com/tesseract-ocr/tessdata_fast)
- Included models: Simplified Chinese (`chi_sim`) and English (`eng`)
- License: Apache License 2.0
- Upstream source archive SHA-256: `d0e3bb6f3b4e75748680524a1d116f2bfb145618f8ceed55b279d15098a530f9`
- License text: [`licenses/Apache-2.0.txt`](licenses/Apache-2.0.txt)

## Native release runtime libraries

The Windows and macOS OCR environments are created from the exact conda-forge package URLs and MD5 integrity hashes checked into `packaging/ocr-runtime-win-64.lock`, `packaging/ocr-runtime-osx-64.lock`, and `packaging/ocr-runtime-osx-arm64.lock`. The staging script copies each package's supplied license files into `ocr-runtime/licenses` and generates `ocr-runtime/licenses/THIRD_PARTY_MANIFEST.md` with the exact package version, build, declared license, and source URL. The package locks intentionally select the LGPL libarchive variant.
