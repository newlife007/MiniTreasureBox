#include "ai/handwriting_removal/removal_widget.h"
#include "ai/handwriting_removal/restoration_engine.h"
#include "ui/widgets/tool_workspace/responsive_pair.h"
#include "ui/widgets/tool_workspace/tool_status_strip.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QMimeData>
#include <QPainter>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QSlider>
#include <QSpinBox>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace mtb::handwriting {
namespace {

enum class EditTool { Brush, Eraser, Rectangle };
enum class DisplayView { Original, Mask, Result };

QPushButton* makeButton(const QString& text, const QString& objectName,
                        const QString& description, QWidget* parent)
{
    auto* button = new QPushButton(text, parent);
    button->setObjectName(objectName);
    button->setAccessibleName(text);
    button->setAccessibleDescription(description);
    button->setToolTip(description);
    return button;
}

qsizetype selectedPixelCount(const QImage& mask)
{
    if (mask.isNull()) return 0;
    const QImage grayscale = mask.format() == QImage::Format_Grayscale8
        ? mask : mask.convertToFormat(QImage::Format_Grayscale8);
    qsizetype selected = 0;
    for (int y = 0; y < grayscale.height(); ++y) {
        const uchar* line = grayscale.constScanLine(y);
        selected += std::count_if(line, line + grayscale.width(),
                                  [](uchar value) { return value != 0; });
    }
    return selected;
}

bool withinImageBudget(const QSize& size)
{
    return size.width() > 0 && size.height() > 0
        && static_cast<qint64>(size.width())
            <= MaximumImagePixels / static_cast<qint64>(size.height());
}

QString stageLabel(ProcessingStage stage)
{
    switch (stage) {
    case ProcessingStage::Validating:
        return QStringLiteral("校正方向");
    case ProcessingStage::Initializing:
        return QStringLiteral("校正文档");
    case ProcessingStage::Segmenting:
    case ProcessingStage::Fusing:
        return QStringLiteral("识别手写");
    case ProcessingStage::Restoring:
    case ProcessingStage::Completed:
        return QStringLiteral("还原纸面");
    }
    return QStringLiteral("还原纸面");
}

QString confidenceReason(const RestorationMetadata& metadata)
{
    if (metadata.confidenceReasons.isEmpty()) return {};
    return metadata.confidenceReasons.constFirst().simplified().left(80);
}

}  // namespace

class RemovalCanvas final : public QWidget {
public:
    explicit RemovalCanvas(MaskDocument* document, QWidget* parent = nullptr)
        : QWidget(parent), document_(document)
    {
        setObjectName(QStringLiteral("restorationCanvas"));
        setAccessibleName(tr("试卷原图、Mask 和复原结果编辑区域"));
        setMinimumSize(360, 260);
        setMouseTracking(true);
    }

    void setResult(const QImage& result) { result_ = result; update(); }
    void setView(DisplayView view) { view_ = view; update(); }
    void setTool(EditTool tool) { tool_ = tool; }
    void setBrushRadius(int radius) { brushRadius_ = radius; }
    void setMaskOpacity(int opacity) { maskOpacity_ = opacity; update(); }
    void setEditedCallback(std::function<void(bool)> callback) { edited_ = std::move(callback); }
    void setEditingStartedCallback(std::function<void()> callback)
    {
        editingStarted_ = std::move(callback);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), palette().brush(QPalette::Base));
        if (!document_->isLoaded()) {
            painter.setPen(palette().color(QPalette::Text));
            painter.drawText(rect(), Qt::AlignCenter,
                             tr("拖放或导入试卷图片。处理全程在本机完成。"));
            return;
        }

        QImage displayed;
        if (view_ == DisplayView::Original) {
            displayed = document_->original();
        } else if (view_ == DisplayView::Result && !result_.isNull()) {
            displayed = result_;
        } else {
            displayed = document_->original();
        }
        const QRect target = imageRect(displayed.size());
        painter.drawImage(target, displayed);

        if (view_ == DisplayView::Mask && !target.isEmpty()) {
            const QImage scaledMask = document_->mask().scaled(
                target.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation);
            QImage overlay(target.size(), QImage::Format_ARGB32_Premultiplied);
            const int alpha = qRound(maskOpacity_ * 255.0 / 100.0);
            for (int y = 0; y < scaledMask.height(); ++y) {
                const uchar* source = scaledMask.constScanLine(y);
                QRgb* destination = reinterpret_cast<QRgb*>(overlay.scanLine(y));
                for (int x = 0; x < scaledMask.width(); ++x) {
                    destination[x] = source[x] ? qRgba(220, 45, 45, alpha)
                                               : qRgba(0, 0, 0, 0);
                }
            }
            setProperty("maskOverlayPixelCount", overlay.width() * overlay.height());
            painter.drawImage(target, overlay);
        }

        if (draggingRectangle_) {
            painter.setPen(QPen(palette().color(QPalette::Highlight), 2, Qt::DashLine));
            painter.drawRect(QRect(rectangleStart_, rectangleEnd_).normalized());
        }
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton || !document_->isLoaded()) {
            return;
        }
        if (editingStarted_) editingStarted_();
        if (tool_ == EditTool::Rectangle) {
            draggingRectangle_ = true;
            rectangleStart_ = event->position().toPoint();
            rectangleEnd_ = rectangleStart_;
            update();
            return;
        }
        painting_ = document_->beginEdit();
        if (painting_) editAt(event->position().toPoint());
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (draggingRectangle_) {
            rectangleEnd_ = event->position().toPoint();
            update();
        } else if (painting_ && (event->buttons() & Qt::LeftButton)) {
            editAt(event->position().toPoint());
        }
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton) {
            return;
        }
        if (draggingRectangle_) {
            rectangleEnd_ = event->position().toPoint();
            const QPoint first = imagePoint(rectangleStart_);
            const QPoint second = imagePoint(rectangleEnd_);
            draggingRectangle_ = false;
            if (first.x() >= 0 && second.x() >= 0
                && document_->fillRectangle(QRect(first, second), true) && edited_) {
                edited_(true);
            }
            update();
        } else if (painting_) {
            painting_ = false;
            if (document_->endEdit() && edited_) {
                edited_(true);
            }
        }
    }

