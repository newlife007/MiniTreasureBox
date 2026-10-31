#include "ai/ocr/ocr_widget.h"
#include "ui/widgets/tool_workspace/responsive_pair.h"
#include "ui/widgets/tool_workspace/tool_status_strip.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QCoreApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QMimeData>
#include <QPointer>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QPixmap>
#include <QResizeEvent>
#include <QSaveFile>
#include <QSettings>
#include <QTextEdit>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <functional>
#include <utility>

namespace mtb::ocr {
namespace {

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

class OcrProcessTask final : public QObject {
public:
    using Completion = std::function<void(const OcrResult&)>;

    OcrProcessTask(OcrRequest request, Completion completion, QObject* parent)
        : QObject(parent)
        , request_(std::move(request))
        , completion_(std::move(completion))
        , process_(new QProcess(this))
        , deadline_(new QTimer(this))
    {
        deadline_->setSingleShot(true);
        connect(deadline_, &QTimer::timeout, this, [this] {
            stop({false, {}, OcrError::TimedOut,
                  QStringLiteral("本地 PaddleOCR 识别超时。原图片未修改，可以稍后重试。")});
        });
        connect(process_, &QProcess::readyReadStandardOutput, this, [this] {
            if (!appendProtocolOutput(process_->readAllStandardOutput())) {
                stop({false, {}, OcrError::ProtocolError,
                      QStringLiteral("应用内置 PaddleOCR 组件返回的数据过大，识别已停止。")});
            }
        });
        connect(process_, &QProcess::errorOccurred, this,
                [this](QProcess::ProcessError error) {
                    if (reported_ || error != QProcess::FailedToStart) return;
                    complete({false, {}, OcrError::RuntimeMissing,
                              QStringLiteral("应用内置 PaddleOCR 组件无法启动，请修复或重新安装应用。")});
                });
        connect(process_, &QProcess::finished, this,
                [this](int exitCode, QProcess::ExitStatus exitStatus) {
                    if (!appendProtocolOutput(process_->readAllStandardOutput()) && !reported_) {
                        complete({false, {}, OcrError::ProtocolError,
                                  QStringLiteral("应用内置 PaddleOCR 组件返回的数据过大，识别已停止。")});
                        return;
                    }
                    if (reported_) {
                        deleteLater();
                        return;
                    }
                    if (standardOutput_.trimmed().isEmpty()) {
                        complete({false, {}, OcrError::ProcessFailed,
                                  QStringLiteral("本地 PaddleOCR 组件未返回结果。原图片未修改，请稍后重试。")});
                        return;
                    }
                    const OcrResult response = PaddleOcrEngine::parseResponse(standardOutput_);
                    if (!response.ok) {
                        complete(response);
                    } else if (exitStatus != QProcess::NormalExit || exitCode != 0) {
                        complete({false, {}, OcrError::ProcessFailed,
                                  QStringLiteral("本地 PaddleOCR 进程异常退出。原图片未修改，请稍后重试。")});
                    } else {
                        complete(response);
                    }
                });
    }

    void start()
    {
        deadline_->start(qMax(1, request_.timeoutMs));
        process_->setProcessChannelMode(QProcess::SeparateChannels);
        process_->setStandardErrorFile(QProcess::nullDevice());
        process_->setProgram(request_.workerExecutable);
        process_->setArguments(PaddleOcrEngine::argumentsFor(request_));
        process_->start(QIODevice::ReadOnly);
    }

    void cancel()
    {
        stop({false, {}, OcrError::Cancelled,
              QStringLiteral("识别已取消。原图片未修改。")});
    }

    void abandon()
    {
        completion_ = {};
        deadline_->stop();
        reported_ = true;
        if (process_->state() != QProcess::NotRunning) process_->kill();
        setParent(QCoreApplication::instance());
        if (process_->state() == QProcess::NotRunning) deleteLater();
    }

private:
    bool appendProtocolOutput(const QByteArray& data)
    {
        constexpr qsizetype maximumProtocolOutput = 1024 * 1024;
        if (data.size() > maximumProtocolOutput - standardOutput_.size()) {
            return false;
        }
        standardOutput_.append(data);
        return true;
    }

    void stop(const OcrResult& result)
    {
        if (reported_) return;
        const bool running = process_->state() != QProcess::NotRunning;
        complete(result);
        if (running) process_->kill();
    }

