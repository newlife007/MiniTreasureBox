#pragma once

#include "ai/handwriting_removal/removal_engine.h"

#include <QString>

namespace mtb::handwriting {

struct RestorationRuntimeConfig {
    QString workerExecutable;
    QString modelRoot;
    QString temporaryRoot;
    int timeoutMs{120000};
};

struct RestorationRequest {
    QImage correctedImage;
    RemovalOptions options;
    QString requestId;
    ProgressCallback progress;
};

class RestorationWorkerEngine final : public IHandwritingRemovalEngine {
public:
    explicit RestorationWorkerEngine(RestorationRuntimeConfig config = {});

    [[nodiscard]] RemovalResult restore(
        const RestorationRequest& request,
        const CancellationCheck& cancelled = {}) const;

    [[nodiscard]] RemovalResult detectAndRemove(
        const QImage& image, const RemovalOptions& options = {},
        const CancellationCheck& cancelled = {}) const override;
    [[nodiscard]] RemovalResult detectAndRemoveWithProgress(
        const QImage& image, const RemovalOptions& options,
        const ProgressCallback& progress,
        const CancellationCheck& cancelled = {}) const override;
    [[nodiscard]] RemovalResult removeWithMask(
        const QImage& image, const QImage& mask,
        const RemovalOptions& options = {},
        const CancellationCheck& cancelled = {}) const override;

    [[nodiscard]] static RestorationRuntimeConfig resolvedConfig(
        const RestorationRuntimeConfig& config,
        const QString& applicationDirPath = {});
    static void cleanupStaleTasks(const QString& temporaryRoot = {});

private:
    RestorationRuntimeConfig config_;
    BaselineHandwritingRemovalEngine baseline_;
};

}  // namespace mtb::handwriting
