#include "ai/ocr/ocr_widget.h"

#include <QApplication>
#include <QClipboard>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPointer>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QTextEdit>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>

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

OcrWidget::OcrWidget(QWidget* parent)
    : QWidget(parent)
    , preview_(new QLabel(this))
    , status_(new QLabel(tr("等待导入图片。"), this))
    , executable_(new QLineEdit(this))
    , tessdata_(new QLineEdit(this))
    , language_(new QLineEdit(this))
    , result_(new QTextEdit(this))
    , recognizeButton_(makeButton(tr("开始识别"), QStringLiteral("ocrRecognizeButton"),
                                  tr("使用配置的本地 Tesseract 识别当前图片"), this))
    , cancelButton_(makeButton(tr("取消"), QStringLiteral("ocrCancelButton"),
                               tr("取消正在运行的本地识别任务"), this))
    , retryButton_(makeButton(tr("重试"), QStringLiteral("ocrRetryButton"),
                              tr("使用当前配置重新识别图片"), this))
    , copyButton_(makeButton(tr("复制结果"), QStringLiteral("ocrCopyButton"),
                             tr("将可编辑识别结果复制到剪贴板"), this))
    , exportButton_(makeButton(tr("导出"), QStringLiteral("ocrExportButton"),
                               tr("将识别结果保存为 TXT 或 Markdown 文件"), this))
{
    setObjectName(QStringLiteral("ocrWidget"));
    setAcceptDrops(true);

    QSettings settings;
    const QString environmentExecutable = qEnvironmentVariable("MINITREASUREBOX_TESSERACT",
                                                                 QStringLiteral("tesseract"));
    executable_->setText(settings.value(QStringLiteral("ai/ocr/executable"),
                                         environmentExecutable).toString());
    tessdata_->setText(settings.value(QStringLiteral("ai/ocr/tessdata"),
                                       qEnvironmentVariable("TESSDATA_PREFIX")).toString());
    language_->setText(settings.value(QStringLiteral("ai/ocr/language"),
                                       QStringLiteral("chi_sim+eng")).toString());

    executable_->setObjectName(QStringLiteral("ocrExecutable"));
    executable_->setPlaceholderText(tr("例如 tesseract 或本地可执行文件路径"));
    executable_->setAccessibleName(tr("Tesseract 可执行文件"));
    tessdata_->setObjectName(QStringLiteral("ocrTessdata"));
    tessdata_->setPlaceholderText(tr("可选，本地 tessdata 目录"));
    tessdata_->setAccessibleName(tr("Tesseract 语言模型目录"));
    language_->setObjectName(QStringLiteral("ocrLanguage"));
    language_->setPlaceholderText(QStringLiteral("chi_sim+eng"));
    language_->setAccessibleName(tr("OCR 语言代码"));

    preview_->setObjectName(QStringLiteral("ocrPreview"));
    preview_->setAccessibleName(tr("待识别图片预览"));
    preview_->setAlignment(Qt::AlignCenter);
    preview_->setMinimumSize(280, 180);
    preview_->setFrameShape(QFrame::StyledPanel);
    preview_->setText(tr("拖放图片到此处，或使用导入、粘贴按钮。"));
    preview_->setWordWrap(true);

    status_->setObjectName(QStringLiteral("ocrStatus"));
    status_->setAccessibleName(tr("OCR 处理状态"));
    status_->setWordWrap(true);

    result_->setObjectName(QStringLiteral("ocrResult"));
    result_->setAccessibleName(tr("可编辑 OCR 识别结果"));
    result_->setPlaceholderText(tr("识别结果会显示在这里，并且可以编辑。"));

    auto* importButton = makeButton(tr("导入图片"), QStringLiteral("ocrImportButton"),
                                    tr("从本地文件系统选择图片"), this);
    auto* pasteButton = makeButton(tr("粘贴图片"), QStringLiteral("ocrPasteButton"),
                                   tr("从系统剪贴板粘贴图片并仅在本机处理"), this);
    auto* clearButton = makeButton(tr("清空"), QStringLiteral("ocrClearButton"),
                                   tr("清除当前图片和识别结果"), this);

    auto* sourceActions = new QHBoxLayout;
    sourceActions->addWidget(importButton);
    sourceActions->addWidget(pasteButton);
    sourceActions->addWidget(recognizeButton_);
    sourceActions->addWidget(cancelButton_);
    sourceActions->addWidget(retryButton_);
    sourceActions->addStretch();

    auto* configuration = new QFormLayout;
    configuration->addRow(tr("Tesseract 程序"), executable_);
    configuration->addRow(tr("tessdata 目录"), tessdata_);
    configuration->addRow(tr("识别语言"), language_);

    auto* resultActions = new QHBoxLayout;
    resultActions->addWidget(copyButton_);
    resultActions->addWidget(exportButton_);
    resultActions->addWidget(clearButton);
    resultActions->addStretch();

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(sourceActions);
    layout->addLayout(configuration);
    layout->addWidget(preview_, 1);
    layout->addWidget(status_);
    layout->addWidget(result_, 1);
    layout->addLayout(resultActions);

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
        status_->setText(tr("识别结果已复制到剪贴板。"));
    });
    connect(exportButton_, &QPushButton::clicked, this, [this] { chooseExportPath(); });
    connect(clearButton, &QPushButton::clicked, this, [this] { clearAll(); });
    connect(result_, &QTextEdit::textChanged, this, [this] {
        const bool hasText = !result_->toPlainText().isEmpty();
        copyButton_->setEnabled(hasText && !workerThread_);
        exportButton_->setEnabled(hasText && !workerThread_);
    });
    connect(executable_, &QLineEdit::editingFinished, this, [this] {
        QSettings().setValue(QStringLiteral("ai/ocr/executable"), executable_->text().trimmed());
    });
    connect(tessdata_, &QLineEdit::editingFinished, this, [this] {
        QSettings().setValue(QStringLiteral("ai/ocr/tessdata"), tessdata_->text().trimmed());
    });
    connect(language_, &QLineEdit::editingFinished, this, [this] {
        QSettings().setValue(QStringLiteral("ai/ocr/language"), language_->text().trimmed());
    });
}