    void complete(const OcrResult& result)
    {
        if (reported_) return;
        reported_ = true;
        deadline_->stop();
        Completion completion = std::move(completion_);
        if (completion) completion(result);
        if (process_->state() == QProcess::NotRunning) deleteLater();
    }

    OcrRequest request_;
    Completion completion_;
    QProcess* process_;
    QTimer* deadline_;
    QByteArray standardOutput_;
    bool reported_{false};
};

OcrWidget::OcrWidget(QWidget* parent)
    : QWidget(parent)
    , preview_(new QLabel(this))
    , sourceInfo_(new QLabel(this))
    , status_(new QLabel(tr("等待导入图片。"), this))
    , language_(new QComboBox(this))
    , result_(new QTextEdit(this))
    , recognizeButton_(makeButton(tr("开始识别"), QStringLiteral("ocrRecognizeButton"),
                                  tr("使用应用内置的离线 OCR 组件识别当前图片"), this))
    , cancelButton_(makeButton(tr("取消"), QStringLiteral("ocrCancelButton"),
                               tr("取消正在运行的本地识别任务"), this))
    , retryButton_(makeButton(tr("重试"), QStringLiteral("ocrRetryButton"),
                              tr("使用当前配置重新识别图片"), this))
    , copyButton_(makeButton(tr("复制结果"), QStringLiteral("ocrCopyButton"),
                             tr("将可编辑识别结果复制到剪贴板"), this))
    , exportButton_(makeButton(tr("导出"), QStringLiteral("ocrExportButton"),
                               tr("将识别结果保存为 TXT 或 Markdown 文件"), this))
    , progress_(new QProgressBar(this))
    , statusStrip_(new mtb::ui::ToolStatusStrip(status_, this))
{
    setObjectName(QStringLiteral("ocrWidget"));
    setAcceptDrops(true);

    language_->setObjectName(QStringLiteral("ocrLanguage"));
    language_->setAccessibleName(tr("OCR 识别语言"));
    language_->addItem(tr("简体中文和英文（PP-OCRv5）"), QStringLiteral("ch"));
    const QString savedLanguage = QSettings().value(
        QStringLiteral("ai/ocr/language"), QStringLiteral("ch")).toString();
    const QString migratedLanguage = savedLanguage == QStringLiteral("ch")
        ? savedLanguage : QStringLiteral("ch");
    const int savedLanguageIndex = language_->findData(migratedLanguage);
    language_->setCurrentIndex(savedLanguageIndex >= 0 ? savedLanguageIndex : 0);

    preview_->setObjectName(QStringLiteral("ocrPreview"));
    preview_->setAccessibleName(tr("待识别图片预览"));
    preview_->setAlignment(Qt::AlignCenter);
    preview_->setMinimumSize(280, 220);
    preview_->setText(tr("拖放图片到此处，或使用导入、粘贴按钮。"));
    preview_->setWordWrap(true);
    sourceInfo_->setObjectName(QStringLiteral("ocrSourceInfo"));
    sourceInfo_->setText(tr("尚未选择图片"));
    sourceInfo_->setWordWrap(true);

    status_->setObjectName(QStringLiteral("ocrStatus"));
    status_->setAccessibleName(tr("OCR 处理状态"));
    status_->setWordWrap(true);
    statusStrip_->setObjectName(QStringLiteral("ocrStatusStrip"));
    statusStrip_->setState(mtb::ui::ToolUiState::Idle, tr("等待导入图片。"));

    result_->setObjectName(QStringLiteral("ocrResult"));
    result_->setAccessibleName(tr("可编辑 OCR 识别结果"));
    result_->setPlaceholderText(tr("识别结果会显示在这里，并且可以编辑。"));
    progress_->setObjectName(QStringLiteral("ocrProgress"));
    progress_->setRange(0, 0);
    progress_->setTextVisible(false);
    progress_->hide();

    auto* importButton = makeButton(tr("导入图片"), QStringLiteral("ocrImportButton"),
                                    tr("从本地文件系统选择图片"), this);
    auto* pasteButton = makeButton(tr("粘贴图片"), QStringLiteral("ocrPasteButton"),
                                   tr("从系统剪贴板粘贴图片并仅在本机处理"), this);
    auto* clearButton = makeButton(tr("清空"), QStringLiteral("ocrClearButton"),
                                   tr("清除当前图片和识别结果"), this);
    importButton->setProperty("toolButtonRole", QStringLiteral("secondary"));
    pasteButton->setProperty("toolButtonRole", QStringLiteral("secondary"));
    recognizeButton_->setProperty("toolButtonRole", QStringLiteral("primary"));
    cancelButton_->setProperty("toolButtonRole", QStringLiteral("warning"));
    retryButton_->setProperty("toolButtonRole", QStringLiteral("secondary"));
    copyButton_->setProperty("toolButtonRole", QStringLiteral("secondary"));
    exportButton_->setProperty("toolButtonRole", QStringLiteral("primary"));
    clearButton->setProperty("toolButtonRole", QStringLiteral("quiet"));

    auto* leftPanel = new QFrame(this);
    leftPanel->setObjectName(QStringLiteral("toolEditorPanel"));
    auto* leftTitle = new QLabel(tr("图片输入"), leftPanel);
    leftTitle->setObjectName(QStringLiteral("toolSectionTitle"));
    auto* sourceActions = new QHBoxLayout;
    sourceActions->setSpacing(8);
    sourceActions->addWidget(importButton);
    sourceActions->addWidget(pasteButton);
    sourceActions->addStretch();
    auto* configuration = new QFormLayout;
    configuration->addRow(tr("识别模型"), language_);
    auto* recognizeActions = new QHBoxLayout;
    recognizeActions->setSpacing(8);
    recognizeActions->addWidget(recognizeButton_);
    recognizeActions->addWidget(cancelButton_);
    recognizeActions->addWidget(retryButton_);
    recognizeActions->addStretch();
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(14, 12, 14, 14);
    leftLayout->setSpacing(8);
    leftLayout->addWidget(leftTitle);
    leftLayout->addWidget(preview_, 1);
    leftLayout->addWidget(sourceInfo_);
    leftLayout->addLayout(sourceActions);
    leftLayout->addLayout(configuration);
    leftLayout->addLayout(recognizeActions);

    auto* rightPanel = new QFrame(this);
    rightPanel->setObjectName(QStringLiteral("toolEditorPanel"));
    auto* rightTitle = new QLabel(tr("识别结果"), rightPanel);
    rightTitle->setObjectName(QStringLiteral("toolSectionTitle"));
    auto* resultCount = new QLabel(tr("0 字符"), rightPanel);
    resultCount->setObjectName(QStringLiteral("ocrResultCount"));
    auto* resultHeader = new QHBoxLayout;
    resultHeader->addWidget(rightTitle);
    resultHeader->addStretch();
    resultHeader->addWidget(resultCount);
    auto* resultActions = new QHBoxLayout;
    resultActions->setSpacing(8);
    resultActions->addWidget(copyButton_);
    resultActions->addWidget(exportButton_);
    resultActions->addStretch();
    resultActions->addWidget(clearButton);
    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(14, 12, 14, 14);
    rightLayout->setSpacing(8);
    rightLayout->addLayout(resultHeader);
    rightLayout->addWidget(result_, 1);
    rightLayout->addLayout(resultActions);

    auto* pair = new mtb::ui::ResponsivePair(leftPanel, rightPanel, 700, this);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    layout->addWidget(pair, 1);
    layout->addWidget(progress_);
    layout->addWidget(statusStrip_);

    connect(result_, &QTextEdit::textChanged, this, [this, resultCount] {
        resultCount->setText(tr("%1 字符").arg(result_->toPlainText().size()));
    });

    cancelButton_->setEnabled(false);
    retryButton_->setEnabled(false);
    recognizeButton_->setEnabled(false);
    copyButton_->setEnabled(false);
    exportButton_->setEnabled(false);

    connect(importButton, &QPushButton::clicked, this, [this] { chooseImage(); });
    connect(pasteButton, &QPushButton::clicked, this, [this] { (void)pasteImage(); });
    connect(recognizeButton_, &QPushButton::clicked, this, [this] { startRecognition(); });
    connect(cancelButton_, &QPushButton::clicked, this, [this] { cancelRecognition(); });
    connect(retryButton_, &QPushButton::clicked, this, [this] { startRecognition(); });
    connect(copyButton_, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(result_->toPlainText());
        statusStrip_->setState(mtb::ui::ToolUiState::Success,
                               tr("识别结果已复制到剪贴板。"));
    });
    connect(exportButton_, &QPushButton::clicked, this, [this] { chooseExportPath(); });
    connect(clearButton, &QPushButton::clicked, this, [this] { clearAll(); });
    connect(result_, &QTextEdit::textChanged, this, [this] {
        const bool canUseResult = resultValid_ && !result_->toPlainText().isEmpty()
            && !processTask_;
        copyButton_->setEnabled(canUseResult);
        exportButton_->setEnabled(canUseResult);
    });
    connect(language_, &QComboBox::currentIndexChanged, this, [this] {
        QSettings().setValue(QStringLiteral("ai/ocr/language"),
                             language_->currentData().toString());
    });
}

OcrWidget::~OcrWidget()
{
    ++taskId_;
    if (processTask_) {
        processTask_->abandon();
        processTask_ = nullptr;
    }
}

bool OcrWidget::importImage(const QString& path)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (image.isNull()) {
        showImportError(tr("图片无法读取或格式不受支持。原文件未修改，请选择有效的本地图片。"));
        return false;
    }
    const QFileInfo info(path);
    const QString localPath = info.canonicalFilePath();
    if (localPath.isEmpty()) {
        showImportError(tr("无法访问所选本地图片。原文件未修改，请检查文件权限。"));
        return false;
    }

