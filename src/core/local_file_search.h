#pragma once

#include <QList>
#include <QString>

#include <atomic>
#include <memory>

namespace mtb {

struct LocalFileSearchResult {
    QString path;
    bool isDirectory{false};
    bool nameMatched{false};
    bool contentMatched{false};
};

struct LocalFileSearchRequest {
    QString rootPath;
    QString query;
    bool searchNames{true};
    bool searchContents{false};
    int resultLimit{200};
    qsizetype maximumContentBytes{1024 * 1024};
    std::shared_ptr<std::atomic_bool> cancelled;
};

class LocalFileSearch final {
public:
    static constexpr int DefaultResultLimit = 200;
    static constexpr qsizetype DefaultMaximumContentBytes = 1024 * 1024;

    [[nodiscard]] static QList<LocalFileSearchResult> search(
        const LocalFileSearchRequest& request);
};

}  // namespace mtb
