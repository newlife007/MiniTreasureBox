#include "domain/games/game_logic.h"

#include <QRandomGenerator>

#include <algorithm>
#include <vector>

namespace mtb::games {

void GuessNumberGame::start(int target)
{
    target_ = std::clamp(target, 1, 100);
    attempts_ = 0;
    state_ = GameState::Running;
}

void GuessNumberGame::togglePause()
{
    if (state_ == GameState::Running) {
        state_ = GameState::Paused;
    } else if (state_ == GameState::Paused) {
        state_ = GameState::Running;
    }
}

GuessResult GuessNumberGame::guess(const QString& text)
{
    bool ok = false;
    const int value = text.trimmed().toInt(&ok);
    if (state_ != GameState::Running || !ok || value < 1 || value > 100) {
        return {GuessResult::Outcome::Invalid, QStringLiteral("请输入 1 到 100 之间的整数。")};
    }
    ++attempts_;
    if (value < target_) {
        return {GuessResult::Outcome::TooLow, QStringLiteral("太小了")};
    }
    if (value > target_) {
        return {GuessResult::Outcome::TooHigh, QStringLiteral("太大了")};
    }
    state_ = GameState::Won;
    return {GuessResult::Outcome::Correct, QStringLiteral("猜对了")};
}

void Game2048::start()
{
    board_ = {};
    score_ = 0;
    spawn(-1, 0);
    spawn(-1, 0);
}

void Game2048::start(int firstCell, int secondCell, int firstValue, int secondValue)
{
    board_ = {};
    score_ = 0;
    spawn(firstCell, firstValue);
    spawn(secondCell, secondValue);
}

void Game2048::setBoard(const Board& board)
{
    board_ = board;
    score_ = 0;
}

QVector<int> Game2048::emptyCells() const
{
    QVector<int> result;
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            if (board_[row][column] == 0) {
                result.append(row * 4 + column);
            }
        }
    }
    return result;
}

void Game2048::spawn(int cell, int value)
{
    const QVector<int> empty = emptyCells();
    if (empty.isEmpty()) {
        return;
    }
    if (!empty.contains(cell)) {
        cell = empty.at(QRandomGenerator::global()->bounded(empty.size()));
    }
    if (value != 2 && value != 4) {
        value = QRandomGenerator::global()->bounded(10) == 0 ? 4 : 2;
    }
    board_[cell / 4][cell % 4] = value;
}

bool Game2048::move(Direction direction, int spawnCell, int spawnValue)
{
    const Board before = board_;
    for (int line = 0; line < 4; ++line) {
        std::vector<int> values;
        for (int offset = 0; offset < 4; ++offset) {
            int row = 0;
            int column = 0;
            if (direction == Direction::Left || direction == Direction::Right) {
                row = line;
                column = direction == Direction::Left ? offset : 3 - offset;
            } else {
                column = line;
                row = direction == Direction::Up ? offset : 3 - offset;
            }
            if (board_[row][column] != 0) {
                values.push_back(board_[row][column]);
            }
        }

        std::vector<int> merged;
        for (std::size_t index = 0; index < values.size(); ++index) {
            if (index + 1 < values.size() && values[index] == values[index + 1]) {
                const int value = values[index] * 2;
                merged.push_back(value);
                score_ += value;
                ++index;
            } else {
                merged.push_back(values[index]);
            }
        }
        merged.resize(4, 0);
        for (int offset = 0; offset < 4; ++offset) {
            int row = 0;
            int column = 0;
            if (direction == Direction::Left || direction == Direction::Right) {
                row = line;
                column = direction == Direction::Left ? offset : 3 - offset;
            } else {
                column = line;
                row = direction == Direction::Up ? offset : 3 - offset;
            }
            board_[row][column] = merged[static_cast<std::size_t>(offset)];
        }
    }
    if (board_ == before) {
        return false;
    }
    spawn(spawnCell, spawnValue);
    return true;
}

bool Game2048::hasWon() const
{
    for (const auto& row : board_) {
        if (std::any_of(row.begin(), row.end(), [](int value) { return value >= 2048; })) {
            return true;
        }
    }
    return false;
}

bool Game2048::isGameOver() const
{
    if (!emptyCells().isEmpty()) {
        return false;
    }
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            if (column + 1 < 4 && board_[row][column] == board_[row][column + 1]) {
                return false;
            }
            if (row + 1 < 4 && board_[row][column] == board_[row + 1][column]) {
                return false;
            }
        }
    }
    return true;
}

SnakeGame::SnakeGame(int width, int height)
    : width_(std::max(width, 6))
    , height_(std::max(height, 6))
{
}

void SnakeGame::start()
{
    body_ = {QPoint(3, height_ / 2), QPoint(2, height_ / 2), QPoint(1, height_ / 2)};
    direction_ = Direction::Right;
    score_ = 0;
    state_ = GameState::Running;
    placeFood();
}

