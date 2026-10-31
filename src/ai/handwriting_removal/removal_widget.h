#pragma once

#include "ai/handwriting_removal/removal_engine.h"

#include <QVector>
#include <QWidget>

#include <memory>

class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QSlider;
class QSpinBox;
class QThread;

namespace mtb::ui {
class ToolStatusStrip;
}

namespace mtb::handwriting {

class RemovalCanvas;

class HandwritingRemovalWidget final : public QWidget {
    Q_OBJECT

public:
    explicit HandwritingRemovalWidget(
        QWidget* parent = nullptr,
        std::shared_ptr<const IHandwritingRemovalEngine> engine = {});
    ~HandwritingRemovalWidget() override;

    [[nodiscard]] bool importImage(const QString& path);
    void startAutomaticProcessing();
    void clearMask();
    void restartMask();
    [[nodiscard]] bool exportPng(const QString& path, QString* error = nullptr) const;

    [[nodiscard]] const QImage& originalImage() const { return document_.original(); }
    [[nodiscard]] const QImage& maskImage() const { return document_.mask(); }
    [[nodiscard]] const QImage& resultImage() const { return result_; }

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void chooseImage();
    void chooseExportPath();
    void finishAutomatic(quint64 taskId, const RemovalResult& result);
    void finishResult(quint64 taskId, const RemovalResult& result);
    void refreshResult();
    void cancelResultProcessing();
    void trackWorker(QThread* thread);
    void refreshControls();
    void setBusy(bool busy);
    void showError(const QString& message);

    std::shared_ptr<const IHandwritingRemovalEngine> engine_;
    MaskDocument document_;
    QImage automaticMask_;
    QImage result_;
    RemovalCanvas* canvas_;
    QLabel* status_;
    QComboBox* viewSelector_;
    QSpinBox* brushSize_;
    QSlider* maskOpacity_;
    QPushButton* automaticButton_;
    QPushButton* cancelButton_;
    QPushButton* undoButton_;
    QPushButton* redoButton_;
    QPushButton* exportButton_;
    QProgressBar* progress_;
    mtb::ui::ToolStatusStrip* statusStrip_;
    QVector<QThread*> workerThreads_;
    QThread* automaticThread_{nullptr};
    QThread* resultThread_{nullptr};
    quint64 automaticTaskId_{0};
    quint64 resultTaskId_{0};
    int compareViewIndex_{1};
    bool automaticCancellationRequested_{false};
    bool resultPending_{false};
    bool resultValid_{false};
};

[[nodiscard]] QWidget* createHandwritingRemovalWidget(QWidget* parent = nullptr);

}  // namespace mtb::handwriting
