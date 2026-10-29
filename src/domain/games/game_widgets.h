#pragma once

class QWidget;

namespace mtb::games {

[[nodiscard]] QWidget* createGuessNumberWidget(QWidget* parent = nullptr);
[[nodiscard]] QWidget* create2048Widget(QWidget* parent = nullptr);
[[nodiscard]] QWidget* createSnakeWidget(QWidget* parent = nullptr);
[[nodiscard]] QWidget* createTetrisWidget(QWidget* parent = nullptr);
[[nodiscard]] QWidget* createGomokuWidget(QWidget* parent = nullptr);

}  // namespace mtb::games
