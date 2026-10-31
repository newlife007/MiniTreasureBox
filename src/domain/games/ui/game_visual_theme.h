#pragma once

#include <QColor>
#include <QPalette>
#include <QString>

namespace mtb::games::ui {

struct GameVisualTheme {
    QColor accent;
    QColor accentStrong;
    QColor accentSoft;
    QColor canvas;
    QColor board;
    QColor grid;
    QColor text;
    QColor muted;
    QColor overlay;
    bool dark{false};
};

[[nodiscard]] GameVisualTheme gameVisualTheme(const QString& gameId,
                                               const QPalette& palette);
[[nodiscard]] QColor readableTextColor(const QColor& background);
[[nodiscard]] QColor tile2048Color(int value, bool dark);
[[nodiscard]] QColor tetrisPieceColor(int pieceIndex, bool dark);

}  // namespace mtb::games::ui