    if (processTask_) processTask_->cancel();
    ++taskId_;
    imagePath_ = localPath;
    invalidateResult();
    previewImage_ = image;
    updatePreview();
    sourceInfo_->setText(tr("%1 · %2 × %3 · %4")
                             .arg(info.fileName())
                             .arg(image.width())
                             .arg(image.height())
                             .arg(QString::fromLatin1(reader.format()).toUpper()));
    statusStrip_->setState(mtb::ui::ToolUiState::Ready,
                           tr("图片已就绪。识别在本机完成，不会上传图片。"));
    recognizeButton_->setEnabled(processTask_ == nullptr);
    retryButton_->setEnabled(false);
    return true;
}

bool OcrWidget::pasteImage()
{
    const QImage image = QApplication::clipboard()->image();
    if (image.isNull()) {
        showImportError(tr("剪贴板中没有可用图片。请先复制图片后重试。"));
        return false;
    }
    if (!clipboardDirectory_.isValid()) {
        showImportError(tr("无法创建本地临时目录，剪贴板图片未被处理。"));
        return false;
    }
    const QString path = clipboardDirectory_.filePath(QStringLiteral("clipboard-image.png"));
    if (!image.save(path, "PNG")) {
        showImportError(tr("无法保存本地临时图片，剪贴板图片未被处理。"));
        return false;
    }
    return importImage(path);
}

