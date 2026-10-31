# MiniTreasureBox

小小百宝箱是一款面向桌面的 AI 小工具、实用工具和轻量小游戏集合应用。

## 构建要求

- CMake 3.22 或更高版本
- 支持 C++20 的编译器
- Qt 6 的 Core、Concurrent、Gui、Widgets、Multimedia 和 Test 组件
- Python 3.10 或更高版本，用于发布打包脚本和对应的回归测试

## 配置、构建与测试

Qt 安装在标准系统路径时：

```bash
cmake -S . -B build -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

本开发环境中的 Qt 6.8.3 需要显式提供安装前缀：

```bash
cmake -S . -B build -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH='/projects/sandbox/Qt/6.8.3/gcc_64;/projects/sandbox/.qt-sysroot/usr'
cmake --build build --parallel
LD_LIBRARY_PATH=/projects/sandbox/Qt/6.8.3/gcc_64/lib:/projects/sandbox/.qt-sysroot/usr/lib64 \
  ctest --test-dir build --output-on-failure
```

CMake 会为需要创建 Qt 窗口的测试自动设置 `QT_QPA_PLATFORM=offscreen`，因此上述 CTest 命令可直接在无显示服务器的环境中运行。

## 运行

标准 Qt 安装环境：

```bash
./build/MiniTreasureBox
```

本开发环境：

```bash
LD_LIBRARY_PATH=/projects/sandbox/Qt/6.8.3/gcc_64/lib:/projects/sandbox/.qt-sysroot/usr/lib64 \
  ./build/MiniTreasureBox
```

## OCR 运行时

OCR 使用 PaddleOCR 3.7、PaddleX OCR Core 和 ONNX Runtime，随发布安装包内置 PP-OCRv5 Mobile 检测及中英文识别模型。应用不会调用系统 Python，也不会在安装后下载模型。

配置 `MINITREASUREBOX_OCR_RUNTIME_ROOT` 后，普通构建会把已验证的运行时部署到构建目录的 `ocr-runtime`，因此可以直接启动构建产物测试 OCR。可用 `-DMINITREASUREBOX_STAGE_BUNDLED_OCR_FOR_BUILD=OFF` 关闭该部署。未配置内置运行时时，也可以通过以下环境变量使用本地 Worker：

```text
MINITREASUREBOX_PADDLEOCR_WORKER=<paddleocr-worker 可执行文件>
MINITREASUREBOX_PADDLEOCR_MODELS=<包含两个 PP-OCRv5 模型目录的路径>
```

发布构建先在目标平台生成独立运行时：

```bash
python scripts/build_paddleocr_runtime.py \
  --output build/ocr-runtime-staged \
  --work build/ocr-runtime-build
cmake -S . -B build-package \
  -DMINITREASUREBOX_REQUIRE_BUNDLED_OCR=ON \
  -DMINITREASUREBOX_OCR_RUNTIME_ROOT="$PWD/build/ocr-runtime-staged"
cmake --build build-package --config Release --parallel
```

构建脚本会固定 Python 依赖版本、对官方模型归档执行 SHA-256 校验、运行 PaddleOCR Worker 自检，并收集第三方许可证清单。Windows、macOS Intel 和 Apple Silicon 安装包均通过原生 CI 构建 Worker。

## 项目文档

- [产品文档](docs/product/product-requirements.md)
- [技术文档](docs/technical/technical-design.md)
- [试卷照片自动还原方案](docs/technical/exam-restoration-solution.md)
- [整体 UI 设计方案](docs/design/ui-design-plan.md)
- [非游戏小工具 UI 设计规范](docs/design/tool-ui-style-guide.md)
- [小游戏 UI 设计规范](docs/design/game-ui-style-guide.md)

技术方案采用 Qt 6、C++ 和 CMake，AI 推理基于 ONNX Runtime C++。

## 许可协议 / License

本项目采用 [PolyForm Noncommercial License 1.0.0](LICENSE) 协议，**仅限非商业用途**。

✅ **允许**：

-   个人学习、研究、实验、测试
    
-   个人兴趣项目、业余爱好
    
-   慈善组织、教育机构、公共科研机构、政府机构使用
    
-   在非商业目的下修改、二次开发和分发（需保留本协议及版权声明）
    

❌ **禁止**：

-   任何商业用途，包括但不限于：用于商业产品、对外提供收费服务、企业内部营利性使用
    

📩 **商业授权**：如需商业使用，请联系 [544121799@qq.com](mailto:544121799@qq.com) 获取单独授权。

> This project is licensed under the PolyForm Noncommercial License 1.0.0. Commercial use requires a separate license — contact [544121799@qq.com](mailto:544121799@qq.com).

Copyright © 2026 [544121799@qq.com](mailto:544121799@qq.com)