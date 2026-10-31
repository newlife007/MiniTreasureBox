#include "domain/games/ui/game_audio_controller.h"

#include <QApplication>
#include <QAudioOutput>
#include <QMediaPlayer>
#include <QResource>
#include <QUrl>
#include <QWidget>

void initializeGameAudioResource()
{
    Q_INIT_RESOURCE(game_audio);
}

namespace mtb::games::ui {

GameAudioController& GameAudioController::instance()
{
    static GameAudioController controller;
    return controller;
}

GameAudioController::GameAudioController(QObject* parent)
    : QObject(parent)
    , player_(new QMediaPlayer(this))
    , output_(new QAudioOutput(this))
{
    initializeGameAudioResource();
    output_->setVolume(0.16f);
    player_->setAudioOutput(output_);
    player_->setSource(QUrl(QStringLiteral("qrc:/audio/space_quest_loop.ogg")));
    player_->setLoops(QMediaPlayer::Infinite);

    connect(qApp, &QGuiApplication::applicationStateChanged, this,
            [this](Qt::ApplicationState state) {
        if (state == Qt::ApplicationActive) playIfAllowed();
        else player_->pause();
    });
}

void GameAudioController::claim(QWidget* owner, bool enabled)
{
    if (!owner) return;
    if (owner_ != owner) player_->stop();
    owner_ = owner;
    enabled_ = enabled;
    playIfAllowed();
}

void GameAudioController::release(QWidget* owner)
{
    if (owner_ != owner) return;
    player_->stop();
    owner_.clear();
}

void GameAudioController::setEnabled(QWidget* owner, bool enabled)
{
    if (owner_ != owner) return;
    enabled_ = enabled;
    if (enabled_) playIfAllowed();
    else player_->stop();
}

bool GameAudioController::isPlayingFor(const QWidget* owner) const
{
    return owner_ == owner && player_->playbackState() == QMediaPlayer::PlayingState;
}

void GameAudioController::playIfAllowed()
{
    if (!owner_ || !owner_->isVisible() || !enabled_
        || QApplication::platformName() == QStringLiteral("offscreen")
        || QApplication::applicationState() != Qt::ApplicationActive) {
        return;
    }
    if (player_->playbackState() != QMediaPlayer::PlayingState) player_->play();
}

}  // namespace mtb::games::ui