private:
    QRect imageRect(const QSize& imageSize) const
    {
        const QSize scaled = imageSize.scaled(size(), Qt::KeepAspectRatio);
        return QRect(QPoint((width() - scaled.width()) / 2,
                            (height() - scaled.height()) / 2), scaled);
    }

    QPoint imagePoint(const QPoint& widgetPoint) const
    {
        const QRect target = imageRect(document_->original().size());
        if (!target.contains(widgetPoint) || target.width() <= 0 || target.height() <= 0) {
            return {-1, -1};
        }
        const int x = (widgetPoint.x() - target.left()) * document_->original().width()
            / target.width();
        const int y = (widgetPoint.y() - target.top()) * document_->original().height()
            / target.height();
        return {qBound(0, x, document_->original().width() - 1),
                qBound(0, y, document_->original().height() - 1)};
    }

    void editAt(const QPoint& position)
    {
        const QPoint point = imagePoint(position);
        if (point.x() < 0) {
            return;
        }
        const QRect target = imageRect(document_->original().size());
        const int imageRadius = qMax(1, brushRadius_ * document_->original().width()
                                           / qMax(1, target.width()));
        if (document_->paint(point, imageRadius, tool_ == EditTool::Brush)) {
            if (edited_) edited_(false);
            update();
        }
    }

    MaskDocument* document_;
    QImage result_;
    DisplayView view_{DisplayView::Mask};
    EditTool tool_{EditTool::Brush};
    int brushRadius_{12};
    int maskOpacity_{55};
    bool painting_{false};
    bool draggingRectangle_{false};
    QPoint rectangleStart_;
    QPoint rectangleEnd_;
    std::function<void()> editingStarted_;
    std::function<void(bool)> edited_;
};

