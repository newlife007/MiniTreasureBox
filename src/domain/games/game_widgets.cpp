#include "domain/games/game_widgets.h"

#include "domain/games/game_logic.h"
#include "domain/games/game_settings.h"

#include <QApplication>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <memory>

namespace mtb::games {
namespace {

class GameSurface final : public QWidget {
public:
    explicit GameSurface(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("gameSurface"));
        setFocusPolicy(Qt::StrongFocus);
        setAccessibleName(QWidget::tr("游戏区域"));
        setAccessibleDescription(QWidget::tr("使用键盘操作游戏，空格暂停或继续，R 重新开始"));
        setToolTip(QWidget::tr("方向键或 WASD 操作，空格暂停或继续，R 重新开始"));
        setMinimumSize(280, 280);
    }

    std::function<void(QPainter&, const QRect&)> paint;
    std::function<void(int)> key;
    std::function<void(const QPoint&)> click;

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), palette().color(QPalette::Base));
        if (paint) {
            paint(painter, rect().adjusted(4, 4, -4, -4));
        }
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        if (key) {
            key(event->key());
            event->accept();
            return;
        }
        QWidget::keyPressEvent(event);
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (click) {
            click(event->position().toPoint());
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }
};

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

struct PageParts {
    QWidget* page;
    QVBoxLayout* layout;
    GameSurface* surface;
    QLabel* status;
    QLabel* score;
    QLabel* highScore;
    QPushButton* start;
    QPushButton* pause;
    QPushButton* restart;
    QCheckBox* sound;
    std::shared_ptr<GameSettings> settings;
};

QPushButton* controlButton(const QString& text, const QString& name, const QString& tip,
                           QWidget* parent)
{
    auto* button = new QPushButton(text, parent);
    button->setObjectName(name);
    button->setToolTip(tip);
    button->setAccessibleName(text);
    button->setAccessibleDescription(tip);
    button->setFocusPolicy(Qt::StrongFocus);
    return button;
}

PageParts makePage(const QString& id, QWidget* parent)
{
    auto* page = new QWidget(parent);
    page->setObjectName(QStringLiteral("game_%1").arg(id));
    auto* storage = new QSettings(page);
    auto settings = std::make_shared<GameSettings>(*storage);

    auto* status = new QLabel(QWidget::tr("准备开始"), page);
    status->setObjectName(QStringLiteral("gameStatus"));
    status->setAccessibleName(QWidget::tr("游戏状态"));
    auto* score = new QLabel(QWidget::tr("得分：0"), page);
    score->setObjectName(QStringLiteral("scoreLabel"));
    score->setAccessibleName(QWidget::tr("当前得分"));
    auto* highScore = new QLabel(QWidget::tr("最高分：%1").arg(settings->highScore(id)), page);
    highScore->setObjectName(QStringLiteral("highScoreLabel"));
    highScore->setAccessibleName(QWidget::tr("本地最高分"));

    auto* start = controlButton(QWidget::tr("开始"), QStringLiteral("startButton"), QWidget::tr("开始新游戏"), page);
    auto* pause = controlButton(QWidget::tr("暂停"), QStringLiteral("pauseButton"), QWidget::tr("暂停或继续游戏（空格）"), page);
    auto* restart = controlButton(QWidget::tr("重新开始"), QStringLiteral("restartButton"), QWidget::tr("重新开始游戏（R）"), page);
    auto* sound = new QCheckBox(QWidget::tr("音效"), page);
    sound->setObjectName(QStringLiteral("soundToggle"));
    sound->setToolTip(QWidget::tr("启用或关闭本地游戏提示音"));
    sound->setAccessibleName(QWidget::tr("音效开关"));
    sound->setChecked(settings->soundEnabled());
    QObject::connect(sound, &QCheckBox::toggled, page,
                     [settings](bool enabled) { settings->setSoundEnabled(enabled); });

    auto* information = new QHBoxLayout;
    information->addWidget(status, 1);
    information->addWidget(score);
    information->addWidget(highScore);
    auto* controls = new QHBoxLayout;
    controls->addWidget(start);
    controls->addWidget(pause);
    controls->addWidget(restart);
    controls->addStretch();
    controls->addWidget(sound);
    auto* surface = new GameSurface(page);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(information);
    layout->addLayout(controls);
    layout->addWidget(surface, 1);
    return {page, layout, surface, status, score, highScore, start, pause, restart, sound,
            std::move(settings)};
}

