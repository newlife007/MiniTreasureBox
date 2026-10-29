#pragma once

#include <QImage>
#include <QPoint>
#include <QRect>
#include <QString>

#include <functional>
#include <vector>

namespace mtb::handwriting {

enum class RemovalError {
    None,
    InvalidInput,
    ProcessingFailed,
    Cancelled,
};

using CancellationCheck = std::function<bool()>;

struct RemovalOptions {
    int colorDifferenceThreshold{55};
    int maximumInkBrightness{210};
    int fillRadius{8};
};

struct RemovalResult {
    bool ok{false};
    QImage mask;
    QImage restored;
    RemovalError error{RemovalError::None};
    QString message;
};

class IHandwritingRemovalEngine {
public:
    virtual ~IHandwritingRemovalEngine() = default;
    [[nodiscard]] virtual RemovalResult detectAndRemove(
        const QImage& image, const RemovalOptions& options = {},
        const CancellationCheck& cancelled = {}) const = 0;
    [[nodiscard]] virtual RemovalResult removeWithMask(
        const QImage& image, const QImage& mask,
        const RemovalOptions& options = {},
        const CancellationCheck& cancelled = {}) const = 0;
};

// A deterministic color-contrast heuristic. This is a conservative local baseline,
// not a trained handwriting model. A packaged model can replace it through the interface.
class BaselineHandwritingRemovalEngine final : public IHandwritingRemovalEngine {
public:
    [[nodiscard]] RemovalResult detectAndRemove(
        const QImage& image, const RemovalOptions& options = {},
        const CancellationCheck& cancelled = {}) const override;
    [[nodiscard]] RemovalResult removeWithMask(
        const QImage& image, const QImage& mask,
        const RemovalOptions& options = {},
        const CancellationCheck& cancelled = {}) const override;

    [[nodiscard]] static QImage proposeMask(
        const QImage& image, const RemovalOptions& options = {},
        const CancellationCheck& cancelled = {});
    [[nodiscard]] static QImage conservativeFill(
        const QImage& image, const QImage& mask, int radius = 8,
        const CancellationCheck& cancelled = {});
};

class MaskDocument {
public:
    static constexpr qint64 HistoryByteLimit = 64LL * 1024 * 1024;

    [[nodiscard]] bool load(const QImage& image);
    [[nodiscard]] bool setMask(const QImage& mask);
    [[nodiscard]] bool beginEdit();
    [[nodiscard]] bool endEdit();
    [[nodiscard]] bool paint(const QPoint& center, int radius, bool add);
    [[nodiscard]] bool fillRectangle(const QRect& rectangle, bool add);
    [[nodiscard]] bool undo();
    [[nodiscard]] bool redo();
    void clearMask();

    [[nodiscard]] bool isLoaded() const { return !original_.isNull(); }
    [[nodiscard]] const QImage& original() const { return original_; }
    [[nodiscard]] const QImage& mask() const { return mask_; }
    [[nodiscard]] bool canUndo() const { return historyIndex_ > 0; }
    [[nodiscard]] bool canRedo() const { return historyIndex_ + 1 < history_.size(); }
    [[nodiscard]] std::size_t historySize() const { return history_.size(); }
    [[nodiscard]] qint64 historyBytes() const;

private:
    void commit(QImage mask);
    void appendCurrentToHistory();
    void enforceHistoryBudget();

    QImage original_;
    QImage mask_;
    std::vector<QImage> history_;
    std::size_t historyIndex_{0};
    bool editActive_{false};
    bool editChanged_{false};
};

}  // namespace mtb::handwriting