HandwritingRemovalWidget::HandwritingRemovalWidget(
    QWidget* parent, std::shared_ptr<const IHandwritingRemovalEngine> engine)
    : QWidget(parent)
    , engine_(engine ? std::move(engine)
                     : std::make_shared<BaselineHandwritingRemovalEngine>())
    , canvas_(new RemovalCanvas(&document_, this))
    , status_(new QLabel(tr("等待导入图片。导入后会立即在本机自动还原。"), this))
    , viewSelector_(new QComboBox(this))
    , brushSize_(new QSpinBox(this))
    , maskOpacity_(new QSlider(Qt::Horizontal, this))
    , automaticButton_(makeButton(tr("重新自动还原"), QStringLiteral("restorationAutoButton"),
                                   tr("重新运行本地自动还原并替换自动 Mask"), this))
    , cancelButton_(makeButton(tr("取消处理"), QStringLiteral("restorationCancelButton"),
                                tr("取消正在运行的试卷还原任务"), this))
    , undoButton_(makeButton(tr("撤销"), QStringLiteral("restorationUndoButton"),
                             tr("撤销上一次 Mask 修改"), this))
    , redoButton_(makeButton(tr("重做"), QStringLiteral("restorationRedoButton"),
                             tr("重做上一次撤销的 Mask 修改"), this))
    , exportButton_(makeButton(tr("导出 PNG"), QStringLiteral("restorationExportButton"),
                               tr("将复原预览原子保存到用户选择的位置"), this))
    , progress_(new QProgressBar(this))
    , statusStrip_(new mtb::ui::ToolStatusStrip(status_, this))
{
    setObjectName(QStringLiteral("handwritingRemovalWidget"));
    setAcceptDrops(true);

    status_->setObjectName(QStringLiteral("restorationStatus"));
    status_->setAccessibleName(tr("试卷复原处理状态"));
    status_->setWordWrap(true);
    statusStrip_->setObjectName(QStringLiteral("restorationStatusStrip"));
    statusStrip_->setState(mtb::ui::ToolUiState::Idle,
                           tr("等待导入图片。导入后会立即在本机自动还原。"));
    progress_->setObjectName(QStringLiteral("restorationProgress"));
    progress_->setRange(0, 100);
    progress_->setValue(0);
    progress_->setFormat(tr("总进度 %p%"));
    progress_->setTextVisible(true);
    viewSelector_->setObjectName(QStringLiteral("restorationViewSelector"));
    viewSelector_->setAccessibleName(tr("选择原图、Mask 或结果视图"));
    viewSelector_->addItem(tr("原图"), static_cast<int>(DisplayView::Original));
    viewSelector_->addItem(tr("Mask"), static_cast<int>(DisplayView::Mask));
    viewSelector_->addItem(tr("结果"), static_cast<int>(DisplayView::Result));
    viewSelector_->setCurrentIndex(1);
    viewSelector_->hide();

    brushSize_->setObjectName(QStringLiteral("restorationBrushSize"));
    brushSize_->setAccessibleName(tr("Mask 画笔大小"));
    brushSize_->setRange(2, 80);
    brushSize_->setValue(12);
    brushSize_->setSuffix(tr(" px"));
    maskOpacity_->setObjectName(QStringLiteral("restorationMaskOpacity"));
    maskOpacity_->setAccessibleName(tr("Mask 透明度"));
    maskOpacity_->setRange(10, 100);
    maskOpacity_->setValue(55);

    auto* importButton = makeButton(tr("导入图片"), QStringLiteral("restorationImportButton"),
                                    tr("从本地文件系统选择试卷图片"), this);
    auto* brushButton = makeButton(tr("画笔"), QStringLiteral("restorationBrushButton"),
                                   tr("在 Mask 中增加区域"), this);
    auto* eraserButton = makeButton(tr("橡皮擦"), QStringLiteral("restorationEraserButton"),
                                    tr("从 Mask 中移除区域"), this);
    auto* rectangleButton = makeButton(tr("矩形"), QStringLiteral("restorationRectangleButton"),
                                       tr("拖动矩形以增加 Mask 区域"), this);
    auto* compareButton = makeButton(tr("按住看原图"), QStringLiteral("restorationCompareButton"),
                                     tr("按住时显示原图，松开后显示结果"), this);
    auto* restartButton = makeButton(tr("恢复自动 Mask"), QStringLiteral("restorationRestartButton"),
                                     tr("放弃手动修改并恢复最近一次自动 Mask"), this);
    auto* clearButton = makeButton(tr("清空 Mask"), QStringLiteral("restorationClearButton"),
                                   tr("清除 Mask，但保留内存中的原图"), this);
    importButton->setProperty("toolButtonRole", QStringLiteral("secondary"));
    automaticButton_->setProperty("toolButtonRole", QStringLiteral("primary"));
    cancelButton_->setProperty("toolButtonRole", QStringLiteral("warning"));
    undoButton_->setProperty("toolButtonRole", QStringLiteral("secondary"));
    redoButton_->setProperty("toolButtonRole", QStringLiteral("secondary"));
    exportButton_->setProperty("toolButtonRole", QStringLiteral("primary"));
    for (QPushButton* button : {brushButton, eraserButton, rectangleButton,
                                compareButton, restartButton, clearButton}) {
        button->setProperty("toolButtonRole", QStringLiteral("quiet"));
    }
    brushButton->setCheckable(true);
    eraserButton->setCheckable(true);
    rectangleButton->setCheckable(true);
    brushButton->setChecked(true);
    auto* toolGroup = new QButtonGroup(this);
    toolGroup->setExclusive(true);
    toolGroup->addButton(brushButton, static_cast<int>(EditTool::Brush));
    toolGroup->addButton(eraserButton, static_cast<int>(EditTool::Eraser));
    toolGroup->addButton(rectangleButton, static_cast<int>(EditTool::Rectangle));

    auto* sourceBar = new QFrame(this);
    sourceBar->setObjectName(QStringLiteral("toolControlBar"));
    auto* sourceRow = new QHBoxLayout(sourceBar);
    sourceRow->setContentsMargins(12, 10, 12, 10);
    sourceRow->setSpacing(8);
    sourceRow->addWidget(importButton);
    sourceRow->addWidget(automaticButton_);
    sourceRow->addWidget(cancelButton_);
    sourceRow->addStretch();
    sourceRow->addWidget(undoButton_);
    sourceRow->addWidget(redoButton_);

    auto* canvasPanel = new QFrame(this);
    canvasPanel->setObjectName(QStringLiteral("toolEditorPanel"));
    auto* canvasTitle = new QLabel(tr("复原画布"), canvasPanel);
    canvasTitle->setObjectName(QStringLiteral("toolSectionTitle"));
    auto* canvasHint = new QLabel(tr("在 Mask 视图中标记需要移除的手写区域。所有处理仅在本机完成。"),
                                  canvasPanel);
    canvasHint->setObjectName(QStringLiteral("toolSectionDescription"));
    canvasHint->setWordWrap(true);
    auto* canvasLayout = new QVBoxLayout(canvasPanel);
    canvasLayout->setContentsMargins(14, 12, 14, 14);
    canvasLayout->setSpacing(8);
    canvasLayout->addWidget(canvasTitle);
    canvasLayout->addWidget(canvasHint);
    canvasLayout->addWidget(canvas_, 1);

    auto* inspector = new QFrame(this);
    inspector->setObjectName(QStringLiteral("toolInspectorPanel"));
    auto* inspectorTitle = new QLabel(tr("编辑与检查"), inspector);
    inspectorTitle->setObjectName(QStringLiteral("toolSectionTitle"));
    auto* toolTitle = new QLabel(tr("Mask 工具"), inspector);
    toolTitle->setObjectName(QStringLiteral("toolInspectorLabel"));
    auto* toolGrid = new QGridLayout;
    toolGrid->setSpacing(7);
    toolGrid->addWidget(brushButton, 0, 0);
    toolGrid->addWidget(eraserButton, 0, 1);
    toolGrid->addWidget(rectangleButton, 1, 0, 1, 2);

    auto* brushLabel = new QLabel(tr("画笔大小"), inspector);
    brushLabel->setObjectName(QStringLiteral("toolInspectorLabel"));
    auto* opacityLabel = new QLabel(tr("Mask 透明度"), inspector);
    opacityLabel->setObjectName(QStringLiteral("toolInspectorLabel"));

    auto* viewTitle = new QLabel(tr("查看"), inspector);
    viewTitle->setObjectName(QStringLiteral("toolInspectorLabel"));
    auto* originalViewButton = makeButton(tr("原图"), QStringLiteral("restorationOriginalViewButton"),
                                          tr("查看未修改的原图"), inspector);
    auto* maskViewButton = makeButton(tr("Mask"), QStringLiteral("restorationMaskViewButton"),
                                      tr("查看并编辑候选 Mask"), inspector);
    auto* resultViewButton = makeButton(tr("结果"), QStringLiteral("restorationResultViewButton"),
                                        tr("查看当前复原结果"), inspector);
    for (QPushButton* button : {originalViewButton, maskViewButton, resultViewButton}) {
        button->setCheckable(true);
        button->setProperty("toolButtonRole", QStringLiteral("quiet"));
    }
    maskViewButton->setChecked(true);
    auto* viewGroup = new QButtonGroup(this);
    viewGroup->setExclusive(true);
    viewGroup->addButton(originalViewButton, 0);
    viewGroup->addButton(maskViewButton, 1);
    viewGroup->addButton(resultViewButton, 2);
    auto* viewRow = new QHBoxLayout;
    viewRow->setSpacing(6);
    viewRow->addWidget(originalViewButton);
    viewRow->addWidget(maskViewButton);
    viewRow->addWidget(resultViewButton);

    auto* inspectorLayout = new QVBoxLayout(inspector);
    inspectorLayout->setContentsMargins(14, 12, 14, 14);
    inspectorLayout->setSpacing(8);
    inspectorLayout->addWidget(inspectorTitle);
    inspectorLayout->addWidget(toolTitle);
    inspectorLayout->addLayout(toolGrid);
    inspectorLayout->addWidget(brushLabel);
    inspectorLayout->addWidget(brushSize_);
    inspectorLayout->addWidget(opacityLabel);
    inspectorLayout->addWidget(maskOpacity_);
    inspectorLayout->addWidget(viewTitle);
    inspectorLayout->addLayout(viewRow);
    inspectorLayout->addWidget(compareButton);
    inspectorLayout->addSpacing(4);
    inspectorLayout->addWidget(restartButton);
    inspectorLayout->addWidget(clearButton);
    inspectorLayout->addStretch();
    inspectorLayout->addWidget(exportButton_);
    inspectorLayout->addWidget(viewSelector_);

    auto* pair = new mtb::ui::ResponsivePair(canvasPanel, inspector, 760, this);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    layout->addWidget(sourceBar);
    layout->addWidget(progress_);
    layout->addWidget(statusStrip_);
    layout->addWidget(pair, 1);

    connect(importButton, &QPushButton::clicked, this, [this] { chooseImage(); });
    connect(automaticButton_, &QPushButton::clicked, this, [this] { startAutomaticProcessing(); });
    connect(cancelButton_, &QPushButton::clicked, this, [this] {
        if (automaticThread_) {
            ++automaticTaskId_;
            automaticCancellationRequested_ = true;
            automaticRestartPending_ = false;
            automaticThread_->requestInterruption();
            cancelButton_->setEnabled(false);
            statusStrip_->setState(mtb::ui::ToolUiState::Warning,
                                   tr("正在取消试卷还原任务…"));
            return;
        }
        if (resultThread_) {
            cancelResultProcessing();
            statusStrip_->setState(mtb::ui::ToolUiState::Warning,
                                   tr("复原预览更新已取消，当前 Mask 保持不变。"));
            refreshControls();
        }
    });
    connect(undoButton_, &QPushButton::clicked, this, [this] {
        if (document_.undo()) refreshResult();
    });
    connect(redoButton_, &QPushButton::clicked, this, [this] {
        if (document_.redo()) refreshResult();
    });
    connect(restartButton, &QPushButton::clicked, this, [this] { restartMask(); });
    connect(clearButton, &QPushButton::clicked, this, [this] { clearMask(); });
    connect(exportButton_, &QPushButton::clicked, this, [this] { chooseExportPath(); });
    connect(viewSelector_, &QComboBox::currentIndexChanged, this,
            [this, viewGroup](int index) {
                canvas_->setView(static_cast<DisplayView>(viewSelector_->itemData(index).toInt()));
                if (auto* button = viewGroup->button(index)) button->setChecked(true);
            });
    connect(viewGroup, &QButtonGroup::idClicked, viewSelector_,
            [this](int index) { viewSelector_->setCurrentIndex(index); });
    connect(brushSize_, &QSpinBox::valueChanged, canvas_, &RemovalCanvas::setBrushRadius);
    connect(maskOpacity_, &QSlider::valueChanged, canvas_, &RemovalCanvas::setMaskOpacity);
    connect(toolGroup, &QButtonGroup::idClicked, this,
            [this](int id) { canvas_->setTool(static_cast<EditTool>(id)); });
    connect(compareButton, &QPushButton::pressed, this, [this] {
        compareViewIndex_ = viewSelector_->currentIndex();
        viewSelector_->setCurrentIndex(0);
    });
    connect(compareButton, &QPushButton::released, this, [this] {
        viewSelector_->setCurrentIndex(compareViewIndex_);
    });
    canvas_->setEditingStartedCallback([this] {
        if (viewSelector_->currentIndex() != 1) viewSelector_->setCurrentIndex(1);
    });
    canvas_->setEditedCallback([this](bool final) {
        if (final) {
            refreshResult();
        } else {
            resultValid_ = false;
            refreshControls();
        }
    });
    refreshControls();
}

