# MiniTreasureBox

小小百宝箱是一款面向桌面的 AI 小工具、实用工具和轻量小游戏集合应用。

## 第一阶段功能

当前桌面应用已提供：

- Qt 6 Widgets 桌面窗口，以及首页、分类、收藏、最近使用和设置导航；“实用工具”会汇总第一阶段的全部六个本地工具，并保留具体分类入口。
- 基于统一 `ToolMetadata`/`ITool` 接口的六个离线实用工具：JSON 格式化与校验、Base64 编解码、URL 百分号编解码、Unix 时间戳转换、UUID 生成器和文本统计。
- 图片 OCR 工具使用发布安装包内置的本地 Tesseract 5.5.3 和 `chi_sim+eng` 语言模型识别文字，支持文件、拖放和剪贴板图片导入，以及编辑、复制和 TXT/Markdown 导出。
- 试卷复原小工具提供保守的本地图像检测、可编辑 Mask、原图对比和 PNG 导出。内置检测器不是训练模型。
- 五个本地小游戏：猜数字、2048、贪吃蛇、俄罗斯方块和五子棋。
- 按名称、分类、描述、关键词和别名进行全局搜索；使用 `Ctrl+K`（macOS 为 `Cmd+K`）聚焦搜索，使用 `Esc` 清除搜索或返回首页。
- 基于统一 `ToolMetadata`/`ITool` 接口的六个离线工具：JSON 格式化与校验、Base64 编解码、URL 百分号编解码、Unix 时间戳转换、UUID 生成器和文本统计。
- 按名称、分类、描述、关键词和别名进行全局工具搜索；也可切换到本地文件模式，为选定目录建立内存元数据索引，按文件名或目录路径即时搜索，并直接打开结果或其所在目录。使用 `Ctrl+K`（macOS 为 `Cmd+K`）聚焦搜索，使用 `Esc` 清除当前模式的搜索或返回首页。
- 使用 QSettings 持久化收藏、最近使用、主题和窗口布局。
- 可即时切换并保存跟随系统、浅色和深色主题。
- 所有工具完全在本地运行，不需要网络服务，也不会记录工具输入内容。

## 图片 OCR 本地运行要求

Windows 和 macOS 发布安装包内置 Apache-2.0 许可的 Tesseract 5.5.3 运行时，以及 `tessdata_fast` 的简体中文 `chi_sim` 和英文 `eng` 模型。安装后不需要另行下载、安装或配置 OCR 程序，应用运行时也不会下载程序或语言数据。安装包内的 OCR 组件缺失或损坏时，页面会提示修复或重新安装应用。

OCR 页面默认使用 `chi_sim+eng`，并只提供安装包实际包含的“简体中文和英文”“简体中文”“英文”三种选择，避免把未随应用发布的语言误报为安装损坏。开发者仍可直接通过 `OcrRequest` 测试其他已自行准备的模型。应用使用 `QProcess` 直接启动安装包内的程序，不经过命令行 shell。图片和识别文字不会上传到网络，也不会写入普通日志。剪贴板图片只会写入应用生命周期内的本地临时目录。输入图片不会被修改，只有用户主动选择导出路径时才会保存 TXT 或 Markdown 结果。

源码树开发构建不会下载或自动准备 Tesseract。开发者和测试可以使用 `MINITREASUREBOX_TESSERACT` 指定可执行文件，并使用 `TESSDATA_PREFIX` 指定包含 `.traineddata` 文件的目录。未设置覆盖变量且构建目录旁没有打包运行时时，应用会回退到 `PATH` 中的 `tesseract`。发布安装包优先自动发现相对于应用可执行文件的内置运行时，显式环境变量仅用于开发和测试覆盖。

Tesseract、语言模型和原生运行库的归属、版本、完整性锁定和许可证说明见 [第三方声明](THIRD_PARTY_NOTICES.md)。源代码依赖审计确认 Tesseract 是当前唯一由工具启动的外部程序；另一个 AI 工具“试卷复原小工具”使用进程内的确定性 C++ 图像基线，实用工具和小游戏也都在进程内运行，不需要外部程序或动态下载模型。

## 试卷复原小工具的能力和限制

