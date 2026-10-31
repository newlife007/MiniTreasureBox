#include "domain/games/ui/game_visual_theme.h"

#include <QHash>

#include <algorithm>

namespace mtb::games::ui {
namespace {

QColor accentForGame(const QString& gameId)
{
    if (gameId == QStringLiteral("guess-number")) return QColor(QStringLiteral("#D89A22"));
    if (gameId == QStringLiteral("game-2048")) return QColor(QStringLiteral("#E97835"));
    if (gameId == QStringLiteral("snake")) return QColor(QStringLiteral("#2E9B63"));
    if (gameId == QStringLiteral("tetris")) return QColor(QStringLiteral("#3977D5"));
    if (gameId == QStringLiteral("gomoku")) return QColor(QStringLiteral("#A6533F"));
    return QColor(QStringLiteral("#665CF2"));
}

QColor mix(const QColor& first, const QColor& second, qreal amount)
{
    return QColor::fromRgbF(first.redF() * (1.0 - amount) + second.redF() * amount,
                            first.greenF() * (1.0 - amount) + second.greenF() * amount,
                            first.blueF() * (1.0 - amount) + second.blueF() * amount,
                            first.alphaF() * (1.0 - amount) + second.alphaF() * amount);
}

}  // namespace

GameVisualTheme gameVisualTheme(const QString& gameId, const QPalette& palette)
{
    const bool dark = palette.color(QPalette::Window).lightness() < 128;
    const QColor accent = accentForGame(gameId);
    const QColor window = palette.color(QPalette::Window);
    const QColor base = palette.color(QPalette::Base);
    return {dark ? accent.lighter(125) : accent,
            dark ? accent.lighter(145) : accent.darker(112),
            mix(accent, window, dark ? 0.76 : 0.86),
            mix(base, window, dark ? 0.22 : 0.08),
            mix(accent, base, dark ? 0.88 : 0.94),
            mix(accent, base, dark ? 0.67 : 0.78),
            palette.color(QPalette::Text),
            palette.color(QPalette::PlaceholderText),
            QColor(10, 14, 24, dark ? 205 : 178),
            dark};
}

QColor readableTextColor(const QColor& background)
{
    return background.lightness() < 145 ? QColor(QStringLiteral("#FFFFFF"))
                                        : QColor(QStringLiteral("#243044"));
}

QColor tile2048Color(int value, bool dark)
{
    static const QHash<int, QColor> colors{
        {2, QColor("#EEE4DA")}, {4, QColor("#EDE0C8")},
        {8, QColor("#F2B179")}, {16, QColor("#F59563")},
        {32, QColor("#F67C5F")}, {64, QColor("#F65E3B")},
        {128, QColor("#EDCF72")}, {256, QColor("#EDCC61")},
        {512, QColor("#EDC850")}, {1024, QColor("#EDC53F")},
        {2048, QColor("#EDC22E")}};
    QColor color = colors.value(value, QColor(QStringLiteral("#3C4252")));
    if (dark) color = color.darker(value <= 4 ? 165 : 120);
    return color;
}

QColor tetrisPieceColor(int pieceIndex, bool dark)
{
    static const QColor colors[]{QColor("#36C5F0"), QColor("#F3C846"),
                                 QColor("#A96DE0"), QColor("#F08A3C"),
                                 QColor("#477DE5"), QColor("#52B96B"),
                                 QColor("#E75A67")};
    QColor color = colors[std::clamp(pieceIndex, 0, 6)];
    return dark ? color.lighter(118) : color;
}

}  // namespace mtb::games::ui