void SnakeGame::setDirection(Direction direction)
{
    const bool opposite = (direction_ == Direction::Left && direction == Direction::Right)
        || (direction_ == Direction::Right && direction == Direction::Left)
        || (direction_ == Direction::Up && direction == Direction::Down)
        || (direction_ == Direction::Down && direction == Direction::Up);
    if (!opposite) {
        direction_ = direction;
    }
}

bool SnakeGame::advance()
{
    if (state_ != GameState::Running || body_.isEmpty()) {
        return false;
    }
    QPoint head = body_.first();
    switch (direction_) {
    case Direction::Up: --head.ry(); break;
    case Direction::Down: ++head.ry(); break;
    case Direction::Left: --head.rx(); break;
    case Direction::Right: ++head.rx(); break;
    }
    const bool eating = head == food_;
    QVector<QPoint> collisionBody = body_;
    if (!eating) {
        collisionBody.removeLast();
    }
    if (head.x() < 0 || head.x() >= width_ || head.y() < 0 || head.y() >= height_
        || collisionBody.contains(head)) {
        state_ = GameState::GameOver;
        return false;
    }
    body_.prepend(head);
    if (eating) {
        score_ += 10;
        placeFood();
    } else {
        body_.removeLast();
    }
    return true;
}

void SnakeGame::togglePause()
{
    if (state_ == GameState::Running) {
        state_ = GameState::Paused;
    } else if (state_ == GameState::Paused) {
        state_ = GameState::Running;
    }
}

void SnakeGame::placeFood()
{
    QVector<QPoint> empty;
    empty.reserve(width_ * height_ - body_.size());
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const QPoint candidate(x, y);
            if (!body_.contains(candidate)) {
                empty.append(candidate);
            }
        }
    }
    if (!empty.isEmpty()) {
        food_ = empty.at(QRandomGenerator::global()->bounded(empty.size()));
    }
}

QVector<QPoint> TetrisGame::cells(Piece piece, int rotation, QPoint origin) const
{
    QVector<QPoint> offsets;
    switch (piece) {
    case Piece::I: offsets = {{-1, 0}, {0, 0}, {1, 0}, {2, 0}}; break;
    case Piece::O: offsets = {{0, 0}, {1, 0}, {0, 1}, {1, 1}}; break;
    case Piece::T: offsets = {{-1, 0}, {0, 0}, {1, 0}, {0, 1}}; break;
    case Piece::L: offsets = {{-1, 0}, {0, 0}, {1, 0}, {1, 1}}; break;
    case Piece::J: offsets = {{-1, 0}, {0, 0}, {1, 0}, {-1, 1}}; break;
    case Piece::S: offsets = {{0, 0}, {1, 0}, {-1, 1}, {0, 1}}; break;
    case Piece::Z: offsets = {{-1, 0}, {0, 0}, {0, 1}, {1, 1}}; break;
    }
    if (piece != Piece::O) {
        for (int turn = 0; turn < rotation % 4; ++turn) {
            for (QPoint& point : offsets) {
                point = QPoint(-point.y(), point.x());
            }
        }
    }
    for (QPoint& point : offsets) {
        point += origin;
    }
    return offsets;
}

bool TetrisGame::fits(Piece piece, int rotation, QPoint origin) const
{
    for (const QPoint& point : cells(piece, rotation, origin)) {
        if (point.x() < 0 || point.x() >= Width || point.y() >= Height) {
            return false;
        }
        if (point.y() >= 0 && board_[point.y()][point.x()] != 0) {
            return false;
        }
    }
    return true;
}

void TetrisGame::reset()
{
    board_ = {};
    score_ = 0;
    lines_ = 0;
    bagIndex_ = static_cast<int>(bag_.size());
    origin_ = QPoint(4, 1);
    rotation_ = 0;
}

void TetrisGame::refillBag()
{
    bag_ = {Piece::I, Piece::O, Piece::T, Piece::L, Piece::J, Piece::S, Piece::Z};
    for (int index = static_cast<int>(bag_.size()) - 1; index > 0; --index) {
        const int other = QRandomGenerator::global()->bounded(index + 1);
        std::swap(bag_[static_cast<std::size_t>(index)],
                  bag_[static_cast<std::size_t>(other)]);
    }
    bagIndex_ = 0;
}

void TetrisGame::start()
{
    reset();
    refillBag();
    piece_ = bag_[static_cast<std::size_t>(bagIndex_++)];
    state_ = fits(piece_, rotation_, origin_) ? GameState::Running : GameState::GameOver;
}

