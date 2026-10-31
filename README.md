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

## 试卷还原运行时

导入或拖入受支持的试卷照片后，工具会自动开始处理并打开结果。仓库已实现版本化本地 Worker 协议、确定性全分辨率分块融合、40 MP 上限、取消和超时、Mask 外像素硬拷贝、基础模式回退、可选 Mask 编辑及 PNG 导出。没有获批模型时，界面明确显示“基础模式”，不会把规则算法描述为 AI 模型结果。

生产模型发布默认关闭。`packaging/exam-restoration-models.json` 当前没有制品，因为 HandWritingEraser 外链 checkpoint 尚无可独立核验的不可变来源、实际架构、精确字节数、SHA-256、许可证文本和再分发批准，H0 至 H4 的质量及三平台证据也尚未建立。该 checkpoint 不会被下载、镜像或打包。

`scripts/build_exam_restoration_runtime.py` 只接受完整获批的单模型清单。脚本会在下载前检查不可变 HTTPS 来源、精确字节数、SHA-256、用途、许可证和再分发批准，随后固定 Python 依赖、生成 PyInstaller Worker、收集许可证并执行自检。可以检查当前门禁状态：

```bash
python scripts/build_exam_restoration_runtime.py --manifest-status
```

输出 `disabled` 表示发布继续使用已测试的基础模式。只有清单加入完整获批制品后，Windows x64、macOS Intel 和 macOS Apple Silicon 发布任务才会生成、安装、自检并离线解析独立的 `exam-restoration-runtime`。启用的本地发布构建必须显式要求完整运行时：

```bash
python scripts/build_exam_restoration_runtime.py \
  --output build/exam-restoration-runtime-staged \
  --work build/exam-restoration-runtime-build
cmake -S . -B build-package \
  -DREQUIRE_BUNDLED_EXAM_RESTORATION=ON \
  -DMINITREASUREBOX_EXAM_RESTORATION_RUNTIME_ROOT="$PWD/build/exam-restoration-runtime-staged"
```

缺少 Worker、模型、哈希匹配的自检元数据或许可证时，启用的 CMake 配置会失败。应用不会回退到系统 Python，也不会在运行时下载模型。

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