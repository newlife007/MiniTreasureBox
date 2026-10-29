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

bool validOptions(const RemovalOptions& options)
{
    return options.colorDifferenceThreshold >= 0
        && options.colorDifferenceThreshold <= 255
        && options.maximumInkBrightness >= 0
        && options.maximumInkBrightness <= 255
        && options.fillRadius > 0 && options.fillRadius <= 128;
}

QImage normalizedMask(const QImage& mask, const QSize& size)
{
    if (mask.isNull() || mask.size() != size) {
        return {};
    }
    QImage normalized(size, QImage::Format_Grayscale8);
    for (int y = 0; y < size.height(); ++y) {
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
    const QImage& image, const RemovalOptions& options) const
{
    if (image.isNull() || !validOptions(options)) {
        return failure(RemovalError::InvalidInput,
                       QStringLiteral("图片或处理参数无效。原图片未修改。"));
    }
    const QImage mask = proposeMask(image, options);
    const QImage restored = conservativeFill(image, mask, options.fillRadius);
    if (mask.isNull() || restored.isNull()) {
        return failure(RemovalError::ProcessingFailed,
                       QStringLiteral("本地基线处理失败。原图片仍保留，可以重试。"));
    }
    return {true, mask, restored, RemovalError::None,
            QStringLiteral("本地保守基线已生成候选 Mask，请检查并手动修正。")};
}

RemovalResult BaselineHandwritingRemovalEngine::removeWithMask(
    const QImage& image, const QImage& mask, const RemovalOptions& options) const
{
    if (image.isNull() || !validOptions(options)) {
        return failure(RemovalError::InvalidInput,
                       QStringLiteral("图片或处理参数无效。原图片未修改。"));
    }
    const QImage normalized = normalizedMask(mask, image.size());
    if (normalized.isNull()) {
        return failure(RemovalError::InvalidInput,
                       QStringLiteral("Mask 尺寸与原图不一致。原图片未修改。"));
    }
    const QImage restored = conservativeFill(image, normalized, options.fillRadius);
    if (restored.isNull()) {
        return failure(RemovalError::ProcessingFailed,
                       QStringLiteral("本地背景填充失败。原图片仍保留。"));
    }
    return {true, normalized, restored, RemovalError::None,
            QStringLiteral("已按当前 Mask 更新预览。")};
}

QImage BaselineHandwritingRemovalEngine::proposeMask(
    const QImage& image, const RemovalOptions& options)
{
    if (image.isNull() || !validOptions(options)) {
        return {};
    }
    const QImage source = image.convertToFormat(QImage::Format_RGB32);
    QImage seeds(source.size(), QImage::Format_Grayscale8);
    seeds.fill(0);

    for (int y = 0; y < source.height(); ++y) {
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
    const QImage& image, const QImage& mask, int radius)
{
    if (image.isNull() || radius <= 0 || radius > 128) {
        return {};
    }
    const QImage normalized = normalizedMask(mask, image.size());
    if (normalized.isNull()) {
        return {};
    }
    const QImage source = image.convertToFormat(QImage::Format_ARGB32);
    QImage output = source.copy();

    for (int y = 0; y < source.height(); ++y) {
        for (int x = 0; x < source.width(); ++x) {
            if (!selected(normalized, x, y)) {
                continue;
            }
            qint64 red = 0;
            qint64 green = 0;
            qint64 blue = 0;
            int samples = 0;
            const int top = std::max(0, y - radius);
            const int bottom = std::min(source.height() - 1, y + radius);
            const int left = std::max(0, x - radius);
            const int right = std::min(source.width() - 1, x + radius);
            for (int sampleY = top; sampleY <= bottom; ++sampleY) {
                for (int sampleX = left; sampleX <= right; ++sampleX) {
                    if (selected(normalized, sampleX, sampleY)) {
                        continue;
                    }
                    const QRgb candidate = source.pixel(sampleX, sampleY);
                    const int minimum = std::min({qRed(candidate), qGreen(candidate), qBlue(candidate)});
                    const int maximum = std::max({qRed(candidate), qGreen(candidate), qBlue(candidate)});
                    if (qGray(candidate) < 170 || maximum - minimum > 35) {
                        continue;
                    }
                    red += qRed(candidate);
                    green += qGreen(candidate);
                    blue += qBlue(candidate);
                    ++samples;
                }
            }
            const int alpha = qAlpha(source.pixel(x, y));
            output.setPixel(x, y, samples > 0
                ? qRgba(static_cast<int>(red / samples), static_cast<int>(green / samples),
                        static_cast<int>(blue / samples), alpha)
                : qRgba(255, 255, 255, alpha));
        }
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

bool MaskDocument::paint(const QPoint& center, int radius, bool add)
{
    if (!isLoaded() || radius <= 0 || !original_.rect().contains(center)) {
        return false;
    }
    QImage edited = mask_;
    QPainter painter(&edited);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(Qt::NoPen);
    painter.setBrush(add ? Qt::white : Qt::black);
    painter.drawEllipse(center, radius, radius);
    painter.end();
    commit(std::move(edited));
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
    history_.erase(history_.begin() + static_cast<std::ptrdiff_t>(historyIndex_ + 1),
                   history_.end());
    history_.push_back(std::move(mask));
    if (history_.size() > 50) {
        history_.erase(history_.begin());
    }
    historyIndex_ = history_.size() - 1;
    mask_ = history_.back();
}

}  // namespace mtb::handwriting
