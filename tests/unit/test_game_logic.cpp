#include "domain/games/game_logic.h"
#include "domain/games/game_settings.h"

#include <QSet>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

using namespace mtb::games;

class GameLogicTest final : public QObject {
    Q_OBJECT

private slots:
    void guessNumberValidatesAndWins();
    void game2048MergesOnceAndDetectsEnd();
    void snakeGrowsPausesAndRejectsReverse();
    void tetrisMovesDropsClearsAndEnds();
    void gomokuFindsFiveInEveryDirection();
    void settingsPersistScoresAndSound();
};

void GameLogicTest::guessNumberValidatesAndWins()
{
    GuessNumberGame game;
    game.start(42);
    QCOMPARE(game.guess(QStringLiteral("hello")).outcome, GuessResult::Outcome::Invalid);
    QCOMPARE(game.guess(QStringLiteral("101")).outcome, GuessResult::Outcome::Invalid);
    QCOMPARE(game.attempts(), 0);
    QCOMPARE(game.guess(QStringLiteral("20")).outcome, GuessResult::Outcome::TooLow);
    game.togglePause();
    QCOMPARE(game.state(), GameState::Paused);
    QCOMPARE(game.guess(QStringLiteral("42")).outcome, GuessResult::Outcome::Invalid);
    QCOMPARE(game.attempts(), 1);
    game.togglePause();
    QCOMPARE(game.guess(QStringLiteral("70")).outcome, GuessResult::Outcome::TooHigh);
    QCOMPARE(game.guess(QStringLiteral("42")).outcome, GuessResult::Outcome::Correct);
    QCOMPARE(game.attempts(), 3);
    QCOMPARE(game.state(), GameState::Won);
}

void GameLogicTest::game2048MergesOnceAndDetectsEnd()
{
    Game2048 game;
    game.start(0, 15, 2, 4);
    QCOMPARE(game.board()[0][0], 2);
    QCOMPARE(game.board()[3][3], 4);

    for (int attempt = 0; attempt < 24; ++attempt) {
        game.start();
        int tileCount = 0;
        for (const auto& row : game.board()) {
            for (int value : row) {
                if (!value) continue;
                QVERIFY(value == 2 || value == 4);
                ++tileCount;
            }
        }
        QCOMPARE(tileCount, 2);
        QCOMPARE(game.emptyCells().size(), 14);
    }

    Game2048::Board board{};
    board[0] = {2, 2, 2, 2};
    game.setBoard(board);
    QVERIFY(game.move(Direction::Left, 2, 2));
    QCOMPARE(game.board()[0], (std::array<int, 4>{4, 4, 2, 0}));
    QCOMPARE(game.score(), 8);

    const auto unchanged = game.board();
    QVERIFY(!game.move(Direction::Up));
    QCOMPARE(game.board(), unchanged);

    Game2048::Board won{};
    won[0][0] = 2048;
    game.setBoard(won);
    QVERIFY(game.hasWon());

    Game2048::Board ended{};
    for (int row = 0; row < 4; ++row) for (int column = 0; column < 4; ++column)
        ended[row][column] = ((row + column) % 2) ? 2 : 4;
    game.setBoard(ended);
    QVERIFY(game.isGameOver());
}

void GameLogicTest::snakeGrowsPausesAndRejectsReverse()
{
    SnakeGame game(8, 8);
    for (int attempt = 0; attempt < 20; ++attempt) {
        game.start();
        QVERIFY(!game.body().contains(game.food()));
    }
    game.start();
    const QPoint head = game.body().first();
    game.setDirection(Direction::Left);
    QCOMPARE(game.direction(), Direction::Right);
    game.setFood(head + QPoint(1, 0));
    QVERIFY(game.advance());
    QCOMPARE(game.body().size(), 4);
    QCOMPARE(game.score(), 10);

    game.togglePause();
    const auto pausedBody = game.body();
    QVERIFY(!game.advance());
    QCOMPARE(game.body(), pausedBody);
    game.togglePause();
    while (game.state() == GameState::Running) (void)game.advance();
    QCOMPARE(game.state(), GameState::GameOver);
}