QColor blended(const QColor& first, const QColor& second, qreal amount)
{
    return QColor::fromRgbF(first.redF() * (1.0 - amount) + second.redF() * amount,
                            first.greenF() * (1.0 - amount) + second.greenF() * amount,
                            first.blueF() * (1.0 - amount) + second.blueF() * amount);
}

void beepIfEnabled(const std::shared_ptr<GameSettings>& settings)
{
    if (settings->soundEnabled()) {
        QApplication::beep();
    }
}

Direction directionForKey(int key, Direction fallback)
{
    if (key == Qt::Key_Left || key == Qt::Key_A) return Direction::Left;
    if (key == Qt::Key_Right || key == Qt::Key_D) return Direction::Right;
    if (key == Qt::Key_Up || key == Qt::Key_W) return Direction::Up;
    if (key == Qt::Key_Down || key == Qt::Key_S) return Direction::Down;
    return fallback;
}

}  // namespace

QWidget* createGuessNumberWidget(QWidget* parent)
{
    PageParts parts = makePage(QStringLiteral("guess-number"), parent);
    auto game = std::make_shared<GuessNumberGame>();
    auto* input = new GuessInput(parts.page);
    input->setObjectName(QStringLiteral("guessInput"));
    input->setPlaceholderText(QWidget::tr("输入 1 到 100 的整数"));
    input->setAccessibleName(QWidget::tr("猜测数字"));
    auto* submit = controlButton(QWidget::tr("猜一猜"), QStringLiteral("guessButton"), QWidget::tr("提交猜测（Enter）"), parts.page);
    auto* entry = new QHBoxLayout;
    entry->addWidget(input, 1);
    entry->addWidget(submit);
    parts.layout->insertLayout(2, entry);

    const auto start = [=] {
        game->start(QRandomGenerator::global()->bounded(1, 101));
        input->clear();
        parts.status->setText(QWidget::tr("请输入 1 到 100 之间的数字"));
        parts.score->setText(QWidget::tr("尝试次数：0"));
        parts.pause->setText(QWidget::tr("暂停"));
        input->setEnabled(true);
        submit->setEnabled(true);
        parts.surface->setProperty("gameState", QStringLiteral("running"));
        input->setFocus();
        parts.surface->update();
    };
    const auto pause = [=] {
        game->togglePause();
        const bool paused = game->state() == GameState::Paused;
        if (game->state() != GameState::Running && !paused) return;
        parts.pause->setText(paused ? QWidget::tr("继续") : QWidget::tr("暂停"));
        parts.status->setText(paused ? QWidget::tr("已暂停")
                                     : QWidget::tr("请输入 1 到 100 之间的数字"));
        input->setEnabled(!paused);
        submit->setEnabled(!paused);
        parts.surface->setProperty("gameState", paused ? QStringLiteral("paused")
                                                       : QStringLiteral("running"));
        if (!paused) input->setFocus();
    };
    const auto submitGuess = [=] {
        const GuessResult result = game->guess(input->text());
        parts.status->setText(result.message);
        parts.score->setText(QWidget::tr("尝试次数：%1").arg(game->attempts()));
        if (result.outcome == GuessResult::Outcome::Correct) {
            const int score = std::max(1, 101 - game->attempts());
            parts.settings->updateHighScore(QStringLiteral("guess-number"), score);
            parts.highScore->setText(QWidget::tr("最高分：%1").arg(parts.settings->highScore(QStringLiteral("guess-number"))));
            parts.surface->setProperty("gameState", QStringLiteral("won"));
            input->setEnabled(false);
            submit->setEnabled(false);
            beepIfEnabled(parts.settings);
        }
        input->selectAll();
    };
    QObject::connect(parts.start, &QPushButton::clicked, parts.page, start);
    QObject::connect(parts.restart, &QPushButton::clicked, parts.page, start);
    QObject::connect(parts.pause, &QPushButton::clicked, parts.page, pause);
    QObject::connect(submit, &QPushButton::clicked, parts.page, submitGuess);
    QObject::connect(input, &QLineEdit::returnPressed, parts.page, submitGuess);
    input->gameKey = [=](int key) {
        if (key == Qt::Key_R) start();
        else if (key == Qt::Key_Space) pause();
    };
    parts.surface->key = input->gameKey;
    parts.surface->paint = [=](QPainter& painter, const QRect& area) {
        painter.setPen(parts.surface->palette().color(QPalette::Text));
        QFont font = painter.font();
        font.setPointSize(28);
        font.setBold(true);
        painter.setFont(font);
        painter.drawText(area, Qt::AlignCenter, QStringLiteral("1 - 100"));
    };
    return parts.page;
}

