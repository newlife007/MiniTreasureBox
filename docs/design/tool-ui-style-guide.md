# MiniTreasureBox 非游戏小工具 UI 设计规范

## 1. 文档目的

本文规定 MiniTreasureBox 中非游戏小工具的页面结构、公共组件、视觉层级、状态反馈、响应式行为和兼容性要求。新增或修改实用工具、OCR、图片处理等非游戏工具时，必须遵循本文。

本文与 [整体 UI 设计方案](ui-design-plan.md) 配套使用。小游戏使用独立的 [小游戏 UI 设计规范](game-ui-style-guide.md)。

当前主要实现位置：

- `src/app/main_window.cpp`
- `src/domain/tools/tool_widgets.cpp`
- `src/ai/ocr/ocr_widget.cpp`
- `src/ai/handwriting_removal/removal_widget.cpp`
- `src/ui/widgets/tool_workspace/`

## 2. 设计目标

非游戏工具应呈现为安静、清晰、可预测的桌面工作台：

1. 用户进入页面后能立即区分输入、操作和结果。
2. 主要动作始终清楚，次要动作不会抢占视觉焦点。
3. 本地处理、处理中、成功、警告和错误状态均可见。
4. 宽窗口有效利用横向空间，窄窗口不产生横向滚动。
5. UI 改造不改变工具算法、默认输出、快捷键或测试契约。

## 3. 工具详情页外壳

所有工具由 `MainWindow::showToolOverview()` 放入统一详情页。

```text
┌──────────────────────────────────────────────────────┐
│ ← 返回  工具名称、描述、分类、本地状态       ☆ 收藏 │
└──────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────┐
│                    工具工作区                        │
└──────────────────────────────────────────────────────┘

[返回上一页]
```

必须保留：

- 顶部返回入口 `toolHeaderBackButton`。
- 底部 `backButton`，文本为“返回上一页”。
- 两个返回入口的 tooltip 和 accessible description 均说明返回“工具列表”。
- `Esc` 返回行为。
- 工具名称、描述、分类、本地状态和收藏入口。
- 打开工具前的搜索、分类、收藏和最近使用上下文。

详情页公共对象名：

- `toolDetailHeader`
- `toolDetailTitle`
- `toolDetailDescription`
- `toolDetailCategory`
- `toolDetailLocalState`
- `toolDetailFavoriteButton`
- `toolDetailScroll`
- `toolWorkspaceHost`
- `toolWorkspaceFrame`

`toolWorkspaceFrame` 使用 `workspaceKind` 动态属性区分 `tool` 与 `game`。非游戏样式必须限定在 `workspaceKind="tool"` 范围内，避免覆盖小游戏视觉。

## 4. 工作区结构

### 4.1 双栏编辑型工具

适用于 JSON、Base64、URL 编解码和 OCR。

```text
┌──────────────────────┐  ┌──────────────────────┐
│ 输入标题与说明        │  │ 结果标题与说明        │
│                      │  │                      │
│ 输入或图片预览        │  │ 文本结果              │
│                      │  │                      │
└──────────────────────┘  └──────────────────────┘

[主要操作] [次要操作]                     [清空]
状态条
```

使用 `ResponsivePair` 管理宽屏双栏和窄屏单列，不在具体工具中复制 resize 判断。

编辑面板统一使用：

- `toolEditorPanel`
- `toolSectionTitle`
- `toolSectionDescription`

输入和输出文本区域应使用等宽字体。只读结果必须明确设置 `readOnly`，不能只通过颜色暗示。

### 4.2 单值工具

适用于时间戳转换和 UUID 生成。

- 输入区使用 `toolInputCard`。
- 输出区使用 `toolResultCard`。
- 主要结果应可复制。
- 输入、结果和动作保持明显分组，不使用占满页面的大型空编辑器。

### 4.3 指标型工具

适用于文本统计。

- 输入区继续使用 `toolEditorPanel`。
- 每项指标使用 `MetricCard`。
- 指标标题使用 `toolMetricTitle`。
- 指标值使用 `toolMetricValue=true` 动态属性。
- 指标应实时更新，不要求用户点击额外按钮。

### 4.4 Inspector 型图片工具

适用于试卷复原等需要画布和参数检查的工具。

```text
[导入] [自动处理] [取消]                    [撤销] [重做]

┌────────────────────────────┐  ┌──────────────────┐
│ 画布                       │  │ 编辑与检查        │
│                            │  │ 工具、参数、视图  │
│                            │  │ 恢复、清空、导出  │
└────────────────────────────┘  └──────────────────┘

状态条
```

