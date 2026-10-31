#include "ui/widgets/tool_workspace/metric_card.h"

#include <QLabel>
#include <QVBoxLayout>

namespace mtb::ui {

MetricCard::MetricCard(const QString& title, QLabel* valueLabel, QWidget* parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("toolMetricCard"));
    auto* titleLabel = new QLabel(title, this);
    titleLabel->setObjectName(QStringLiteral("toolMetricTitle"));
    valueLabel->setProperty("toolMetricValue", true);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 10, 14, 10);
    layout->setSpacing(3);
    layout->addWidget(titleLabel);
    layout->addWidget(valueLabel);
}

}  // namespace mtb::ui
