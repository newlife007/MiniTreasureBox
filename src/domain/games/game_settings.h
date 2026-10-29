#pragma once

#include <QString>

class QSettings;

namespace mtb::games {

class GameSettings {
public:
    explicit GameSettings(QSettings& storage);
    [[nodiscard]] int highScore(const QString& gameId) const;
    bool updateHighScore(const QString& gameId, int score);
    [[nodiscard]] bool soundEnabled() const;
    void setSoundEnabled(bool enabled);
private:
    QSettings* storage_;
};

}  // namespace mtb::games
