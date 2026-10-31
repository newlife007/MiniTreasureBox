# 试卷照片自动还原方案

- 对应需求：[Issue #26](https://github.com/newlife007/MiniTreasureBox/issues/26)
- 文档状态：待维护者评审的方案，不代表已实现
- 调研访问日期：2026-10-31
- 本次提交范围：仅文档，不包含应用、模型、数据集、训练、打包、测试或 CI 改动

## 1. 结论

按维护者意见，本版以 [AndSonder/HandWritingEraser-Pytorch 固定提交 `b216bff33f2db184a0ba3c782fe70086bb3c754a`](https://github.com/AndSonder/HandWritingEraser-Pytorch/tree/b216bff33f2db184a0ba3c782fe70086bb3c754a) 为首要技术参考。基线方向采用其“背景 / 手写 / 印刷”三分类分割思路和全分辨率分块推理思路，再通过固定预处理、确定性重叠融合和保守的文档后处理适配产品。该仓库及其外链 checkpoint **不是可直接投入生产的依赖**：必须先完成 H0 至 H4 Spike，独立验证复现、质量、权利、ONNX、CPU 性能与打包条件，才能决定采用、重训或放弃。

产品路径沿用现有 Qt Widget、`IHandwritingRemovalEngine`、后台任务和离线 Worker 架构，不把 Python 或模型下载暴露给用户。方向和几何校正、确定性修复仍是完整流程的必要组件；其他开源模型只作为补充候选，不改变 HandWritingEraser-first 的决策顺序。

目标流程是：用户导入或拖入一张受支持的试卷照片后，预处理和还原立即自动开始；界面显示阶段进度和取消入口；任务完成后自动打开结果视图。用户无需再点击“自动 Mask”，也无需编辑 Mask。现有 Inspector、Mask 画笔、橡皮擦、矩形、撤销和重做继续保留，但只作为可选的高级修正入口。

“还原”在本方案中的准确含义是：自动移除照片中可见的手写字迹和批改痕迹，并尽量保留照片中实际可见的印刷文字、线条、表格和纸张纹理。单张照片里被不透明笔迹完全遮住的印刷信息没有可验证的像素证据，系统不能承诺精确恢复。生成模型对这部分的补全只能标为估计，不得表述为原卷内容。

精确恢复被遮挡内容的唯一产品路径，是用户另行提供对应空白原卷，由系统完成版面匹配和几何对齐，再从空白原卷复制有证据的印刷内容。空白原卷是可选输入，不影响单图自动流程。

## 2. 用户体验与输出语义

### 2.1 默认的零额外操作流程

1. 用户点击导入或把一张 JPEG、PNG 等受支持图片拖入 `handwritingRemovalWidget`。
2. 图片通过格式、像素上限和可读性检查后，任务自动启动，不显示需要确认的参数页。
3. `restorationStatusStrip` 依次显示“校正方向”“校正文档”“识别手写”“还原纸面”等阶段和总体进度；取消按钮在整个后台任务期间可见。
4. 成功后，`restorationCanvas` 自动切换到结果视图，导出可用。流程至此已经完成，不要求复核或再点一次按钮。
5. 结果带有“高 / 中 / 低”可信度和简短原因。中、低可信度只提示“可选检查”，不会阻塞结果或导出。
6. 用户需要时才展开现有 Inspector，编辑 Mask、对照原图或重新运行。现有“自动 Mask”入口改为显式重试或高级操作，不再是首次导入后的必需动作。

取消后显示可恢复的警告状态，保留已导入原图，不展示未完成的结果。错误文案面向用户说明输入问题、资源不足或模型不可用等类别，不暴露 Worker 路径、模型路径和堆栈。

### 2.2 可信度与复核提示

页面级可信度由以下信号的最小值或校准后的组合得到：文档边界与透视置信度、分割置信度、Mask 面积异常、印刷连通域被 Mask 截断比例、修复区域纹理异常以及可选 OCR 一致性。首版阈值由 Spike 校准，UI 只显示等级和原因，不把未经校准的神经网络分数显示为“准确率”。

以下情况标为低可信度：大面积文字被覆盖、Mask 与印刷行或表格线重叠明显、页面边界不完整、强反光或阴影、分辨率不足、修复区域超过页面的 20%。低可信度仍自动产出结果，并默认打开结果视图，同时突出“检查 Mask”和“查看原图”两个可选动作。

若提供空白原卷，结果元数据要区分“来自对齐空白原卷的已观察像素”和“单图估计填充”。对齐置信度不足时不得复制空白原卷像素，应回退到单图路径并给出提示。

## 3. 当前实现与差距

现有工具由 `src/domain/tools/builtin_tools.cpp` 以 `exam-restoration` 注册，并由 `createHandwritingRemovalWidget` 创建。`src/ai/handwriting_removal/removal_engine.*` 已提供可注入的 `IHandwritingRemovalEngine`；`src/ai/handwriting_removal/removal_widget.*` 已有 `QThread` 后台执行、线程中断、task ID 陈旧结果保护、可编辑 `MaskDocument`、可选参数和原子 PNG 导出。这些边界应保留。

当前能力离需求还有以下差距：

- `BaselineHandwritingRemovalEngine` 使用确定性的颜色和对比度规则，主要识别彩色墨迹，黑色或铅笔手写与印刷文字难以区分。
- 被 Mask 覆盖的区域使用附近浅色纸面局部填充，不能理解印刷字、横线、表格和复杂背景。
- 首次导入后还需要用户点击“自动 Mask”，不满足导入即自动完成。
- 没有方向分类、页面边界检测、透视校正或弯曲展开模型。
- 没有训练过的印刷 / 手写 / 背景三分类模型，也没有受控的模型清单和发布权重。
- 现有 Inspector 和 Mask 编辑很适合作为失败时的可选兜底，不应被自动流程删除。

## 4. 开源方案调研

### 4.1 评估原则

代码许可证、预训练权重权利和数据集权利必须分别核验。代码仓库采用宽松许可证，不代表外链权重、训练数据或生成权重自动拥有相同许可。只有来源、固定版本、许可证文本、再分发权和 SHA-256 都明确的制品，才可以进入离线发布包。无法确认时一律不打包。

下表中的维护状态仅描述访问日所见，不是对未来维护的保证。所有链接访问于 2026-10-31。

| 候选 | 固定版本与状态 | 功能及运行 / 导出适配 | 源代码许可 | 预训练权重 | 数据集权利 | 离线再分发结论 |
| --- | --- | --- | --- | --- | --- | --- |
| [PaddleOCR](https://github.com/PaddlePaddle/PaddleOCR) 的 [PP-LCNet_x1_0_doc_ori](https://huggingface.co/PaddlePaddle/PP-LCNet_x1_0_doc_ori/tree/d3b95a6dff5fe8a94f2748e12b61cb26818a0df8) | 模型修订 `d3b95a6dff5fe8a94f2748e12b61cb26818a0df8`，6,754,166 bytes；访问时属于官方维护组件 | 四向页面方向分类，适合放在分割前；Paddle / ONNX 转换需要 Spike 验证 | PaddleOCR 为 Apache-2.0 | 模型卡标为 Apache-2.0 | 模型卡未授予任何训练数据的再分发权，本项目不取得训练集 | 可作为候选打包，但合入前仍需固定文件 SHA-256、保存许可证和 NOTICE，并验证导出一致性 |
| [PaddleOCR UVDoc](https://huggingface.co/PaddlePaddle/UVDoc/tree/16c3f0ea9c2f0c6a57e24160f7eeaa7574613fa3) | 模型修订 `16c3f0ea9c2f0c6a57e24160f7eeaa7574613fa3`，32,054,311 bytes；官方组件 | 文档几何展开，适合透视和弯曲校正；需验证普通平面试卷不会被过度变形 | PaddleOCR 为 Apache-2.0 | 模型卡标为 Apache-2.0 | 不随应用分发训练数据，训练数据权利需独立核验 | 与方向模型相同，可进入候选包，不等于已经批准发布 |
| [AndSonder/HandWritingEraser-Pytorch](https://github.com/AndSonder/HandWritingEraser-Pytorch/tree/b216bff33f2db184a0ba3c782fe70086bb3c754a) | 源提交 `b216bff33f2db184a0ba3c782fe70086bb3c754a`，2022-06-26；未见近期维护 | DeepLabV3+ 三分类，可直接提供印刷 / 手写 / 背景 Mask；PyTorch 原型可评估 ONNX 导出 | MIT | README 指向外部 checkpoint，但未给出独立、明确的权重再分发条款 | 外部数据集的独立再分发和衍生权重条款未在 README 明确 | 技术上最接近，法律门禁不通过；只能做受控本地 Spike，不得把外链权重或由权利不明数据产生的权重放入发布包 |
| [Yanting-K/Hand-Text-Erasure](https://github.com/Yanting-K/Hand-Text-Erasure/tree/57e59422377e87bc3179d695a51c8de27b8f17a3) | 源提交 `57e59422377e87bc3179d695a51c8de27b8f17a3`，2021-12-22；未见近期维护 | 基于 EraseNet 的端到端擦除，代码需要训练和适配，未提供可直接打包的 checkpoint | MIT，但 README 另要求商业使用联系作者，需法律确认实际适用边界 | 没有仓库内可用、许可明确的预训练权重 | 指向竞赛数据与 Mask，使用和再分发需单独获权 | 无可打包权重，且数据与商业条款不清楚；不进入发布集成 |
| [advimman/lama](https://github.com/advimman/lama/tree/786f5936b27fb3dacd2b1ad799e4de968ea697e7) 与 [smartywu/big-lama](https://huggingface.co/smartywu/big-lama/tree/05cb2be7f8dbe6ca7c6e78f4fc827a4b2baaa4a9) | LaMa 源提交 `786f5936b27fb3dacd2b1ad799e4de968ea697e7`，2025-02-05；模型修订 `05cb2be7f8dbe6ca7c6e78f4fc827a4b2baaa4a9`，381,428,720 bytes | 通用大 Mask 修复，不检测手写，必须由分割模型提供 Mask；体积和 CPU 内存是主要风险 | LaMa 为 Apache-2.0 | big-lama 模型卡标为 Apache-2.0 | 不需要随应用分发训练集，原训练数据合规仍应进入法律清单 | 权重许可比直接擦除方案清楚，可作为修复候选；只有尺寸、性能、质量、许可证归档全部过门禁后才可打包 |
| [Sanster/IOPaint](https://github.com/Sanster/IOPaint) | 访问时仓库已归档；框架而非单模型 | 提供多种交互式修复后端，但不检测手写；引入完整框架会重复现有 Qt / Worker 职责 | Apache-2.0 | 每个可下载模型有各自权利，框架许可证不能覆盖它们 | 各后端训练数据分别核验 | 不整体集成或打包；只参考模型适配与 Mask 修复流程 |
| [adbu42/WPI_inpainting](https://github.com/adbu42/WPI_inpainting) | 访问时未见仓库许可证；研究代码 | 组合独立分割与 LBAM 修复权重，接近两阶段任务，但工程和导出成本高 | 无仓库许可证，默认无复制、修改和再分发授权 | 分割和 LBAM 权重来自不同来源，仓库未给出统一的再分发授权 | 相关训练数据权利未形成可发布闭环 | 源、权重和数据门禁都不通过，不使用、不打包 |
| [SCUT-DLVCLab/SCUT-EnsExam](https://github.com/SCUT-DLVCLab/SCUT-EnsExam/tree/94de3ab7a1c01c63b8c70ec8070be9e7bd1b866b) | 文档修订 `94de3ab7a1c01c63b8c70ec8070be9e7bd1b866b`；545 对配对试卷，430 训练、115 测试，约 1.14 GB，需申请访问 | 与任务高度相关，可在获准环境做研究对照 | 仓库未提供可供产品集成的源代码许可证，本方案也不集成其代码 | 不提供本项目可发布的预训练权重 | CC BY-NC-ND 4.0，并注明非商业研究用途；访问还需申请 | 仅限按批准条件做研究评测；不得随应用再分发，也不得在未取得书面许可前用它训练或派生发布权重 |

### 4.2 HandWritingEraser 固定版本分析

以下结论来自固定提交 `b216bff33f2db184a0ba3c782fe70086bb3c754a` 的源码、[README](https://github.com/AndSonder/HandWritingEraser-Pytorch/blob/b216bff33f2db184a0ba3c782fe70086bb3c754a/README.md) 和 [MIT LICENSE](https://github.com/AndSonder/HandWritingEraser-Pytorch/blob/b216bff33f2db184a0ba3c782fe70086bb3c754a/LICENSE)，不把仓库 README 的自报结果视为独立复测：

- **任务和环境**：README 指定 Python 3.7、PyTorch 1.7+。`datasets/handwriting.py` 将标签定义为 `background=0`、`hand=1`、`print=2`，即背景、手写和印刷三分类。模型采用 [DeepLabV3+](https://arxiv.org/abs/1802.02611)，实现源自同为 MIT 的 [VainF/DeepLabV3Plus-Pytorch](https://github.com/VainF/DeepLabV3Plus-Pytorch)。训练可选 focal loss 处理类别不平衡，并通过重叠裁剪增加样本。
- **仓库报告指标**：README 汇总报告 Overall Acc `0.990526`、Mean Acc `0.920829`、FreqW Acc `0.982231`、Mean IoU `0.867805`。README 没有给出按手写和印刷类别拆分的召回率、误删率、评测硬件、重复实验或独立评测结果，因此这些汇总值不满足第 6 节产品门禁，也不能证明真实试卷上的擦除质量。
- **推理实际语义**：`predict_one.py` 把长边方向按 1024 像素块进行重叠分块预测，对手写 Mask 和印刷 Mask 做形态学扩展，并叠加红色阈值处理批改色。结果保留预测为印刷的像素，把其他像素置白。它没有修复纸张纹理，也没有重建被不透明手写完全遮住的印刷内容，不能称为图像修复或原始信息恢复。
- **切块与并发缺口**：脚本使用固定文件名 `tmp_img.png` 传递切块，多任务会发生冲突；重叠块按写入顺序覆盖，没有概率加权、边缘窗或确定性融合，接缝与顺序敏感性没有测试。
- **模型和预处理不一致**：README 的测试命令使用 ResNet-50 checkpoint 名，`main.py` 默认 `deeplabv3plus_resnet101`，`predict_one.py` 又硬编码 ResNet-101。训练路径以均值和标准差 `0.5` 归一化；预测脚本虽然声明了变换，却没有把该归一化变换应用到送入模型的切块。外链 checkpoint 的真实骨干和预处理契约必须实测确认，不能靠文件名推断。
- **复现和验证缺口**：仓库没有 tag 或 release，固定源码的最后提交日期为 2022-06-26；没有自动化测试、ONNX 导出或部署说明。`requirements.txt` 未固定版本，且 `predict_one.py` 使用的 `cv2` 没有列入依赖。`datasets/handwriting.py` 对排序后的文件做 85% / 15% 划分，但没有模板或试卷来源分组，不能证明训练集与验证集在模板级隔离。
- **外部反馈边界**：[GitHub issues](https://github.com/AndSonder/HandWritingEraser-Pytorch/issues) 中有数据目录不清楚和手写类别效果较弱等用户报告。这些报告只用于提示 Spike 覆盖相应风险，没有统一数据、版本和指标，属于轶事性证据，不能作为 benchmark 或否定模型的依据。

#### 4.2.1 源码、权重和数据权利必须分开

1. **源码**：固定提交内的代码由仓库 [MIT LICENSE](https://github.com/AndSonder/HandWritingEraser-Pytorch/blob/b216bff33f2db184a0ba3c782fe70086bb3c754a/LICENSE) 覆盖，可在遵守许可证通知的前提下研究和改造。
2. **checkpoint**：README 只提供[外部百度网盘 checkpoint 页面](https://pan.baidu.com/s/1QmCGhwpKNtdKtYoGU0YK3w?pwd=a2jh)。仓库没有为该二进制给出可独立核验的来源、架构清单、文件哈希、权重许可证或再分发授权。页面可访问性和页面内容可能受地区、登录及后续变更影响，本方案未独立确认其条款。
3. **数据集**：README 链接 [Baidu AI Studio 数据页](https://aistudio.baidu.com/aistudio/datasetdetail/121039) 和[外部百度网盘数据页面](https://pan.baidu.com/s/1l2mQ3FLx_NAPJEh30cOeZA?pwd=05bl)。源码的 MIT 许可证不覆盖这些外部数据，也不自动授予训练、派生权重或再分发权。本方案未从外部页面独立确认完整来源和授权链。

因此，外链 checkpoint 在完成来源、实际架构、SHA-256、许可证和再分发权核验前，不得随 MiniTreasureBox 打包、镜像、上传到本项目控制的存储，也不得成为发布构建依赖。数据集必须单独通过用途、隐私、训练和派生权重发布审查。H0 即使获准下载，也仅限隔离研究环境，不构成任何发布批准。

### 4.3 其他官方指标只能作为背景

Paddle 文档报告 [PP-LCNet_x1_0_doc_ori](https://www.paddleocr.ai/main/en/version3.x/module_usage/doc_img_orientation_classification.html) Top-1 为 99.06%、模型约 7 MB；[UVDoc](https://www.paddleocr.ai/main/en/version3.x/module_usage/text_image_unwarping.html) CER 为 0.179、模型约 30.3 MB。这些数字来自 Paddle 的数据、硬件和评测环境，不是 MiniTreasureBox 在真实试卷照片上的实测结果，也不能替代本方案的门禁。

### 4.4 选型建议

- 手写分割先执行第 6 节 HandWritingEraser-first Spike。只继承三分类任务定义和全分辨率重叠切块方向；仓库脚本、外链 checkpoint、切块临时文件、覆盖式拼接和白底输出都不能原样进入产品。
- 方向和几何校正可并行验证 Paddle 官方小模型，因为来源、版本和许可证相对清楚，但它们是分割前处理，不替代 H0 至 H4 对 HandWritingEraser 路线的决策。
- 修复同时比较确定性文档修复和 big-lama。表格、横线和印刷笔画应优先使用可解释的线条重建与空白原卷复制，不让通用生成模型任意改写页面。
- IOPaint 只作工程参考；Hand-Text-Erasure、WPI_inpainting 和 SCUT-EnsExam 不进入发布依赖。

## 5. 推荐架构

### 5.1 处理管线

```text
导入 / 拖入单张图片
  -> 输入解码、EXIF 方向、像素与内存预算检查
  -> 页面方向分类
  -> 页面边界、透视与弯曲校正
  -> 分块三分类：印刷 / 手写批改 / 背景
  -> 重叠块融合与保守 Mask 后处理
  -> 文档感知修复：纸面、印刷笔画、横线和表格分别处理
  -> 仅在接受 Mask 内合成
  -> 质量信号和可信度
  -> 自动打开结果视图
```

三分类分割使用带重叠和边缘权重的瓦片，避免把整张高分辨率照片缩小后丢失细笔画。后处理应移除孤立噪点、限制膨胀半径、检测 Mask 与印刷连通域及长直线的冲突，并保留一份未经编辑的自动 Mask 供“恢复自动 Mask”使用。

修复按内容选择策略：纯纸面使用局部颜色和低频照明估计；被笔迹切断的规则横线或表格线使用方向受限的几何连接；可从对齐空白原卷取得的区域复制真实像素；其余接受 Mask 内才允许使用经批准的修复模型。单图无法证明的印刷字符不生成“确定内容”标签。

几何校正会建立新的页面坐标系，属于显式预处理。进入该坐标系后，最终合成必须逐像素满足：接受 Mask 外的像素值与校正后的输入完全一致；所有擦除、生成、线条重建和空白原卷复制都只能写入接受 Mask。导出记录管线版本、模型哈希和可信度，但不得把原图路径或个人信息写入 PNG 元数据。

### 5.2 与现有代码的映射

- 保留 `src/ai/handwriting_removal/removal_engine.h` 的注入边界，扩展请求、阶段进度、结构化错误和结果元数据；`removal_engine.cpp` 的基线算法继续作为无模型兜底。
- 在 `src/ai/handwriting_removal/` 增加本地 Worker 引擎和协议层，做法复用 `src/ai/ocr/ocr_engine.*` 的离线进程模式，不把模型逻辑放进 `MainWindow`。
- `src/ai/handwriting_removal/removal_widget.*` 在导入成功后自动提交任务，并继续使用 `QThread` / task ID、线程中断、`ToolStatusStrip`、响应式 Canvas + Inspector、原有对象名和原子 PNG 导出。
- 运行时构建复用 `scripts/build_paddleocr_runtime.py` 的原则，但使用独立的 exam-restoration 构建脚本、固定依赖和模型 JSON 清单，不能把两种 Worker 的协议和生命周期耦合起来。
- 模型清单记录 URL、不可变版本、字节数、SHA-256、许可证文件、来源和用途。构建时下载并校验，安装后完全离线，不回退系统 Python，不在首次运行时下载。
- `src/domain/tools/builtin_tools.cpp` 的工具 ID 和工厂保持不变；`src/app/main_window.cpp` 只在确有新增动态属性时维护集中 QSS，不承载业务逻辑。

### 5.3 安全、隐私与可靠性

- 默认全程本地处理，Worker 禁止网络访问。UI 显示“本地处理”，协议和日志不得包含图片内容或绝对用户路径。
- 解码前检查格式和文件大小，解码后检查总像素、宽高、通道数及乘法溢出。初始建议上限为 40 MP，超限给出缩放副本选项，不覆盖原文件。
- 临时目录按 task ID 隔离，使用最小权限；结果经校验后读取；成功、失败、取消和崩溃都清理临时文件。启动时清理过期任务目录。
- Worker 使用有上限的 JSON 协议输出、固定 schema、request ID、阶段进度和结构化错误码。第三方 stdout / stderr 不得污染协议。
- 每阶段检查取消；进程具备软取消和硬终止上限。总任务设置 120 秒硬超时，UI 关闭时不无限等待。
- 新任务递增 task ID。任何 request ID 不匹配、页面已关闭或输入已替换的回调都丢弃，不能覆盖较新的结果。
- 模型和结果文件分别校验 SHA-256、尺寸、解码结果和像素预算。导出继续使用临时文件加原子替换，只允许 PNG，不覆盖未经确认的已有文件。
- 模型不可用、崩溃、超时或内存不足时，自动回退现有基线 Mask，并清楚标为“基础模式”；Inspector 仍可完成手工修正。

## 6. HandWritingEraser-first 分阶段 Spike

Spike 是实现前的固定 go / no-go 门禁，不修改正式应用或发布流程。主线从固定提交 `b216bff33f2db184a0ba3c782fe70086bb3c754a` 开始，而不是同时对所有候选做宽泛比较。各阶段必须保存环境、输入清单、命令、原始指标、失败样例和结论；上一阶段产物未冻结，不进入下一阶段。第 6.3 节数值是**待维护者批准的初始目标，不是仓库 README 指标，也不是 MiniTreasureBox 已测性能**。

### 6.1 H0 至 H4 阶段与产出

| 阶段 | 操作范围 | 必须产出与退出条件 |
| --- | --- | --- |
| **H0：隔离复现** | 仅在获授权、禁止发布制品流出的隔离研究环境中，检出固定提交，记录 Python、PyTorch、CUDA / CPU 和全部依赖，并在项目法律 / 数据负责人确认下载与研究用途有合法依据后取得外链 checkpoint。不得把权利不明的数据或权重复制到产品仓库、CI、发布缓存或项目镜像。 | 产出可重放命令、依赖锁定草案、checkpoint 文件名 / 字节数 / SHA-256、实际骨干识别结果、输入归一化实验和代表样例结果。无法合法取得制品、无法确认 checkpoint 与 ResNet-50 / 101 架构匹配，或固定源码仍不可复现，则记录原因并直接进入 H4 的“重训 / no-go”判断。 |
| **H1：冻结推理契约** | 以源码行为为起点，明确三类标签映射、颜色空间、缩放和均值 / 标准差；固定 checkpoint 架构；固定 1024 像素基准瓦片、实际重叠宽度、边缘窗、概率融合、argmax 和形态学参数。用内存或 task ID 隔离文件替代共享 `tmp_img.png`，禁止按顺序覆盖重叠块。 | 产出版本化推理规范、边界尺寸 / 相邻瓦片 / 顺序置换夹具和确定性结果哈希。同一输入不同遍历顺序必须产生相同概率融合与 Mask；训练、原脚本和冻结预处理差异必须形成可审计对照。未消除接缝、并发冲突或预处理歧义则不进入 H2。 |
| **H2：合法数据质量评测** | 在第 6.2 节模板隔离且权利明确的数据上，至少测三类混淆矩阵、手写类 recall、印刷类被判为手写的 false deletion，并完成手写残留、线表保护、OCR 一致性、Mask 外不变性和全页盲审。README 四项汇总指标只能列作背景。 | 产出总体和按笔色、模板、语言、拍摄条件分层的报告、95% 置信区间及失败样例集。第 6.3 节任一质量、隐私或 Mask 外不变性门禁失败，就不得以 README `0.990526` 等汇总值覆盖失败。 |
| **H3：ONNX 与桌面预算** | 对 H2 冻结模型导出 ONNX，锁定 opset、动态尺寸和算子；比较框架与 ONNX 输出；在 Windows 11 x64、macOS 13 Intel、macOS 13 Apple M1 的纯 CPU 档位测冷启动、连续 30 次任务、取消、峰值 RSS、可靠性和压缩包增量。 | 产出 ONNX 文件哈希、逐样本一致性、三平台延迟 / 内存 / 取消 / 可靠性报告、依赖和许可证清单、包体估算。ONNX、性能、可靠性、隐私、许可证或包体任一门禁失败则不能 go。 |
| **H4：决策** | 汇总 H0 至 H3 的证据，并再次独立审查源码、checkpoint、数据和派生制品的权利。 | 仅有三种书面结论：**go** 表示全部门禁通过且 checkpoint 获明确发布再分发权，可进入批准后的实现；**retrain** 表示任务定义有效但外链权利或模型门禁失败，按第 7 节使用合法数据独立训练；**no-go** 表示三分类路线达不到质量 / CPU 预算，或无法建立合法、可复现的训练评测闭环。不得以“有开源仓库”代替结论证据。 |

### 6.2 合法评测集与场景

建立至少 300 对内部评测页，空白页与人工标注后的照片成对。数据由贡献者原创或取得明确的评测、训练和内部共享授权，保留来源、授权范围、同意记录、采集日期和撤回流程。按试卷模板分组为互斥的 60% 调参、20% 验证、20% 最终测试，避免同一模板泄漏。最终测试集在门禁冻结后不得调参。

每页覆盖以下现实条件并在元数据中分层统计：黑色、蓝色、红色笔和铅笔；文字、勾叉、圆圈、涂改和大片批注；中文、英文、公式、答题线、表格、选择框和灰度插图；手机旋转、透视、轻度弯曲、阴影、反光、压缩、模糊和 6 MP 至 20 MP 分辨率。另保留至少 50 张无手写页检查误删。

SCUT-EnsExam 只能在单独、获批的研究环境中作为外部参考，不能复制到本评测集，也不能据此训练或产出发布权重，除非权利人另行书面许可。

### 6.3 固定门禁

下列原有质量、性能、隐私、可靠性、包体和 ONNX 门禁全部保留；H0 复现成功或 README 汇总指标较高都不能豁免任何一项。

| 维度 | 测量方法 | 初始 go 条件 |
| --- | --- | --- |
| 法律与来源 | 逐项审查代码、每个权重、训练数据、许可证、NOTICE、固定 URL 和 SHA-256 | 100% 制品可追溯且明确允许目标发布方式；任一项不明即 no-go |
| 手写类召回 | 对冻结三分类 Mask 与像素级真值计算 hand 类 recall，并按笔色、笔画和场景分层 | 最终测试集总体不低于 0.95，每个预定义场景分层不低于 0.90 |
| 手写残留 | 在真实手写标注区计算还原后仍可见墨迹像素占原墨迹像素比例，并人工复核 | 最终测试集均值不高于 5%，P95 不高于 12% |
| 印刷内容误删 | 在空白原卷可见印刷区域计算被错误改变的前景像素比例 | 均值不高于 0.5%，任何页面不高于 2% |
| 线条 / 表格保护 | 对空白原卷和结果的长线段做匹配 F1 | 总体 F1 不低于 0.98，每个场景分层不低于 0.95 |
| 未遮挡印刷 OCR 一致性 | 固定 OCR 对接受 Mask 外印刷区域计算字符错误率变化 | 相对校正输入的 CER 增量不高于 1.0 个百分点 |
| 全页视觉检查 | 两名评审盲审，严重缺陷指错误文字、断表格、大片残留或非 Mask 改写 | 至少 95% 页面无严重缺陷；分歧由第三人裁定 |
| Mask 外不变性 | 校正坐标中逐像素比较接受 Mask 外输入与结果 | 100% 相同，任何差异即 no-go |
| CPU 延迟 | 12 MP 页，冷启动后连续 30 次，报告中位数和 P95 | 每个平台 P95 不高于 10 秒，取消到进程结束不高于 2 秒 |
| 内存 | 记录 Worker 与应用合计峰值 RSS | 12 MP 页峰值不高于 1.5 GiB，40 MP 上限输入不高于 2.5 GiB |
| 包体 | 对比不含该功能模型的安装包 | 三个平台压缩安装包增量均不高于 500 MiB |
| 可靠性 | 每平台 1,000 次混合任务，含取消、损坏输入和连续换图 | 无崩溃、无陈旧结果覆盖、无临时文件泄漏；正常输入成功率不低于 99.5% |
| 隐私 | 阻断网络后运行并审查系统调用、日志和 PNG 元数据 | 功能完整；0 个网络请求；0 个包含图片或绝对用户路径的日志 / 元数据 |
| ONNX 一致性 | 同一冻结样本比较训练框架与 ONNX Runtime | 三分类 argmax Mask IoU 不低于 0.995；最终图 PSNR 不低于 45 dB；各质量指标差异不超过 0.5 个百分点 |

目标平台至少包括 Windows 11 x64（4 核 CPU、8 GiB RAM）、macOS 13 Intel（4 核 CPU、8 GiB RAM）和 macOS 13 Apple M1（8 GiB 统一内存），全程只用 CPU。Linux 作为开发验证平台记录但不阻塞当前发布矩阵。若某候选超过门禁，不通过降低图片质量、隐藏失败或联网计算绕过，应更换模型或进入训练提案。

## 7. H4 选择 retrain 后的独立训练预案

只有 H4 选择 **retrain**，即维护者已批准方案，且直接开源权重因质量、许可或打包任一门禁失败后，才新增以下工作区。本次提交不创建它。训练预案只继承 H0 至 H2 验证有效的 HandWritingEraser **三分类任务定义**和**全分辨率重叠切块方向**；不复制其权利未核验的数据、checkpoint、临时文件推理方式或未经验证的实现细节。模型结构、数据管线、融合、评测和导出在本项目中独立、可复现地实现：

```text
ml/exam_restoration/
├── README.md
├── configs/
│   ├── segmentation.yaml
│   └── restoration.yaml
├── datasets/
│   ├── README.md
│   ├── manifest.schema.json
│   └── splits/                 # 只存去身份化 ID 与许可元数据
├── synthetic/
│   ├── generate_overlays.py
│   └── assets/README.md
├── models/
│   ├── segmentation.py
│   └── restoration.py
├── training/
│   ├── train_segmentation.py
│   └── train_restoration.py
├── evaluation/
│   ├── evaluate.py
│   └── metrics.py
├── export/
│   ├── export_onnx.py
│   └── verify_onnx.py
├── tests/
│   ├── test_datasets.py
│   ├── test_models.py
│   └── test_export.py
└── model_cards/
    └── exam-restoration.md
```

原图、标注图、数据缓存、checkpoint、ONNX 和评测产物全部通过 `.gitignore` 排除，不进入 Git。仓库只保留 schema、去身份化清单模板、脚本、配置、测试和模型卡。每条数据记录来源、所有者、采集同意、允许用途、保留期限、撤回状态、内容哈希和来源证明；构建确定性 manifest，按稳定 ID 排序并固定随机种子、工具版本和划分哈希。未成年人试卷、姓名、学号、二维码等个人信息必须先取得合法同意并去身份化。来源或授权不完整的数据不能训练、验证或发布。

训练数据由有权使用的空白 / 标注配对页和许可明确的合成覆盖组成。合成器只使用许可明确的字体、纸面和笔迹资产，随机生成笔色、粗细、透明度、压力、勾叉、公式、涂改、拍摄几何和光照。训练、验证和最终测试严格按试卷模板或同源版式分组，真实测试集不混入训练；数据版本、模板组、确定性划分和清单哈希写入模型卡。

分割模型输出印刷、手写 / 批改、背景三类概率及不确定度；损失初案为类别加权交叉熵 + Dice / Focal，并对印刷误删和线条交叉施加更高代价。修复模型仅接收接受 Mask，并组合 Mask 内重建、感知、边界一致性和线条保持损失；Mask 外使用硬拷贝，不依赖损失近似保证。若确定性文档修复已过门禁，不训练生成修复模型。

每个发布候选必须有模型卡，说明数据范围、许可证、限制、分层指标、失败样例、模型哈希和目标平台。训练框架 checkpoint 不进入桌面集成。只有 ONNX 导出通过第 6 节一致性门禁，且运算符、动态尺寸、CPU 内存和三平台结果全部合格，才能进入应用阶段。

## 8. 批准后的实施阶段

### 阶段 A：冻结协议和模型供应链

可能改动：

- 新增 `packaging/exam-restoration-models.json`、固定依赖和第三方许可证清单。
- 新增独立 Worker 构建及自检脚本，原则对齐 `scripts/build_paddleocr_runtime.py`。
- 定义请求 / 响应 schema、阶段进度、错误码、超时、取消和模型自检。

退出条件：所有制品通过法律门禁；三平台 Worker 可离线自检；错误协议、哈希失败和缺失模型测试通过。否则不进入应用集成。

### 阶段 B：引擎与处理管线

可能改动：

- `src/ai/handwriting_removal/removal_engine.h/.cpp`
- `src/ai/handwriting_removal/` 下新增 Worker 引擎、协议和受控临时文件实现
- `CMakeLists.txt` 中增加源文件及开发运行时 staging

测试重点：扩展 `tests/unit/test_removal_engine.cpp`，覆盖三类 Mask、瓦片接缝、Mask 外逐像素不变、线表保护、空白原卷对齐拒绝、坏协议、超时、取消、像素上限、临时文件清理和 Worker 崩溃。退出条件是第 6 节质量、性能、隐私、可靠性和 ONNX 门禁全部通过。

### 阶段 C：零额外操作 UI

可能改动：

- `src/ai/handwriting_removal/removal_widget.h/.cpp`
- 必要时仅在 `src/app/main_window.cpp::themeStyleSheet()` 增加集中样式
- 保持 `src/domain/tools/builtin_tools.cpp` 中 `exam-restoration` ID 和工厂不变

导入成功自动启动；状态条显示进度与取消；成功自动切结果；Inspector 作为可选高级区。保留全部既有 `restoration...` 对象名、返回行为、可访问说明、宽屏双栏和窄屏单列。扩展 `tests/unit/test_removal_widget.cpp`，覆盖导入自动提交、无需 Mask 点击即可完成、进度、取消、task ID、换图、低可信度非阻塞提示、可选 Mask 编辑、原子导出和无模型回退。退出条件是 Qt offscreen 自动化及浅色、深色、System 主题人工检查通过。

### 阶段 D：发布打包与回滚

可能改动：

- `.github/workflows/release.yml`
- `CMakeLists.txt`
- 新增 exam-restoration 打包清单、构建脚本和许可证产物

Windows、macOS Intel 和 Apple Silicon 原生构建 Worker，校验固定 SHA-256、执行自检、收集第三方许可证，并验证安装后断网运行。不得在安装后下载模型或调用系统 Python。发布包缺任一模型、许可证或自检结果时构建失败。

用构建期开关控制新引擎。运行时模型初始化、自检、任务或资源门禁失败时回退 `BaselineHandwritingRemovalEngine`，保留手动 Inspector 和导出；回退状态明确展示，不能静默声称 AI 还原成功。若发布后发现误删或许可问题，下一补丁可关闭新引擎并移除对应制品，不改变文件格式或工具 ID。

退出条件：干净机器离线安装与处理通过，安装包尺寸过门禁，许可证清单完整，回滚演练成功，全量 CTest 通过。

## 9. 风险与待批准决策

主要风险：

- 黑色手写和印刷字外观接近，分割误删会改变题目含义。
- 强透视、弯曲、阴影和反光会放大后续误差，UVDoc 也可能过校正平面页面。
- 通用修复模型可能生成看似合理但并不存在的文字或断开表格。
- big-lama 体积和 CPU 内存可能超过桌面包预算。
- 开源代码、权重和数据的许可证不一致，最容易在“可下载”与“可再分发”之间误判。
- 配对试卷可能含个人信息，采集、去身份化、保留和撤回需要明确责任人。

进入 Spike 前需维护者批准：

1. 本文对“还原”和被遮挡内容的产品表述。
2. 第 6 节评测集规模、目标平台与全部数值门禁。
3. 是否允许在受控环境研究权重权利不明、但不进入发布包的候选。
4. 最大 500 MiB 包体增量和是否接受 big-lama 类通用修复模型。
5. 配对数据的采集主体、同意文本、保存期限、访问控制和删除流程。
6. 空白原卷对齐是否进入首个实现版本。
7. 若开源候选失败，是否批准创建 `ml/exam_restoration/` 并启动训练。

在这些决定和 Spike 结果获批前，不应添加模型、数据、训练代码、运行时依赖、打包流程、应用实现、测试或 CI。本提交只记录方案。

## 10. 本次验证说明

本次是纯文档变更。文档结构检查发现 23 个 Markdown 链接和 28 个唯一标题，所需固定提交、MIT 许可证、DeepLabV3+ 论文、VainF 上游实现以及 checkpoint / 数据集外部页面均已引用；上述关键外链在验证时返回 HTTP 200。`python3 -m unittest tests.unit.test_build_paddleocr_runtime` 运行 4 项测试并通过，`git diff --check` 通过，变更范围检查只列出 `docs/technical/exam-restoration-solution.md`。

按要求执行了 CMake 配置命令，但沙箱不存在 `/projects/sandbox/Qt/6.8.3/gcc_64`、`/projects/sandbox/.qt-sysroot/usr` 或系统 Qt 6，CMake 在 `CMakeLists.txt:12` 无法找到 `Qt6Config.cmake` / `qt6-config.cmake` 并退出。配置未完成，因此 C++ build 和 CTest 均不可用、没有被执行，也没有被宣称通过。没有 Dockerfile 可供镜像验证。
