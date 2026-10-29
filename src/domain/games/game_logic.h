#pragma once

#include <QPoint>
#include <QString>
#include <QVector>

#include <array>

namespace mtb::games {

enum class GameState { Ready, Running, Paused, Won, GameOver };
enum class Direction { Up, Down, Left, Right };

struct GuessResult {
    enum class Outcome { Invalid, TooLow, TooHigh, Correct };
    Outcome outcome{Outcome::Invalid};
    QString message;
};

class GuessNumberGame {
public:
    void start(int target);
    void togglePause();
    [[nodiscard]] GuessResult guess(const QString& text);
    [[nodiscard]] int attempts() const { return attempts_; }
    [[nodiscard]] GameState state() const { return state_; }
private:
    int target_{50};
    int attempts_{0};
    GameState state_{GameState::Ready};
};

class Game2048 {
public:
    using Board = std::array<std::array<int, 4>, 4>;
    void start();
    void start(int firstCell, int secondCell, int firstValue = 2, int secondValue = 2);
    void setBoard(const Board& board);
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
    void setDirection(Direction direction);
    void setFood(QPoint food) { food_ = food; }
    [[nodiscard]] bool advance();
    void togglePause();
    [[nodiscard]] const QVector<QPoint>& body() const { return body_; }
    [[nodiscard]] QPoint food() const { return food_; }
    [[nodiscard]] Direction direction() const { return direction_; }
    [[nodiscard]] int score() const { return score_; }
    [[nodiscard]] GameState state() const { return state_; }
private:
    void placeFood();
    int width_;
    int height_;
    QVector<QPoint> body_;
    QPoint food_;
    Direction direction_{Direction::Right};
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
    void togglePause();
    [[nodiscard]] QVector<QPoint> currentCells() const;
    [[nodiscard]] const Board& board() const { return board_; }
    [[nodiscard]] int score() const { return score_; }
    [[nodiscard]] int lines() const { return lines_; }
    [[nodiscard]] Piece currentPiece() const { return piece_; }
    [[nodiscard]] GameState state() const { return state_; }
private:
    void reset();
    void refillBag();
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
    std::array<Piece, 7> bag_{};
    int bagIndex_{7};
    GameState state_{GameState::Ready};
};

class GomokuGame {
public:
    static constexpr int Size = 15;
    using Board = std::array<std::array<int, Size>, Size>;
    void start();
    [[nodiscard]] bool place(int x, int y);
    [[nodiscard]] int at(int x, int y) const;
    [[nodiscard]] int currentPlayer() const { return currentPlayer_; }
    [[nodiscard]] int winner() const { return winner_; }
    [[nodiscard]] GameState state() const { return state_; }
private:
    [[nodiscard]] bool hasFive(int x, int y) const;
    Board board_{};
    int currentPlayer_{1};
    int winner_{0};
    int moves_{0};
    GameState state_{GameState::Ready};
};

}  // namespace mtb::games
