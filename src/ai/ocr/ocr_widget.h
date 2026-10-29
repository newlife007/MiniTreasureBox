#pragma once

#include "ai/ocr/ocr_engine.h"

#include <QTemporaryDir>
#include <QWidget>

#include <atomic>
#include <memory>

class QLabel;
class QLineEdit;
class QPushButton;
class QTextEdit;
class QThread;

namespace mtb::ocr {

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

private:
    void chooseImage();
    void chooseExportPath();
    void setBusy(bool busy);
    void invalidateResult();
    void finishRecognition(quint64 taskId, const OcrResult& result);
    void showImportError(const QString& message);
    [[nodiscard]] OcrRequest request() const;

    QString imagePath_;
    QTemporaryDir clipboardDirectory_;
    QLabel* preview_;
    QLabel* status_;
    QLineEdit* executable_;
    QLineEdit* tessdata_;
    QLineEdit* language_;
    QTextEdit* result_;
    QPushButton* recognizeButton_;
    QPushButton* cancelButton_;
    QPushButton* retryButton_;
    QPushButton* copyButton_;
    QPushButton* exportButton_;
    QThread* workerThread_{nullptr};
    std::shared_ptr<std::atomic_bool> cancellation_;
    quint64 taskId_{0};
    bool resultValid_{false};
};

[[nodiscard]] QWidget* createOcrWidget(QWidget* parent = nullptr);

}  // namespace mtb::ocr
