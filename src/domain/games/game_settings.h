#pragma once

#include <QString>

class QSettings;

namespace mtb::games {

class GameSettings {
public:
    explicit GameSettings(QSettings& storage);
    [[nodiscard]] int highScore(const QString& gameId) const;
    bool updateHighScore(const QString& gameId, int score);
    [[nodiscard]] int winCount(const QString& gameId) const;
    int incrementWinCount(const QString& gameId);
    [[nodiscard]] bool soundEnabled() const;
    void setSoundEnabled(bool enabled);
private:
    QSettings* storage_;
};

}  // namespace mtb::games
