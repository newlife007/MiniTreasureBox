#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <atomic>
#include <memory>

namespace mtb {

struct LocalFileSearchResult {
    QString path;
    bool isDirectory{false};
    bool nameMatched{true};
};

struct LocalFileSearchRecord {
    QString path;
    QString relativePath;
    bool isDirectory{false};
};

struct LocalFileSearchIndexStats {
    qsizetype fileCount{0};
    qsizetype directoryCount{0};

    [[nodiscard]] qsizetype recordCount() const
    {
        return fileCount + directoryCount;
    }
};

struct LocalFileSearchIndex {
    QString rootPath;
    QList<LocalFileSearchRecord> records;
    QStringList watchedDirectories;
    LocalFileSearchIndexStats stats;
    bool ready{false};
};

struct LocalFileSearchIndexResult {
    LocalFileSearchIndex index;
    bool success{false};
    bool cancelled{false};
    QString error;
};

class LocalFileSearch final {
public:
    static constexpr int DefaultResultLimit = 200;

    [[nodiscard]] static LocalFileSearchIndexResult buildIndex(
        const QString& rootPath,
        const std::shared_ptr<std::atomic_bool>& cancelled = {});
    [[nodiscard]] static LocalFileSearchIndexResult refreshSubtree(
        const LocalFileSearchIndex& currentIndex,
        const QString& changedPath,
        const std::shared_ptr<std::atomic_bool>& cancelled = {});
    [[nodiscard]] static QList<LocalFileSearchResult> search(
        const LocalFileSearchIndex& index,
        const QString& query,
        int resultLimit = DefaultResultLimit,
        const std::shared_ptr<std::atomic_bool>& cancelled = {});
};

}  // namespace mtb
