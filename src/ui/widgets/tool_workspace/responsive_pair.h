#pragma once

#include <QWidget>

class QBoxLayout;
class QResizeEvent;

namespace mtb::ui {

class ResponsivePair final : public QWidget {
public:
    ResponsivePair(QWidget* first, QWidget* second, int breakpoint = 680,
                   QWidget* parent = nullptr);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void updateDirection();

    QBoxLayout* layout_;
    int breakpoint_;
};

}  // namespace mtb::ui