QWidget* create2048Widget(QWidget* parent)
{
    PageParts parts = makePage(QStringLiteral("game-2048"), parent);
    auto game = std::make_shared<Game2048>();
    auto paused = std::make_shared<bool>(false);
    const auto refresh = [=] {
        parts.score->setText(QWidget::tr("得分：%1").arg(game->score()));
        parts.settings->updateHighScore(QStringLiteral("game-2048"), game->score());
        parts.highScore->setText(QWidget::tr("最高分：%1").arg(parts.settings->highScore(QStringLiteral("game-2048"))));
        parts.surface->setProperty("score", game->score());
        parts.surface->setProperty("tileCount", 16 - game->emptyCells().size());
        QString boardState;
        for (const auto& row : game->board()) {
            for (int value : row) boardState += QString::number(value) + QLatin1Char(',');
        }
        parts.surface->setProperty("boardState", boardState);
        if (game->hasWon()) parts.status->setText(QWidget::tr("已合成 2048，胜利！"));
        else if (game->isGameOver()) parts.status->setText(QWidget::tr("游戏结束"));
        else parts.status->setText(*paused ? QWidget::tr("已暂停") : QWidget::tr("游戏中"));
        parts.surface->update();
    };
    const auto start = [=] {
        game->start();
        *paused = false;
        parts.pause->setText(QWidget::tr("暂停"));
        parts.surface->setProperty("gameState", QStringLiteral("running"));
        refresh();
        parts.surface->setFocus();
    };
    const auto pause = [=] {
        *paused = !*paused;
        parts.pause->setText(*paused ? QWidget::tr("继续") : QWidget::tr("暂停"));
        refresh();
    };
    const auto move = [=](Direction direction) {
        if (*paused || game->isGameOver()) return;
        const QVector<int> beforeEmpty = game->emptyCells();
        Q_UNUSED(beforeEmpty);
        if (game->move(direction)) {
            beepIfEnabled(parts.settings);
            refresh();
        }
    };
    QObject::connect(parts.start, &QPushButton::clicked, parts.page, start);
    QObject::connect(parts.restart, &QPushButton::clicked, parts.page, start);
    QObject::connect(parts.pause, &QPushButton::clicked, parts.page, pause);
    parts.surface->key = [=](int key) {
        if (key == Qt::Key_R) start();
        else if (key == Qt::Key_Space) pause();
        else if (key == Qt::Key_Left || key == Qt::Key_A || key == Qt::Key_Right
                 || key == Qt::Key_D || key == Qt::Key_Up || key == Qt::Key_W
                 || key == Qt::Key_Down || key == Qt::Key_S) {
            move(directionForKey(key, Direction::Left));
        }
    };
    parts.surface->paint = [=](QPainter& painter, const QRect& area) {
        const int side = std::min(area.width(), area.height());
        const QRect board(area.center().x() - side / 2, area.center().y() - side / 2, side, side);
        const int cell = side / 4;
        const QPalette palette = parts.surface->palette();
        painter.setPen(QPen(palette.color(QPalette::Mid), 2));
        for (int row = 0; row < 4; ++row) for (int column = 0; column < 4; ++column) {
            QRect tile(board.left() + column * cell, board.top() + row * cell, cell, cell);
            const int value = game->board()[row][column];
            const QColor fill = value == 0 ? palette.color(QPalette::AlternateBase)
                                           : blended(palette.color(QPalette::Highlight), palette.color(QPalette::Base),
                                                     std::min(0.75, value / 4096.0));
            painter.fillRect(tile.adjusted(2, 2, -2, -2), fill);
            painter.setPen(value == 0 ? palette.color(QPalette::Text) : palette.color(QPalette::HighlightedText));
            if (value) painter.drawText(tile, Qt::AlignCenter, QString::number(value));
            painter.setPen(QPen(palette.color(QPalette::Mid), 2));
        }
    };
    start();
    return parts.page;
}

