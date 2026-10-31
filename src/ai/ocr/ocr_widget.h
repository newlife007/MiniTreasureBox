#pragma once

#include "ai/ocr/ocr_engine.h"

#include <QImage>
#include <QTemporaryDir>
#include <QWidget>

class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QResizeEvent;
class QTextEdit;

namespace mtb::ui {
class ToolStatusStrip;
}

namespace mtb::ocr {

class OcrProcessTask;

class OcrWidget final : public QWidget {
    Q_OBJECT

public:
    explicit OcrWidget(QWidget* parent = nullptr);
    ~OcrWidget() override;

    [[nodiscard]] bool importImage(const QString& path);
    [[nodiscard]] bool pasteImage();
    void startRecognition();
    void cancelRecognition();
    void clearAll();
    [[nodiscard]] bool exportText(const QString& path, QString* error = nullptr) const;

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void chooseImage();
    void chooseExportPath();
    void updatePreview();
    void setBusy(bool busy);
    void invalidateResult();
    void finishRecognition(quint64 taskId, const OcrResult& result);
    void showImportError(const QString& message);
    [[nodiscard]] OcrRequest request() const;

    QString imagePath_;
    QImage previewImage_;
    QTemporaryDir clipboardDirectory_;
    QLabel* preview_;
    QLabel* sourceInfo_;
    QLabel* status_;
    QComboBox* language_;
    QTextEdit* result_;
    QPushButton* recognizeButton_;
    QPushButton* cancelButton_;
    QPushButton* retryButton_;
    QPushButton* copyButton_;
    QPushButton* exportButton_;
    QProgressBar* progress_;
    mtb::ui::ToolStatusStrip* statusStrip_;
    OcrProcessTask* processTask_{nullptr};
    quint64 taskId_{0};
    bool resultValid_{false};
};

[[nodiscard]] QWidget* createOcrWidget(QWidget* parent = nullptr);

}  // namespace mtb::ocr
