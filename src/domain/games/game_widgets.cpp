#include "domain/games/game_widgets.h"

#include "domain/games/game_logic.h"
#include "domain/games/game_settings.h"
#include "domain/games/ui/board_geometry.h"
#include "domain/games/ui/game_canvas.h"
#include "domain/games/ui/game_page_shell.h"
#include "domain/games/ui/game_visual_theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRadialGradient>
#include <QRandomGenerator>
#include <QStringList>
#include <QTimer>
#include <QtConcurrentRun>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <memory>

namespace mtb::games {
namespace {

using ui::GameCanvas;
using ui::GamePageParts;

class GuessInput final : public QLineEdit {
public:
    explicit GuessInput(QWidget* parent = nullptr)
        : QLineEdit(parent)
    {
    }

    std::function<void(int)> gameKey;

protected:
    void keyPressEvent(QKeyEvent* event) override
    {
        if (gameKey && (event->key() == Qt::Key_Space || event->key() == Qt::Key_R)) {
            gameKey(event->key());
            event->accept();
            return;
        }
        QLineEdit::keyPressEvent(event);
    }
};

Direction directionForKey(int key, Direction fallback)
{
    if (key == Qt::Key_Left || key == Qt::Key_A) return Direction::Left;
    if (key == Qt::Key_Right || key == Qt::Key_D) return Direction::Right;
    if (key == Qt::Key_Up || key == Qt::Key_W) return Direction::Up;
    if (key == Qt::Key_Down || key == Qt::Key_S) return Direction::Down;
    return fallback;
}

QComboBox* addGameOption(const GamePageParts& parts, const QString& labelText,
                         const QString& objectName,
                         const QList<QPair<QString, QVariant>>& options)
{
    auto* label = new QLabel(labelText, parts.page);
    label->setObjectName(QStringLiteral("gameOptionLabel"));
    auto* combo = new QComboBox(parts.page);
    combo->setObjectName(objectName);
    for (const auto& [text, value] : options) combo->addItem(text, value);
    const int goalIndex = parts.configLayout->indexOf(parts.goal);
    parts.configLayout->insertWidget(goalIndex, label);
    parts.configLayout->insertWidget(goalIndex + 1, combo);
    return combo;
}

void updateGameStats(const GamePageParts& parts, const QString& gameId,
                     const QString& mode)
{
    parts.stats->setText(QWidget::tr("本地统计\n游玩 %1 局 · 完成 %2 局\n最佳 %3 · 最长连胜 %4")
                             .arg(parts.settings->gamesPlayed(gameId, mode))
                             .arg(parts.settings->gamesWon(gameId, mode))
                             .arg(parts.settings->modeBestScore(gameId, mode))
                             .arg(parts.settings->longestStreak(gameId, mode)));
}

QString selectedMode(const QComboBox* combo)
{
    return combo->currentData().toString();
}

void setRunningVisuals(const GamePageParts& parts)
{
    parts.surface->clearOverlay();
    parts.surface->setProperty("gameState", QStringLiteral("running"));
    parts.start->hide();
    parts.restart->show();
    parts.pause->setEnabled(true);
}

QVector<QPoint> previewCells(TetrisGame::Piece piece)
{
    switch (piece) {
    case TetrisGame::Piece::I: return {{0, 1}, {1, 1}, {2, 1}, {3, 1}};
    case TetrisGame::Piece::O: return {{1, 0}, {2, 0}, {1, 1}, {2, 1}};
    case TetrisGame::Piece::T: return {{0, 0}, {1, 0}, {2, 0}, {1, 1}};
    case TetrisGame::Piece::L: return {{0, 0}, {1, 0}, {2, 0}, {2, 1}};
    case TetrisGame::Piece::J: return {{0, 0}, {1, 0}, {2, 0}, {0, 1}};
    case TetrisGame::Piece::S: return {{1, 0}, {2, 0}, {0, 1}, {1, 1}};
    case TetrisGame::Piece::Z: return {{0, 0}, {1, 0}, {1, 1}, {2, 1}};
    }
    return {};
}

}  // namespace

QWidget* createGuessNumberWidget(QWidget* parent)
{
    GamePageParts parts = ui::createGamePage(
        QStringLiteral("guess-number"),
        QWidget::tr("输入 1–100 的整数并按 Enter\nSpace  暂停 / 继续\nR  重新开始"), parent);
    auto game = std::make_shared<GuessNumberGame>();
    auto lower = std::make_shared<int>(1);
    auto upper = std::make_shared<int>(100);
    auto target = std::make_shared<int>(50);
    auto maxAttempts = std::make_shared<int>(0);
    auto finishedRecorded = std::make_shared<bool>(false);
    auto history = std::make_shared<QStringList>();
    auto* mode = addGameOption(parts, QWidget::tr("模式"),
                               QStringLiteral("guessModeSelector"),
                               {{QWidget::tr("经典"), QStringLiteral("classic")},
                                {QWidget::tr("限次挑战"), QStringLiteral("limited")}});
    auto* range = addGameOption(parts, QWidget::tr("范围"),
                                QStringLiteral("guessRangeSelector"),
                                {{QStringLiteral("1–100"), 100},
                                 {QStringLiteral("1–500"), 500},
                                 {QStringLiteral("1–1000"), 1000}});
    const auto updateGoal = [=] {
        const bool limited = selectedMode(mode) == QStringLiteral("limited");
        parts.goal->setText(limited
            ? QWidget::tr("目标：在限定次数内猜中")
            : QWidget::tr("目标：用尽量少的次数猜中"));
    };
    QObject::connect(mode, &QComboBox::currentIndexChanged, parts.page, updateGoal);
    QObject::connect(range, &QComboBox::currentIndexChanged, parts.page, updateGoal);
    updateGoal();

    auto* input = new GuessInput(parts.page);
    input->setObjectName(QStringLiteral("guessInput"));
    input->setPlaceholderText(QWidget::tr("输入 1 到 100 的整数"));
    input->setAccessibleName(QWidget::tr("猜测数字"));
    input->setValidator(new QIntValidator(1, 1000, input));
    input->setMinimumHeight(42);
    auto* submit = ui::createGameControlButton(
        QWidget::tr("猜一猜"), QStringLiteral("guessButton"),
        QWidget::tr("提交猜测（Enter）"), parts.page);
    auto* entry = new QHBoxLayout;
    entry->setSpacing(8);
    entry->addWidget(input, 1);
    entry->addWidget(submit);
    parts.layout->insertLayout(2, entry);

    const auto start = [=] {
        const int maximum = range->currentData().toInt();
        *target = QRandomGenerator::global()->bounded(1, maximum + 1);
        *maxAttempts = selectedMode(mode) == QStringLiteral("limited")
            ? (maximum <= 100 ? 7 : maximum <= 500 ? 10 : 12)
            : 0;
        *finishedRecorded = false;
        game->start(*target, 1, maximum);
        *lower = 1;
        *upper = maximum;
        history->clear();
        input->setPlaceholderText(QWidget::tr("输入 1 到 %1 的整数").arg(maximum));
        parts.settings->recordGameStarted(QStringLiteral("guess-number"), selectedMode(mode));
        updateGameStats(parts, QStringLiteral("guess-number"), selectedMode(mode));
        input->clear();
        parts.status->setText(QWidget::tr("请输入 1 到 %1 之间的数字").arg(maximum));
        parts.score->setText(*maxAttempts > 0
                                 ? QWidget::tr("剩余 %1 次").arg(*maxAttempts)
                                 : QWidget::tr("尝试 0 次"));
        parts.pause->setText(QWidget::tr("暂停"));
        input->setEnabled(true);
        input->setReadOnly(false);
        submit->setEnabled(true);
        setRunningVisuals(parts);
        input->setFocus();
        parts.surface->update();
    };
    const auto pause = [=] {
        game->togglePause();
        const bool paused = game->state() == GameState::Paused;
        if (game->state() != GameState::Running && !paused) return;
        parts.pause->setText(paused ? QWidget::tr("继续") : QWidget::tr("暂停"));
        parts.status->setText(paused ? QWidget::tr("已暂停")
                                     : QWidget::tr("请输入 %1 到 %2 之间的数字")
                                           .arg(game->minimum()).arg(game->maximum()));
        input->setReadOnly(paused);
        submit->setEnabled(!paused);
        parts.surface->setProperty("gameState", paused ? QStringLiteral("paused")
                                                       : QStringLiteral("running"));
        if (paused) parts.surface->showOverlay(QWidget::tr("已暂停"), QWidget::tr("按 Space 继续"));
        else parts.surface->clearOverlay();
        input->setFocus();
    };
    const auto submitGuess = [=] {
        bool valueOk = false;
        const int value = input->text().toInt(&valueOk);
        const GuessResult result = game->guess(input->text());
        QString feedback = result.message;
        if (valueOk && (result.outcome == GuessResult::Outcome::TooLow
                        || result.outcome == GuessResult::Outcome::TooHigh)) {
            const int distance = std::abs(value - *target);
            const int span = game->maximum() - game->minimum() + 1;
            const QString heat = distance <= std::max(2, span / 50)
                ? QWidget::tr("非常接近")
                : distance <= std::max(5, span / 10)
                    ? QWidget::tr("有点接近") : QWidget::tr("还比较远");
            feedback += QStringLiteral(" · ") + heat;
        }
        parts.status->setText(feedback);
        parts.score->setText(*maxAttempts > 0
            ? QWidget::tr("剩余 %1 次").arg(std::max(0, *maxAttempts - game->attempts()))
            : QWidget::tr("尝试 %1 次").arg(game->attempts()));
        if (valueOk && result.outcome == GuessResult::Outcome::TooLow) {
            *lower = std::max(*lower, value + 1);
            history->prepend(QWidget::tr("%1  ↑ 太小").arg(value));
        } else if (valueOk && result.outcome == GuessResult::Outcome::TooHigh) {
            *upper = std::min(*upper, value - 1);
            history->prepend(QWidget::tr("%1  ↓ 太大").arg(value));
        }
        while (history->size() > 5) history->removeLast();
        if (result.outcome == GuessResult::Outcome::Correct) {
            const int span = game->maximum() - game->minimum() + 1;
            const int score = std::max(1, span * 10 - game->attempts() * 75);
            parts.settings->updateHighScore(QStringLiteral("guess-number"), score);
            parts.settings->recordGameFinished(QStringLiteral("guess-number"),
                                                selectedMode(mode), true, score);
            *finishedRecorded = true;
            updateGameStats(parts, QStringLiteral("guess-number"), selectedMode(mode));
            parts.highScore->setText(QString::number(
                parts.settings->highScore(QStringLiteral("guess-number"))));
            parts.surface->setProperty("gameState", QStringLiteral("won"));
            parts.surface->showOverlay(QWidget::tr("猜对了！"),
                                       QWidget::tr("用了 %1 次，得分 %2")
                                           .arg(game->attempts()).arg(score));
            input->setEnabled(false);
            submit->setEnabled(false);
            parts.pause->setEnabled(false);
        } else if (*maxAttempts > 0 && game->attempts() >= *maxAttempts) {
            game->finish();
            if (!*finishedRecorded) {
                parts.settings->recordGameFinished(QStringLiteral("guess-number"),
                                                    selectedMode(mode), false, 0);
                *finishedRecorded = true;
                updateGameStats(parts, QStringLiteral("guess-number"), selectedMode(mode));
            }
            parts.status->setText(QWidget::tr("次数已用完"));
            parts.surface->showOverlay(QWidget::tr("挑战失败"),
                                       QWidget::tr("答案是 %1，按 R 再试一次").arg(*target));
            input->setEnabled(false);
            submit->setEnabled(false);
            parts.pause->setEnabled(false);
        }
        input->selectAll();
        parts.surface->update();
    };

    QObject::connect(parts.start, &QPushButton::clicked, parts.page, start);
    QObject::connect(parts.restart, &QPushButton::clicked, parts.page, start);
    QObject::connect(parts.pause, &QPushButton::clicked, parts.page, pause);
    QObject::connect(submit, &QPushButton::clicked, parts.page, submitGuess);
    QObject::connect(input, &QLineEdit::returnPressed, parts.page, submitGuess);
    QObject::connect(mode, &QComboBox::currentIndexChanged, parts.page, [=] {
        if (game->state() != GameState::Ready) start();
    });
    QObject::connect(range, &QComboBox::currentIndexChanged, parts.page, [=] {
        if (game->state() != GameState::Ready) start();
    });
    input->gameKey = [=](int key) {
        if (key == Qt::Key_R) start();
        else if (key == Qt::Key_Space) pause();
    };
    parts.surface->setKeyHandler(input->gameKey);
    parts.surface->setPaintHandler([=](QPainter& painter, const QRect& area) {
        const ui::GameVisualTheme theme = parts.surface->visualTheme();
        const QRect card = area.adjusted(area.width() / 10, area.height() / 8,
                                         -area.width() / 10, -area.height() / 8);
        painter.setPen(Qt::NoPen);
        painter.setBrush(theme.accentSoft);
        painter.drawRoundedRect(card, 18, 18);

        painter.setPen(theme.muted);
        QFont caption = painter.font();
        caption.setPointSize(11);
        painter.setFont(caption);
        painter.drawText(card.adjusted(22, 16, -22, -16), Qt::AlignTop | Qt::AlignHCenter,
                         QWidget::tr("当前有效范围"));

        painter.setPen(theme.accentStrong);
        QFont rangeFont = painter.font();
        rangeFont.setPointSize(28);
        rangeFont.setBold(true);
        painter.setFont(rangeFont);
        painter.drawText(card.adjusted(20, 36, -20, -card.height() / 2),
                         Qt::AlignCenter, QStringLiteral("%1  —  %2").arg(*lower).arg(*upper));

        const int barY = card.center().y() + 14;
        const QRect bar(card.left() + 28, barY, card.width() - 56, 8);
        painter.setBrush(theme.grid);
        painter.drawRoundedRect(bar, 4, 4);
        const qreal denominator = std::max(1, game->maximum() - game->minimum());
        const qreal startRatio = (*lower - game->minimum()) / denominator;
        const qreal spanRatio = std::max(0, *upper - *lower) / denominator;
        painter.setBrush(theme.accent);
        painter.drawRoundedRect(QRectF(bar.left() + bar.width() * startRatio, bar.top(),
                                       std::max<qreal>(8, bar.width() * spanRatio), bar.height()),
                                4, 4);

        painter.setPen(theme.text);
        QFont historyFont = painter.font();
        historyFont.setPointSize(10);
        historyFont.setBold(false);
        painter.setFont(historyFont);
        painter.drawText(card.adjusted(28, card.height() / 2 + 30, -28, -18),
                         Qt::AlignTop | Qt::AlignHCenter,
                         history->isEmpty() ? QWidget::tr("开始猜测后会显示最近记录")
                                            : history->join(QStringLiteral("   ")));
    });
    parts.surface->showOverlay(QWidget::tr("准备开始"), QWidget::tr("点击“开始游戏”"));
    input->setEnabled(false);
    submit->setEnabled(false);
    parts.pause->setEnabled(false);
    return parts.page;
}

QWidget* create2048Widget(QWidget* parent)
{
    GamePageParts parts = ui::createGamePage(
        QStringLiteral("game-2048"),
        QWidget::tr("方向键 / WASD  移动\n每次有效移动后会新增一个 2 或 4\nSpace  暂停 / 继续\nR  重新开始"), parent);
    auto game = std::make_shared<Game2048>();
    auto paused = std::make_shared<bool>(false);
    auto moves = std::make_shared<int>(0);
    auto combo = std::make_shared<int>(0);
    auto undoUsed = std::make_shared<bool>(false);
    auto undoBoard = std::make_shared<Game2048::Board>();
    auto undoScore = std::make_shared<int>(0);
    auto undoMoves = std::make_shared<int>(0);
    auto undoCombo = std::make_shared<int>(0);
    auto finishedRecorded = std::make_shared<bool>(false);
    auto challengeOver = std::make_shared<bool>(false);
    auto activeMode = std::make_shared<QString>(QStringLiteral("classic"));
    auto* mode = addGameOption(parts, QWidget::tr("模式"),
                               QStringLiteral("game2048ModeSelector"),
                               {{QWidget::tr("经典"), QStringLiteral("classic")},
                                {QWidget::tr("步数挑战"), QStringLiteral("moves")}});
    auto* undo = ui::createGameControlButton(
        QWidget::tr("撤销一步"), QStringLiteral("undoButton"),
        QWidget::tr("每局可撤销最近一次有效移动"), parts.page);
    undo->setEnabled(false);
    if (auto* controls = qobject_cast<QHBoxLayout*>(parts.restart->parentWidget()->layout()))
        controls->insertWidget(2, undo);
    const auto updateGoal = [=] {
        parts.goal->setText(selectedMode(mode) == QStringLiteral("moves")
            ? QWidget::tr("目标：80 步内合成 512")
            : QWidget::tr("目标：合成 2048"));
    };
    QObject::connect(mode, &QComboBox::currentIndexChanged, parts.page, updateGoal);
    updateGoal();

    const auto refresh = [=] {
        parts.score->setText(*combo > 1
            ? QWidget::tr("%1 分 · %2 步 · x%3").arg(game->score()).arg(*moves).arg(*combo)
            : QWidget::tr("%1 分 · %2 步").arg(game->score()).arg(*moves));
        parts.settings->updateHighScore(QStringLiteral("game-2048"), game->score());
        parts.highScore->setText(QString::number(
            parts.settings->highScore(QStringLiteral("game-2048"))));
        parts.surface->setProperty("score", game->score());
        parts.surface->setProperty("tileCount", 16 - game->emptyCells().size());
        QString boardState;
        int maximumTile = 0;
        for (const auto& row : game->board()) {
            for (int value : row) {
                boardState += QString::number(value) + QLatin1Char(',');
                maximumTile = std::max(maximumTile, value);
            }
        }
        parts.surface->setProperty("boardState", boardState);
        const int targetTile = *activeMode == QStringLiteral("moves") ? 512 : 2048;
        const bool targetReached = maximumTile >= targetTile;
        if (targetReached) {
            parts.status->setText(QWidget::tr("已合成 %1，挑战完成！").arg(targetTile));
            parts.surface->showOverlay(QWidget::tr("达成 %1！").arg(targetTile),
                                       QWidget::tr("得分 %1 · 使用 %2 步")
                                           .arg(game->score()).arg(*moves));
            parts.pause->setEnabled(false);
            undo->setEnabled(false);
            if (!*finishedRecorded) {
                parts.settings->recordGameFinished(QStringLiteral("game-2048"),
                                                    *activeMode, true, game->score());
                *finishedRecorded = true;
                updateGameStats(parts, QStringLiteral("game-2048"), *activeMode);
            }
        } else if (game->isGameOver() || *challengeOver) {
            parts.status->setText(*challengeOver ? QWidget::tr("步数已用完")
                                                  : QWidget::tr("游戏结束"));
            parts.surface->showOverlay(parts.status->text(), QWidget::tr("按 R 再来一局"));
            parts.pause->setEnabled(false);
            undo->setEnabled(false);
            if (!*finishedRecorded) {
                parts.settings->recordGameFinished(QStringLiteral("game-2048"),
                                                    *activeMode, false, game->score());
                *finishedRecorded = true;
                updateGameStats(parts, QStringLiteral("game-2048"), *activeMode);
            }
        } else if (*paused) {
            parts.status->setText(QWidget::tr("已暂停"));
            parts.surface->showOverlay(QWidget::tr("已暂停"), QWidget::tr("按 Space 继续"));
        } else {
            parts.status->setText(*combo > 1 ? QWidget::tr("连锁合并 x%1").arg(*combo)
                                              : QWidget::tr("游戏中"));
            parts.surface->clearOverlay();
        }
        parts.surface->update();
    };
    const auto start = [=] {
        game->start();
        *activeMode = selectedMode(mode);
        *paused = false;
        *moves = 0;
        *combo = 0;
        *undoUsed = false;
        *challengeOver = false;
        *finishedRecorded = false;
        undo->setEnabled(false);
        parts.settings->recordGameStarted(QStringLiteral("game-2048"), *activeMode);
        updateGameStats(parts, QStringLiteral("game-2048"), *activeMode);
        parts.pause->setText(QWidget::tr("暂停"));
        setRunningVisuals(parts);
        refresh();
        parts.surface->setFocus();
    };
    const auto pause = [=] {
        if (game->isGameOver() || *challengeOver || *finishedRecorded) return;
        *paused = !*paused;
        parts.pause->setText(*paused ? QWidget::tr("继续") : QWidget::tr("暂停"));
        refresh();
    };
    const auto move = [=](Direction direction) {
        if (*paused || game->isGameOver() || *challengeOver || *finishedRecorded) return;
        const Game2048::Board beforeBoard = game->board();
        const int beforeScore = game->score();
        const int beforeMoves = *moves;
        const int beforeCombo = *combo;
        if (game->move(direction)) {
            *undoBoard = beforeBoard;
            *undoScore = beforeScore;
            *undoMoves = beforeMoves;
            *undoCombo = beforeCombo;
            undo->setEnabled(!*undoUsed);
            ++*moves;
            if (game->score() > beforeScore) {
                ++*combo;
                if (*combo > 1) game->restore(game->board(), game->score() + (*combo - 1) * 8);
                } else {
                *combo = 0;
            }
            if (*activeMode == QStringLiteral("moves") && *moves >= 80)
                *challengeOver = true;
            refresh();
        }
    };

    QObject::connect(parts.start, &QPushButton::clicked, parts.page, start);
    QObject::connect(parts.restart, &QPushButton::clicked, parts.page, start);
    QObject::connect(parts.pause, &QPushButton::clicked, parts.page, pause);
    QObject::connect(mode, &QComboBox::currentIndexChanged, parts.page, start);
    QObject::connect(undo, &QPushButton::clicked, parts.page, [=] {
        if (*undoUsed || !undo->isEnabled() || *finishedRecorded) return;
        game->restore(*undoBoard, *undoScore);
        *moves = *undoMoves;
        *combo = *undoCombo;
        *challengeOver = false;
        *undoUsed = true;
        undo->setEnabled(false);
        refresh();
        parts.surface->setFocus();
    });
    parts.surface->setKeyHandler([=](int key) {
        if (key == Qt::Key_R) start();
        else if (key == Qt::Key_Space) pause();
        else if (key == Qt::Key_Left || key == Qt::Key_A || key == Qt::Key_Right
                 || key == Qt::Key_D || key == Qt::Key_Up || key == Qt::Key_W
                 || key == Qt::Key_Down || key == Qt::Key_S)
            move(directionForKey(key, Direction::Left));
    });
    parts.surface->setPaintHandler([=](QPainter& painter, const QRect& area) {
        const ui::CellGridGeometry geometry = ui::makeCellGrid(area, 4, 4, 8);
        const ui::GameVisualTheme theme = parts.surface->visualTheme();
        painter.setPen(Qt::NoPen);
        painter.setBrush(theme.grid);
        painter.drawRoundedRect(geometry.board.adjusted(-6, -6, 6, 6), 15, 15);
        for (int row = 0; row < 4; ++row) {
            for (int column = 0; column < 4; ++column) {
                const int value = game->board()[row][column];
                const QRect tile = geometry.cellRect(column, row, 4);
                const QColor fill = value == 0
                    ? theme.board
                    : ui::tile2048Color(value, theme.dark);
                painter.setBrush(fill);
                painter.drawRoundedRect(tile, std::max(7, geometry.cell / 12),
                                        std::max(7, geometry.cell / 12));
                if (!value) continue;
                painter.setPen(ui::readableTextColor(fill));
                QFont font = painter.font();
                font.setBold(true);
                const int digits = QString::number(value).size();
                font.setPixelSize(std::max(15, geometry.cell / (digits >= 4 ? 3 : 2)));
                painter.setFont(font);
                painter.drawText(tile, Qt::AlignCenter, QString::number(value));
                painter.setPen(Qt::NoPen);
            }
        }
    });
    start();
    return parts.page;
}

QWidget* createSnakeWidget(QWidget* parent)
{
    GamePageParts parts = ui::createGamePage(
        QStringLiteral("snake"),
        QWidget::tr("方向键 / WASD  转向\nSpace  暂停 / 继续\nR  重新开始"), parent);
    auto game = std::make_shared<SnakeGame>();
    auto* timer = new QTimer(parts.page);
    auto activeMode = std::make_shared<QString>(QStringLiteral("classic"));
    auto meals = std::make_shared<int>(0);
    auto eatCombo = std::make_shared<int>(0);
    auto finishedRecorded = std::make_shared<bool>(false);
    auto lastEat = std::make_shared<QElapsedTimer>();
    auto* mode = addGameOption(parts, QWidget::tr("模式"),
                               QStringLiteral("snakeModeSelector"),
                               {{QWidget::tr("经典"), QStringLiteral("classic")},
                                {QWidget::tr("穿墙"), QStringLiteral("wrap")},
                                {QWidget::tr("障碍"), QStringLiteral("obstacles")}});
    const auto updateGoal = [=] {
        const QString selected = selectedMode(mode);
        parts.goal->setText(selected == QStringLiteral("obstacles")
            ? QWidget::tr("目标：避开障碍并达到 100 分")
            : selected == QStringLiteral("wrap")
                ? QWidget::tr("目标：利用穿墙路线达到 100 分")
                : QWidget::tr("目标：达到 100 分"));
    };
    QObject::connect(mode, &QComboBox::currentIndexChanged, parts.page, updateGoal);
    updateGoal();
    timer->setInterval(180);

    const auto refresh = [=] {
        const int level = game->score() / 50 + 1;
        parts.score->setText(*eatCombo > 1
            ? QWidget::tr("%1 分 · Lv.%2 · x%3").arg(game->score()).arg(level).arg(*eatCombo)
            : QWidget::tr("%1 分 · Lv.%2").arg(game->score()).arg(level));
        parts.settings->updateHighScore(QStringLiteral("snake"), game->score());
        parts.highScore->setText(QString::number(parts.settings->highScore(QStringLiteral("snake"))));
        parts.surface->setProperty("score", game->score());
        if (game->state() == GameState::Paused) {
            parts.status->setText(QWidget::tr("已暂停"));
            parts.surface->showOverlay(QWidget::tr("已暂停"), QWidget::tr("按 Space 继续"));
        } else if (game->state() == GameState::GameOver || game->state() == GameState::Won) {
            const bool won = game->score() >= 100 || game->state() == GameState::Won;
            parts.status->setText(won ? QWidget::tr("目标完成") : QWidget::tr("游戏结束"));
            parts.surface->showOverlay(won ? QWidget::tr("目标完成！") : QWidget::tr("碰撞了"),
                                       QWidget::tr("得分 %1，按 R 再来一局").arg(game->score()));
            parts.pause->setEnabled(false);
            timer->stop();
            if (!*finishedRecorded) {
                parts.settings->recordGameFinished(QStringLiteral("snake"), *activeMode,
                                                    won, game->score());
                *finishedRecorded = true;
                updateGameStats(parts, QStringLiteral("snake"), *activeMode);
            }
        } else {
            if (game->score() >= 100 && !*finishedRecorded) {
                parts.settings->recordGameFinished(QStringLiteral("snake"), *activeMode,
                                                    true, game->score());
                *finishedRecorded = true;
                updateGameStats(parts, QStringLiteral("snake"), *activeMode);
            }
            parts.status->setText(game->score() >= 100
                ? QWidget::tr("目标完成，继续挑战！")
                : game->foodIsSpecial()
                    ? QWidget::tr("奖励食物出现了！")
                    : *eatCombo > 1 ? QWidget::tr("连续进食 x%1").arg(*eatCombo)
                                    : QWidget::tr("游戏中"));
            parts.surface->clearOverlay();
        }
        parts.surface->update();
    };
    const auto start = [=] {
        *activeMode = selectedMode(mode);
        game->setMode(*activeMode == QStringLiteral("wrap")
                          ? SnakeMode::Wrap
                          : *activeMode == QStringLiteral("obstacles")
                              ? SnakeMode::Obstacles : SnakeMode::Classic);
        game->setObstacles(*activeMode == QStringLiteral("obstacles")
            ? QVector<QPoint>{{6, 4}, {7, 4}, {8, 4}, {9, 4},
                              {6, 11}, {7, 11}, {8, 11}, {9, 11},
                              {12, 7}, {12, 8}}
            : QVector<QPoint>{});
        game->start();
        *meals = 0;
        *eatCombo = 0;
        *finishedRecorded = false;
        lastEat->invalidate();
        timer->setInterval(180);
        parts.settings->recordGameStarted(QStringLiteral("snake"), *activeMode);
        updateGameStats(parts, QStringLiteral("snake"), *activeMode);
        parts.pause->setText(QWidget::tr("暂停"));
        setRunningVisuals(parts);
        timer->start();
        refresh();
        parts.surface->setFocus();
    };
    const auto pause = [=] {
        if (game->state() != GameState::Running && game->state() != GameState::Paused) return;
        game->togglePause();
        if (game->state() == GameState::Paused) timer->stop();
        else timer->start();
        parts.pause->setText(game->state() == GameState::Paused
                                 ? QWidget::tr("继续") : QWidget::tr("暂停"));
        refresh();
    };
    QObject::connect(timer, &QTimer::timeout, parts.page, [=] {
        const int previous = game->score();
        (void) game->advance();
        if (game->score() > previous) {
            ++*meals;
            if (lastEat->isValid() && lastEat->elapsed() <= 3000) ++*eatCombo;
            else *eatCombo = 1;
            lastEat->restart();
            if (*eatCombo > 1) game->addScore((*eatCombo - 1) * 5);
            if (*meals % 4 == 0 && game->state() == GameState::Running)
                game->spawnSpecialFood();
            const int level = game->score() / 50 + 1;
            timer->setInterval(std::max(70, 180 - (level - 1) * 12));
        }
        refresh();
    });
    QObject::connect(parts.start, &QPushButton::clicked, parts.page, start);
    QObject::connect(parts.restart, &QPushButton::clicked, parts.page, start);
    QObject::connect(parts.pause, &QPushButton::clicked, parts.page, pause);
    QObject::connect(mode, &QComboBox::currentIndexChanged, parts.page, start);
    parts.surface->setKeyHandler([=](int key) {
        if (key == Qt::Key_R) start();
        else if (key == Qt::Key_Space) pause();
        else game->setDirection(directionForKey(key, game->direction()));
    });
    parts.surface->setPaintHandler([=](QPainter& painter, const QRect& area) {
        const ui::CellGridGeometry geometry = ui::makeCellGrid(area, 16, 16, 8);
        const ui::GameVisualTheme theme = parts.surface->visualTheme();
        painter.fillRect(geometry.board, theme.board);
        painter.setPen(QPen(theme.grid, 1));
        for (int i = 1; i < 16; ++i) {
            painter.drawLine(geometry.board.left() + i * geometry.cell, geometry.board.top(),
                             geometry.board.left() + i * geometry.cell, geometry.board.bottom());
            painter.drawLine(geometry.board.left(), geometry.board.top() + i * geometry.cell,
                             geometry.board.right(), geometry.board.top() + i * geometry.cell);
        }
        painter.setPen(Qt::NoPen);
        painter.setBrush(theme.dark ? QColor("#5D6573") : QColor("#788392"));
        for (const QPoint& obstacle : game->obstacles())
            painter.drawRoundedRect(geometry.cellRect(obstacle.x(), obstacle.y(), 2), 3, 3);
        for (int index = game->body().size() - 1; index >= 0; --index) {
            const QPoint point = game->body().at(index);
            const QRect segment = geometry.cellRect(point.x(), point.y(), 2);
            painter.setBrush(index == 0 ? theme.accentStrong : theme.accent);
            painter.drawRoundedRect(segment, std::max(3, geometry.cell / 4),
                                    std::max(3, geometry.cell / 4));
        }
        if (!game->body().isEmpty()) {
            const QPoint head = game->body().first();
            const QRect headRect = geometry.cellRect(head.x(), head.y(), 2);
            painter.setBrush(Qt::white);
            const int eye = std::max(2, geometry.cell / 7);
            QPoint first = headRect.center() + QPoint(-eye, -eye);
            QPoint second = headRect.center() + QPoint(eye, -eye);
            if (game->direction() == Direction::Down) {
                first = headRect.center() + QPoint(-eye, eye);
                second = headRect.center() + QPoint(eye, eye);
            } else if (game->direction() == Direction::Left) {
                first = headRect.center() + QPoint(-eye, -eye);
                second = headRect.center() + QPoint(-eye, eye);
            } else if (game->direction() == Direction::Right) {
                first = headRect.center() + QPoint(eye, -eye);
                second = headRect.center() + QPoint(eye, eye);
            }
            painter.drawEllipse(first, eye, eye);
            painter.drawEllipse(second, eye, eye);
        }
        const QRect food = geometry.cellRect(game->food().x(), game->food().y(), 3);
        painter.setBrush(game->foodIsSpecial() ? QColor(QStringLiteral("#F3C846"))
                                                : QColor(QStringLiteral("#F05B62")));
        painter.drawEllipse(food);
        if (game->foodIsSpecial()) {
            painter.setPen(QPen(Qt::white, 2));
            painter.drawLine(food.center() + QPoint(-3, 0), food.center() + QPoint(3, 0));
            painter.drawLine(food.center() + QPoint(0, -3), food.center() + QPoint(0, 3));
        }
    });
    start();
    return parts.page;
}

QWidget* createTetrisWidget(QWidget* parent)
{
    GamePageParts parts = ui::createGamePage(
        QStringLiteral("tetris"),
        QWidget::tr("← → / A D  移动\n↑ / W  旋转\n↓ / S  软降\nEnter  硬降\nC  暂存方块\nSpace  暂停\nR  重新开始"), parent);
    auto game = std::make_shared<TetrisGame>();
    auto* timer = new QTimer(parts.page);
    auto finishedRecorded = std::make_shared<bool>(false);
    auto* speed = addGameOption(parts, QWidget::tr("速度"),
                                QStringLiteral("tetrisSpeedSelector"),
                                {{QWidget::tr("轻松"), 560},
                                 {QWidget::tr("标准"), 450},
                                 {QWidget::tr("快速"), 340}});
    parts.goal->setText(QWidget::tr("马拉松：每消除 10 行提升一级"));
    auto* holdButton = ui::createGameControlButton(
        QWidget::tr("暂存方块"), QStringLiteral("holdButton"),
        QWidget::tr("暂存或交换当前方块（C）"), parts.page);
    if (auto* controls = qobject_cast<QHBoxLayout*>(parts.restart->parentWidget()->layout()))
        controls->insertWidget(2, holdButton);
    timer->setInterval(450);

    const auto refresh = [=] {
        parts.score->setText(game->combo() > 0
            ? QWidget::tr("%1 分 · %2 行 · Lv.%3 · Combo %4")
                  .arg(game->score()).arg(game->lines()).arg(game->level()).arg(game->combo())
            : QWidget::tr("%1 分 · %2 行 · Lv.%3")
                  .arg(game->score()).arg(game->lines()).arg(game->level()));
        holdButton->setEnabled(game->state() == GameState::Running && game->canHold());
        parts.settings->updateHighScore(QStringLiteral("tetris"), game->score());
        parts.highScore->setText(QString::number(parts.settings->highScore(QStringLiteral("tetris"))));
        parts.surface->setProperty("score", game->score());
        if (game->state() == GameState::Paused) {
            parts.status->setText(QWidget::tr("已暂停"));
            parts.surface->showOverlay(QWidget::tr("已暂停"), QWidget::tr("按 Space 继续"));
        } else if (game->state() == GameState::GameOver) {
            parts.status->setText(QWidget::tr("游戏结束"));
            parts.surface->showOverlay(QWidget::tr("游戏结束"), QWidget::tr("得分 %1，按 R 再来一局").arg(game->score()));
            parts.pause->setEnabled(false);
            holdButton->setEnabled(false);
            timer->stop();
            if (!*finishedRecorded) {
                parts.settings->recordGameFinished(QStringLiteral("tetris"),
                                                    QStringLiteral("marathon"), false,
                                                    game->score());
                *finishedRecorded = true;
                updateGameStats(parts, QStringLiteral("tetris"), QStringLiteral("marathon"));
            }
        } else {
            parts.status->setText(game->combo() > 0
                ? QWidget::tr("连续消行 Combo %1").arg(game->combo())
                : QWidget::tr("游戏中"));
            parts.surface->clearOverlay();
        }
        parts.surface->update();
    };
    const auto start = [=] {
        game->start();
        *finishedRecorded = false;
        timer->setInterval(speed->currentData().toInt());
        parts.settings->recordGameStarted(QStringLiteral("tetris"),
                                          QStringLiteral("marathon"));
        updateGameStats(parts, QStringLiteral("tetris"), QStringLiteral("marathon"));
        parts.pause->setText(QWidget::tr("暂停"));
        setRunningVisuals(parts);
        holdButton->setEnabled(true);
        timer->start();
        refresh();
        parts.surface->setFocus();
    };
    const auto pause = [=] {
        if (game->state() != GameState::Running && game->state() != GameState::Paused) return;
        game->togglePause();
        if (game->state() == GameState::Paused) timer->stop();
        else timer->start();
        parts.pause->setText(game->state() == GameState::Paused
                                 ? QWidget::tr("继续") : QWidget::tr("暂停"));
        refresh();
    };
    const auto hold = [=] {
        if (game->hold()) {
            refresh();
            parts.surface->setFocus();
        }
    };
    QObject::connect(timer, &QTimer::timeout, parts.page, [=] {
        (void) game->step();
        timer->setInterval(std::max(80, speed->currentData().toInt()
                                           - (game->level() - 1) * 35));
        refresh();
    });
    QObject::connect(parts.start, &QPushButton::clicked, parts.page, start);
    QObject::connect(parts.restart, &QPushButton::clicked, parts.page, start);
    QObject::connect(parts.pause, &QPushButton::clicked, parts.page, pause);
    QObject::connect(holdButton, &QPushButton::clicked, parts.page, hold);
    QObject::connect(speed, &QComboBox::currentIndexChanged, parts.page, start);
    parts.surface->setKeyHandler([=](int key) {
        if (key == Qt::Key_R) start();
        else if (key == Qt::Key_Space) pause();
        else if (key == Qt::Key_C) hold();
        else if (key == Qt::Key_Left || key == Qt::Key_A) (void) game->move(-1);
        else if (key == Qt::Key_Right || key == Qt::Key_D) (void) game->move(1);
        else if (key == Qt::Key_Up || key == Qt::Key_W) (void) game->rotate();
        else if (key == Qt::Key_Down || key == Qt::Key_S) (void) game->step();
        else if (key == Qt::Key_Return || key == Qt::Key_Enter) game->hardDrop();
        refresh();
    });
    parts.surface->setPaintHandler([=](QPainter& painter, const QRect& area) {
        QRect boardArea = area;
        QRect previewArea;
        if (area.width() > 430) {
            boardArea.adjust(0, 0, -142, 0);
            previewArea = QRect(area.right() - 122, area.top() + 12,
                                110, area.height() - 24);
        }
        const ui::CellGridGeometry geometry = ui::makeCellGrid(
            boardArea, TetrisGame::Width, TetrisGame::Height, 5);
        const ui::GameVisualTheme theme = parts.surface->visualTheme();
        painter.setPen(QPen(theme.grid, 1));
        painter.setBrush(theme.board);
        painter.drawRoundedRect(geometry.board.adjusted(-4, -4, 4, 4), 8, 8);
        for (int row = 0; row < TetrisGame::Height; ++row) {
            for (int column = 0; column < TetrisGame::Width; ++column) {
                const QRect cell = geometry.cellRect(column, row, 1);
                const int piece = game->board()[row][column];
                if (piece) {
                    const QColor color = ui::tetrisPieceColor(piece - 1, theme.dark);
                    painter.fillRect(cell, color);
                    painter.setPen(QPen(color.lighter(135), 1));
                    painter.drawLine(cell.topLeft(), cell.topRight());
                    painter.drawLine(cell.topLeft(), cell.bottomLeft());
                    painter.setPen(QPen(theme.grid, 1));
                }
            }
        }
        const QColor currentColor = ui::tetrisPieceColor(
            static_cast<int>(game->currentPiece()), theme.dark);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(currentColor, 2, Qt::DashLine));
        for (const QPoint& point : game->ghostCells()) {
            if (point.y() >= 0) painter.drawRect(geometry.cellRect(point.x(), point.y(), 3));
        }
        painter.setPen(Qt::NoPen);
        painter.setBrush(currentColor);
        for (const QPoint& point : game->currentCells()) {
            if (point.y() >= 0) painter.drawRoundedRect(
                geometry.cellRect(point.x(), point.y(), 1), 3, 3);
        }

        if (!previewArea.isEmpty()) {
            painter.setPen(theme.muted);
            QFont labelFont = painter.font();
            labelFont.setPointSize(9);
            labelFont.setBold(true);
            painter.setFont(labelFont);
            const int previewCell = std::max(9, std::min(15, previewArea.width() / 6));
            const auto drawPreview = [&](TetrisGame::Piece piece, int top) {
                painter.setBrush(ui::tetrisPieceColor(static_cast<int>(piece), theme.dark));
                painter.setPen(Qt::NoPen);
                const int left = previewArea.center().x() - previewCell * 2;
                for (const QPoint& point : previewCells(piece)) {
                    painter.drawRoundedRect(QRect(left + point.x() * previewCell,
                                                   top + point.y() * previewCell,
                                                   previewCell - 2, previewCell - 2), 3, 3);
                }
            };

            int top = previewArea.top();
            painter.drawText(QRect(previewArea.left(), top, previewArea.width(), 18),
                             Qt::AlignHCenter | Qt::AlignTop, QWidget::tr("暂存"));
            top += 20;
            if (game->heldPiece()) drawPreview(*game->heldPiece(), top);
            else {
                painter.setPen(theme.grid);
                painter.drawText(QRect(previewArea.left(), top, previewArea.width(), 34),
                                 Qt::AlignCenter, QStringLiteral("—"));
            }
            top += previewCell * 3 + 12;
            painter.setPen(theme.muted);
            painter.drawText(QRect(previewArea.left(), top, previewArea.width(), 18),
                             Qt::AlignHCenter | Qt::AlignTop, QWidget::tr("接下来"));
            top += 22;
            const QVector<TetrisGame::Piece> preview = game->previewPieces(3);
            for (TetrisGame::Piece piece : preview) {
                drawPreview(piece, top);
                top += previewCell * 3 + 7;
            }
        }
    });
    start();
    return parts.page;
}

