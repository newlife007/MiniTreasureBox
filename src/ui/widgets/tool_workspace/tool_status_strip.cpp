#include "ui/widgets/tool_workspace/tool_status_strip.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QStyle>

namespace mtb::ui {
namespace {

QString stateName(ToolUiState state)
{
    switch (state) {
    case ToolUiState::Idle: return QStringLiteral("idle");
    case ToolUiState::Ready: return QStringLiteral("ready");
    case ToolUiState::Running: return QStringLiteral("running");
    case ToolUiState::Success: return QStringLiteral("success");
    case ToolUiState::Warning: return QStringLiteral("warning");
    case ToolUiState::Error: return QStringLiteral("error");
    }
    return QStringLiteral("idle");
}

QString indicatorText(ToolUiState state)
{
    switch (state) {
    case ToolUiState::Running: return QStringLiteral("◌");
    case ToolUiState::Success: return QStringLiteral("●");
    case ToolUiState::Warning: return QStringLiteral("◆");
    case ToolUiState::Error: return QStringLiteral("●");
    case ToolUiState::Ready: return QStringLiteral("●");
    case ToolUiState::Idle: return QStringLiteral("○");
    }
    return {};
}

}  // namespace

ToolStatusStrip::ToolStatusStrip(QLabel* messageLabel, QWidget* parent)
    : QFrame(parent)
    , indicator_(new QLabel(this))
    , messageLabel_(messageLabel ? messageLabel : new QLabel(this))
{
    setObjectName(QStringLiteral("toolStatusStrip"));
    indicator_->setObjectName(QStringLiteral("toolStatusIndicator"));
    messageLabel_->setObjectName(messageLabel_->objectName().isEmpty()
                                     ? QStringLiteral("toolStatusMessage")
                                     : messageLabel_->objectName());
    messageLabel_->setWordWrap(true);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 8, 12, 8);
    layout->setSpacing(8);
    layout->addWidget(indicator_);
    layout->addWidget(messageLabel_, 1);
    setState(ToolUiState::Idle, {});
}

void ToolStatusStrip::setState(ToolUiState state, const QString& message)
{
    setProperty("toolState", stateName(state));
    indicator_->setText(indicatorText(state));
    messageLabel_->setText(message);
    const bool visible = state != ToolUiState::Idle || !message.isEmpty();
    setVisible(visible);
    messageLabel_->setVisible(visible);
    style()->unpolish(this);
    style()->polish(this);
    update();
}

}  // namespace mtb::ui
