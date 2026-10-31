#pragma once

#include "domain/games/ui/game_visual_theme.h"

#include <QWidget>

#include <functional>

class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QPainter;
class QVariantAnimation;

namespace mtb::games::ui {

class GameCanvas final : public QWidget {
public:
    using PaintHandler = std::function<void(QPainter&, const QRect&)>;
    using KeyHandler = std::function<void(int)>;
    using ClickHandler = std::function<void(const QPoint&)>;

    explicit GameCanvas(const QString& gameId, QWidget* parent = nullptr);

    void setPaintHandler(PaintHandler handler);
    void setKeyHandler(KeyHandler handler);
    void setClickHandler(ClickHandler handler);
    void setInstructions(const QString& instructions);
    void showOverlay(const QString& title, const QString& hint = {});
    void clearOverlay();
    [[nodiscard]] GameVisualTheme visualTheme() const;

protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    QString gameId_;
    QString overlayTitle_;
    QString overlayHint_;
    PaintHandler paintHandler_;
    KeyHandler keyHandler_;
    ClickHandler clickHandler_;
    QVariantAnimation* overlayAnimation_;
    qreal overlayOpacity_{1.0};
};

}  // namespace mtb::games::ui
