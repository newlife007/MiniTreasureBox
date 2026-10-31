#include "ui/widgets/tool_workspace/responsive_pair.h"

#include <QBoxLayout>
#include <QResizeEvent>

namespace mtb::ui {

ResponsivePair::ResponsivePair(QWidget* first, QWidget* second, int breakpoint,
                               QWidget* parent)
    : QWidget(parent)
    , layout_(new QBoxLayout(QBoxLayout::LeftToRight, this))
    , breakpoint_(breakpoint)
{
    setObjectName(QStringLiteral("responsivePair"));
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(14);
    layout_->addWidget(first, 1);
    layout_->addWidget(second, 1);
    updateDirection();
}

void ResponsivePair::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateDirection();
}

void ResponsivePair::updateDirection()
{
    const auto direction = width() >= breakpoint_
        ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom;
    if (layout_->direction() != direction) layout_->setDirection(direction);
}

}  // namespace mtb::ui