试卷复原小工具完全在本机内存中处理图片，不使用网络服务，也不会在运行时下载模型。内置检测器是确定性的保守图像基线，并非训练过的手写识别模型。它根据颜色对比提出可能的彩色笔迹 Mask，随后使用附近的浅色、低色差纸张像素进行背景填充。用户可以用画笔、橡皮擦和矩形工具修正 Mask，并撤销或重做编辑、切换原图/Mask/结果视图、按住对比原图以及导出 PNG。

原图在内存中单独保留，应用不会覆盖导入文件。只有用户明确选择导出路径时才会写入结果。该基线可能漏掉铅笔、黑色笔迹或把彩色印刷内容选入 Mask，复杂纹理和被笔迹覆盖的内容也无法可靠还原。背景填充只生成视觉上较平整的替代区域，**不能恢复被手写内容遮挡的原始题目文字、线条或答案，也不保证无损复原**。编辑器会要求用户检查候选 Mask。后续可以通过 `IHandwritingRemovalEngine` 接口接入单独打包且授权明确的本地模型，当前版本不包含或下载此类模型。

## 构建要求

- CMake 3.22 或更高版本
- 支持 C++20 的编译器
- Qt 6 的 Core、Gui、Widgets 和 Test 组件
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

## 本地文件搜索

在首页搜索框左侧将模式从“工具搜索”切换为“本地文件”，然后选择一个本地目录作为搜索范围。应用启动及组件构造时不会自动扫描主目录，只有用户明确选择目录后才会在后台扫描一次，将文件名、目录名和相对路径元数据建立为内存索引。首次建立或手动重建索引不会阻塞界面，新任务会取消或取代旧任务。搜索范围始终限制在用户选择的根目录内，符号链接既不会加入索引，也不会被跟随。

索引就绪后，关键词查询只对内存中的名称和路径进行不区分大小写的 Unicode 匹配，不递归访问磁盘，也不读取或索引任何文件内容。嵌套文件和目录都会加入索引，每次最多显示 200 条结果。

应用通过 Qt 的 `QFileSystemWatcher` 使用 Windows、macOS 和 Linux 的原生文件系统监控后端。完整扫描结束后，应用先注册监控，再对根目录执行协调刷新；如果刷新发现并成功监控了新目录，应用会再次协调根目录，直到一轮刷新不再增加监控目录，从而捕获扫描与监控交接期间发生的变更。创建、删除、重命名或移动条目时，应用会合并短时间内的通知，并按顺序只重新扫描受影响的目录子树；更新进行期间收到的路径会保留到下一次更新，不会取消并覆盖前一次结果。新目录会加入监控，已删除目录会移出监控。如果系统资源限制导致部分目录无法注册监控，或增量更新失败，状态区域会明确提示可能遗漏变更；此时可使用“重建索引”执行安全的完整刷新。

每条结果可使用系统默认程序打开。目录结果会直接打开目录；文件结果既可打开文件，也可打开文件所在目录。如果结果已被移动、删除、变成符号链接或不再是有效的本地路径，应用会显示失败提示而不会尝试启动。

## 创建安装包

安装包必须在对应的原生操作系统上构建，并确保 CMake 可以找到 Qt 6.8.3。

Windows 使用 Visual Studio 2022 和 NSIS。先用完整性锁文件创建环境并收集许可证，再把经过验证的运行时路径传给 CMake：