HandwritingRemovalWidget::~HandwritingRemovalWidget()
{
    ++automaticTaskId_;
    ++resultTaskId_;
    const QVector<QThread*> workers = workerThreads_;
    for (QThread* thread : workers) thread->requestInterruption();
    for (QThread* thread : workers) {
        thread->disconnect(this);
        thread->wait();
        delete thread;
    }
}

bool HandwritingRemovalWidget::importImage(const QString& path)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize declaredSize = reader.size();
    if (declaredSize.isValid() && !withinImageBudget(declaredSize)) {
        showError(tr("图片超过 4000 万像素的本地处理上限。原图片未修改，请缩小后重试。"));
        return false;
    }
    const QImage image = reader.read();
    if (image.isNull() || !withinImageBudget(image.size()) || !document_.load(image)) {
        showError(image.isNull()
                      ? tr("图片无法读取或格式不受支持。原文件未修改，请选择有效图片。")
                      : tr("图片超过 4000 万像素的本地处理上限。原图片未修改，请缩小后重试。"));
        return false;
    }
    ++automaticTaskId_;
    automaticRestartPending_ = automaticThread_ != nullptr;
    if (automaticThread_) automaticThread_->requestInterruption();
    automaticCancellationRequested_ = false;
    cancelResultProcessing();
    automaticMask_ = {};
    result_ = {};
    resultValid_ = false;
    canvas_->setResult({});
    viewSelector_->setCurrentIndex(1);
    statusStrip_->setState(mtb::ui::ToolUiState::Running,
                           tr("校正方向 · 总进度 0% · 图片仅在本机处理。"));
    refreshControls();
    if (!automaticThread_) startAutomaticProcessing();
    return true;
}

