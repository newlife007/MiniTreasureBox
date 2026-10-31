#pragma once

#include <QPoint>
#include <QRect>
#include <QSize>
#include <QtMath>

#include <algorithm>
#include <cstdlib>

namespace mtb::games::ui {

struct CellGridGeometry {
    QRect board;
    int columns{1};
    int rows{1};
    int cell{1};

    [[nodiscard]] QRect cellRect(int column, int row, int inset = 0) const
    {
        return QRect(board.left() + column * cell + inset,
                     board.top() + row * cell + inset,
                     std::max(1, cell - inset * 2),
                     std::max(1, cell - inset * 2));
    }

    [[nodiscard]] QPoint cellCenter(int column, int row) const
    {
        return cellRect(column, row).center();
    }
};

inline CellGridGeometry makeCellGrid(const QRect& area, int columns, int rows,
                                     int padding = 12)
{
    const QRect available = area.adjusted(padding, padding, -padding, -padding);
    const int cell = std::max(1, std::min(available.width() / columns,
                                         available.height() / rows));
    const QSize size(cell * columns, cell * rows);
    return {QRect(QPoint(available.center().x() - size.width() / 2,
                         available.center().y() - size.height() / 2), size),
            columns, rows, cell};
}

struct IntersectionGridGeometry {
    QRect board;
    QPoint topLeft;
    int points{1};
    int spacing{1};

    [[nodiscard]] QPoint point(int column, int row) const
    {
        return topLeft + QPoint(column * spacing, row * spacing);
    }

    [[nodiscard]] QPoint hitTest(const QPoint& position) const
    {
        const int column = qRound(static_cast<double>(position.x() - topLeft.x()) / spacing);
        const int row = qRound(static_cast<double>(position.y() - topLeft.y()) / spacing);
        if (column < 0 || column >= points || row < 0 || row >= points) {
            return {-1, -1};
        }
        const QPoint nearest = point(column, row);
        const int tolerance = std::max(5, spacing / 2);
        if (std::abs(position.x() - nearest.x()) > tolerance
            || std::abs(position.y() - nearest.y()) > tolerance) {
            return {-1, -1};
        }
        return {column, row};
    }
};

inline IntersectionGridGeometry makeIntersectionGrid(const QRect& area, int points,
                                                       int padding = 22)
{
    const QRect available = area.adjusted(padding, padding, -padding, -padding);
    const int intervals = std::max(1, points - 1);
    const int spacing = std::max(1, std::min(available.width(), available.height())
                                       / intervals);
    const int side = spacing * intervals;
    const QPoint topLeft(available.center().x() - side / 2,
                         available.center().y() - side / 2);
    return {QRect(topLeft, QSize(side, side)), topLeft, points, spacing};
}

}  // namespace mtb::games::ui
