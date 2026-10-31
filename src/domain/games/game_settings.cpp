#include "domain/games/game_settings.h"

#include <QSettings>

#include <algorithm>

namespace mtb::games {
namespace {
QString scoreKey(const QString& gameId)
{
    return QStringLiteral("games/%1/highScore").arg(gameId);
}

QString winsKey(const QString& gameId)
{
    return QStringLiteral("games/%1/wins").arg(gameId);
}

QString statKey(const QString& gameId, const QString& mode, const QString& name)
{
    return QStringLiteral("games/%1/stats/%2/%3").arg(gameId, mode, name);
}
}

GameSettings::GameSettings(QSettings& storage)
    : storage_(&storage)
{
}

int GameSettings::highScore(const QString& gameId) const
{
    return storage_->value(scoreKey(gameId), 0).toInt();
}

bool GameSettings::updateHighScore(const QString& gameId, int score)
{
    if (score <= highScore(gameId)) {
        return false;
    }
    storage_->setValue(scoreKey(gameId), score);
    storage_->sync();
    return true;
}

int GameSettings::winCount(const QString& gameId) const
{
    const QString key = winsKey(gameId);
    if (storage_->contains(key)) return storage_->value(key).toInt();
    return highScore(gameId);
}

int GameSettings::incrementWinCount(const QString& gameId)
{
    const int count = winCount(gameId) + 1;
    storage_->setValue(winsKey(gameId), count);
    storage_->sync();
    return count;
}

bool GameSettings::soundEnabled() const
{
    return storage_->value(QStringLiteral("games/soundEnabled"), true).toBool();
}

void GameSettings::setSoundEnabled(bool enabled)
{
    storage_->setValue(QStringLiteral("games/soundEnabled"), enabled);
    storage_->sync();
}

bool GameSettings::musicEnabled() const
{
    return storage_->value(QStringLiteral("games/musicEnabled"), true).toBool();
}

void GameSettings::setMusicEnabled(bool enabled)
{
    storage_->setValue(QStringLiteral("games/musicEnabled"), enabled);
    storage_->sync();
}

void GameSettings::recordGameStarted(const QString& gameId, const QString& mode)
{
    const QString key = statKey(gameId, mode, QStringLiteral("played"));
    storage_->setValue(key, storage_->value(key, 0).toInt() + 1);
    storage_->sync();
}

void GameSettings::recordGameFinished(const QString& gameId, const QString& mode,
                                      bool won, int score)
{
    const QString completedKey = statKey(gameId, mode, QStringLiteral("completed"));
    storage_->setValue(completedKey, storage_->value(completedKey, 0).toInt() + 1);
    const QString lastScoreKey = statKey(gameId, mode, QStringLiteral("lastScore"));
    storage_->setValue(lastScoreKey, score);
    const QString bestScoreKey = statKey(gameId, mode, QStringLiteral("bestScore"));
    storage_->setValue(bestScoreKey,
                       std::max(storage_->value(bestScoreKey, 0).toInt(), score));

    const QString streakKey = statKey(gameId, mode, QStringLiteral("currentStreak"));
    int streak = storage_->value(streakKey, 0).toInt();
    if (won) {
        const QString wonKey = statKey(gameId, mode, QStringLiteral("won"));
        storage_->setValue(wonKey, storage_->value(wonKey, 0).toInt() + 1);
        ++streak;
        const QString longestKey = statKey(gameId, mode, QStringLiteral("longestStreak"));
        storage_->setValue(longestKey,
                           std::max(storage_->value(longestKey, 0).toInt(), streak));
    } else {
        streak = 0;
    }
    storage_->setValue(streakKey, streak);
    storage_->sync();
}

int GameSettings::gamesPlayed(const QString& gameId, const QString& mode) const
{
    return storage_->value(statKey(gameId, mode, QStringLiteral("played")), 0).toInt();
}

int GameSettings::gamesWon(const QString& gameId, const QString& mode) const
{
    return storage_->value(statKey(gameId, mode, QStringLiteral("won")), 0).toInt();
}

int GameSettings::longestStreak(const QString& gameId, const QString& mode) const
{
    return storage_->value(statKey(gameId, mode, QStringLiteral("longestStreak")), 0).toInt();
}

int GameSettings::modeBestScore(const QString& gameId, const QString& mode) const
{
    return storage_->value(statKey(gameId, mode, QStringLiteral("bestScore")), 0).toInt();
}

}  // namespace mtb::games
