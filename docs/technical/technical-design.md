# 小小百宝箱技术文档

- 项目名称：MiniTreasureBox
- 技术文档版本：1.0
- 文档状态：技术基线
- 最后更新：2026-10-28

## 1. 技术目标

小小百宝箱是一款跨平台桌面工具集合应用。技术方案需要同时满足以下目标：

- 以 C/C++ 为主要开发语言。
- 支持 Windows、macOS 和 Linux。
- 支持图片、文件、剪贴板、系统托盘和全局快捷键。
- 支持本地 AI 推理和传统图像处理。
- AI 模型可以独立升级和替换。
- 工具可以独立开发、注册和维护。
- 单个工具发生异常时不能拖垮主程序。
- 默认保护本地文件和用户数据。

## 2. 技术选型结论

### 2.1 首选方案

```text
桌面框架：Qt 6
主要语言：C++20（最低可兼容 C++17）
构建系统：CMake
初期 UI：Qt Widgets
图像处理：OpenCV
模型推理：第一阶段 Python/PyTorch 子进程，后续 ONNX Runtime C++
本地存储：SQLite + QSettings
日志：Qt 日志接口 + 文件滚动日志
测试：Qt Test、GoogleTest 或 Catch2
```

### 2.2 为什么选择 Qt

Qt 是完整的跨平台 C++ 桌面开发框架，覆盖 GUI、文件、网络、线程、定时器、剪贴板、系统托盘、数据库和国际化等能力，适合长期维护的工具型桌面产品。

Qt 相对于 wxWidgets、GTKmm、FLTK、JUCE 和 Dear ImGui 的主要优势是：

- 跨平台一致性较好。
- C++ API 完整成熟。
- 传统桌面控件和现代界面能力都较完善。
- 对系统托盘、快捷键、文件和图片处理支持较好。
- 适合将多个独立工具组合到一个应用中。
- 与 OpenCV、SQLite、ONNX Runtime 等组件集成方便。

### 2.3 UI 技术路线

MVP 采用 Qt Widgets，原因是：

- C++ 集成直接。
- 适合表单、文本、文件和配置型工具。
- 架构简单，便于快速验证产品。
- Qt Designer 可以加速页面搭建。

后续可以在首页、工具卡片、图片预览和处理进度页面引入 Qt Quick/QML。第一阶段不建议同时大量使用 Widgets 和 QML，避免增加双 UI 技术栈的维护成本。

## 3. 框架对比

| 方案 | 语言 | 优势 | 局限 | 结论 |
|---|---|---|---|---|
| Qt | C++ | 完整、跨平台、生态成熟、桌面能力强 | 体量较大，需要关注许可证 | 首选 |
| wxWidgets | C++ | 轻量、原生控件、许可证宽松 | 现代 UI 和跨平台一致性较弱 | 备选 |
| GTKmm | C++/C | Linux 集成好、LGPL | Windows/macOS 部署体验相对复杂 | 不作为首选 |
| FLTK | C++ | 轻量、依赖少 | UI 能力和生态有限 | 适合小工具，不适合本项目 |
| JUCE | C++ | 自绘和音频能力强 | 普通业务桌面控件不如 Qt 直接 | 仅音频方向时考虑 |
| Dear ImGui | C++ | 适合调试工具和内部工具 | 不是完整的传统桌面应用框架 | 不作为主界面 |

## 4. 总体架构

```text
┌──────────────────────────────────────────┐
│              Qt Desktop Application      │
├──────────────────────────────────────────┤
│ UI Layer                                 │
│  Home / Navigation / Tool Pages / Dialogs│
├──────────────────────────────────────────┤
│ Application Layer                        │
│  ToolRegistry / TaskManager / Settings  │
│  Favorites / RecentTools / Shortcuts     │
├──────────────────────────────────────────┤
│ Domain Layer                              │
│  Text / Image / Document / Game / AI     │
├──────────────────────────────────────────┤
│ Infrastructure Layer                     │
│  SQLite / Filesystem / Clipboard / Logs  │
│  ModelRuntime / Process / SystemTray     │
└──────────────────────────────────────────┘
```

设计原则：