void HandwritingRemovalWidget::startAutomaticProcessing()
{
    if (automaticThread_ || !document_.isLoaded()) {
        return;
    }
    automaticRestartPending_ = false;
    automaticCancellationRequested_ = false;
    cancelResultProcessing();
    const QImage original = document_.original().copy();
    const auto engine = engine_;
    const quint64 currentTask = ++automaticTaskId_;
    QPointer<HandwritingRemovalWidget> guard(this);
    setBusy(true);
    progress_->setRange(0, 100);
    progress_->setValue(0);
    statusStrip_->setState(mtb::ui::ToolUiState::Running,
                           tr("校正方向 · 总进度 0% · 图片仅在本机处理。"));
    automaticThread_ = QThread::create([original, engine, guard, currentTask] {
        const RemovalResult result = engine->detectAndRemoveWithProgress(
            original, {},
            [guard, currentTask](const RemovalProgress& progress) {
                if (guard) {
                    QMetaObject::invokeMethod(guard, [guard, currentTask, progress] {
                        if (guard) guard->updateAutomaticProgress(currentTask, progress);
                    }, Qt::QueuedConnection);
                }
            },
            [] { return QThread::currentThread()->isInterruptionRequested(); });
        if (guard) {
            QMetaObject::invokeMethod(guard, [guard, currentTask, result] {
                if (guard) guard->finishAutomatic(currentTask, result);
            }, Qt::QueuedConnection);
        }
    });
    trackWorker(automaticThread_);
    automaticThread_->start();
}

