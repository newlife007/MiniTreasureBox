#include "ai/handwriting_removal/removal_engine.h"

#include <QColor>
#include <QPainter>

#include <algorithm>

namespace mtb::handwriting {
namespace {

RemovalResult failure(RemovalError error, const QString& message)
{
    return {false, {}, {}, error, message};
}

bool validImage(const QImage& image)
{
    if (image.isNull() || image.width() <= 0 || image.height() <= 0) return false;
    return static_cast<qint64>(image.width())
        <= MaximumImagePixels / static_cast<qint64>(image.height());
}

bool validOptions(const RemovalOptions& options)
{
    return options.colorDifferenceThreshold >= 0
        && options.colorDifferenceThreshold <= 255
        && options.maximumInkBrightness >= 0
        && options.maximumInkBrightness <= 255
        && options.fillRadius > 0 && options.fillRadius <= 128;
}

QImage normalizedMask(const QImage& mask, const QSize& size,
                      const CancellationCheck& cancelled = {})
{
    if (mask.isNull() || mask.size() != size) {
        return {};
    }
    QImage normalized(size, QImage::Format_Grayscale8);
    for (int y = 0; y < size.height(); ++y) {
        if (cancelled && cancelled()) return {};
        uchar* output = normalized.scanLine(y);
        for (int x = 0; x < size.width(); ++x) {
            output[x] = qGray(mask.pixel(x, y)) >= 128 ? 255 : 0;
        }
    }
    return normalized;
}

bool selected(const QImage& mask, int x, int y)
{
    return mask.constScanLine(y)[x] != 0;
}

}  // namespace

RemovalResult BaselineHandwritingRemovalEngine::detectAndRemove(
    const QImage& image, const RemovalOptions& options,
    const CancellationCheck& cancelled) const
{
    if (!validImage(image) || !validOptions(options)) {
        return failure(RemovalError::InvalidInput,
                       QStringLiteral("图片或处理参数无效。原图片未修改。"));
    }
    const QImage mask = proposeMask(image, options, cancelled);
    if (cancelled && cancelled()) {
        return failure(RemovalError::Cancelled, QStringLiteral("处理已取消。"));
    }
    const QImage restored = conservativeFill(image, mask, options.fillRadius, cancelled);
    if (cancelled && cancelled()) {
        return failure(RemovalError::Cancelled, QStringLiteral("处理已取消。"));
    }
    if (mask.isNull() || restored.isNull()) {
        return failure(RemovalError::ProcessingFailed,
                       QStringLiteral("本地基线处理失败。原图片仍保留，可以重试。"));
    }
    return {true, mask, restored, RemovalError::None,
            QStringLiteral("本地保守基线已生成候选 Mask，请检查并手动修正。")};
}

RemovalResult BaselineHandwritingRemovalEngine::removeWithMask(
    const QImage& image, const QImage& mask, const RemovalOptions& options,
    const CancellationCheck& cancelled) const
{
    if (!validImage(image) || !validOptions(options)) {
        return failure(RemovalError::InvalidInput,
                       QStringLiteral("图片或处理参数无效。原图片未修改。"));
    }
    const QImage normalized = normalizedMask(mask, image.size(), cancelled);
    if (cancelled && cancelled()) {
        return failure(RemovalError::Cancelled, QStringLiteral("处理已取消。"));
    }
    if (normalized.isNull()) {
        return failure(RemovalError::InvalidInput,
                       QStringLiteral("Mask 尺寸与原图不一致。原图片未修改。"));
    }
    const QImage restored = conservativeFill(image, normalized, options.fillRadius, cancelled);
    if (cancelled && cancelled()) {
        return failure(RemovalError::Cancelled, QStringLiteral("处理已取消。"));
    }
    if (restored.isNull()) {
        return failure(RemovalError::ProcessingFailed,
                       QStringLiteral("本地背景填充失败。原图片仍保留。"));
    }
    return {true, normalized, restored, RemovalError::None,
            QStringLiteral("已按当前 Mask 更新预览。")};
}

QImage BaselineHandwritingRemovalEngine::proposeMask(
    const QImage& image, const RemovalOptions& options,
    const CancellationCheck& cancelled)
{
    if (!validImage(image) || !validOptions(options)) {
        return {};
    }
    const QImage source = image.convertToFormat(QImage::Format_RGB32);
    QImage seeds(source.size(), QImage::Format_Grayscale8);
    seeds.fill(0);

    for (int y = 0; y < source.height(); ++y) {
        if (cancelled && cancelled()) return {};
        uchar* target = seeds.scanLine(y);
        for (int x = 0; x < source.width(); ++x) {
            const QRgb pixel = source.pixel(x, y);
            const int minimum = std::min({qRed(pixel), qGreen(pixel), qBlue(pixel)});
            const int maximum = std::max({qRed(pixel), qGreen(pixel), qBlue(pixel)});
            const int brightness = qGray(pixel);
            if (maximum - minimum >= options.colorDifferenceThreshold
                && brightness <= options.maximumInkBrightness) {
                target[x] = 255;
            }
        }
    }

    QImage expanded = seeds;
    for (int y = 0; y < seeds.height(); ++y) {
        if (cancelled && cancelled()) return {};
        for (int x = 0; x < seeds.width(); ++x) {
            if (!selected(seeds, x, y)) {
                continue;
            }
            for (int dy = -1; dy <= 1; ++dy) {
                const int nearbyY = y + dy;
                if (nearbyY < 0 || nearbyY >= expanded.height()) {
                    continue;
                }
                uchar* line = expanded.scanLine(nearbyY);
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nearbyX = x + dx;
                    if (nearbyX >= 0 && nearbyX < expanded.width()) {
                        line[nearbyX] = 255;
                    }
                }
            }
        }
    }
    return expanded;
}

