#include "domain/games/game_settings.h"

#include <QSettings>

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

}  // namespace mtb::games