1. UI 不直接依赖具体 AI 模型。
2. 每个工具通过统一接口注册。
3. 耗时任务通过后台任务调度器运行。
4. AI 模型通过可替换运行时封装。
5. 本地工具和 AI 工具使用统一的工具元数据。
6. 输入文件、临时文件和输出文件生命周期明确。
7. 所有耗时任务支持进度、取消和错误回传。

## 5. 推荐目录结构

```text
MiniTreasureBox/
├── CMakeLists.txt
├── README.md
├── docs/
│   ├── product/
│   │   └── product-requirements.md
│   └── technical/
│       └── technical-design.md
├── cmake/
├── resources/
│   ├── icons/
│   ├── translations/
│   └── themes/
├── src/
│   ├── app/
│   │   ├── application.h/.cpp
│   │   ├── main_window.h/.cpp
│   │   └── system_tray.h/.cpp
│   ├── core/
│   │   ├── tool_registry.h/.cpp
│   │   ├── task_manager.h/.cpp
│   │   ├── settings.h/.cpp
│   │   └── recent_tools.h/.cpp
│   ├── domain/
│   │   ├── tool/
│   │   ├── image/
│   │   ├── document/
│   │   ├── text/
│   │   ├── productivity/
│   │   └── games/
│   ├── ai/
│   │   ├── common/
│   │   ├── ocr/
│   │   ├── handwriting_removal/
│   │   ├── document_enhancement/
│   │   └── background_removal/
│   ├── infrastructure/
│   │   ├── storage/
│   │   ├── filesystem/
│   │   ├── clipboard/
│   │   ├── logging/
│   │   └── model_runtime/
│   └── ui/
│       ├── pages/
│       ├── widgets/
│       └── dialogs/
├── models/
│   ├── manifests/
│   └── README.md
├── scripts/
└── tests/
    ├── unit/
    ├── integration/
    └── fixtures/
```

## 6. 工具插件化设计

第一阶段不必实现动态插件 DLL，但代码结构应按插件化思路设计。每个工具提供统一元数据和执行入口。

```cpp
struct ToolMetadata {
    QString id;
    QString name;
    QString category;
    QString description;
    QStringList keywords;
    QString iconName;
    bool requiresNetwork{false};
};

class ITool {
public:
    virtual ~ITool() = default;
    virtual ToolMetadata metadata() const = 0;
};
```

建议使用静态注册完成 MVP：

```cpp
class ToolRegistry {
public:
    void registerTool(std::shared_ptr<ITool> tool);
    std::shared_ptr<ITool> find(const QString& id) const;
    QList<ToolMetadata> search(const QString& query) const;
};
```

后续如果工具数量较多，再扩展为：

- 动态库插件。
- 工具包。
- 工具版本管理。
- 工具权限声明。
- 工具安装和卸载。

## 7. 任务调度与线程模型

### 7.1 原则

- UI 线程只负责交互和渲染。
- 图片处理、OCR、模型推理和文件批处理必须运行在工作线程。
- 工作任务通过任务管理器统一调度。
- 任务支持 queued、running、success、failed、cancelled 状态。
- 结果通过 Qt signal/slot 回传 UI。

### 7.2 任务状态

```cpp
enum class TaskState {
    Queued,
    Running,
    Succeeded,
    Failed,
    Cancelled
};
```

### 7.3 取消机制

- OpenCV 处理循环检查取消标志。
- Python 子进程任务通过进程终止或取消消息处理。
- ONNX 推理任务在模型运行边界检查取消状态。
- UI 在任务取消后立即恢复可操作状态。

## 8. AI 工具架构

### 8.1 AI 能力接口

AI 能力应通过接口与 UI、工具业务分离：

```cpp
class IOcrEngine {
public:
    virtual ~IOcrEngine() = default;
    virtual OcrResult recognize(const QImage& image) = 0;
};

class IHandwritingRemovalEngine {
public:
    virtual ~IHandwritingRemovalEngine() = default;
    virtual RemovalResult detectAndRemove(
        const QImage& image,
        const RemovalOptions& options) = 0;
};
```

同一接口可以有多个实现：