QWidget* createGomokuWidget(QWidget* parent)
{
    GamePageParts parts = ui::createGamePage(
        QStringLiteral("gomoku"),
        QWidget::tr("鼠标点击交叉点落子\n方向键 / WASD  移动光标\nEnter  落子\nSpace  暂停\nR  重新开始"), parent);
    auto game = std::make_shared<GomokuGame>();
    auto cursor = std::make_shared<QPoint>(GomokuGame::Size / 2, GomokuGame::Size / 2);
    auto paused = std::make_shared<bool>(false);
    auto winRecorded = std::make_shared<bool>(false);
    auto finishedRecorded = std::make_shared<bool>(false);
    auto aiThinking = std::make_shared<bool>(false);
    auto aiGeneration = std::make_shared<quint64>(0);
    auto activeMode = std::make_shared<QString>(QStringLiteral("local"));
    auto humanPlayer = std::make_shared<int>(1);
    auto requestAiMove = std::make_shared<std::function<void()>>();
    auto* mode = addGameOption(parts, QWidget::tr("模式"),
                               QStringLiteral("gomokuModeSelector"),
                               {{QWidget::tr("本地双人"), QStringLiteral("local")},
                                {QWidget::tr("简单 AI"), QStringLiteral("easy")},
                                {QWidget::tr("普通 AI"), QStringLiteral("normal")}});
    auto* side = addGameOption(parts, QWidget::tr("执子"),
                               QStringLiteral("gomokuSideSelector"),
                               {{QWidget::tr("黑方"), 1}, {QWidget::tr("白方"), 2}});
    auto* undo = ui::createGameControlButton(
        QWidget::tr("悔棋"), QStringLiteral("undoButton"),
        QWidget::tr("撤销最近一步；人机模式撤销双方各一步"), parts.page);
    if (auto* controls = qobject_cast<QHBoxLayout*>(parts.restart->parentWidget()->layout()))
        controls->insertWidget(2, undo);
    const auto updateGoal = [=] {
        const bool local = selectedMode(mode) == QStringLiteral("local");
        side->setEnabled(!local);
        parts.goal->setText(local ? QWidget::tr("目标：率先连成五子")
                                  : QWidget::tr("目标：战胜本地 AI"));
    };
    QObject::connect(mode, &QComboBox::currentIndexChanged, parts.page, updateGoal);
    updateGoal();

    const auto refresh = [=] {
        const bool terminal = game->state() == GameState::Won
            || game->state() == GameState::GameOver;
        if (game->state() == GameState::Won) {
            parts.status->setText(game->winner() == 1 ? QWidget::tr("黑方胜利")
                                                       : QWidget::tr("白方胜利"));
            parts.surface->showOverlay(parts.status->text(), QWidget::tr("按 R 再来一局"));
            parts.pause->setEnabled(false);
            if (!*winRecorded && (*activeMode == QStringLiteral("local")
                                  || game->winner() == *humanPlayer)) {
                parts.settings->incrementWinCount(QStringLiteral("gomoku"));
                *winRecorded = true;
                }
        } else if (game->state() == GameState::GameOver) {
            parts.status->setText(QWidget::tr("和棋"));
            parts.surface->showOverlay(QWidget::tr("和棋"), QWidget::tr("按 R 再来一局"));
            parts.pause->setEnabled(false);
        } else if (*paused) {
            parts.status->setText(QWidget::tr("已暂停"));
            parts.surface->showOverlay(QWidget::tr("已暂停"), QWidget::tr("按 Space 继续"));
        } else if (*aiThinking) {
            parts.status->setText(QWidget::tr("电脑思考中…"));
            parts.surface->clearOverlay();
        } else {
            parts.status->setText(QWidget::tr("对局中"));
            parts.surface->clearOverlay();
        }
        if (terminal && !*finishedRecorded) {
            const bool won = game->state() == GameState::Won
                && (*activeMode == QStringLiteral("local")
                    || game->winner() == *humanPlayer);
            parts.settings->recordGameFinished(QStringLiteral("gomoku"),
                                                *activeMode, won, won ? 1 : 0);
            *finishedRecorded = true;
            updateGameStats(parts, QStringLiteral("gomoku"), *activeMode);
        }
        parts.score->setText(game->currentPlayer() == 1 ? QWidget::tr("黑方")
                                                         : QWidget::tr("白方"));
        parts.highScore->setText(QString::number(
            parts.settings->winCount(QStringLiteral("gomoku"))));
        const int requiredMoves = *activeMode == QStringLiteral("local") ? 1 : 2;
        undo->setEnabled(!terminal && !*paused && !*aiThinking
                         && game->moveCount() >= requiredMoves);
        parts.surface->setProperty("currentPlayer", game->currentPlayer());
        parts.surface->update();
    };

    *requestAiMove = [=] {
        if (*activeMode == QStringLiteral("local") || *paused || *aiThinking
            || game->state() != GameState::Running
            || game->currentPlayer() == *humanPlayer) return;
        *aiThinking = true;
        const quint64 generation = ++*aiGeneration;
        const GomokuGame::Board board = game->board();
        const int player = game->currentPlayer();
        const GomokuAiLevel level = *activeMode == QStringLiteral("normal")
            ? GomokuAiLevel::Normal : GomokuAiLevel::Easy;
        refresh();
        auto* watcher = new QFutureWatcher<QPoint>(parts.page);
        QObject::connect(watcher, &QFutureWatcher<QPoint>::finished, parts.page,
                         [=] {
            const QPoint move = watcher->result();
            watcher->deleteLater();
            if (generation != *aiGeneration || *paused
                || game->state() != GameState::Running
                || game->currentPlayer() != player) return;
            *aiThinking = false;
            if (move.x() >= 0) (void) game->place(move.x(), move.y());
            refresh();
        });
        watcher->setFuture(QtConcurrent::run([board, player, level] {
            return chooseGomokuMove(board, player, level);
        }));
    };

    const auto start = [=] {
        ++*aiGeneration;
        *activeMode = selectedMode(mode);
        *humanPlayer = side->currentData().toInt();
        game->start();
        *cursor = QPoint(GomokuGame::Size / 2, GomokuGame::Size / 2);
        *paused = false;
        *aiThinking = false;
        *winRecorded = false;
        *finishedRecorded = false;
        parts.settings->recordGameStarted(QStringLiteral("gomoku"), *activeMode);
        updateGameStats(parts, QStringLiteral("gomoku"), *activeMode);
        parts.pause->setText(QWidget::tr("暂停"));
        setRunningVisuals(parts);
        refresh();
        parts.surface->setFocus();
        (*requestAiMove)();
    };
    const auto pause = [=] {
        if (game->state() != GameState::Running) return;
        *paused = !*paused;
        if (*paused) {
            ++*aiGeneration;
            *aiThinking = false;
        }
        parts.pause->setText(*paused ? QWidget::tr("继续") : QWidget::tr("暂停"));
        refresh();
        if (!*paused) (*requestAiMove)();
    };
    const auto place = [=](int x, int y) {
        const bool humanTurn = *activeMode == QStringLiteral("local")
            || game->currentPlayer() == *humanPlayer;
        if (!*paused && !*aiThinking && humanTurn && game->place(x, y)) {
            refresh();
            (*requestAiMove)();
        }
    };

    QObject::connect(parts.start, &QPushButton::clicked, parts.page, start);
    QObject::connect(parts.restart, &QPushButton::clicked, parts.page, start);
    QObject::connect(parts.pause, &QPushButton::clicked, parts.page, pause);
    QObject::connect(mode, &QComboBox::currentIndexChanged, parts.page, start);
    QObject::connect(side, &QComboBox::currentIndexChanged, parts.page, [=] {
        if (side->isEnabled()) start();
    });
    QObject::connect(undo, &QPushButton::clicked, parts.page, [=] {
        if (!undo->isEnabled()) return;
        ++*aiGeneration;
        *aiThinking = false;
        const int count = *activeMode == QStringLiteral("local") ? 1 : 2;
        if (game->undo(count)) {
            *paused = false;
            parts.pause->setText(QWidget::tr("暂停"));
            refresh();
            parts.surface->setFocus();
        }
    });
    parts.surface->setKeyHandler([=](int key) {
        if (key == Qt::Key_R) start();
        else if (key == Qt::Key_Space) pause();
        else if (key == Qt::Key_Return || key == Qt::Key_Enter) place(cursor->x(), cursor->y());
        else if (key == Qt::Key_Left || key == Qt::Key_A || key == Qt::Key_Right
                 || key == Qt::Key_D || key == Qt::Key_Up || key == Qt::Key_W
                 || key == Qt::Key_Down || key == Qt::Key_S) {
            const Direction direction = directionForKey(key, Direction::Left);
            if (direction == Direction::Left) cursor->rx() = std::max(0, cursor->x() - 1);
            if (direction == Direction::Right) cursor->rx() = std::min(GomokuGame::Size - 1, cursor->x() + 1);
            if (direction == Direction::Up) cursor->ry() = std::max(0, cursor->y() - 1);
            if (direction == Direction::Down) cursor->ry() = std::min(GomokuGame::Size - 1, cursor->y() + 1);
            parts.surface->update();
        }
    });
    parts.surface->setClickHandler([=](const QPoint& position) {
        const QRect content = parts.surface->rect().adjusted(16, 16, -16, -16);
        const QPoint cell = ui::makeIntersectionGrid(content, GomokuGame::Size, 16)
                                .hitTest(position);
        if (cell.x() >= 0) place(cell.x(), cell.y());
    });
    parts.surface->setPaintHandler([=](QPainter& painter, const QRect& area) {
        const ui::IntersectionGridGeometry geometry = ui::makeIntersectionGrid(
            area, GomokuGame::Size, 16);
        const ui::GameVisualTheme theme = parts.surface->visualTheme();
        const QColor wood = theme.dark ? QColor(QStringLiteral("#6C4D35"))
                                        : QColor(QStringLiteral("#E6BE7A"));
        painter.setPen(Qt::NoPen);
        painter.setBrush(wood);
        painter.drawRoundedRect(geometry.board.adjusted(-geometry.spacing / 2,
                                                         -geometry.spacing / 2,
                                                          geometry.spacing / 2,
                                                          geometry.spacing / 2), 12, 12);
        painter.setPen(QPen(theme.dark ? QColor("#C7A978") : QColor("#70512F"), 1));
        for (int i = 0; i < GomokuGame::Size; ++i) {
            painter.drawLine(geometry.point(i, 0), geometry.point(i, GomokuGame::Size - 1));
            painter.drawLine(geometry.point(0, i), geometry.point(GomokuGame::Size - 1, i));
        }
        painter.setBrush(theme.dark ? QColor("#D7B57D") : QColor("#6A4728"));
        painter.setPen(Qt::NoPen);
        for (const QPoint star : {QPoint(3, 3), QPoint(11, 3), QPoint(7, 7),
                                  QPoint(3, 11), QPoint(11, 11)})
            painter.drawEllipse(geometry.point(star.x(), star.y()), 3, 3);

        const int radius = std::max(5, geometry.spacing / 2 - 2);
        for (int y = 0; y < GomokuGame::Size; ++y) {
            for (int x = 0; x < GomokuGame::Size; ++x) {
                const int stone = game->at(x, y);
                if (!stone) continue;
                const QPoint center = geometry.point(x, y);
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(0, 0, 0, 45));
                painter.drawEllipse(center + QPoint(2, 3), radius, radius);
                QRadialGradient gradient(center - QPoint(radius / 3, radius / 3), radius * 2);
                if (stone == 1) {
                    gradient.setColorAt(0, QColor("#555861"));
                    gradient.setColorAt(1, QColor("#15171C"));
                } else {
                    gradient.setColorAt(0, QColor("#FFFFFF"));
                    gradient.setColorAt(1, QColor("#D9DDE5"));
                }
                painter.setBrush(gradient);
                painter.setPen(QPen(stone == 1 ? QColor("#0A0B0D") : QColor("#AEB4C0"), 1));
                painter.drawEllipse(center, radius, radius);
            }
        }

        if (game->lastMove().x() >= 0) {
            painter.setPen(QPen(QColor("#E45A55"), 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(geometry.point(game->lastMove().x(), game->lastMove().y()),
                                std::max(3, radius / 3), std::max(3, radius / 3));
        }
        const QVector<QPoint> winning = game->winningLine();
        if (winning.size() >= 5) {
            painter.setPen(QPen(QColor("#F4C653"), 4, Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(geometry.point(winning.first().x(), winning.first().y()),
                             geometry.point(winning.last().x(), winning.last().y()));
        }
        painter.setPen(QPen(theme.accentStrong, 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(geometry.point(cursor->x(), cursor->y()),
                            radius + 3, radius + 3);
    });
    start();
    return parts.page;
}

}  // namespace mtb::games
