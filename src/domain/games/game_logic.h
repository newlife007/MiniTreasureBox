#pragma once

#include <QPoint>
#include <QString>
#include <QVector>

#include <algorithm>
#include <array>
#include <deque>
#include <optional>

namespace mtb::games {

enum class GameState { Ready, Running, Paused, Won, GameOver };
enum class Direction { Up, Down, Left, Right };
enum class SnakeMode { Classic, Wrap, Obstacles };
enum class GomokuAiLevel { Easy, Normal };

struct GuessResult {
    enum class Outcome { Invalid, TooLow, TooHigh, Correct };
    Outcome outcome{Outcome::Invalid};
    QString message;
};

class GuessNumberGame {
public:
    void start(int target, int minimum = 1, int maximum = 100);
    void togglePause();
    void finish();
    [[nodiscard]] GuessResult guess(const QString& text);
    [[nodiscard]] int attempts() const { return attempts_; }
    [[nodiscard]] int minimum() const { return minimum_; }
    [[nodiscard]] int maximum() const { return maximum_; }
    [[nodiscard]] GameState state() const { return state_; }
private:
    int target_{50};
    int minimum_{1};
    int maximum_{100};
    int attempts_{0};
    GameState state_{GameState::Ready};
};

class Game2048 {
public:
    using Board = std::array<std::array<int, 4>, 4>;
    void start();
    void start(int firstCell, int secondCell, int firstValue = 2, int secondValue = 2);
    void setBoard(const Board& board);
    void restore(const Board& board, int score);
    [[nodiscard]] bool move(Direction direction, int spawnCell = -1, int spawnValue = 0);
    [[nodiscard]] const Board& board() const { return board_; }
    [[nodiscard]] int score() const { return score_; }
    [[nodiscard]] bool hasWon() const;
    [[nodiscard]] bool isGameOver() const;
    [[nodiscard]] QVector<int> emptyCells() const;
private:
    void spawn(int cell, int value);
    Board board_{};
    int score_{0};
};

class SnakeGame {
public:
    SnakeGame(int width = 16, int height = 16);
    void start();
    void setMode(SnakeMode mode) { mode_ = mode; }
    void setObstacles(const QVector<QPoint>& obstacles) { obstacles_ = obstacles; }
    void setDirection(Direction direction);
    void setFood(QPoint food) { food_ = food; specialFood_ = false; }
    void spawnSpecialFood();
    [[nodiscard]] bool advance();
    void togglePause();
    [[nodiscard]] const QVector<QPoint>& body() const { return body_; }
    [[nodiscard]] QPoint food() const { return food_; }
    [[nodiscard]] Direction direction() const { return direction_; }
    [[nodiscard]] const QVector<QPoint>& obstacles() const { return obstacles_; }
    [[nodiscard]] bool foodIsSpecial() const { return specialFood_; }
    void addScore(int bonus) { score_ += std::max(0, bonus); }
    [[nodiscard]] int score() const { return score_; }
    [[nodiscard]] GameState state() const { return state_; }
private:
    void placeFood(bool special = false);
    int width_;
    int height_;
    QVector<QPoint> body_;
    QVector<QPoint> obstacles_;
    QPoint food_;
    SnakeMode mode_{SnakeMode::Classic};
    bool specialFood_{false};
    Direction direction_{Direction::Right};
    Direction committedDirection_{Direction::Right};
    int score_{0};
    GameState state_{GameState::Ready};
};

class TetrisGame {
public:
    static constexpr int Width = 10;
    static constexpr int Height = 20;
    using Board = std::array<std::array<int, Width>, Height>;
    enum class Piece { I, O, T, L, J, S, Z };

    void start();
    void start(Piece piece);
    void setBoard(const Board& board) { board_ = board; }
    [[nodiscard]] bool move(int dx);
    [[nodiscard]] bool rotate();
    [[nodiscard]] bool step();
    int hardDrop();
    bool hold();
    void togglePause();
    [[nodiscard]] QVector<QPoint> currentCells() const;
    [[nodiscard]] QVector<QPoint> ghostCells() const;
    [[nodiscard]] QVector<Piece> previewPieces(int count = 3) const;
    [[nodiscard]] std::optional<Piece> heldPiece() const { return heldPiece_; }
    [[nodiscard]] bool canHold() const { return !holdUsed_; }
    [[nodiscard]] const Board& board() const { return board_; }
    [[nodiscard]] int score() const { return score_; }
    [[nodiscard]] int lines() const { return lines_; }
    [[nodiscard]] int level() const { return lines_ / 10 + 1; }
    [[nodiscard]] int combo() const { return combo_; }
    [[nodiscard]] Piece currentPiece() const { return piece_; }
    [[nodiscard]] Piece nextPiece() const { return previewPieces(1).value(0, Piece::T); }
    [[nodiscard]] GameState state() const { return state_; }
private:
    void reset();
    void refillQueue();
    [[nodiscard]] Piece takeNext();
    [[nodiscard]] QVector<QPoint> cells(Piece piece, int rotation, QPoint origin) const;
    [[nodiscard]] bool fits(Piece piece, int rotation, QPoint origin) const;
    int lockAndClear();
    void spawnNext();
    Board board_{};
    Piece piece_{Piece::T};
    QPoint origin_{4, 1};
    int rotation_{0};
    int score_{0};
    int lines_{0};
    int combo_{-1};
    std::deque<Piece> queue_;
    std::optional<Piece> heldPiece_;
    bool holdUsed_{false};
    GameState state_{GameState::Ready};
};

class GomokuGame {
public:
    static constexpr int Size = 15;
    using Board = std::array<std::array<int, Size>, Size>;
    void start();
    [[nodiscard]] bool place(int x, int y);
    [[nodiscard]] bool undo(int moveCount = 1);
    [[nodiscard]] int at(int x, int y) const;
    [[nodiscard]] const Board& board() const { return board_; }
    [[nodiscard]] int currentPlayer() const { return currentPlayer_; }
    [[nodiscard]] int winner() const { return winner_; }
    [[nodiscard]] int moveCount() const { return moves_; }
    [[nodiscard]] QPoint lastMove() const { return lastMove_; }
    [[nodiscard]] QVector<QPoint> winningLine() const;
    [[nodiscard]] GameState state() const { return state_; }
private:
    [[nodiscard]] bool hasFive(int x, int y) const;
    Board board_{};
    int currentPlayer_{1};
    int winner_{0};
    int moves_{0};
    QPoint lastMove_{-1, -1};
    QVector<QPoint> history_;
    GameState state_{GameState::Ready};
};

[[nodiscard]] QPoint chooseGomokuMove(const GomokuGame::Board& board,
                                      int player, GomokuAiLevel level);

}  // namespace mtb::games
