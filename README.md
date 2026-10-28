# MiniTreasureBox

小小百宝箱是一款面向桌面的 AI 小工具、实用工具和轻量小游戏集合应用。

## 项目文档

- [产品文档](docs/product/product-requirements.md)
- [技术文档](docs/technical/technical-design.md)

当前 AI 方向聚焦 OCR、图像识别、文档处理、图片增强和试卷去手写等传统 AI 小工具，暂不考虑 Agent 能力。技术方案采用 Qt 6、C++ 和 CMake，AI 推理逐步从 Python/PyTorch Worker 迁移到 ONNX Runtime C++。
