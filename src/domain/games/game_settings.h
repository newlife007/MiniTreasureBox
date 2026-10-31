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
    [[nodiscard]] bool musicEnabled() const;
    void setMusicEnabled(bool enabled);
    void recordGameStarted(const QString& gameId, const QString& mode);
    void recordGameFinished(const QString& gameId, const QString& mode,
                            bool won, int score);
    [[nodiscard]] int gamesPlayed(const QString& gameId, const QString& mode) const;
    [[nodiscard]] int gamesWon(const QString& gameId, const QString& mode) const;
    [[nodiscard]] int longestStreak(const QString& gameId, const QString& mode) const;
    [[nodiscard]] int modeBestScore(const QString& gameId, const QString& mode) const;
private:
    QSettings* storage_;
};

}  // namespace mtb::games