void OcrWidget::startRecognition()
{
    if (processTask_) {
        return;
    }
    invalidateResult();
    const OcrRequest currentRequest = PaddleOcrEngine::resolvedRequest(request());
    const OcrResult validation = PaddleOcrEngine::validate(currentRequest);
    if (!validation.ok) {
        statusStrip_->setState(mtb::ui::ToolUiState::Error, validation.message);
        retryButton_->setEnabled(!imagePath_.isEmpty());
        return;
    }

    const quint64 currentTask = ++taskId_;
    QPointer<OcrWidget> guard(this);
    setBusy(true);
    statusStrip_->setState(mtb::ui::ToolUiState::Running,
                           tr("正在使用应用内置的离线 PaddleOCR 模型识别，可以随时取消。最长等待 2 分钟。"));

    processTask_ = new OcrProcessTask(
        currentRequest,
        [guard, currentTask](const OcrResult& recognized) {
            if (!guard) return;
            guard->processTask_ = nullptr;
            guard->finishRecognition(currentTask, recognized);
        },
        this);
    processTask_->start();
}

void OcrWidget::cancelRecognition()
{
    if (!processTask_) {
        return;
    }
    cancelButton_->setEnabled(false);
    statusStrip_->setState(mtb::ui::ToolUiState::Warning,
                           tr("正在停止本地 OCR 进程…"));
    processTask_->cancel();
}