QImage BaselineHandwritingRemovalEngine::conservativeFill(
    const QImage& image, const QImage& mask, int radius,
    const CancellationCheck& cancelled)
{
    if (!validImage(image) || radius <= 0 || radius > 128) {
        return {};
    }
    const QImage normalized = normalizedMask(mask, image.size(), cancelled);
    if (normalized.isNull()) {
        return {};
    }
    const QImage source = image.convertToFormat(QImage::Format_ARGB32);
    QImage output = source.copy();
    const int width = source.width();
    const int height = source.height();
    std::vector<qint64> columnRed(static_cast<std::size_t>(width));
    std::vector<qint64> columnGreen(static_cast<std::size_t>(width));
    std::vector<qint64> columnBlue(static_cast<std::size_t>(width));
    std::vector<int> columnSamples(static_cast<std::size_t>(width));

    const auto updateRow = [&](int y, int sign) {
        if (y < 0 || y >= height) return;
        const QRgb* pixels = reinterpret_cast<const QRgb*>(source.constScanLine(y));
        const uchar* maskLine = normalized.constScanLine(y);
        for (int x = 0; x < width; ++x) {
            if (maskLine[x]) continue;
            const QRgb candidate = pixels[x];
            const int minimum = std::min({qRed(candidate), qGreen(candidate), qBlue(candidate)});
            const int maximum = std::max({qRed(candidate), qGreen(candidate), qBlue(candidate)});
            if (qGray(candidate) < 170 || maximum - minimum > 35) continue;
            const std::size_t index = static_cast<std::size_t>(x);
            columnRed[index] += sign * qRed(candidate);
            columnGreen[index] += sign * qGreen(candidate);
            columnBlue[index] += sign * qBlue(candidate);
            columnSamples[index] += sign;
        }
    };

    for (int y = 0; y <= std::min(radius, height - 1); ++y) updateRow(y, 1);
    for (int y = 0; y < height; ++y) {
        if (cancelled && cancelled()) return {};
        qint64 red = 0;
        qint64 green = 0;
        qint64 blue = 0;
        int samples = 0;
        for (int x = 0; x <= std::min(radius, width - 1); ++x) {
            const std::size_t index = static_cast<std::size_t>(x);
            red += columnRed[index];
            green += columnGreen[index];
            blue += columnBlue[index];
            samples += columnSamples[index];
        }
        const QRgb* sourceLine = reinterpret_cast<const QRgb*>(source.constScanLine(y));
        const uchar* maskLine = normalized.constScanLine(y);
        QRgb* outputLine = reinterpret_cast<QRgb*>(output.scanLine(y));
        for (int x = 0; x < width; ++x) {
            if (maskLine[x]) {
                const int alpha = qAlpha(sourceLine[x]);
                outputLine[x] = samples > 0
                    ? qRgba(static_cast<int>(red / samples), static_cast<int>(green / samples),
                            static_cast<int>(blue / samples), alpha)
                    : qRgba(255, 255, 255, alpha);
            }
            const int leaving = x - radius;
            if (leaving >= 0) {
                const std::size_t index = static_cast<std::size_t>(leaving);
                red -= columnRed[index];
                green -= columnGreen[index];
                blue -= columnBlue[index];
                samples -= columnSamples[index];
            }
            const int entering = x + radius + 1;
            if (entering < width) {
                const std::size_t index = static_cast<std::size_t>(entering);
                red += columnRed[index];
                green += columnGreen[index];
                blue += columnBlue[index];
                samples += columnSamples[index];
            }
        }
        updateRow(y - radius, -1);
        updateRow(y + radius + 1, 1);
    }
    return output;
}

