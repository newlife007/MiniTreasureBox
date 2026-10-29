#pragma once

#include "core/local_file_search.h"

#include <QUrl>
#include <QWidget>

#include <atomic>
#include <functional>
#include <memory>

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QTimer;

namespace mtb {

class LocalFileSearchWidget final : public QWidget {
    Q_OBJECT

public:
    using UrlLauncher = std::function<bool(const QUrl&)>;

    explicit LocalFileSearchWidget(QWidget* parent = nullptr);
    ~LocalFileSearchWidget() override;

    void setQuery(const QString& query);
    void setRootDirectory(const QString& path);
    void setUrlLauncher(UrlLauncher launcher);
    [[nodiscard]] QList<LocalFileSearchResult> results() const;

signals:
    void resultsChanged();

private:
    void scheduleSearch();
    void startSearch();
    void displayResults(const QList<LocalFileSearchResult>& results);
    void addResultRow(const LocalFileSearchResult& result);
    bool launchPath(const QString& path, bool containingDirectory);

    QLineEdit* rootPath_;
    QCheckBox* searchNames_;
    QCheckBox* searchContents_;
    QLabel* status_;
    QListWidget* resultList_;
    QTimer* debounceTimer_;
    QString query_;
    QList<LocalFileSearchResult> results_;
    std::shared_ptr<std::atomic_bool> cancellation_;
    quint64 generation_{0};
    UrlLauncher urlLauncher_;
};

}  // namespace mtb