void OcrWidget::clearAll()
{
    if (processTask_) processTask_->cancel();
    ++taskId_;
    imagePath_.clear();
    previewImage_ = {};
    preview_->setPixmap({});
    preview_->setText(tr("拖放图片到此处，或使用导入、粘贴按钮。"));
    sourceInfo_->setText(tr("尚未选择图片"));
    invalidateResult();
    statusStrip_->setState(mtb::ui::ToolUiState::Idle, tr("已清空。等待导入图片。"));
    recognizeButton_->setEnabled(false);
    retryButton_->setEnabled(false);
}

bool OcrWidget::exportText(const QString& path, QString* error) const
{
    if (path.trimmed().isEmpty() || !resultValid_ || result_->toPlainText().isEmpty()) {
        if (error) {
            *error = tr("没有可导出的识别结果或导出路径无效。");
        }
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) {
            *error = tr("无法写入所选位置，请检查目录权限。");
        }
        return false;
    }
    const QByteArray data = result_->toPlainText().toUtf8();
    if (file.write(data) != data.size() || !file.commit()) {
        if (error) {
            *error = tr("导出失败，原识别结果仍保留在页面中。");
        }
        return false;
    }
    return true;
}

void OcrWidget::dragEnterEvent(QDragEnterEvent* event)
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

void OcrWidget::dropEvent(QDropEvent* event)
{
    for (const QUrl& url : event->mimeData()->urls()) {
        if (url.isLocalFile() && importImage(url.toLocalFile())) {
            event->acceptProposedAction();
            return;
        }
    }
    event->ignore();
}

void OcrWidget::chooseImage()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("选择本地图片"), {},
        tr("图片文件 (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.webp);;所有文件 (*)"));
    if (!path.isEmpty()) {
        (void)importImage(path);
    }
}

void OcrWidget::chooseExportPath()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("导出识别结果"), QStringLiteral("ocr-result.txt"),
        tr("文本文件 (*.txt);;Markdown 文件 (*.md)"));
    if (path.isEmpty()) {
        return;
    }
    QString error;
    if (exportText(path, &error)) {
        statusStrip_->setState(mtb::ui::ToolUiState::Success,
                               tr("识别结果已导出到用户选择的位置。"));
    } else {
        statusStrip_->setState(mtb::ui::ToolUiState::Error, error);
    }
}

void OcrWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updatePreview();
}

void OcrWidget::updatePreview()
{
    if (previewImage_.isNull()) {
        return;
    }
    const QSize available = preview_->contentsRect().size();
    if (available.isEmpty()) {
        return;
    }
    preview_->setText({});
    preview_->setPixmap(QPixmap::fromImage(
        previewImage_.scaled(available, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
}

void OcrWidget::setBusy(bool busy)
{
    recognizeButton_->setEnabled(!busy && !imagePath_.isEmpty());
    cancelButton_->setEnabled(busy);
    retryButton_->setEnabled(!busy && !imagePath_.isEmpty());
    language_->setEnabled(!busy);
    copyButton_->setEnabled(!busy && resultValid_ && !result_->toPlainText().isEmpty());
    exportButton_->setEnabled(!busy && resultValid_ && !result_->toPlainText().isEmpty());
    progress_->setVisible(busy);
}

void OcrWidget::invalidateResult()
{
    resultValid_ = false;
    result_->clear();
    copyButton_->setEnabled(false);
    exportButton_->setEnabled(false);
}

void OcrWidget::finishRecognition(quint64 taskId, const OcrResult& recognized)
{
    if (taskId != taskId_) {
        return;
    }
    if (recognized.ok) {
        resultValid_ = true;
        result_->setPlainText(recognized.text);
        statusStrip_->setState(mtb::ui::ToolUiState::Success, recognized.message);
    } else {
        invalidateResult();
        statusStrip_->setState(
            recognized.message.contains(tr("取消"))
                ? mtb::ui::ToolUiState::Warning : mtb::ui::ToolUiState::Error,
            recognized.message);
    }
    setBusy(false);
}

void OcrWidget::showImportError(const QString& message)
{
    statusStrip_->setState(mtb::ui::ToolUiState::Error, message);
}

OcrRequest OcrWidget::request() const
{
    return {imagePath_, {}, {}, language_->currentData().toString(), 120000};
}

QWidget* createOcrWidget(QWidget* parent)
{
    return new OcrWidget(parent);
}

}  // namespace mtb::ocr
