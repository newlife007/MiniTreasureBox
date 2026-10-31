#pragma once

#include <QFrame>

class QLabel;

namespace mtb::ui {

enum class ToolUiState { Idle, Ready, Running, Success, Warning, Error };

class ToolStatusStrip final : public QFrame {
public:
    explicit ToolStatusStrip(QLabel* messageLabel = nullptr,
                             QWidget* parent = nullptr);

    void setState(ToolUiState state, const QString& message);
    [[nodiscard]] QLabel* messageLabel() const { return messageLabel_; }

private:
    QLabel* indicator_;
    QLabel* messageLabel_;
};

}  // namespace mtb::ui