void TetrisGame::start(Piece piece)
{
    reset();
    bag_ = {Piece::I, Piece::O, Piece::T, Piece::L, Piece::J, Piece::S, Piece::Z};
    const auto position = std::find(bag_.begin(), bag_.end(), piece);
    bagIndex_ = static_cast<int>(std::distance(bag_.begin(), position)) + 1;
    piece_ = piece;
    state_ = fits(piece_, rotation_, origin_) ? GameState::Running : GameState::GameOver;
}

bool TetrisGame::move(int dx)
{
    if (state_ != GameState::Running || !fits(piece_, rotation_, origin_ + QPoint(dx, 0))) {
        return false;
    }
    origin_.rx() += dx;
    return true;
}

bool TetrisGame::rotate()
{
    if (state_ != GameState::Running) {
        return false;
    }
    const int next = (rotation_ + 1) % 4;
    for (int kick : {0, -1, 1, -2, 2}) {
        const QPoint candidate = origin_ + QPoint(kick, 0);
        if (fits(piece_, next, candidate)) {
            rotation_ = next;
            origin_ = candidate;
            return true;
        }
    }
    return false;
}

bool TetrisGame::step()
{
    if (state_ != GameState::Running) {
        return false;
    }
    if (fits(piece_, rotation_, origin_ + QPoint(0, 1))) {
        ++origin_.ry();
        return true;
    }
    lockAndClear();
    spawnNext();
    return false;
}

int TetrisGame::hardDrop()
{
    if (state_ != GameState::Running) {
        return 0;
    }
    int distance = 0;
    while (fits(piece_, rotation_, origin_ + QPoint(0, 1))) {
        ++origin_.ry();
        ++distance;
    }
    score_ += distance * 2;
    lockAndClear();
    spawnNext();
    return distance;
}

int TetrisGame::lockAndClear()
{
    for (const QPoint& point : currentCells()) {
        if (point.y() < 0) {
            state_ = GameState::GameOver;
            return 0;
        }
        board_[point.y()][point.x()] = 1;
    }
    int cleared = 0;
    for (int row = Height - 1; row >= 0;) {
        const bool full = std::all_of(board_[row].begin(), board_[row].end(),
                                      [](int value) { return value != 0; });
        if (!full) {
            --row;
            continue;
        }
        for (int moveRow = row; moveRow > 0; --moveRow) {
            board_[moveRow] = board_[moveRow - 1];
        }
        board_[0].fill(0);
        ++cleared;
    }
    lines_ += cleared;
    static constexpr int rewards[] = {0, 100, 300, 500, 800};
    score_ += rewards[std::min(cleared, 4)];
    return cleared;
}

void TetrisGame::spawnNext()
{
    if (state_ == GameState::GameOver) {
        return;
    }
    if (bagIndex_ >= static_cast<int>(bag_.size())) {
        refillBag();
    }
    piece_ = bag_[static_cast<std::size_t>(bagIndex_++)];
    origin_ = QPoint(4, 1);
    rotation_ = 0;
    if (!fits(piece_, rotation_, origin_)) {
        state_ = GameState::GameOver;
    }
}

void TetrisGame::togglePause()
{
    if (state_ == GameState::Running) {
        state_ = GameState::Paused;
    } else if (state_ == GameState::Paused) {
        state_ = GameState::Running;
    }
}

QVector<QPoint> TetrisGame::currentCells() const
{
    return cells(piece_, rotation_, origin_);
}

void GomokuGame::start()
{
    board_ = {};
    currentPlayer_ = 1;
    winner_ = 0;
    moves_ = 0;
    state_ = GameState::Running;
}

int GomokuGame::at(int x, int y) const
{
    if (x < 0 || x >= Size || y < 0 || y >= Size) {
        return 0;
    }
    return board_[y][x];
}

bool GomokuGame::place(int x, int y)
{
    if (state_ != GameState::Running || x < 0 || x >= Size || y < 0 || y >= Size
        || board_[y][x] != 0) {
        return false;
    }
    board_[y][x] = currentPlayer_;
    ++moves_;
    if (hasFive(x, y)) {
        winner_ = currentPlayer_;
        state_ = GameState::Won;
    } else if (moves_ == Size * Size) {
        state_ = GameState::GameOver;
    } else {
        currentPlayer_ = 3 - currentPlayer_;
    }
    return true;
}

bool GomokuGame::hasFive(int x, int y) const
{
    const int player = board_[y][x];
    for (const QPoint direction : {QPoint(1, 0), QPoint(0, 1), QPoint(1, 1), QPoint(1, -1)}) {
        int count = 1;
        for (int sign : {-1, 1}) {
            QPoint point(x, y);
            while (true) {
                point += direction * sign;
                if (at(point.x(), point.y()) != player) {
                    break;
                }
                ++count;
            }
        }
        if (count >= 5) {
            return true;
        }
    }
    return false;
}

}  // namespace mtb::games