void HandwritingRemovalWidget::clearMask()
{
    if (!document_.isLoaded() || automaticThread_) return;
    document_.clearMask();
    refreshResult();
    statusStrip_->setState(mtb::ui::ToolUiState::Running,
                           tr("Mask 已清空，正在后台更新复原预览。"));
}

void HandwritingRemovalWidget::restartMask()
{
    if (!document_.isLoaded() || automaticMask_.isNull() || automaticThread_) return;
    if (document_.setMask(automaticMask_)) {
        refreshResult();
        statusStrip_->setState(mtb::ui::ToolUiState::Running,
                               tr("已恢复自动 Mask，正在后台更新复原预览。"));
    }
}

bool HandwritingRemovalWidget::exportPng(const QString& path, QString* error) const
{
    if (path.trimmed().isEmpty() || result_.isNull() || resultPending_ || !resultValid_) {
        if (error) *error = tr("没有可导出的最新结果、结果仍在处理，或导出路径无效。");
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = tr("无法写入所选位置，请检查目录权限。");
        return false;
    }
    if (!result_.save(&file, "PNG") || !file.commit()) {
        if (error) *error = tr("PNG 导出失败，原图和当前编辑仍保留在内存中。");
        return false;
    }
    return true;
}

void HandwritingRemovalWidget::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        for (const QUrl& url : event->mimeData()->urls()) {
            if (url.isLocalFile()) {
                event->acceptProposedAction();
                return;
            }
        }
    }
    event->ignore();
}

void HandwritingRemovalWidget::dropEvent(QDropEvent* event)
{
    for (const QUrl& url : event->mimeData()->urls()) {
        if (url.isLocalFile() && importImage(url.toLocalFile())) {
            event->acceptProposedAction();
            return;
        }
    }
    event->ignore();
}

void HandwritingRemovalWidget::chooseImage()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("选择本地试卷图片"), {},
        tr("图片文件 (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.webp);;所有文件 (*)"));
    if (!path.isEmpty()) (void)importImage(path);
}

