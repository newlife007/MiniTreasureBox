#include "domain/games/game_settings.h"

#include <QSettings>

namespace mtb::games {
namespace {
QString scoreKey(const QString& gameId)
{
    return QStringLiteral("games/%1/highScore").arg(gameId);
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
