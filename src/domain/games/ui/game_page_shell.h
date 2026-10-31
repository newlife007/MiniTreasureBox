#pragma once

#include <QString>

#include <memory>

class QCheckBox;
class QFrame;
class QHBoxLayout;
class QLabel;
class QPushButton;
class QVBoxLayout;
class QWidget;

namespace mtb::games {
class GameSettings;
}

namespace mtb::games::ui {

class GameCanvas;

struct GamePageParts {
    QWidget* page;
    QVBoxLayout* layout;
    GameCanvas* surface;
    QLabel* status;
    QLabel* score;
    QLabel* highScore;
    QPushButton* start;
    QPushButton* pause;
    QPushButton* restart;
    QCheckBox* sound;
    QLabel* instructions;
    QHBoxLayout* configLayout;
    QLabel* goal;
    QLabel* stats;
    std::shared_ptr<GameSettings> settings;
};

[[nodiscard]] QPushButton* createGameControlButton(const QString& text,
                                                    const QString& name,
                                                    const QString& tip,
                                                    QWidget* parent);
[[nodiscard]] GamePageParts createGamePage(const QString& id,
                                           const QString& instructions,
                                           QWidget* parent = nullptr);

}  // namespace mtb::games::ui