QWidget* createSnakeWidget(QWidget* parent)
{
    PageParts parts = makePage(QStringLiteral("snake"), parent);
    auto game = std::make_shared<SnakeGame>();
    auto* timer = new QTimer(parts.page);
    timer->setInterval(180);
    const auto refresh = [=] {
        parts.score->setText(QWidget::tr("得分：%1").arg(game->score()));
        parts.settings->updateHighScore(QStringLiteral("snake"), game->score());
        parts.highScore->setText(QWidget::tr("最高分：%1").arg(parts.settings->highScore(QStringLiteral("snake"))));
        parts.surface->setProperty("score", game->score());
        if (game->state() == GameState::Paused) parts.status->setText(QWidget::tr("已暂停"));
        else if (game->state() == GameState::GameOver) parts.status->setText(QWidget::tr("游戏结束"));
        else parts.status->setText(QWidget::tr("游戏中"));
        parts.surface->update();
    };
    const auto start = [=] {
        game->start();
        parts.pause->setText(QWidget::tr("暂停"));
        timer->start();
        refresh();
        parts.surface->setFocus();
    };
    const auto pause = [=] {
        game->togglePause();
        parts.pause->setText(game->state() == GameState::Paused ? QWidget::tr("继续") : QWidget::tr("暂停"));
        refresh();
    };
    QObject::connect(timer, &QTimer::timeout, parts.page, [=] {
        const int previous = game->score();
        (void)game->advance();
        if (game->score() > previous) beepIfEnabled(parts.settings);
        if (game->state() == GameState::GameOver) timer->stop();
        refresh();
    });
    QObject::connect(parts.start, &QPushButton::clicked, parts.page, start);
    QObject::connect(parts.restart, &QPushButton::clicked, parts.page, start);
    QObject::connect(parts.pause, &QPushButton::clicked, parts.page, pause);
    parts.surface->key = [=](int key) {
        if (key == Qt::Key_R) start();
        else if (key == Qt::Key_Space) pause();
        else game->setDirection(directionForKey(key, game->direction()));
    };
    parts.surface->paint = [=](QPainter& painter, const QRect& area) {
        const int side = std::min(area.width(), area.height());
        const int cell = side / 16;
        const QRect board(area.center().x() - cell * 8, area.center().y() - cell * 8, cell * 16, cell * 16);
        const QPalette palette = parts.surface->palette();
        painter.fillRect(board, palette.color(QPalette::AlternateBase));
        painter.setPen(QPen(palette.color(QPalette::Mid), 1));
        for (int i = 0; i <= 16; ++i) {
            painter.drawLine(board.left() + i * cell, board.top(), board.left() + i * cell, board.bottom());
            painter.drawLine(board.left(), board.top() + i * cell, board.right(), board.top() + i * cell);
        }
        painter.setPen(Qt::NoPen);
        painter.setBrush(palette.color(QPalette::Highlight));
        for (const QPoint& point : game->body()) painter.drawRect(board.left() + point.x() * cell + 1, board.top() + point.y() * cell + 1, cell - 1, cell - 1);
        painter.setBrush(palette.color(QPalette::BrightText));
        painter.drawEllipse(QRect(board.left() + game->food().x() * cell + 2, board.top() + game->food().y() * cell + 2, cell - 4, cell - 4));
    };
    start();
    return parts.page;
}