```text
IOcrEngine
├── PythonOcrEngine
├── OnnxOcrEngine
└── RemoteOcrEngine（后续可选）

IHandwritingRemovalEngine
├── PythonHandwritingEngine
├── OnnxHandwritingEngine
└── ManualMaskEngine
```

### 8.2 模型运行阶段

#### 阶段一：Python/PyTorch 子进程

Qt 主程序启动本地推理 Worker，通过标准输入输出或本地 HTTP/Unix Socket 通信。

```text
Qt/C++
  ↓ JSON + 输入文件路径
Python Worker
  ↓ PyTorch/OpenCV
输出 Mask/结果文件
  ↑ JSON 结果和错误信息
Qt/C++
```

示例请求：

```json
{
  "requestId": "task-001",
  "task": "detect_handwriting",
  "input": "input/page-001.png",
  "maskOutput": "cache/task-001-mask.png",
  "device": "cpu"
}
```

示例结果：

```json
{
  "requestId": "task-001",
  "status": "succeeded",
  "mask": "cache/task-001-mask.png",
  "confidence": 0.91,
  "durationMs": 1840
}
```

Python Worker 的职责：

- 加载模型。
- 接收任务。
- 执行推理。
- 保存结果。
- 返回结构化错误。
- 管理模型初始化状态。

Qt 主程序的职责：

- 启动和监控 Worker。
- 管理任务取消。
- 显示处理进度。
- 让用户编辑 Mask。
- 调用本地图像修复。
- 统一处理结果和历史。

#### 阶段二：ONNX Runtime C++

模型稳定后，将可用模型导出为 ONNX，并使用 ONNX Runtime C++ API 直接推理：

```text
PyTorch 模型
→ ONNX 导出
→ 输出一致性验证
→ ONNX Runtime C++ 封装
→ Qt 应用内直接推理
```

阶段二可以减少 Python 环境依赖，更适合正式桌面分发。模型导出前必须验证算子兼容性、CPU/GPU 输出一致性和不同分辨率表现。

## 9. 试卷去手写技术方案

### 9.1 处理流水线

```text
输入图片
→ 方向检测
→ 文档裁边和透视校正
→ 去阴影/去噪/对比度增强
→ 手写区域检测
→ 概率图转 Mask
→ Mask 膨胀、去噪和连通域处理
→ 用户人工修正
→ 背景填充或图像修复
→ 线条恢复
→ 结果合成
→ 输出 PNG/PDF
```

### 9.2 手写区域检测