void HandwritingRemovalWidget::chooseExportPath()
{
    const auto choice = QMessageBox::warning(
        this, tr("导出前检查"),
        tr("请先确认复原结果。复杂纹理和被手写内容覆盖的区域可能无法无损恢复。\n\n是否继续导出当前结果？"),
        QMessageBox::Save | QMessageBox::Cancel, QMessageBox::Cancel);
    if (choice != QMessageBox::Save) return;

    const QString path = QFileDialog::getSaveFileName(
        this, tr("导出复原结果"), QStringLiteral("restored-exam.png"), tr("PNG 图片 (*.png)"));
    if (path.isEmpty()) return;
    QString error;
    if (exportPng(path, &error)) {
        statusStrip_->setState(mtb::ui::ToolUiState::Success,
                               tr("PNG 已导出到用户选择的位置，原图片未被覆盖。"));
    } else {
        showError(error);
    }
}

void HandwritingRemovalWidget::updateAutomaticProgress(
    quint64 taskId, const RemovalProgress& progress)
{
    if (taskId != automaticTaskId_ || !automaticThread_) return;
    const int percent = qBound(0, progress.percent, 100);
    progress_->setRange(0, 100);
    progress_->setValue(percent);
    statusStrip_->setState(
        mtb::ui::ToolUiState::Running,
        tr("%1 · 总进度 %2% · 图片仅在本机处理。")
            .arg(stageLabel(progress.stage)).arg(percent));
}

void HandwritingRemovalWidget::finishAutomatic(quint64 taskId, const RemovalResult& result)
{
    if (taskId != automaticTaskId_ || result.error == RemovalError::Cancelled) return;
    if (!result.ok || !document_.setMask(result.mask)) {
        result_ = {};
        resultValid_ = false;
        canvas_->setResult({});
        showError(result.message.isEmpty() ? tr("试卷还原失败，原图片仍保留，可重新自动还原。")
                                           : result.message);
        refreshControls();
        return;
    }
    automaticMask_ = result.mask.copy();
    result_ = result.restored.copy();
    resultValid_ = true;
    canvas_->setResult(result_);
    viewSelector_->setCurrentIndex(2);
    progress_->setRange(0, 100);
    progress_->setValue(100);

    const qsizetype candidatePixels = selectedPixelCount(result.mask);
    const QString reason = confidenceReason(result.metadata);
    QString message;
    mtb::ui::ToolUiState state = mtb::ui::ToolUiState::Success;
    if (result.metadata.executionMode == ExecutionMode::Baseline) {
        state = mtb::ui::ToolUiState::Warning;
        message = tr("基础模式：未使用已批准的 AI 模型。结果可查看和导出，建议在 Mask 视图检查并按需修正。");
    } else {
        switch (result.metadata.confidence) {
        case ConfidenceLevel::High:
            message = reason.isEmpty()
                ? tr("高置信度：本地还原已完成并显示结果。")
                : tr("高置信度：%1。已显示本地还原结果。").arg(reason);
            break;
        case ConfidenceLevel::Medium:
            state = mtb::ui::ToolUiState::Warning;
            message = reason.isEmpty()
                ? tr("中置信度：结果可查看和导出，建议检查 Mask。")
                : tr("中置信度：%1。结果可查看和导出，建议检查 Mask。").arg(reason);
            break;
        case ConfidenceLevel::Low:
            state = mtb::ui::ToolUiState::Warning;
            message = reason.isEmpty()
                ? tr("低置信度：结果可查看和导出，可选检查 Mask 和原图后再修正。")
                : tr("低置信度：%1。结果可查看和导出，可选检查 Mask 和原图后再修正。")
                      .arg(reason);
            break;
        case ConfidenceLevel::Unknown:
            state = mtb::ui::ToolUiState::Warning;
            message = tr("置信度未标定：结果可查看和导出，建议检查 Mask。");
            break;
        }
    }
    if (candidatePixels == 0) {
        state = mtb::ui::ToolUiState::Warning;
        message += tr(" 未识别到候选手写区域，可选使用 Mask 工具标记。");
    }
    statusStrip_->setState(state, message);
    refreshControls();
}