bool MaskDocument::load(const QImage& image)
{
    if (image.isNull()) {
        return false;
    }
    original_ = image.convertToFormat(QImage::Format_ARGB32).copy();
    mask_ = QImage(original_.size(), QImage::Format_Grayscale8);
    mask_.fill(0);
    history_.clear();
    history_.push_back(mask_);
    historyIndex_ = 0;
    editActive_ = false;
    editChanged_ = false;
    return true;
}

bool MaskDocument::setMask(const QImage& mask)
{
    if (!isLoaded()) {
        return false;
    }
    const QImage normalized = normalizedMask(mask, original_.size());
    if (normalized.isNull()) {
        return false;
    }
    commit(normalized);
    return true;
}

bool MaskDocument::beginEdit()
{
    if (!isLoaded() || editActive_) return false;
    editActive_ = true;
    editChanged_ = false;
    return true;
}

bool MaskDocument::endEdit()
{
    if (!editActive_) return false;
    editActive_ = false;
    if (!editChanged_) return false;
    appendCurrentToHistory();
    editChanged_ = false;
    return true;
}

bool MaskDocument::paint(const QPoint& center, int radius, bool add)
{
    if (!isLoaded() || radius <= 0 || !original_.rect().contains(center)) {
        return false;
    }
    const QRect changedArea = QRect(center - QPoint(radius, radius),
                                    QSize(radius * 2 + 1, radius * 2 + 1))
                                  .intersected(original_.rect());
    const QImage before = mask_.copy(changedArea);
    QPainter painter(&mask_);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(Qt::NoPen);
    painter.setBrush(add ? Qt::white : Qt::black);
    painter.drawEllipse(center, radius, radius);
    painter.end();
    if (mask_.copy(changedArea) == before) return false;
    if (editActive_) {
        editChanged_ = true;
    } else {
        appendCurrentToHistory();
    }
    return true;
}

bool MaskDocument::fillRectangle(const QRect& rectangle, bool add)
{
    if (!isLoaded()) {
        return false;
    }
    const QRect bounded = rectangle.normalized().intersected(original_.rect());
    if (bounded.isEmpty()) {
        return false;
    }
    QImage edited = mask_;
    QPainter painter(&edited);
    painter.fillRect(bounded, add ? Qt::white : Qt::black);
    painter.end();
    commit(std::move(edited));
    return true;
}

bool MaskDocument::undo()
{
    if (!canUndo()) {
        return false;
    }
    --historyIndex_;
    mask_ = history_[historyIndex_];
    return true;
}

bool MaskDocument::redo()
{
    if (!canRedo()) {
        return false;
    }
    ++historyIndex_;
    mask_ = history_[historyIndex_];
    return true;
}

void MaskDocument::clearMask()
{
    if (!isLoaded()) {
        return;
    }
    QImage empty(original_.size(), QImage::Format_Grayscale8);
    empty.fill(0);
    commit(std::move(empty));
}

void MaskDocument::commit(QImage mask)
{
    if (mask == mask_) {
        return;
    }
    mask_ = std::move(mask);
    appendCurrentToHistory();
}

void MaskDocument::appendCurrentToHistory()
{
    history_.erase(history_.begin() + static_cast<std::ptrdiff_t>(historyIndex_ + 1),
                   history_.end());
    history_.push_back(mask_);
    historyIndex_ = history_.size() - 1;
    enforceHistoryBudget();
}

qint64 MaskDocument::historyBytes() const
{
    qint64 bytes = 0;
    for (const QImage& image : history_) bytes += image.sizeInBytes();
    return bytes;
}

void MaskDocument::enforceHistoryBudget()
{
    while (history_.size() > 1 && historyBytes() > HistoryByteLimit) {
        history_.erase(history_.begin());
        --historyIndex_;
    }
}

}  // namespace mtb::handwriting
