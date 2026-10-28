# MiniTreasureBox

小小百宝箱是一款面向桌面的 AI 小工具、实用工具和轻量小游戏集合应用。

## 第一阶段功能

当前桌面应用已提供：

- Qt 6 Widgets 桌面窗口，以及首页、分类、收藏、最近使用和设置导航。
- 基于统一 `ToolMetadata`/`ITool` 接口的六个离线工具：JSON 格式化与校验、Base64 编解码、URL 百分号编解码、Unix 时间戳转换、UUID 生成器和文本统计。
- 按名称、分类、描述、关键词和别名进行全局搜索；使用 `Ctrl+K`（macOS 为 `Cmd+K`）聚焦搜索，使用 `Esc` 清除搜索或返回首页。
- 使用 QSettings 持久化收藏、最近使用、主题和窗口布局。
- 可即时切换并保存跟随系统、浅色和深色主题。
- 所有工具完全在本地运行，不需要网络服务，也不会记录工具输入内容。

## 构建要求

- CMake 3.22 或更高版本
- 支持 C++20 的编译器
- Qt 6 的 Core、Gui、Widgets 和 Test 组件

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

## 项目文档

- [产品文档](docs/product/product-requirements.md)
- [技术文档](docs/technical/technical-design.md)

当前 AI 方向聚焦 OCR、图像识别、文档处理、图片增强和试卷去手写等传统 AI 小工具，暂不考虑 Agent 能力。技术方案采用 Qt 6、C++ 和 CMake，AI 推理逐步从 Python/PyTorch Worker 迁移到 ONNX Runtime C++。