void HandwritingRemovalWidget::refreshResult()
{
    if (!document_.isLoaded()) return;
    cancelResultProcessing();
    const QImage original = document_.original();
    const QImage mask = document_.mask();
    const auto engine = engine_;
    const quint64 currentTask = ++resultTaskId_;
    QPointer<HandwritingRemovalWidget> guard(this);
    resultPending_ = true;
    resultValid_ = false;
    statusStrip_->setState(mtb::ui::ToolUiState::Running,
                           tr("正在后台更新复原预览。"));
    resultThread_ = QThread::create([original, mask, engine, guard, currentTask] {
        const RemovalResult result = engine->removeWithMask(original, mask, {}, [] {
            return QThread::currentThread()->isInterruptionRequested();
        });
        if (guard) {
            QMetaObject::invokeMethod(guard, [guard, currentTask, result] {
                if (guard) guard->finishResult(currentTask, result);
            }, Qt::QueuedConnection);
        }
    });
    trackWorker(resultThread_);
    resultThread_->start();
    refreshControls();
}

void HandwritingRemovalWidget::finishResult(quint64 taskId, const RemovalResult& result)
{
    if (taskId != resultTaskId_ || result.error == RemovalError::Cancelled) return;
    resultPending_ = false;
    if (!result.ok) {
        showError(result.message);
        refreshControls();
        return;
    }
    result_ = result.restored;
    resultValid_ = true;
    canvas_->setResult(result_);
    viewSelector_->setCurrentIndex(2);
    statusStrip_->setState(mtb::ui::ToolUiState::Success, result.message);
    refreshControls();
}

void HandwritingRemovalWidget::cancelResultProcessing()
{
    ++resultTaskId_;
    resultPending_ = false;
    if (resultThread_) {
        resultThread_->requestInterruption();
        resultThread_ = nullptr;
    }
}

void HandwritingRemovalWidget::trackWorker(QThread* thread)
{
    workerThreads_.append(thread);
    connect(thread, &QThread::finished, this, [this, thread] {
        workerThreads_.removeAll(thread);
        if (automaticThread_ == thread) {
            automaticThread_ = nullptr;
            setBusy(false);
            refreshControls();
            if (automaticRestartPending_) {
                automaticRestartPending_ = false;
                startAutomaticProcessing();
            } else if (automaticCancellationRequested_) {
                automaticCancellationRequested_ = false;
                statusStrip_->setState(mtb::ui::ToolUiState::Warning,
                                       tr("试卷还原任务已取消，当前图片和编辑保持不变。"));
            }
        }
        if (resultThread_ == thread) resultThread_ = nullptr;
        thread->deleteLater();
    });
}

void HandwritingRemovalWidget::refreshControls()
{
    const bool loaded = document_.isLoaded();
    const bool automaticIdle = automaticThread_ == nullptr;
    const bool processing = !automaticIdle || resultPending_;
    automaticButton_->setText(automaticIdle ? tr("重新自动还原") : tr("正在自动还原…"));
    automaticButton_->setEnabled(loaded && automaticIdle);
    cancelButton_->setEnabled(loaded && processing);
    if (resultPending_ && automaticIdle) {
        progress_->setRange(0, 0);
    } else {
        progress_->setRange(0, 100);
        if (!processing) progress_->setValue(resultValid_ ? 100 : 0);
    }
    undoButton_->setEnabled(loaded && automaticIdle && document_.canUndo());
    redoButton_->setEnabled(loaded && automaticIdle && document_.canRedo());
    exportButton_->setEnabled(loaded && automaticIdle && !resultPending_ && resultValid_
                              && !result_.isNull());
    canvas_->setEnabled(loaded && automaticIdle);
    canvas_->update();
}

void HandwritingRemovalWidget::setBusy(bool busy)
{
    automaticButton_->setText(busy ? tr("正在自动还原…") : tr("重新自动还原"));
    automaticButton_->setEnabled(!busy && document_.isLoaded());
    cancelButton_->setEnabled(busy || resultPending_);
    const bool processing = busy || resultPending_;
    if (resultPending_ && !busy) {
        progress_->setRange(0, 0);
    } else {
        progress_->setRange(0, 100);
        if (!processing) progress_->setValue(resultValid_ ? 100 : 0);
    }
    undoButton_->setEnabled(!busy && document_.canUndo());
    redoButton_->setEnabled(!busy && document_.canRedo());
    exportButton_->setEnabled(!busy && !resultPending_ && resultValid_
                              && !result_.isNull());
    canvas_->setEnabled(!busy && document_.isLoaded());
}

void HandwritingRemovalWidget::showError(const QString& message)
{
    statusStrip_->setState(mtb::ui::ToolUiState::Error, message);
}

QWidget* createHandwritingRemovalWidget(QWidget* parent)
{
    return new HandwritingRemovalWidget(
        parent, std::make_shared<RestorationWorkerEngine>());
}

}  // namespace mtb::handwriting