void GameLogicTest::tetrisMovesDropsClearsAndEnds()
{
    const std::array<TetrisGame::Piece, 7> pieces{
        TetrisGame::Piece::I, TetrisGame::Piece::O, TetrisGame::Piece::T,
        TetrisGame::Piece::L, TetrisGame::Piece::J, TetrisGame::Piece::S,
        TetrisGame::Piece::Z};
    for (TetrisGame::Piece piece : pieces) {
        TetrisGame shape;
        shape.start(piece);
        QCOMPARE(shape.currentCells().size(), 4);
        QCOMPARE(shape.currentPiece(), piece);
    }

    TetrisGame bagGame;
    bagGame.start();
    QSet<int> bagPieces;
    for (int index = 0; index < 7; ++index) {
        bagPieces.insert(static_cast<int>(bagGame.currentPiece()));
        if (index < 6) bagGame.hardDrop();
    }
    QCOMPARE(bagPieces.size(), 7);

    TetrisGame game;
    game.start(TetrisGame::Piece::I);
    const auto activeCells = game.currentCells();
    game.togglePause();
    QVERIFY(!game.step());
    QCOMPARE(game.currentCells(), activeCells);
    game.togglePause();
    QVERIFY(game.move(-1));
    QVERIFY(game.rotate());
    QVERIFY(game.hardDrop() > 0);

    game.start(TetrisGame::Piece::I);
    TetrisGame::Board board{};
    board[TetrisGame::Height - 1].fill(1);
    for (int column = 3; column <= 6; ++column) board[TetrisGame::Height - 1][column] = 0;
    game.setBoard(board);
    game.hardDrop();
    QCOMPARE(game.lines(), 1);
    QVERIFY(game.score() >= 100);

    game.start(TetrisGame::Piece::T);
    board = {};
    board[2][5] = 1;
    game.setBoard(board);
    (void)game.step();
    QCOMPARE(game.state(), GameState::GameOver);
}

void GameLogicTest::gomokuFindsFiveInEveryDirection()
{
    const QVector<QPoint> directions{{1, 0}, {0, 1}, {1, 1}, {1, -1}};
    for (const QPoint& direction : directions) {
        GomokuGame game;
        game.start();
        const QPoint origin = direction.y() < 0 ? QPoint(3, 8) : QPoint(3, 3);
        for (int index = 0; index < 5; ++index) {
            QVERIFY(game.place(origin.x() + direction.x() * index,
                               origin.y() + direction.y() * index));
            if (index < 4) QVERIFY(game.place(14, index));
        }
        QCOMPARE(game.winner(), 1);
        QCOMPARE(game.state(), GameState::Won);
        QVERIFY(!game.place(0, 0));
    }
}

void GameLogicTest::settingsPersistScoresAndSound()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("games.ini"));
    {
        QSettings storage(path, QSettings::IniFormat);
        GameSettings settings(storage);
        QVERIFY(settings.soundEnabled());
        settings.setSoundEnabled(false);
        QVERIFY(settings.updateHighScore(QStringLiteral("snake"), 30));
        QVERIFY(!settings.updateHighScore(QStringLiteral("snake"), 20));
        for (int match = 0; match < 2; ++match) {
            GomokuGame game;
            game.start();
            for (int x = 0; x < 5; ++x) {
                QVERIFY(game.place(x, 0));
                if (x < 4) QVERIFY(game.place(x, 2));
            }
            QCOMPARE(game.state(), GameState::Won);
            QCOMPARE(settings.incrementWinCount(QStringLiteral("gomoku")), match + 1);
        }
    }
    QSettings recreated(path, QSettings::IniFormat);
    GameSettings settings(recreated);
    QVERIFY(!settings.soundEnabled());
    QCOMPARE(settings.highScore(QStringLiteral("snake")), 30);
    QCOMPARE(settings.highScore(QStringLiteral("tetris")), 0);
    QCOMPARE(settings.winCount(QStringLiteral("gomoku")), 2);
}

QTEST_APPLESS_MAIN(GameLogicTest)
#include "test_game_logic.moc"
