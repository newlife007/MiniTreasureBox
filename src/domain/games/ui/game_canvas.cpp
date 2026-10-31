#include "domain/games/ui/game_canvas.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVariantAnimation>

#include <utility>

namespace mtb::games::ui {

GameCanvas::GameCanvas(const QString& gameId, QWidget* parent)
    : QWidget(parent)
    , gameId_(gameId)
    , overlayAnimation_(new QVariantAnimation(this))
{
    overlayAnimation_->setDuration(160);
    overlayAnimation_->setEasingCurve(QEasingCurve::OutCubic);
    connect(overlayAnimation_, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& value) {
                overlayOpacity_ = value.toReal();
                update();
            });
    setObjectName(QStringLiteral("gameSurface"));
    setProperty("gameId", gameId);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(tr("游戏区域"));
    setMinimumSize(280, 280);
}

void GameCanvas::setPaintHandler(PaintHandler handler)
{
    paintHandler_ = std::move(handler);
}

void GameCanvas::setKeyHandler(KeyHandler handler)
{
    keyHandler_ = std::move(handler);
}

void GameCanvas::setClickHandler(ClickHandler handler)
{
    clickHandler_ = std::move(handler);
}

void GameCanvas::setInstructions(const QString& instructions)
{
    setToolTip(instructions);
    setAccessibleDescription(instructions);
}

void GameCanvas::showOverlay(const QString& title, const QString& hint)
{
    overlayTitle_ = title;
    overlayHint_ = hint;
    overlayAnimation_->stop();
    overlayAnimation_->setStartValue(0.0);
    overlayAnimation_->setEndValue(1.0);
    overlayAnimation_->start();
}

void GameCanvas::clearOverlay()
{
    overlayAnimation_->stop();
    overlayOpacity_ = 1.0;
    overlayTitle_.clear();
    overlayHint_.clear();
    update();
}

GameVisualTheme GameCanvas::visualTheme() const
{
    return gameVisualTheme(gameId_, palette());
}

void GameCanvas::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing
                           | QPainter::SmoothPixmapTransform);
    const GameVisualTheme theme = visualTheme();
    const QRect stage = rect().adjusted(2, 2, -2, -2);
    painter.setPen(QPen(hasFocus() ? theme.accent : theme.grid, hasFocus() ? 2 : 1));
    painter.setBrush(theme.canvas);
    painter.drawRoundedRect(stage, 16, 16);

    const QRect content = stage.adjusted(14, 14, -14, -14);
    if (paintHandler_) paintHandler_(painter, content);

    if (!overlayTitle_.isEmpty()) {
        painter.setPen(Qt::NoPen);
        QColor overlay = theme.overlay;
        overlay.setAlphaF(overlay.alphaF() * overlayOpacity_);
        painter.setBrush(overlay);
        painter.drawRoundedRect(stage, 16, 16);
        painter.setPen(Qt::white);
        QFont titleFont = painter.font();
        titleFont.setPointSize(22);
        titleFont.setBold(true);
        painter.setFont(titleFont);
        const QRect titleRect = stage.adjusted(24, stage.height() / 3, -24,
                                                -stage.height() / 2);
        painter.drawText(titleRect, Qt::AlignHCenter | Qt::AlignBottom,
                         overlayTitle_);
        if (!overlayHint_.isEmpty()) {
            QFont hintFont = painter.font();
            hintFont.setPointSize(11);
            hintFont.setBold(false);
            painter.setFont(hintFont);
            painter.setPen(QColor(255, 255, 255, 210));
            const QRect hintRect = stage.adjusted(24, stage.height() / 2 + 8,
                                                   -24, -24);
            painter.drawText(hintRect, Qt::AlignHCenter | Qt::AlignTop,
                             overlayHint_);
        }
    }
}

void GameCanvas::keyPressEvent(QKeyEvent* event)
{
    if (keyHandler_) {
        keyHandler_(event->key());
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void GameCanvas::mousePressEvent(QMouseEvent* event)
{
    if (clickHandler_) {
        clickHandler_(event->position().toPoint());
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

}  // namespace mtb::games::ui
