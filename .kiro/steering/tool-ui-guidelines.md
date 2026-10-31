---
inclusion: auto
name: tool-ui-guidelines
description: 新增或修改非游戏小工具 UI 时，遵循统一工具工作台设计规范
---

# 非游戏小工具 UI 实施约束

新增或修改实用工具、OCR、图片处理等非游戏工具 UI 时，必须先阅读并遵循：

#[[file:docs/design/tool-ui-style-guide.md]]

实施时还必须满足以下仓库约束：

- 具体工具逻辑留在工具模块中，不硬编码进 `MainWindow`。
- 优先复用 `src/ui/widgets/tool_workspace/` 中的 `ResponsivePair`、`ToolStatusStrip` 和 `MetricCard`。
- 所有非游戏工具样式集中维护在 `src/app/main_window.cpp` 的 `themeStyleSheet()`，不要添加工具级硬编码 stylesheet。
- 非游戏 QSS 限定在 `toolWorkspaceFrame[workspaceKind="tool"]` 下，不能覆盖小游戏的 `gameId` 样式。
- 保持工具 ID、注册表工厂、现有对象名、accessible 信息、默认输出和业务算法不变。
- 保持顶部返回、底部“返回上一页”、`Esc`、`Ctrl/Cmd + K`、收藏、最近使用和分类上下文行为。
- 后台工具必须显示处理中状态和取消入口，并保留线程中断与陈旧结果保护。
- 同时检查宽屏双栏、窄屏单列、浅色、深色、System 主题和 Qt offscreen 环境。
- 完成修改后运行现有构建、测试和 `git diff --check`，不要仅凭编译成功判断 UI 契约已满足。
