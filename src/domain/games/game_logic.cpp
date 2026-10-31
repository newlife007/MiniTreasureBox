#include "domain/games/game_logic.h"

#include <QRandomGenerator>

#include <algorithm>
#include <limits>
#include <vector>

namespace mtb::games {

void GuessNumberGame::start(int target, int minimum, int maximum)
{
    minimum_ = std::min(minimum, maximum);
    maximum_ = std::max(minimum, maximum);
    target_ = std::clamp(target, minimum_, maximum_);
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

void GuessNumberGame::finish()
{
    if (state_ == GameState::Running || state_ == GameState::Paused)
        state_ = GameState::GameOver;
}

GuessResult GuessNumberGame::guess(const QString& text)
{
    bool ok = false;
    const int value = text.trimmed().toInt(&ok);
    if (state_ != GameState::Running || !ok || value < minimum_ || value > maximum_) {
        return {GuessResult::Outcome::Invalid,
                QStringLiteral("请输入 %1 到 %2 之间的整数。")
                    .arg(minimum_).arg(maximum_)};
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
    restore(board, 0);
}

void Game2048::restore(const Board& board, int score)
{
    board_ = board;
    score_ = std::max(0, score);
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
    committedDirection_ = Direction::Right;
    score_ = 0;
    specialFood_ = false;
    obstacles_.erase(std::remove_if(obstacles_.begin(), obstacles_.end(),
                                    [this](const QPoint& point) {
                                        return body_.contains(point);
                                    }),
                     obstacles_.end());
    state_ = GameState::Running;
    placeFood();
}

void SnakeGame::setDirection(Direction direction)
{
    if (direction_ != committedDirection_) {
        return;
    }
    const bool opposite = (committedDirection_ == Direction::Left && direction == Direction::Right)
        || (committedDirection_ == Direction::Right && direction == Direction::Left)
        || (committedDirection_ == Direction::Up && direction == Direction::Down)
        || (committedDirection_ == Direction::Down && direction == Direction::Up);
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
    if (mode_ == SnakeMode::Wrap) {
        head.setX((head.x() + width_) % width_);
        head.setY((head.y() + height_) % height_);
    }
    const bool eating = head == food_;
    committedDirection_ = direction_;
    QVector<QPoint> collisionBody = body_;
    if (!eating) {
        collisionBody.removeLast();
    }
    const bool outside = head.x() < 0 || head.x() >= width_
        || head.y() < 0 || head.y() >= height_;
    if (outside || collisionBody.contains(head) || obstacles_.contains(head)) {
        state_ = GameState::GameOver;
        return false;
    }
    body_.prepend(head);
    if (eating) {
        score_ += specialFood_ ? 30 : 10;
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

void SnakeGame::spawnSpecialFood()
{
    placeFood(true);
}

void SnakeGame::placeFood(bool special)
{
    QVector<QPoint> empty;
    empty.reserve(width_ * height_ - body_.size() - obstacles_.size());
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const QPoint candidate(x, y);
            if (!body_.contains(candidate) && !obstacles_.contains(candidate)) {
                empty.append(candidate);
            }
        }
    }
    if (!empty.isEmpty()) {
        food_ = empty.at(QRandomGenerator::global()->bounded(empty.size()));
        specialFood_ = special;
    } else {
        state_ = GameState::Won;
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
    combo_ = -1;
    queue_.clear();
    heldPiece_.reset();
    holdUsed_ = false;
    origin_ = QPoint(4, 1);
    rotation_ = 0;
}

void TetrisGame::refillQueue()
{
    std::array<Piece, 7> bag{Piece::I, Piece::O, Piece::T, Piece::L,
                             Piece::J, Piece::S, Piece::Z};
    for (int index = static_cast<int>(bag.size()) - 1; index > 0; --index) {
        const int other = QRandomGenerator::global()->bounded(index + 1);
        std::swap(bag[static_cast<std::size_t>(index)],
                  bag[static_cast<std::size_t>(other)]);
    }
    for (Piece piece : bag) queue_.push_back(piece);
}

TetrisGame::Piece TetrisGame::takeNext()
{
    if (queue_.size() < 4) refillQueue();
    const Piece next = queue_.front();
    queue_.pop_front();
    if (queue_.size() < 4) refillQueue();
    return next;
}

void TetrisGame::start()
{
    reset();
    refillQueue();
    piece_ = takeNext();
    state_ = fits(piece_, rotation_, origin_) ? GameState::Running : GameState::GameOver;
}

void TetrisGame::start(Piece piece)
{
    reset();
    refillQueue();
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

bool TetrisGame::hold()
{
    if (state_ != GameState::Running || holdUsed_) return false;
    const Piece outgoing = piece_;
    if (heldPiece_) {
        piece_ = *heldPiece_;
        heldPiece_ = outgoing;
    } else {
        heldPiece_ = outgoing;
        piece_ = takeNext();
    }
    origin_ = QPoint(4, 1);
    rotation_ = 0;
    holdUsed_ = true;
    if (!fits(piece_, rotation_, origin_)) state_ = GameState::GameOver;
    return true;
}

int TetrisGame::lockAndClear()
{
    for (const QPoint& point : currentCells()) {
        if (point.y() < 0) {
            state_ = GameState::GameOver;
            return 0;
        }
        board_[point.y()][point.x()] = static_cast<int>(piece_) + 1;
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
    if (cleared > 0) {
        ++combo_;
        if (combo_ > 0) score_ += combo_ * 50;
    } else {
        combo_ = -1;
    }
    return cleared;
}

void TetrisGame::spawnNext()
{
    if (state_ == GameState::GameOver) {
        return;
    }
    piece_ = takeNext();
    holdUsed_ = false;
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

QVector<TetrisGame::Piece> TetrisGame::previewPieces(int count) const
{
    QVector<Piece> preview;
    const int bounded = std::max(0, std::min(count, static_cast<int>(queue_.size())));
    preview.reserve(bounded);
    auto iterator = queue_.cbegin();
    for (int index = 0; index < bounded; ++index, ++iterator) {
        preview.append(*iterator);
    }
    return preview;
}

QVector<QPoint> TetrisGame::ghostCells() const
{
    QPoint ghostOrigin = origin_;
    while (fits(piece_, rotation_, ghostOrigin + QPoint(0, 1))) {
        ++ghostOrigin.ry();
    }
    return cells(piece_, rotation_, ghostOrigin);
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
    lastMove_ = {-1, -1};
    history_.clear();
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
    lastMove_ = {x, y};
    history_.append(lastMove_);
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

bool GomokuGame::undo(int moveCount)
{
    if (history_.isEmpty() || moveCount <= 0) return false;
    const int removeCount = std::min(moveCount, static_cast<int>(history_.size()));
    for (int index = 0; index < removeCount; ++index) history_.removeLast();

    const QVector<QPoint> retained = history_;
    board_ = {};
    currentPlayer_ = 1;
    winner_ = 0;
    moves_ = 0;
    lastMove_ = {-1, -1};
    history_.clear();
    state_ = GameState::Running;
    for (const QPoint& move : retained) {
        board_[move.y()][move.x()] = currentPlayer_;
        lastMove_ = move;
        history_.append(move);
        ++moves_;
        currentPlayer_ = 3 - currentPlayer_;
    }
    return true;
}

QVector<QPoint> GomokuGame::winningLine() const
{
    if (state_ != GameState::Won || winner_ == 0 || lastMove_.x() < 0) {
        return {};
    }
    for (const QPoint direction : {QPoint(1, 0), QPoint(0, 1),
                                   QPoint(1, 1), QPoint(1, -1)}) {
        QVector<QPoint> line{lastMove_};
        for (int sign : {-1, 1}) {
            QPoint point = lastMove_;
            while (true) {
                point += direction * sign;
                if (at(point.x(), point.y()) != winner_) break;
                if (sign < 0) line.prepend(point);
                else line.append(point);
            }
        }
        if (line.size() >= 5) return line;
    }
    return {};
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

namespace {

int gomokuAt(const GomokuGame::Board& board, int x, int y)
{
    if (x < 0 || x >= GomokuGame::Size || y < 0 || y >= GomokuGame::Size) return 0;
    return board[y][x];
}

int lineLength(const GomokuGame::Board& board, const QPoint& move,
               int player, const QPoint& direction)
{
    int length = 1;
    for (int sign : {-1, 1}) {
        QPoint point = move;
        while (true) {
            point += direction * sign;
            if (gomokuAt(board, point.x(), point.y()) != player) break;
            ++length;
        }
    }
    return length;
}

int movePotential(GomokuGame::Board board, const QPoint& move, int player)
{
    board[move.y()][move.x()] = player;
    int best = 1;
    int openEnds = 0;
    for (const QPoint direction : {QPoint(1, 0), QPoint(0, 1),
                                   QPoint(1, 1), QPoint(1, -1)}) {
        best = std::max(best, lineLength(board, move, player, direction));
        for (int sign : {-1, 1}) {
            QPoint point = move + direction * sign;
            while (gomokuAt(board, point.x(), point.y()) == player)
                point += direction * sign;
            if (gomokuAt(board, point.x(), point.y()) == 0) ++openEnds;
        }
    }
    return best * best * 20 + openEnds * 3;
}

QVector<QPoint> gomokuCandidates(const GomokuGame::Board& board)
{
    QVector<QPoint> candidates;
    bool hasStone = false;
    for (int y = 0; y < GomokuGame::Size; ++y) {
        for (int x = 0; x < GomokuGame::Size; ++x) {
            if (!board[y][x]) continue;
            hasStone = true;
            for (int dy = -2; dy <= 2; ++dy) {
                for (int dx = -2; dx <= 2; ++dx) {
                    const QPoint point(x + dx, y + dy);
                    if (point.x() >= 0 && point.x() < GomokuGame::Size
                        && point.y() >= 0 && point.y() < GomokuGame::Size
                        && board[point.y()][point.x()] == 0
                        && !candidates.contains(point)) {
                        candidates.append(point);
                    }
                }
            }
        }
    }
    if (!hasStone) candidates.append({GomokuGame::Size / 2, GomokuGame::Size / 2});
    return candidates;
}

}  // namespace

QPoint chooseGomokuMove(const GomokuGame::Board& board, int player,
                        GomokuAiLevel level)
{
    const QVector<QPoint> candidates = gomokuCandidates(board);
    if (candidates.isEmpty()) return {-1, -1};
    const int opponent = 3 - player;

    for (const QPoint& move : candidates) {
        if (movePotential(board, move, player) >= 500) return move;
    }
    for (const QPoint& move : candidates) {
        if (movePotential(board, move, opponent) >= 500) return move;
    }
    if (level == GomokuAiLevel::Easy) {
        return candidates.at(QRandomGenerator::global()->bounded(candidates.size()));
    }

    QPoint best = candidates.first();
    int bestScore = std::numeric_limits<int>::min();
    const QPoint center(GomokuGame::Size / 2, GomokuGame::Size / 2);
    for (const QPoint& move : candidates) {
        const int attack = movePotential(board, move, player);
        const int defense = movePotential(board, move, opponent);
        const int centerDistance = std::abs(move.x() - center.x())
            + std::abs(move.y() - center.y());
        const int score = attack * 2 + defense - centerDistance;
        if (score > bestScore) {
            bestScore = score;
            best = move;
        }
    }
    return best;
}

}  // namespace mtb::games
