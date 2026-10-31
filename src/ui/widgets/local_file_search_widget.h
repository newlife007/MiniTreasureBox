#pragma once

#include "core/local_file_search.h"

#include <QFuture>
#include <QSet>
#include <QUrl>
#include <QWidget>

#include <atomic>
#include <functional>
#include <memory>

class QFileSystemWatcher;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTimer;

namespace mtb {

class LocalFileSearchWidget final : public QWidget {
    Q_OBJECT

public:
    using IndexBuilder = std::function<QFuture<LocalFileSearchIndexResult>(
        const QString&, const std::shared_ptr<std::atomic_bool>&)>;
    using IndexUpdater = std::function<QFuture<LocalFileSearchIndexResult>(
        const LocalFileSearchIndex&, const QStringList&,
        const std::shared_ptr<std::atomic_bool>&)>;
    using SearchStarter = std::function<QFuture<QList<LocalFileSearchResult>>(
        const LocalFileSearchIndex&, const QString&, int,
        const std::shared_ptr<std::atomic_bool>&)>;
    using UrlLauncher = std::function<bool(const QUrl&)>;

    explicit LocalFileSearchWidget(QWidget* parent = nullptr);
    explicit LocalFileSearchWidget(IndexBuilder indexBuilder,
                                   QWidget* parent = nullptr);
    ~LocalFileSearchWidget() override;

    void setQuery(const QString& query);
    void setRootDirectory(const QString& path);
    void rebuildIndex();
    void setIndexBuilder(IndexBuilder indexBuilder);
    void setIndexUpdater(IndexUpdater indexUpdater);
    void setSearchStarter(SearchStarter searchStarter);
    void setUrlLauncher(UrlLauncher launcher);
    [[nodiscard]] QList<LocalFileSearchResult> results() const;
    [[nodiscard]] bool isIndexReady() const;
    [[nodiscard]] LocalFileSearchIndexStats indexStats() const;

signals:
    void resultsChanged();
    void indexStateChanged();

private slots:
    void queueChangedPath(const QString& path);
    void applyPendingUpdates();

private:
    void beginIndexBuild();
    [[nodiscard]] bool configureWatcher();
    void scheduleSearch();
    void startSearch();
    void displayResults(const QList<LocalFileSearchResult>& results);
    void addResultRow(const LocalFileSearchResult& result);
    void showReadyStatus();
    [[nodiscard]] QString watcherWarning() const;
    bool launchPath(const QString& path, bool containingDirectory);

    QLineEdit* rootPath_;
    QPushButton* rebuildButton_;
    QLabel* status_;
    QListWidget* resultList_;
    QTimer* debounceTimer_;
    QTimer* updateTimer_;
    QFileSystemWatcher* fileWatcher_;
    QString query_;
    QList<LocalFileSearchResult> results_;
    LocalFileSearchIndex index_;
    QSet<QString> pendingChangedPaths_;
    std::shared_ptr<std::atomic_bool> indexCancellation_;
    std::shared_ptr<std::atomic_bool> searchCancellation_;
    quint64 indexGeneration_{0};
    quint64 searchGeneration_{0};
    bool indexUpdateInProgress_{false};
    bool watchRegistrationComplete_{false};
    QStringList activeUpdatePaths_;
    QSet<QString> recentlyRefreshedPaths_;
    IndexBuilder indexBuilder_;
    IndexUpdater indexUpdater_;
    SearchStarter searchStarter_;
    UrlLauncher urlLauncher_;
};

}  // namespace mtb