候选实现：[HandWritingEraser-Pytorch](https://github.com/AndSonder/HandWritingEraser-Pytorch)。该项目将中文纸张上的手写擦除建模为分割任务，并使用 DeepLabV3+ 预测手写区域。

建议产品中保存以下中间结果：

- 原图。
- 预处理图。
- 手写概率图。
- 自动 Mask。
- 用户修正后的 Mask。
- 修复结果。

这样可以支持调试、前后对比和问题追踪。

### 9.3 Mask 编辑

Qt 页面需要提供：

- 画笔增加区域。
- 橡皮擦减少区域。
- 矩形和套索选区。
- Mask 透明度调整。
- 自动扩展边界。
- 撤销和重做。
- 原图/Mask/结果三种视图。
- 局部放大和缩小。

### 9.4 图像修复

简单纸张背景优先采用 OpenCV 局部颜色或纹理填充，减少模型依赖。复杂区域可使用 LaMa：[advimman/lama](https://github.com/advimman/lama)，其代码仓库为 Apache-2.0。

也可以参考或验证 IOPaint：[Sanster/IOPaint](https://github.com/Sanster/IOPaint)，但其包含的不同模型仍需分别核查授权。

注意：图像修复只能生成视觉上合理的内容，不能保证恢复被手写覆盖的原始题目内容。

### 9.5 线条和表格恢复

对于试卷横线和表格，不完全依赖生成式修复：

- OpenCV 检测水平线和垂直线。
- 使用 Hough 变换或形态学操作恢复线条。
- 根据邻近线段补全断裂部分。
- 将恢复线条与修复图合成。

## 10. 开源方案与许可证记录

| 项目 | 用途 | 已知许可证/限制 | 产品使用建议 |
|---|---|---|---|
| [HandWritingEraser-Pytorch](https://github.com/AndSonder/HandWritingEraser-Pytorch) | 中文试卷手写分割 | 代码标记 MIT；预训练模型和数据需另行核查 | 可作为候选检测模型 |
| [SCUT-EnsExam](https://github.com/SCUT-DLVCLab/SCUT-EnsExam) | 试卷去手写数据集 | 非商业研究用途；CC BY-NC-ND 4.0 | 不直接用于商业产品训练或分发 |
| [WPI_inpainting](https://github.com/adbu42/WPI_inpainting) | 手写检测与 LBAM 修复 | 仓库未明确许可证 | 仅参考流程，未经确认不直接集成 |
| [LaMa](https://github.com/advimman/lama) | 通用图像修复 | 代码仓库 Apache-2.0；权重需另查 | 可作为修复候选 |
| [IOPaint](https://github.com/Sanster/IOPaint) | 图像擦除和修复 | 代码仓库 Apache-2.0；依赖模型需另查 | 可作为验证工具或参考实现 |
| [Hand-Text-Erasure](https://github.com/Yanting-K/Hand-Text-Erasure) | EraseNet 文字擦除 | 代码标记 MIT；README 对商业使用有额外联系说明 | 商业化前联系作者 |
| [PERT](https://github.com/wangyuxin87/PERT) | 场景文字擦除 | 仓库未明确许可证 | 不直接作为产品依赖 |

许可证管理要求：

1. 代码、模型权重、数据集和第三方依赖分别记录许可证。
2. 没有明确许可证的代码不直接复制到产品。
3. 模型权重的再分发权限必须单独确认。
4. 数据集只用于被允许的场景。
5. 发布包中保留第三方许可证和版权声明。
6. 每次升级模型或依赖都重新检查授权条件。

SCUT-EnsExam 数据集适合研究和原型验证，但仓库明确限制为非商业研究用途。正式商业化方案应使用获得授权的数据，或自行采集并标注合法数据。

## 11. 本地数据存储

### 11.1 QSettings

用于保存：

- 主题。
- 窗口大小和位置。
- 最近使用工具数量。
- 快捷键设置。
- 系统托盘开关。
- 模型目录。

### 11.2 SQLite

用于保存：

- 收藏工具。
- 最近使用记录。
- OCR 历史元数据。
- 任务历史。
- 游戏最高分。
- 用户可恢复的工具配置。

不默认保存原始图片和敏感文本；如果工具需要保存结果，必须向用户说明存储位置。

建议表：

```text
tool_favorites
recent_tools
task_history
game_scores
model_registry
```

## 12. 文件和缓存管理

目录建议：

```text
应用数据目录/
├── config/
├── database/
├── cache/
│   ├── ai/
│   └── thumbnails/
├── models/
├── logs/
└── exports/
```

要求：

- 临时文件使用唯一任务 ID 命名。
- 任务完成或取消后按策略清理临时文件。
- 用户主动导出的文件不自动删除。
- 设置页提供清理缓存功能。
- 日志不记录图片内容和完整 OCR 文本。

## 13. 系统能力

Qt 层统一封装：

- 系统托盘。
- 剪贴板。
- 全局快捷键。
- 文件选择器。
- 拖拽文件。
- 系统通知。
- 窗口置顶。
- 开机启动。

不同平台的系统差异放在 `infrastructure` 层，不应散落在业务工具中。

## 14. 性能目标

- 冷启动目标小于 2 秒，不包含首次加载大型 AI 模型的时间。
- 本地文本工具响应目标小于 500ms。
- 常规图片工具在 2 秒内显示处理结果或进度。
- AI 工具必须显示初始化和推理状态。
- UI 在模型推理和批处理期间保持可交互。
- 单个任务占用的临时内存可被回收。
- 失败任务不导致主程序崩溃。

## 15. 安全与隐私

- 文件路径必须经过规范化和权限检查。
- 导出路径由用户明确选择。
- 不执行用户输入中的命令或脚本。
- 不将图片和 OCR 内容写入普通日志。
- API Key 等敏感配置不能明文写入日志。
- 剪贴板历史支持关闭、清空和过期策略。
- 模型下载必须校验来源和文件完整性。
- 在线服务需要显示联网状态和隐私说明。

## 16. 构建与发布

### 16.1 构建

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

建议将第三方依赖通过 CMake 统一管理，优先使用：

- `find_package(Qt6 ...)`
- `find_package(OpenCV ...)`
- `find_package(SQLite3 ...)`
- 独立的 ONNX Runtime 安装包或预构建目录

### 16.2 发布包

初期可采用：

- Windows：安装包或便携版。
- macOS：应用包和 DMG。
- Linux：AppImage，后续视需求提供 deb/rpm。

AI 模型建议独立为模型包：

```text
基础应用安装包
+ OCR 模型包
+ 试卷去手写模型包
+ 图片增强模型包
```

模型包支持版本号、大小、哈希和许可证信息。

## 17. 错误处理

错误分为：

- 用户输入错误。
- 文件格式错误。
- 文件读写错误。
- 模型加载错误。
- 推理超时。
- 内存不足。
- 外部服务错误。
- 权限错误。

用户可见提示应说明：

1. 发生了什么。
2. 是否已经保留原文件。
3. 用户可以做什么。
4. 是否可以重试。

内部日志记录错误类型、任务 ID、组件和耗时，但不记录敏感文件内容。

## 18. 测试策略

### 18.1 单元测试

覆盖：

- 工具搜索。
- 工具注册。
- 配置读写。
- 文件名处理。
- Mask 合并和编辑操作。
- 任务状态转换。
- JSON 协议解析。

### 18.2 集成测试

覆盖：

- Qt 页面与工具执行器连接。
- Python Worker 启停和异常处理。
- 模型加载和推理输出。
- SQLite 初始化和迁移。
- 文件导入、导出和缓存清理。

### 18.3 AI 质量评估

试卷去手写至少记录：

- 手写区域检测准确性。
- 印刷文字误删率。
- 手写残留率。
- 背景修复质量。
- 不同笔颜色和纸张条件下的表现。
- CPU 和 GPU 推理时间。

不能只用 PSNR、SSIM 等图像指标判断产品可用性，应增加人工检查和误删率评估。

## 19. 开发阶段规划

### 阶段一：项目骨架

- 初始化 Qt 6、CMake 和 C++ 工程。
- 创建主窗口、导航和工具注册机制。
- 加入主题、设置和本地存储。
- 实现 5 个以上本地工具。

### 阶段二：AI Worker

- 定义 AI 引擎接口。
- 创建 Python 推理 Worker。
- 实现 OCR 调用和任务协议。
- 加入任务进度、取消和错误处理。

### 阶段三：试卷去手写

- 实现图片导入和预处理。
- 接入手写检测模型。
- 实现 Mask 编辑器。
- 实现 OpenCV 背景填充。
- 评估 LaMa 修复效果。
- 完成图片导出。

### 阶段四：部署优化

- 固定模型包格式。
- 验证 Windows、macOS、Linux 构建。
- 评估 ONNX 导出。
- 用 ONNX Runtime 替换 Python 推理路径。
- 建立第三方许可证清单。

## 20. 需要后续确认的问题

- 首发平台是否优先 Windows。
- 是否接受 MVP 携带 Python 推理运行时。
- 试卷去手写是否面向商业发布。
- 是否有可合法使用的自有试卷数据。
- 首批 AI 模型是否全部本地运行。
- 是否需要支持 GPU、Apple Silicon 或仅 CPU。
- 应用是否需要自动更新。
- 是否需要多语言和国际化。

## 21. 技术结论

最终推荐架构为：

```text
Qt 6 + C++ + CMake
├── MVP：Qt Widgets
├── 图像处理：OpenCV
├── AI 原型：Python/PyTorch 本地 Worker
├── AI 正式集成：ONNX Runtime C++
├── 数据：SQLite + QSettings
├── 试卷去手写：手写分割 + Mask 编辑 + 图像修复
└── 发布：跨平台安装包 + 独立模型包
```

试卷去手写采用半自动产品形态最稳妥：模型负责提出手写区域，用户负责确认和修正，OpenCV/LaMa 负责生成结果。这样能够充分利用现有开源实现，同时避免将不稳定的模型结果直接作为最终输出。