右侧 Inspector 使用 `toolInspectorPanel`，顶部操作栏使用 `toolControlBar`。窄窗口下 Inspector 移到画布下方。

## 5. 公共组件

### 5.1 ResponsivePair

文件：`src/ui/widgets/tool_workspace/responsive_pair.*`

职责：

- 达到 breakpoint 时左右排列。
- 低于 breakpoint 时上下排列。
- 只管理方向和间距，不包含业务逻辑。
- 子控件继续拥有自身布局和对象名。

建议 breakpoint：

- 文本双栏：约 `660 px`。
- OCR：约 `700 px`。
- 画布与 Inspector：约 `760 px`。

### 5.2 ToolStatusStrip

文件：`src/ui/widgets/tool_workspace/tool_status_strip.*`

工具状态必须通过状态条显示，不应只修改散落的普通标签。状态值：

| 状态 | `toolState` | 用途 |
| --- | --- | --- |
| 空闲 | `idle` | 尚无任务或已清空 |
| 就绪 | `ready` | 输入有效，可以执行 |
| 处理中 | `running` | 后台任务正在运行 |
| 成功 | `success` | 操作完成或结果已复制、导出 |
| 警告 | `warning` | 结果过期、正在取消或需要检查 |
| 错误 | `error` | 输入、处理或导出失败 |

状态消息应说明发生了什么，并在失败时提供下一步。不要暴露 Worker 路径、模型路径、堆栈或内部组件名。

传入已有状态标签时，必须保留该标签的原对象名，例如 `errorLabel`、`ocrStatus`、`restorationStatus`。

### 5.3 MetricCard

文件：`src/ui/widgets/tool_workspace/metric_card.*`

用于展示一个短标题和一个醒目的数值。不得在卡片中加入与指标无关的操作按钮。

## 6. 按钮层级

按钮使用 `toolButtonRole` 动态属性：

| 角色 | 值 | 用途 |
| --- | --- | --- |
| 主要 | `primary` | 当前页面最重要且安全的执行动作 |
| 次要 | `secondary` | 复制、转换、导入、重试等辅助动作 |
| 安静 | `quiet` | 清空、视图切换、低频编辑操作 |
| 警告 | `warning` | 取消任务或需要额外注意的动作 |

规则：

- 同一区域通常只有一个主要按钮。
- 不通过按钮文本颜色单独表达禁用状态，必须使用 Qt 的 disabled 状态。
- 图标不能替代文字和辅助功能名称。
- 所有动作按钮必须设置 tooltip、accessible name 和 accessible description。
- 可切换按钮在 checked 状态下使用主色边框和浅主色背景。

## 7. 状态与任务行为

### 7.1 同步工具

- 成功后显示 `success`。
- 输入不合法时显示 `error`，并清空不可用结果。
- 输入变化导致现有结果过期时显示 `warning`，禁用复制与导出。
- 清空后回到 `idle`。

### 7.2 后台任务

OCR 和图片处理必须提供：

- 明确的 `running` 状态。
- 可见的取消入口。
- 取消后的 `warning` 状态。
- 失败原因和可恢复入口。
- 对陈旧后台回调的 task ID 或同等保护。

UI 重构不得削弱线程中断、对象生命周期或陈旧结果保护。

## 8. 各工具规则

### 8.1 JSON 格式化与校验

- 输入和结果双栏。
- 支持格式化、压缩、校验、结果转输入、复制和清空。
- 保持现有 JSON 算法和默认输出语义。
- 不在首期增加语法树编辑器。

### 8.2 Base64 编解码

- 输入和结果双栏。
- 明确按 UTF-8 文本处理。
- 非 UTF-8 二进制内容继续按现有逻辑拒绝。
- 不在首期增加二进制文件模式。

### 8.3 URL 百分号编解码

- 输入和结果双栏。
- 明确按整段 UTF-8 文本处理。
- 不在首期增加 URL 路径、查询参数分段编辑。

### 8.4 Unix 时间戳转换

- 输入、单位和“使用当前时间”放在输入卡。
- UTC 与本地时间分别使用结果卡。
- 保持现有单向转换语义，不在首期增加日期反向生成时间戳。

### 8.5 UUID 生成器

- 使用单个结果卡。
- 主要动作是生成 UUID，结果可复制。
- 保持单个 UUID v4 输出，不在首期增加批量生成。

### 8.6 文本统计

- 输入区上方说明统计口径。
- 使用指标卡显示 Unicode 字符、非空白字符、词数、行数和 UTF-8 字节数。
- 统计实时更新。

### 8.7 图片 OCR

