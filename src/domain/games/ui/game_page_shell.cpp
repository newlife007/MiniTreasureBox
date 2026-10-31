#include "domain/games/ui/game_page_shell.h"

#include "domain/games/game_settings.h"
#include "domain/games/ui/game_audio_controller.h"
#include "domain/games/ui/game_canvas.h"

#include <QCheckBox>
#include <QFrame>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QShowEvent>
#include <QVBoxLayout>

namespace mtb::games::ui {
namespace {

class GamePage final : public QWidget {
public:
    explicit GamePage(QWidget* parent = nullptr)
        : QWidget(parent)
    {
    }

    ~GamePage() override
    {
        GameAudioController::instance().release(this);
    }

    void setMusicEnabled(bool enabled)
    {
        musicEnabled_ = enabled;
        GameAudioController::instance().setEnabled(this, enabled);
    }

protected:
    void showEvent(QShowEvent* event) override
    {
        QWidget::showEvent(event);
        GameAudioController::instance().claim(this, musicEnabled_);
    }

    void hideEvent(QHideEvent* event) override
    {
        GameAudioController::instance().release(this);
        QWidget::hideEvent(event);
    }

private:
    bool musicEnabled_{true};
};

QFrame* metricCard(const QString& title, QLabel* value, QWidget* parent)
{
    auto* frame = new QFrame(parent);
    frame->setObjectName(QStringLiteral("gameMetricCard"));
    auto* titleLabel = new QLabel(title, frame);
    titleLabel->setObjectName(QStringLiteral("gameMetricTitle"));
    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(14, 9, 14, 9);
    layout->setSpacing(2);
    layout->addWidget(titleLabel);
    layout->addWidget(value);
    return frame;
}

}  // namespace

QPushButton* createGameControlButton(const QString& text, const QString& name,
                                     const QString& tip, QWidget* parent)
{
    auto* button = new QPushButton(text, parent);
    button->setObjectName(name);
    button->setToolTip(tip);
    button->setAccessibleName(text);
    button->setAccessibleDescription(tip);
    button->setFocusPolicy(Qt::StrongFocus);
    return button;
}

GamePageParts createGamePage(const QString& id, const QString& instructions,
                             QWidget* parent)
{
    auto* page = new GamePage(parent);
    page->setObjectName(QStringLiteral("game_%1").arg(id));
    page->setProperty("gameId", id);
    auto* storage = new QSettings(page);
    auto settings = std::make_shared<GameSettings>(*storage);

    auto* status = new QLabel(QWidget::tr("准备开始"), page);
    status->setObjectName(QStringLiteral("gameStatus"));
    status->setAccessibleName(QWidget::tr("游戏状态"));
    auto* score = new QLabel(QStringLiteral("0"), page);
    score->setObjectName(QStringLiteral("scoreLabel"));
    score->setAccessibleName(QWidget::tr("当前得分"));
    auto* highScore = new QLabel(QString::number(settings->highScore(id)), page);
    highScore->setObjectName(QStringLiteral("highScoreLabel"));
    highScore->setAccessibleName(QWidget::tr("本地最高分"));

    auto* stateCard = new QFrame(page);
    stateCard->setObjectName(QStringLiteral("gameStateCard"));
    auto* stateCaption = new QLabel(QWidget::tr("●  状态"), stateCard);
    stateCaption->setObjectName(QStringLiteral("gameStateCaption"));
    auto* stateLayout = new QVBoxLayout(stateCard);
    stateLayout->setContentsMargins(14, 9, 14, 9);
    stateLayout->setSpacing(2);
    stateLayout->addWidget(stateCaption);
    stateLayout->addWidget(status);

    auto* information = new QHBoxLayout;
    information->setSpacing(10);
    information->addWidget(stateCard, 1);
    information->addWidget(metricCard(QWidget::tr("当前"), score, page));
    information->addWidget(metricCard(QWidget::tr("最佳"), highScore, page));

    auto* start = createGameControlButton(QWidget::tr("开始游戏"),
                                          QStringLiteral("startButton"),
                                          QWidget::tr("开始新游戏"), page);
    auto* pause = createGameControlButton(QWidget::tr("暂停"),
                                          QStringLiteral("pauseButton"),
                                          QWidget::tr("暂停或继续游戏（空格）"), page);
    auto* restart = createGameControlButton(QWidget::tr("重新开始"),
                                            QStringLiteral("restartButton"),
                                            QWidget::tr("重新开始游戏（R）"), page);
    restart->hide();
    auto* sound = new QCheckBox(QWidget::tr("背景音乐"), page);
    sound->setObjectName(QStringLiteral("soundToggle"));
    sound->setToolTip(QWidget::tr("播放或关闭游戏背景音乐"));
    sound->setAccessibleName(QWidget::tr("背景音乐开关"));
    sound->setChecked(settings->musicEnabled());
    page->setMusicEnabled(settings->musicEnabled());
    QObject::connect(sound, &QCheckBox::toggled, page,
                     [settings, page](bool enabled) {
        settings->setMusicEnabled(enabled);
        page->setMusicEnabled(enabled);
    });

    auto* configFrame = new QFrame(page);
    configFrame->setObjectName(QStringLiteral("gameConfigBar"));
    auto* configLayout = new QHBoxLayout(configFrame);
    configLayout->setContentsMargins(12, 8, 12, 8);
    configLayout->setSpacing(8);
    auto* goal = new QLabel(page);
    goal->setObjectName(QStringLiteral("gameGoal"));
    goal->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    configLayout->addWidget(goal, 1);

    auto* controlsFrame = new QFrame(page);
    controlsFrame->setObjectName(QStringLiteral("gameControlBar"));
    auto* controls = new QHBoxLayout(controlsFrame);
    controls->setContentsMargins(12, 9, 12, 9);
    controls->setSpacing(8);
    controls->addWidget(start);
    controls->addWidget(pause);
    controls->addWidget(restart);
    controls->addStretch();
    controls->addWidget(sound);

    auto* surface = new GameCanvas(id, page);
    surface->setInstructions(instructions);
    auto* helpCard = new QFrame(page);
    helpCard->setObjectName(QStringLiteral("gameHelpCard"));
    helpCard->setMinimumWidth(168);
    helpCard->setMaximumWidth(205);
    auto* helpTitle = new QLabel(QWidget::tr("操作说明"), helpCard);
    helpTitle->setObjectName(QStringLiteral("gameHelpTitle"));
    auto* help = new QLabel(instructions, helpCard);
    help->setObjectName(QStringLiteral("gameInstructions"));
    help->setWordWrap(true);
    help->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    auto* stats = new QLabel(helpCard);
    stats->setObjectName(QStringLiteral("gameStats"));
    stats->setWordWrap(true);
    auto* helpLayout = new QVBoxLayout(helpCard);
    helpLayout->setContentsMargins(15, 14, 15, 14);
    helpLayout->setSpacing(8);
    helpLayout->addWidget(helpTitle);
    helpLayout->addWidget(help);
    helpLayout->addStretch();
    helpLayout->addWidget(stats);

    auto* workspace = new QHBoxLayout;
    workspace->setSpacing(14);
    workspace->addWidget(surface, 1);
    workspace->addWidget(helpCard);

    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    layout->addLayout(information);
    layout->addWidget(configFrame);
    layout->addLayout(workspace, 1);
    layout->addWidget(controlsFrame);

    return {page, layout, surface, status, score, highScore, start, pause,
            restart, sound, help, configLayout, goal, stats, std::move(settings)};
}

}  // namespace mtb::games::ui
