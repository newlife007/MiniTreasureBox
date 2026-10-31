#pragma once

#include <QFrame>

class QLabel;

namespace mtb::ui {

class MetricCard final : public QFrame {
public:
    MetricCard(const QString& title, QLabel* valueLabel,
               QWidget* parent = nullptr);
};

}  // namespace mtb::ui