- 图片输入和识别结果使用响应式双栏。
- 图片侧显示预览、文件名、尺寸和格式。
- 结果侧显示可编辑文本和字符数。
- 处理中显示进度条、取消按钮和统一状态条。
- 复制、导出和重试只在状态允许时启用。
- 不在 UI 暴露 PaddleOCR Worker 或模型文件路径。
- 不在首期增加文字框叠加编辑。

必须保留 OCR 对象名：

- `ocrWidget`
- `ocrLanguage`
- `ocrPreview`
- `ocrStatus`
- `ocrResult`
- `ocrSourceInfo`
- `ocrProgress`
- `ocrStatusStrip`
- `ocrResultCount`
- 所有既有 `ocr...Button`

### 8.8 试卷复原

- 顶部提供导入、自动 Mask、取消、撤销和重做。
- 主区使用画布与右侧 Inspector。
- Inspector 提供画笔、橡皮擦、矩形、画笔大小、Mask 透明度、原图/Mask/结果视图、按住看原图、恢复自动 Mask、清空 Mask 和导出。
- `restorationViewSelector` 可以隐藏，但必须保留供兼容和自动化使用。
- “按住看原图”释放后恢复按下前的视图。
- 导出前提示复杂纹理和被覆盖内容可能无法无损恢复。
- 导出确认只放在交互按钮路径，不改变 `exportPng()` 的程序化接口。
- 不在首期增加分割对比线。

必须保留试卷复原对象名：

- `handwritingRemovalWidget`
- `restorationCanvas`
- `restorationStatus`
- `restorationStatusStrip`
- `restorationViewSelector`
- `restorationBrushSize`
- `restorationMaskOpacity`
- 所有既有 `restoration...Button`

## 9. 视觉 Token 与主题

视觉 Token 由 `themeStyleSheet()` 集中提供。工具不能设置硬编码的局部 stylesheet。

- 页面背景：浅色 `#F5F6FA`，深色 `#111521`。
- 卡片背景：浅色 `#FFFFFF`，深色 `#1B2130`。
- 次级表面：浅色 `#F5F6FA`，深色 `#232A3B`。
- 主色：浅色 `#665CF2`，深色 `#8B83FF`。
- 成功色：浅色 `#19A982`，深色 `#55D0AE`。
- 警告和错误使用低饱和背景，并保证文字可读。

浅色、深色和 System 主题都必须可用。新增对象名或动态属性时，应在集中 QSS 中添加规则，而不是在某个工具内调用 `setStyleSheet()`。

## 10. 响应式和尺寸

- 宽屏优先双栏，不要求左右栏等宽时可通过子面板尺寸策略限制。
- 窄屏改为单列，主窗口详情区不出现水平滚动条。
- 操作行过长时应拆到面板、网格或纵向 Inspector，不依赖控件被压缩到不可读。
- 图片和画布保持比例缩放，不拉伸原始内容。
- 结果区应获得可增长空间，状态条和关键按钮不得被挤出页面。

## 11. 可访问性与兼容性

- 保留现有工具 ID、注册表工厂和页面对象名 `toolPage_<id>`。
- 保留 `Ctrl/Cmd + K`、`Esc`、收藏、最近使用和分类导航。
- 保留现有业务控件对象名，新增容器不能使测试或辅助技术失去目标。
- 所有可交互控件支持键盘焦点。
- 状态不能只通过颜色表达，必须同时显示文字和指示符。
- Qt offscreen 测试环境下不得依赖动画、窗口管理器或 GPU 特性。

通用工具还必须保留：

- `toolInput`
- `toolOutput`
- `errorLabel`
- `timestampInput`
- `timestampUnit`
- `utcOutput`
- `localOutput`
- `uuidOutput`
- `characterCount`
- `nonWhitespaceCount`
- `wordCount`
- `lineCount`
- `byteCount`

## 12. 新增工具检查清单

新增非游戏工具前确认：

- [ ] 工具逻辑仍由工具模块负责，没有硬编码进 `MainWindow`。
- [ ] 选择了双栏、单值、指标或 Inspector 中最接近的结构。
- [ ] 复用了 `ResponsivePair`、`ToolStatusStrip` 或 `MetricCard`。
- [ ] 所有按钮设置了正确的 `toolButtonRole`。
- [ ] 输入、结果、状态和主要动作的视觉层级清楚。
- [ ] 成功、警告、错误和后台处理中状态有明确文案。
- [ ] 宽屏和窄屏均无水平滚动或不可操作控件。
- [ ] 浅色、深色和 System 主题均可读。
- [ ] 对象名、快捷键、辅助功能信息和现有业务语义未被改变。
- [ ] 文件、图片和文本默认只在本机处理；如需网络，页面明确标注。
- [ ] 构建、现有单元测试和 `git diff --check` 通过。
