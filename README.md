# MiniTreasureBox

小小百宝箱是一款面向桌面的 AI 小工具、实用工具和轻量小游戏集合应用。

## 第一阶段功能

当前桌面应用已提供：

- Qt 6 Widgets 桌面窗口，以及首页、分类、收藏、最近使用和设置导航；“实用工具”会汇总第一阶段的全部六个本地工具，并保留具体分类入口。
- 基于统一 `ToolMetadata`/`ITool` 接口的六个离线工具：JSON 格式化与校验、Base64 编解码、URL 百分号编解码、Unix 时间戳转换、UUID 生成器和文本统计。
- 按名称、分类、描述、关键词和别名进行全局工具搜索；也可切换到本地文件模式，在选定目录内按文件名、目录名或 UTF-8 文本内容递归搜索，并直接打开结果或其所在目录。使用 `Ctrl+K`（macOS 为 `Cmd+K`）聚焦搜索，使用 `Esc` 清除当前模式的搜索或返回首页。
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

## 本地文件搜索

在首页搜索框左侧将模式从“工具搜索”切换为“本地文件”，然后选择一个本地目录作为搜索范围。搜索不会访问所选目录之外的位置。可分别启用名称搜索和文本文件内容搜索，两种方式也可同时启用。名称匹配不区分大小写，并包含嵌套的文件和目录。

文件内容搜索仅读取不超过 1 MiB、可解码为 UTF-8 且不含二进制空字节的普通文件。扫描不会跟随目录符号链接，会跳过无法读取、二进制或过大的文件内容。每次最多显示 200 条结果，递归扫描在后台线程运行，新查询会取消或取代旧查询。

每条结果可使用系统默认程序打开。目录结果会直接打开目录；文件结果既可打开文件，也可打开文件所在目录。如果结果已被移动、删除或不再是有效的本地路径，应用会显示失败提示而不会尝试启动。

## 创建安装包

安装包必须在对应的原生操作系统上构建，并确保 CMake 可以找到 Qt 6.8.3。

Windows 使用 Visual Studio 2022 和 NSIS：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
cpack --config build/CPackConfig.cmake -C Release -G NSIS
```

生成的安装程序为 `build/packages/MiniTreasureBox-0.1.2-windows-x64-unsigned-preview.exe`，其中包含应用程序和运行所需的 Qt 库及插件。安装程序是自包含的，安装后无需修改 `PATH` 或其他用户环境变量即可运行。该预览安装程序尚未进行 Authenticode 签名，因此 Windows 会将发布者显示为未知。

macOS 使用 Qt 的 `clang_64` 工具链：

```bash
cmake -S . -B build -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
cpack --config build/CPackConfig.cmake -C Release -G DragNDrop
```

生成的磁盘映像为 `build/packages/MiniTreasureBox-0.1.2-macos-unsigned-preview.dmg`，其中包含可部署的 `MiniTreasureBox.app` 和运行所需的 Qt 库及插件。该预览应用尚未进行 Developer ID 签名、公证或装订，因此 macOS Gatekeeper 可能阻止其运行。

GitHub Actions 的 **Build release packages** 工作流也可以手动运行。手动运行会保留 Windows 和 macOS 安装包各 14 天，可从该次工作流的 Artifacts 区域下载，但不会创建 GitHub Release。发布新版本时，先确保 `CMakeLists.txt` 中的项目版本正确，再推送同版本的 `v` 前缀标签，例如：

```bash
git tag v0.1.2
git push origin v0.1.2
```

工作流会从 `CMakeLists.txt` 读取项目版本；标签必须与 `v${PROJECT_VERSION}` 完全一致，否则会在打包前失败。有效的 `v*` 标签会触发两个原生平台的构建和测试，并对生成的 `.exe` 与 `.dmg` 执行安装、启动和移除冒烟测试。全部验证通过后，工作流会将两个文件发布到该标签对应、明确标记为 **unsigned preview** 的预发布 GitHub Release。当前安装包不适合作为经过生产签名的正式发布物；待配置受保护的签名凭据后，才应移除该标记。

## 项目文档

- [产品文档](docs/product/product-requirements.md)
- [技术文档](docs/technical/technical-design.md)

当前 AI 方向聚焦 OCR、图像识别、文档处理、图片增强和试卷去手写等传统 AI 小工具，暂不考虑 Agent 能力。技术方案采用 Qt 6、C++ 和 CMake，AI 推理逐步从 Python/PyTorch Worker 迁移到 ONNX Runtime C++。
