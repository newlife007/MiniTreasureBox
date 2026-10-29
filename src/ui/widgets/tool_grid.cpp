#include "ui/widgets/tool_grid.h"

#include "ui/widgets/tool_card.h"

#include <QGridLayout>
#include <QResizeEvent>
#include <QScrollBar>
#include <QWidget>

#include <algorithm>

namespace mtb {
namespace {
constexpr int kPreferredCardWidth = 250;
constexpr int kGridSpacing = 14;
}  // namespace

ToolGrid::ToolGrid(QWidget* parent)
    : QScrollArea(parent)
    , content_(new QWidget(this))
    , layout_(new QGridLayout(content_))
{
    setObjectName(QStringLiteral("toolGrid"));
    content_->setObjectName(QStringLiteral("toolGridContent"));
    setWidget(content_);
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setFocusPolicy(Qt::NoFocus);
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setHorizontalSpacing(kGridSpacing);
    layout_->setVerticalSpacing(kGridSpacing);
    layout_->setAlignment(Qt::AlignTop);
    setAccessibleName(tr("工具宫格"));
    setAccessibleDescription(tr("根据窗口宽度自动排列的工具卡片"));
}

void ToolGrid::setTools(const QList<ToolMetadata>& tools, const QSet<QString>& favoriteIds)
{
    while (QLayoutItem* item = layout_->takeAt(0)) {
        delete item;
    }
    for (ToolCard* card : std::as_const(cards_)) {
        card->hide();
        card->deleteLater();
    }
    cards_.clear();

    for (const ToolMetadata& metadata : tools) {
        auto* card = new ToolCard(metadata, favoriteIds.contains(metadata.id), content_);
        connect(card, &ToolCard::activationRequested, this, &ToolGrid::toolOpenRequested);
        connect(card, &ToolCard::favoriteToggleRequested,
                this, &ToolGrid::favoriteToggleRequested);
        cards_.append(card);
    }
    relayout();
}

int ToolGrid::cardCount() const
{
    return cards_.size();
}

int ToolGrid::columnCount() const
{
    return columnCount_;
}

QStringList ToolGrid::toolIds() const
{
    QStringList ids;
    ids.reserve(cards_.size());
    for (const ToolCard* card : cards_) {
        ids.append(card->toolId());
    }
    return ids;
}

ToolCard* ToolGrid::cardForTool(const QString& toolId) const
{
    const auto found = std::find_if(cards_.cbegin(), cards_.cend(), [&toolId](const ToolCard* card) {
        return card->toolId() == toolId;
    });
    return found == cards_.cend() ? nullptr : *found;
}

void ToolGrid::resizeEvent(QResizeEvent* event)
{
    QScrollArea::resizeEvent(event);
    relayout();
}

void ToolGrid::relayout()
{
    const int availableWidth = std::max(1, viewport()->width());
    const int columns = std::max(1, (availableWidth + kGridSpacing)
                                        / (kPreferredCardWidth + kGridSpacing));
    const int previousColumns = columnCount_;
    const bool changed = previousColumns != columns;
    columnCount_ = columns;

    while (QLayoutItem* item = layout_->takeAt(0)) {
        delete item;
    }
    for (int column = 0; column < previousColumns; ++column) {
        layout_->setColumnStretch(column, 0);
    }
    for (int index = 0; index < cards_.size(); ++index) {
        layout_->addWidget(cards_.at(index), index / columnCount_, index % columnCount_);
    }
    for (int column = 0; column < columnCount_; ++column) {
        layout_->setColumnStretch(column, 1);
    }
    if (changed || !cards_.isEmpty()) {
        emit layoutChanged();
    }
}

}  // namespace mtb
