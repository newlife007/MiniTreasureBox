#include "ai/handwriting_removal/removal_widget.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QMouseEvent>
#include <QMimeData>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QSaveFile>
#include <QSlider>
#include <QSpinBox>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>

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

}  // namespace

class RemovalCanvas final : public QWidget {
public:
    explicit RemovalCanvas(MaskDocument* document, QWidget* parent = nullptr)
        : QWidget(parent), document_(document)
    {
        setObjectName(QStringLiteral("restorationCanvas"));
        setAccessibleName(tr("试卷原图、Mask 和复原结果编辑区域"));
        setMinimumSize(420, 260);
        setMouseTracking(true);
    }

    void setResult(const QImage& result) { result_ = result; update(); }
    void setView(DisplayView view) { view_ = view; update(); }
    void setTool(EditTool tool) { tool_ = tool; }
    void setBrushRadius(int radius) { brushRadius_ = radius; }
    void setMaskOpacity(int opacity) { maskOpacity_ = opacity; update(); }
    void setEditedCallback(std::function<void(bool)> callback) { edited_ = std::move(callback); }

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
    std::function<void(bool)> edited_;
};

HandwritingRemovalWidget::HandwritingRemovalWidget(
    QWidget* parent, std::shared_ptr<const IHandwritingRemovalEngine> engine)
    : QWidget(parent)
    , engine_(engine ? std::move(engine)
                     : std::make_shared<BaselineHandwritingRemovalEngine>())
    , canvas_(new RemovalCanvas(&document_, this))
    , status_(new QLabel(tr("等待导入图片。内置功能是保守的本地图像基线，不是训练模型。"), this))
    , viewSelector_(new QComboBox(this))
    , brushSize_(new QSpinBox(this))
    , maskOpacity_(new QSlider(Qt::Horizontal, this))
    , automaticButton_(makeButton(tr("自动生成 Mask"), QStringLiteral("restorationAutoButton"),
                                   tr("运行保守的本地颜色对比检测，不使用训练模型"), this))
    , undoButton_(makeButton(tr("撤销"), QStringLiteral("restorationUndoButton"),
                             tr("撤销上一次 Mask 修改"), this))
    , redoButton_(makeButton(tr("重做"), QStringLiteral("restorationRedoButton"),
                             tr("重做上一次撤销的 Mask 修改"), this))
    , exportButton_(makeButton(tr("导出 PNG"), QStringLiteral("restorationExportButton"),
                               tr("将复原预览原子保存到用户选择的位置"), this))
{
    setObjectName(QStringLiteral("handwritingRemovalWidget"));
    setAcceptDrops(true);

    status_->setObjectName(QStringLiteral("restorationStatus"));
    status_->setAccessibleName(tr("试卷复原处理状态"));
    status_->setWordWrap(true);
    viewSelector_->setObjectName(QStringLiteral("restorationViewSelector"));
    viewSelector_->setAccessibleName(tr("选择原图、Mask 或结果视图"));
    viewSelector_->addItem(tr("原图"), static_cast<int>(DisplayView::Original));
    viewSelector_->addItem(tr("Mask"), static_cast<int>(DisplayView::Mask));
    viewSelector_->addItem(tr("结果"), static_cast<int>(DisplayView::Result));
    viewSelector_->setCurrentIndex(1);

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
    brushButton->setCheckable(true);
    eraserButton->setCheckable(true);
    rectangleButton->setCheckable(true);
    brushButton->setChecked(true);
    auto* toolGroup = new QButtonGroup(this);
    toolGroup->setExclusive(true);
    toolGroup->addButton(brushButton, static_cast<int>(EditTool::Brush));
    toolGroup->addButton(eraserButton, static_cast<int>(EditTool::Eraser));
    toolGroup->addButton(rectangleButton, static_cast<int>(EditTool::Rectangle));

    auto* sourceRow = new QHBoxLayout;
    sourceRow->addWidget(importButton);
    sourceRow->addWidget(automaticButton_);
    sourceRow->addWidget(undoButton_);
    sourceRow->addWidget(redoButton_);
    sourceRow->addWidget(restartButton);
    sourceRow->addWidget(clearButton);
    sourceRow->addStretch();

    auto* editRow = new QHBoxLayout;
    editRow->addWidget(brushButton);
    editRow->addWidget(eraserButton);
    editRow->addWidget(rectangleButton);
    editRow->addWidget(new QLabel(tr("画笔大小"), this));
    editRow->addWidget(brushSize_);
    editRow->addWidget(new QLabel(tr("Mask 透明度"), this));
    editRow->addWidget(maskOpacity_, 1);
    editRow->addWidget(viewSelector_);
    editRow->addWidget(compareButton);
    editRow->addWidget(exportButton_);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(sourceRow);
    layout->addLayout(editRow);
    layout->addWidget(canvas_, 1);
    layout->addWidget(status_);

    connect(importButton, &QPushButton::clicked, this, [this] { chooseImage(); });
    connect(automaticButton_, &QPushButton::clicked, this, [this] { startAutomaticProcessing(); });
    connect(undoButton_, &QPushButton::clicked, this, [this] {
        if (document_.undo()) refreshResult();
    });
    connect(redoButton_, &QPushButton::clicked, this, [this] {
        if (document_.redo()) refreshResult();
    });
    connect(restartButton, &QPushButton::clicked, this, [this] { restartMask(); });
    connect(clearButton, &QPushButton::clicked, this, [this] { clearMask(); });
    connect(exportButton_, &QPushButton::clicked, this, [this] { chooseExportPath(); });
    connect(viewSelector_, &QComboBox::currentIndexChanged, this, [this](int index) {
        canvas_->setView(static_cast<DisplayView>(viewSelector_->itemData(index).toInt()));
    });
    connect(brushSize_, &QSpinBox::valueChanged, canvas_, &RemovalCanvas::setBrushRadius);
    connect(maskOpacity_, &QSlider::valueChanged, canvas_, &RemovalCanvas::setMaskOpacity);
    connect(toolGroup, &QButtonGroup::idClicked, this,
            [this](int id) { canvas_->setTool(static_cast<EditTool>(id)); });
    connect(compareButton, &QPushButton::pressed, this, [this] {
        viewSelector_->setCurrentIndex(0);
    });
    connect(compareButton, &QPushButton::released, this, [this] {
        viewSelector_->setCurrentIndex(2);
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
    const QImage image = reader.read();
    if (image.isNull() || !document_.load(image)) {
        showError(tr("图片无法读取或格式不受支持。原文件未修改，请选择有效图片。"));
        return false;
    }
    ++automaticTaskId_;
    if (automaticThread_) automaticThread_->requestInterruption();
    cancelResultProcessing();
    automaticMask_ = {};
    result_ = document_.original().copy();
    resultValid_ = true;
    canvas_->setResult(result_);
    viewSelector_->setCurrentIndex(1);
    status_->setText(tr("图片已载入内存。原文件不会被覆盖，可生成候选 Mask 或手动编辑。"));
    refreshControls();
    return true;
}

void HandwritingRemovalWidget::startAutomaticProcessing()
{
    if (automaticThread_ || !document_.isLoaded()) {
        return;
    }
    cancelResultProcessing();
    const QImage original = document_.original().copy();
    const auto engine = engine_;
    const quint64 currentTask = ++automaticTaskId_;
    QPointer<HandwritingRemovalWidget> guard(this);
    setBusy(true);
    status_->setText(tr("正在运行保守的本地图像检测，不会上传图片。"));
    automaticThread_ = QThread::create([original, engine, guard, currentTask] {
        const RemovalResult result = engine->detectAndRemove(original, {}, [] {
            return QThread::currentThread()->isInterruptionRequested();
        });
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
    status_->setText(tr("Mask 已清空，内存中的原图仍保持不变。"));
}

void HandwritingRemovalWidget::restartMask()
{
    if (!document_.isLoaded() || automaticMask_.isNull() || automaticThread_) return;
    if (document_.setMask(automaticMask_)) {
        refreshResult();
        status_->setText(tr("已恢复最近一次自动生成的候选 Mask。"));
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
    const QString path = QFileDialog::getSaveFileName(
        this, tr("导出复原结果"), QStringLiteral("restored-exam.png"), tr("PNG 图片 (*.png)"));
    if (path.isEmpty()) return;
    QString error;
    if (exportPng(path, &error)) {
        status_->setText(tr("PNG 已导出到用户选择的位置，原图片未被覆盖。"));
    } else {
        showError(error);
    }
}

void HandwritingRemovalWidget::finishAutomatic(quint64 taskId, const RemovalResult& result)
{
    if (taskId != automaticTaskId_ || result.error == RemovalError::Cancelled) return;
    if (!result.ok || !document_.setMask(result.mask)) {
        showError(result.message.isEmpty() ? tr("本地基线处理失败，原图片仍保留。")
                                           : result.message);
        return;
    }
    automaticMask_ = result.mask.copy();
    result_ = result.restored.copy();
    resultValid_ = true;
    canvas_->setResult(result_);
    viewSelector_->setCurrentIndex(1);
    status_->setText(result.message);
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
    status_->setText(tr("正在后台更新复原预览。"));
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
    status_->setText(result.message);
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
        }
        if (resultThread_ == thread) resultThread_ = nullptr;
        thread->deleteLater();
    });
}

void HandwritingRemovalWidget::refreshControls()
{
    const bool loaded = document_.isLoaded();
    const bool idle = automaticThread_ == nullptr;
    automaticButton_->setEnabled(loaded && idle);
    undoButton_->setEnabled(loaded && idle && document_.canUndo());
    redoButton_->setEnabled(loaded && idle && document_.canRedo());
    exportButton_->setEnabled(loaded && idle && !resultPending_ && resultValid_
                              && !result_.isNull());
    canvas_->setEnabled(loaded && idle);
    canvas_->update();
}

void HandwritingRemovalWidget::setBusy(bool busy)
{
    automaticButton_->setEnabled(!busy && document_.isLoaded());
    undoButton_->setEnabled(!busy && document_.canUndo());
    redoButton_->setEnabled(!busy && document_.canRedo());
    exportButton_->setEnabled(!busy && !resultPending_ && resultValid_
                              && !result_.isNull());
    canvas_->setEnabled(!busy && document_.isLoaded());
}

void HandwritingRemovalWidget::showError(const QString& message)
{
    status_->setText(message);
}

QWidget* createHandwritingRemovalWidget(QWidget* parent)
{
    return new HandwritingRemovalWidget(parent);
}

}  // namespace mtb::handwriting