OcrWidget::~OcrWidget()
{
    if (cancellation_) {
        cancellation_->store(true);
    }
    if (workerThread_) {
        workerThread_->wait();
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

    if (cancellation_) {
        cancellation_->store(true);
    }
    ++taskId_;
    imagePath_ = localPath;
    preview_->setPixmap(QPixmap::fromImage(image).scaled(520, 280, Qt::KeepAspectRatio,
                                                          Qt::SmoothTransformation));
    status_->setText(tr("图片已就绪。识别仅调用本地程序，不会上传图片。"));
    recognizeButton_->setEnabled(workerThread_ == nullptr);
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
    if (workerThread_) {
        return;
    }
    const OcrRequest currentRequest = request();
    const OcrResult validation = TesseractOcrEngine::validate(currentRequest);
    if (!validation.ok) {
        status_->setText(validation.message);
        retryButton_->setEnabled(!imagePath_.isEmpty());
        return;
    }

    const quint64 currentTask = ++taskId_;
    cancellation_ = std::make_shared<std::atomic_bool>(false);
    const auto cancellation = cancellation_;
    QPointer<OcrWidget> guard(this);
    setBusy(true);
    status_->setText(tr("正在使用本地 Tesseract 识别，可以随时取消。"));

    workerThread_ = QThread::create([currentRequest, cancellation, guard, currentTask] {
        const TesseractOcrEngine engine;
        const OcrResult recognized = engine.recognize(currentRequest, cancellation.get());
        if (guard) {
            QMetaObject::invokeMethod(guard, [guard, currentTask, recognized] {
                if (guard) {
                    guard->finishRecognition(currentTask, recognized);
                }
            }, Qt::QueuedConnection);
        }
    });
    QThread* const thread = workerThread_;
    connect(thread, &QThread::finished, this, [this, thread] {
        if (workerThread_ == thread) {
            workerThread_ = nullptr;
            setBusy(false);
        }
        thread->deleteLater();
    });
    thread->start();
}

void OcrWidget::cancelRecognition()
{
    if (!workerThread_ || !cancellation_) {
        return;
    }
    cancellation_->store(true);
    cancelButton_->setEnabled(false);
    status_->setText(tr("正在取消本地识别任务…"));
}

void OcrWidget::clearAll()
{
    if (cancellation_) {
        cancellation_->store(true);
    }
    ++taskId_;
    imagePath_.clear();
    preview_->setPixmap({});
    preview_->setText(tr("拖放图片到此处，或使用导入、粘贴按钮。"));
    result_->clear();
    status_->setText(tr("已清空。等待导入图片。"));
    recognizeButton_->setEnabled(false);
    retryButton_->setEnabled(false);
}

bool OcrWidget::exportText(const QString& path, QString* error) const
{
    if (path.trimmed().isEmpty() || result_->toPlainText().isEmpty()) {
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
        status_->setText(tr("识别结果已导出到用户选择的位置。"));
    } else {
        status_->setText(error);
    }
}

void OcrWidget::setBusy(bool busy)
{
    recognizeButton_->setEnabled(!busy && !imagePath_.isEmpty());
    cancelButton_->setEnabled(busy);
    retryButton_->setEnabled(!busy && !imagePath_.isEmpty());
    executable_->setEnabled(!busy);
    tessdata_->setEnabled(!busy);
    language_->setEnabled(!busy);
    copyButton_->setEnabled(!busy && !result_->toPlainText().isEmpty());
    exportButton_->setEnabled(!busy && !result_->toPlainText().isEmpty());
}

void OcrWidget::finishRecognition(quint64 taskId, const OcrResult& recognized)
{
    if (taskId != taskId_) {
        return;
    }
    if (recognized.ok) {
        result_->setPlainText(recognized.text);
    }
    setBusy(false);
    status_->setText(recognized.message);
}

void OcrWidget::showImportError(const QString& message)
{
    status_->setText(message);
}

OcrRequest OcrWidget::request() const
{
    return {imagePath_, executable_->text().trimmed(), tessdata_->text().trimmed(),
            language_->text().trimmed(), 60000};
}

QWidget* createOcrWidget(QWidget* parent)
{
    return new OcrWidget(parent);
}

}  // namespace mtb::ocr