```powershell
$env:MAMBA_ROOT_PREFIX="$PWD/build/micromamba"
python scripts/fetch_ocr_runtime.py `
  --lock-file packaging/ocr-runtime-win-64.lock `
  --sha256-manifest packaging/ocr-runtime-sha256.json `
  --output-cache build/ocr-packages `
  --output-lock build/ocr-runtime-local.lock
micromamba create --yes --prefix build/ocr-environment --file build/ocr-runtime-local.lock
python scripts/stage_ocr_runtime.py --environment build/ocr-environment `
  --output build/ocr-runtime-staged `
  --package-cache build/ocr-packages `
  --lock-file packaging/ocr-runtime-win-64.lock `
  --sha256-manifest packaging/ocr-runtime-sha256.json `
  --platform windows
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON `
  -DMINITREASUREBOX_REQUIRE_BUNDLED_OCR=ON `
  -DMINITREASUREBOX_OCR_RUNTIME_ROOT="$PWD/build/ocr-runtime-staged"
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
cpack --config build/CPackConfig.cmake -C Release -G NSIS
```

生成的安装程序为 `build/packages/MiniTreasureBox-0.1.4-windows-x64-unsigned-preview.exe`，其中包含应用程序、Qt 库及插件、可重定位的 Tesseract 运行库、`chi_sim+eng` 模型和第三方许可证。安装后无需修改 `PATH`、安装 Tesseract 或配置其他用户环境变量即可运行。该预览安装程序尚未进行 Authenticode 签名，因此 Windows 会将发布者显示为未知。

macOS 分别在原生 Intel 和 Apple Silicon 构建机上使用 `ocr-runtime-osx-64.lock` 与 `ocr-runtime-osx-arm64.lock`。以下为 Intel 示例，Apple Silicon 将架构和锁文件分别改为 `arm64` 与 `ocr-runtime-osx-arm64.lock`：

```bash
export MAMBA_ROOT_PREFIX="$PWD/build/micromamba"
OCR_LOCK=packaging/ocr-runtime-osx-64.lock
python3 scripts/fetch_ocr_runtime.py \
  --lock-file "$OCR_LOCK" \
  --sha256-manifest packaging/ocr-runtime-sha256.json \
  --output-cache build/ocr-packages \
  --output-lock build/ocr-runtime-local.lock
micromamba create --yes --prefix build/ocr-environment --file build/ocr-runtime-local.lock
python3 scripts/stage_ocr_runtime.py --environment build/ocr-environment \
  --output build/ocr-runtime-staged \
  --package-cache build/ocr-packages \
  --lock-file "$OCR_LOCK" \
  --sha256-manifest packaging/ocr-runtime-sha256.json \
  --platform macos
cmake -S . -B build -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=x86_64 \
  -DMINITREASUREBOX_PACKAGE_ARCH_SUFFIX=x64 \
  -DMINITREASUREBOX_REQUIRE_BUNDLED_OCR=ON \
  -DMINITREASUREBOX_OCR_RUNTIME_ROOT="$PWD/build/ocr-runtime-staged"
cmake --build build --parallel
ctest --test-dir build --output-on-failure
cpack --config build/CPackConfig.cmake -C Release -G DragNDrop
```

生成的磁盘映像按架构命名为 `build/packages/MiniTreasureBox-0.1.4-macos-x64-unsigned-preview.dmg` 或 `MiniTreasureBox-0.1.4-macos-arm64-unsigned-preview.dmg`，其中包含可部署的 `MiniTreasureBox.app`、Qt 库及插件、可重定位的 Tesseract 运行库、`chi_sim+eng` 模型和第三方许可证。复制应用后无需另行安装 OCR 依赖。该预览应用尚未进行 Developer ID 签名、公证或装订，因此 macOS Gatekeeper 可能阻止其运行。

GitHub Actions 的 **Build release packages** 工作流也可以手动运行。手动运行会保留 Windows 和 macOS 安装包各 14 天，可从该次工作流的 Artifacts 区域下载，但不会创建 GitHub Release。发布新版本时，先确保 `CMakeLists.txt` 中的项目版本正确，再推送同版本的 `v` 前缀标签，例如：

```bash
git tag v0.1.4
git push origin v0.1.4
```

工作流会从 `CMakeLists.txt` 读取项目版本；标签必须与 `v${PROJECT_VERSION}` 完全一致，否则会在打包前失败。有效的 `v*` 标签会触发 Windows x64、macOS Intel 和 macOS Apple Silicon 的原生构建与测试，并对生成的 `.exe` 与两个 `.dmg` 执行安装、启动、内置 OCR 实图识别和移除冒烟测试。全部验证通过后，工作流会将三个文件发布到该标签对应、明确标记为 **unsigned preview** 的预发布 GitHub Release。当前安装包不适合作为经过生产签名的正式发布物；待配置受保护的签名凭据后，才应移除该标记。

## 项目文档

- [产品文档](docs/product/product-requirements.md)
- [技术文档](docs/technical/technical-design.md)

当前 AI 方向聚焦 OCR、图像识别、文档处理、图片增强和试卷去手写等传统 AI 小工具，暂不考虑 Agent 能力。技术方案采用 Qt 6、C++ 和 CMake，AI 推理逐步从 Python/PyTorch Worker 迁移到 ONNX Runtime C++。
