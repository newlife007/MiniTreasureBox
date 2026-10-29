#pragma once

#include "ai/handwriting_removal/removal_engine.h"

#include <QWidget>

#include <memory>

class QComboBox;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;
class QThread;

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
    void refreshResult();
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
    QPushButton* undoButton_;
    QPushButton* redoButton_;
    QPushButton* exportButton_;
    QThread* workerThread_{nullptr};
    quint64 taskId_{0};
};

[[nodiscard]] QWidget* createHandwritingRemovalWidget(QWidget* parent = nullptr);

}  // namespace mtb::handwriting