QWidget* createTetrisWidget(QWidget* parent)
{
    PageParts parts = makePage(QStringLiteral("tetris"), parent);
    auto game = std::make_shared<TetrisGame>();
    auto* timer = new QTimer(parts.page);
    timer->setInterval(450);
    const auto refresh = [=] {
        parts.score->setText(QWidget::tr("得分：%1  消除：%2").arg(game->score()).arg(game->lines()));
        parts.settings->updateHighScore(QStringLiteral("tetris"), game->score());
        parts.highScore->setText(QWidget::tr("最高分：%1").arg(parts.settings->highScore(QStringLiteral("tetris"))));
        parts.surface->setProperty("score", game->score());
        if (game->state() == GameState::Paused) parts.status->setText(QWidget::tr("已暂停"));
        else if (game->state() == GameState::GameOver) parts.status->setText(QWidget::tr("游戏结束"));
        else parts.status->setText(QWidget::tr("游戏中"));
        parts.surface->update();
    };
    const auto start = [=] {
        game->start();
        parts.pause->setText(QWidget::tr("暂停"));
        timer->start();
        refresh();
        parts.surface->setFocus();
    };
    const auto pause = [=] {
        game->togglePause();
        parts.pause->setText(game->state() == GameState::Paused ? QWidget::tr("继续") : QWidget::tr("暂停"));
        refresh();
    };
    QObject::connect(timer, &QTimer::timeout, parts.page, [=] {
        const int lines = game->lines();
        (void)game->step();
        if (game->lines() > lines) beepIfEnabled(parts.settings);
        if (game->state() == GameState::GameOver) timer->stop();
        refresh();
    });
    QObject::connect(parts.start, &QPushButton::clicked, parts.page, start);
    QObject::connect(parts.restart, &QPushButton::clicked, parts.page, start);
    QObject::connect(parts.pause, &QPushButton::clicked, parts.page, pause);
    parts.surface->key = [=](int key) {
        if (key == Qt::Key_R) start();
        else if (key == Qt::Key_Space) pause();
        else if (key == Qt::Key_Left || key == Qt::Key_A) (void)game->move(-1);
        else if (key == Qt::Key_Right || key == Qt::Key_D) (void)game->move(1);
        else if (key == Qt::Key_Up || key == Qt::Key_W) (void)game->rotate();
        else if (key == Qt::Key_Down || key == Qt::Key_S) (void)game->step();
        else if (key == Qt::Key_Return || key == Qt::Key_Enter) game->hardDrop();
        refresh();
    };
    parts.surface->paint = [=](QPainter& painter, const QRect& area) {
        const int cell = std::max(4, std::min(area.width() / TetrisGame::Width, area.height() / TetrisGame::Height));
        const QRect board(area.center().x() - cell * TetrisGame::Width / 2, area.center().y() - cell * TetrisGame::Height / 2,
                          cell * TetrisGame::Width, cell * TetrisGame::Height);
        const QPalette palette = parts.surface->palette();
        painter.fillRect(board, palette.color(QPalette::AlternateBase));
        painter.setPen(QPen(palette.color(QPalette::Mid), 1));
        for (int row = 0; row < TetrisGame::Height; ++row) for (int column = 0; column < TetrisGame::Width; ++column) {
            if (game->board()[row][column]) painter.fillRect(board.left() + column * cell + 1, board.top() + row * cell + 1, cell - 1, cell - 1, palette.color(QPalette::Highlight));
            painter.drawRect(board.left() + column * cell, board.top() + row * cell, cell, cell);
        }
        painter.setBrush(palette.color(QPalette::Highlight));
        for (const QPoint& point : game->currentCells()) if (point.y() >= 0) painter.drawRect(board.left() + point.x() * cell + 1, board.top() + point.y() * cell + 1, cell - 2, cell - 2);
    };
    start();
    return parts.page;
}

