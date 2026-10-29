#include "core/local_file_search.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStringDecoder>

namespace mtb {
namespace {

bool isCancelled(const LocalFileSearchRequest& request)
{
    return request.cancelled && request.cancelled->load(std::memory_order_relaxed);
}

bool contentMatches(const QFileInfo& info, const QString& query, qsizetype maximumBytes)
{
    constexpr auto readPermissions = QFileDevice::ReadOwner | QFileDevice::ReadUser
        | QFileDevice::ReadGroup | QFileDevice::ReadOther;
    if (!info.isReadable() || !(info.permissions() & readPermissions)
        || info.size() < 0 || info.size() > maximumBytes) {
        return false;
    }

    QFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    const QByteArray bytes = file.read(maximumBytes + 1);
    if (bytes.size() > maximumBytes || bytes.contains('\0')) {
        return false;
    }

    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString text = decoder.decode(bytes);
    return !decoder.hasError() && text.contains(query, Qt::CaseInsensitive);
}

}  // namespace

QList<LocalFileSearchResult> LocalFileSearch::search(const LocalFileSearchRequest& request)
{
    QList<LocalFileSearchResult> results;
    const QString query = request.query.trimmed();
    const QString rootPath = QDir::cleanPath(request.rootPath);
    const QFileInfo root(rootPath);
    if (query.isEmpty() || request.resultLimit <= 0 || request.maximumContentBytes < 0
        || !QDir::isAbsolutePath(rootPath) || !root.exists() || !root.isDir()
        || root.isSymLink() || (!request.searchNames && !request.searchContents)
        || isCancelled(request)) {
        return results;
    }

    QDirIterator iterator(rootPath,
                          QDir::AllEntries | QDir::Hidden | QDir::System
                              | QDir::NoDotAndDotDot,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext() && results.size() < request.resultLimit) {
        if (isCancelled(request)) {
            break;
        }

        iterator.next();
        const QFileInfo info = iterator.fileInfo();
        if (info.isSymLink() || (!info.isFile() && !info.isDir())) {
            continue;
        }

        const bool nameMatched = request.searchNames
            && info.fileName().contains(query, Qt::CaseInsensitive);
        const bool contentMatched = request.searchContents && info.isFile()
            && contentMatches(info, query, request.maximumContentBytes);
        if (nameMatched || contentMatched) {
            results.append({info.absoluteFilePath(), info.isDir(), nameMatched,
                            contentMatched});
        }
    }
    return results;
}

}  // namespace mtb
