#include "core/local_file_search.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QSet>

#include <algorithm>

namespace mtb {
namespace {

bool isCancelled(const std::shared_ptr<std::atomic_bool>& cancelled)
{
    return cancelled && cancelled->load(std::memory_order_relaxed);
}

bool isPathInside(const QString& rootPath, const QString& candidatePath)
{
    const QString relative = QDir::fromNativeSeparators(
        QDir(rootPath).relativeFilePath(candidatePath));
    return relative == QStringLiteral(".")
        || (!QDir::isAbsolutePath(relative) && relative != QStringLiteral("..")
            && !relative.startsWith(QStringLiteral("../")));
}

bool isSameOrDescendant(const QString& parentPath, const QString& candidatePath)
{
    return isPathInside(parentPath, candidatePath);
}

void appendRecord(LocalFileSearchIndex& index, const QFileInfo& info)
{
    const bool isDirectory = info.isDir();
    index.records.append({QDir::cleanPath(info.absoluteFilePath()),
                          QDir::fromNativeSeparators(
                              QDir(index.rootPath).relativeFilePath(
                                  info.absoluteFilePath())),
                          isDirectory});
    if (isDirectory) {
        ++index.stats.directoryCount;
    } else {
        ++index.stats.fileCount;
    }
}

bool scanPath(LocalFileSearchIndex& index, const QString& scanPath, bool includeScanPath,
              const std::shared_ptr<std::atomic_bool>& cancelled)
{
    const QFileInfo scanInfo(scanPath);
    if (scanInfo.isSymLink() || (!scanInfo.isFile() && !scanInfo.isDir())) {
        return true;
    }

    if (includeScanPath) {
        appendRecord(index, scanInfo);
    }
    if (!scanInfo.isDir()) {
        return true;
    }

    index.watchedDirectories.append(QDir::cleanPath(scanInfo.absoluteFilePath()));
    QDirIterator iterator(scanPath,
                          QDir::AllEntries | QDir::Hidden | QDir::System
                              | QDir::NoDotAndDotDot,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        if (isCancelled(cancelled)) {
            return false;
        }

        iterator.next();
        const QFileInfo info = iterator.fileInfo();
        if (info.isSymLink() || (!info.isFile() && !info.isDir())) {
            continue;
        }
        appendRecord(index, info);
        if (info.isDir()) {
            index.watchedDirectories.append(
                QDir::cleanPath(info.absoluteFilePath()));
        }
    }
    return !isCancelled(cancelled);
}

void normalizeIndex(LocalFileSearchIndex& index)
{
    std::sort(index.records.begin(), index.records.end(),
              [](const auto& left, const auto& right) {
                  return left.relativePath.compare(right.relativePath,
                                                   Qt::CaseInsensitive)
                      < 0;
              });
    index.watchedDirectories.removeDuplicates();
    std::sort(index.watchedDirectories.begin(), index.watchedDirectories.end(),
              [](const QString& left, const QString& right) {
                  return left.compare(right, Qt::CaseInsensitive) < 0;
              });
}

LocalFileSearchIndexResult failure(const QString& message, bool cancelled = false)
{
    return {{}, false, cancelled, message};
}

}  // namespace

LocalFileSearchIndexResult LocalFileSearch::buildIndex(
    const QString& rootPath, const std::shared_ptr<std::atomic_bool>& cancelled)
{
    const QString normalized = QDir::cleanPath(rootPath);
    const QFileInfo root(normalized);
    if (!QDir::isAbsolutePath(normalized) || !root.exists() || !root.isDir()
        || root.isSymLink()) {
        return failure(QStringLiteral("The selected search root is invalid."));
    }
    if (isCancelled(cancelled)) {
        return failure(QStringLiteral("Indexing was cancelled."), true);
    }

    LocalFileSearchIndex index;
    index.rootPath = normalized;
    if (!scanPath(index, normalized, false, cancelled)) {
        return failure(QStringLiteral("Indexing was cancelled."), true);
    }
    normalizeIndex(index);
    index.ready = true;
    return {std::move(index), true, false, {}};
}

LocalFileSearchIndexResult LocalFileSearch::refreshSubtree(
    const LocalFileSearchIndex& currentIndex, const QString& changedPath,
    const std::shared_ptr<std::atomic_bool>& cancelled)
{
    if (!currentIndex.ready) {
        return failure(QStringLiteral("The current index is not ready."));
    }

    const QFileInfo root(currentIndex.rootPath);
    if (!root.exists() || !root.isDir() || root.isSymLink()) {
        return failure(QStringLiteral("The indexed search root is no longer available."));
    }

    const QString normalized = QDir::cleanPath(changedPath);
    if (!QDir::isAbsolutePath(normalized)
        || !isPathInside(currentIndex.rootPath, normalized)) {
        return failure(QStringLiteral("The changed path is outside the indexed root."));
    }
    if (isCancelled(cancelled)) {
        return failure(QStringLiteral("Index update was cancelled."), true);
    }

    LocalFileSearchIndex updated = currentIndex;
    updated.ready = false;
    updated.records.erase(
        std::remove_if(updated.records.begin(), updated.records.end(),
                       [&normalized](const LocalFileSearchRecord& record) {
                           return isSameOrDescendant(normalized, record.path);
                       }),
        updated.records.end());
    updated.watchedDirectories.erase(
        std::remove_if(updated.watchedDirectories.begin(),
                       updated.watchedDirectories.end(),
                       [&normalized](const QString& directory) {
                           return isSameOrDescendant(normalized, directory);
                       }),
        updated.watchedDirectories.end());

    updated.stats = {};
    for (const LocalFileSearchRecord& record : updated.records) {
        if (record.isDirectory) {
            ++updated.stats.directoryCount;
        } else {
            ++updated.stats.fileCount;
        }
    }

    const QFileInfo changed(normalized);
    if (changed.exists() && !changed.isSymLink()
        && (changed.isFile() || changed.isDir())) {
        const bool includeChangedPath = normalized != currentIndex.rootPath;
        if (!scanPath(updated, normalized, includeChangedPath, cancelled)) {
            return failure(QStringLiteral("Index update was cancelled."), true);
        }
    } else if (normalized == currentIndex.rootPath) {
        return failure(QStringLiteral("The indexed search root is no longer available."));
    }

    if (isCancelled(cancelled)) {
        return failure(QStringLiteral("Index update was cancelled."), true);
    }
    normalizeIndex(updated);
    updated.ready = true;
    return {std::move(updated), true, false, {}};
}

QList<LocalFileSearchResult> LocalFileSearch::search(
    const LocalFileSearchIndex& index, const QString& query, int resultLimit,
    const std::shared_ptr<std::atomic_bool>& cancelled)
{
    QList<LocalFileSearchResult> results;
    const QString normalizedQuery = query.trimmed();
    const int boundedLimit = std::clamp(resultLimit, 0, DefaultResultLimit);
    if (!index.ready || normalizedQuery.isEmpty() || boundedLimit == 0
        || isCancelled(cancelled)) {
        return results;
    }

    results.reserve(std::min<qsizetype>(boundedLimit, index.records.size()));
    for (const LocalFileSearchRecord& record : index.records) {
        if (isCancelled(cancelled) || results.size() >= boundedLimit) {
            break;
        }
        if (record.relativePath.contains(normalizedQuery, Qt::CaseInsensitive)) {
            results.append({record.path, record.isDirectory, true});
        }
    }
    return results;
}

}  // namespace mtb