QWidget* createGomokuWidget(QWidget* parent)
{
    PageParts parts = makePage(QStringLiteral("gomoku"), parent);
    auto game = std::make_shared<GomokuGame>();
    auto cursor = std::make_shared<QPoint>(GomokuGame::Size / 2, GomokuGame::Size / 2);
    auto paused = std::make_shared<bool>(false);
    auto winRecorded = std::make_shared<bool>(false);
    parts.score->setText(QWidget::tr("当前：黑方"));
    const auto refresh = [=] {
        if (game->state() == GameState::Won) {
            parts.status->setText(game->winner() == 1 ? QWidget::tr("黑方胜利") : QWidget::tr("白方胜利"));
            if (!*winRecorded) {
                parts.settings->incrementWinCount(QStringLiteral("gomoku"));
                *winRecorded = true;
                beepIfEnabled(parts.settings);
            }
        } else {
            parts.status->setText(*paused ? QWidget::tr("已暂停") : QWidget::tr("对局中"));
        }
        parts.score->setText(game->currentPlayer() == 1 ? QWidget::tr("当前：黑方") : QWidget::tr("当前：白方"));
        parts.highScore->setText(QWidget::tr("本地胜局：%1").arg(parts.settings->winCount(QStringLiteral("gomoku"))));
        parts.surface->setProperty("currentPlayer", game->currentPlayer());
        parts.surface->update();
    };
    const auto start = [=] {
        game->start();
        *cursor = QPoint(GomokuGame::Size / 2, GomokuGame::Size / 2);
        *paused = false;
        *winRecorded = false;
        parts.pause->setText(QWidget::tr("暂停"));
        refresh();
        parts.surface->setFocus();
    };
    const auto pause = [=] {
        *paused = !*paused;
        parts.pause->setText(*paused ? QWidget::tr("继续") : QWidget::tr("暂停"));
        refresh();
    };
    const auto place = [=](int x, int y) {
        if (!*paused && game->place(x, y)) refresh();
    };
    QObject::connect(parts.start, &QPushButton::clicked, parts.page, start);
    QObject::connect(parts.restart, &QPushButton::clicked, parts.page, start);
    QObject::connect(parts.pause, &QPushButton::clicked, parts.page, pause);
    parts.surface->key = [=](int key) {
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
    };
    parts.surface->click = [=](const QPoint& position) {
        const int side = std::min(parts.surface->width(), parts.surface->height()) - 24;
        const int cell = side / (GomokuGame::Size - 1);
        const QPoint topLeft((parts.surface->width() - cell * (GomokuGame::Size - 1)) / 2,
                             (parts.surface->height() - cell * (GomokuGame::Size - 1)) / 2);
        const int x = qRound(static_cast<double>(position.x() - topLeft.x()) / cell);
        const int y = qRound(static_cast<double>(position.y() - topLeft.y()) / cell);
        place(x, y);
    };
    parts.surface->paint = [=](QPainter& painter, const QRect& area) {
        const int side = std::min(area.width(), area.height()) - 20;
        const int cell = side / (GomokuGame::Size - 1);
        const QPoint topLeft(area.center().x() - cell * (GomokuGame::Size - 1) / 2,
                             area.center().y() - cell * (GomokuGame::Size - 1) / 2);
        const QPalette palette = parts.surface->palette();
        painter.setPen(QPen(palette.color(QPalette::Mid), 1));
        for (int i = 0; i < GomokuGame::Size; ++i) {
            painter.drawLine(topLeft + QPoint(i * cell, 0), topLeft + QPoint(i * cell, cell * (GomokuGame::Size - 1)));
            painter.drawLine(topLeft + QPoint(0, i * cell), topLeft + QPoint(cell * (GomokuGame::Size - 1), i * cell));
        }
        for (int y = 0; y < GomokuGame::Size; ++y) for (int x = 0; x < GomokuGame::Size; ++x) {
            const int stone = game->at(x, y);
            if (!stone) continue;
            painter.setPen(QPen(palette.color(QPalette::Text), 1));
            painter.setBrush(stone == 1 ? palette.color(QPalette::Text) : palette.color(QPalette::Base));
            painter.drawEllipse(QPoint(topLeft.x() + x * cell, topLeft.y() + y * cell), cell / 2 - 1, cell / 2 - 1);
        }
        painter.setPen(QPen(palette.color(QPalette::Highlight), 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(topLeft.x() + cursor->x() * cell - cell / 2, topLeft.y() + cursor->y() * cell - cell / 2, cell, cell);
    };
    start();
    return parts.page;
}

}  // namespace mtb::games
