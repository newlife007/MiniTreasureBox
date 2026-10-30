#include "ui/widgets/local_file_search_widget.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrentRun>

#include <algorithm>
#include <utility>

namespace mtb {

LocalFileSearchWidget::LocalFileSearchWidget(QWidget* parent)
    : QWidget(parent)
    , rootPath_(new QLineEdit(this))
    , rebuildButton_(new QPushButton(tr("重建索引"), this))
    , status_(new QLabel(this))
    , resultList_(new QListWidget(this))
    , debounceTimer_(new QTimer(this))
    , updateTimer_(new QTimer(this))
    , fileWatcher_(new QFileSystemWatcher(this))
    , indexBuilder_([](const QString& root,
                       const std::shared_ptr<std::atomic_bool>& cancelled) {
        return QtConcurrent::run(
            [root, cancelled] { return LocalFileSearch::buildIndex(root, cancelled); });
    })
    , indexUpdater_([](const LocalFileSearchIndex& current, const QStringList& paths,
                       const std::shared_ptr<std::atomic_bool>& cancelled) {
        return QtConcurrent::run([current, paths, cancelled] {
            LocalFileSearchIndexResult result{current, true, false, {}};
            for (const QString& path : paths) {
                if (cancelled && cancelled->load(std::memory_order_relaxed)) {
                    return LocalFileSearchIndexResult{
                        {}, false, true, QStringLiteral("Index update was cancelled.")};
                }
                result = LocalFileSearch::refreshSubtree(result.index, path, cancelled);
                if (!result.success) {
                    return result;
                }
            }
            return result;
        });
    })
    , searchStarter_([](const LocalFileSearchIndex& index, const QString& query,
                        int limit,
                        const std::shared_ptr<std::atomic_bool>& cancelled) {
        return QtConcurrent::run([index, query, limit, cancelled] {
            return LocalFileSearch::search(index, query, limit, cancelled);
        });
    })
    , urlLauncher_([](const QUrl& url) { return QDesktopServices::openUrl(url); })
{
    setObjectName(QStringLiteral("localFileSearchWidget"));
    rootPath_->setObjectName(QStringLiteral("localSearchRoot"));
    rebuildButton_->setObjectName(QStringLiteral("rebuildSearchIndex"));
    status_->setObjectName(QStringLiteral("localSearchStatus"));
    resultList_->setObjectName(QStringLiteral("localSearchResults"));

    rootPath_->setReadOnly(true);
    rootPath_->setAccessibleName(tr("本地搜索目录"));
    rebuildButton_->setAccessibleDescription(tr("重新扫描所选目录并建立文件名索引"));
    status_->setWordWrap(true);
    status_->setAccessibleName(tr("本地文件搜索状态"));
    resultList_->setAccessibleName(tr("本地文件搜索结果"));
    debounceTimer_->setSingleShot(true);
    debounceTimer_->setInterval(250);
    updateTimer_->setSingleShot(true);
    updateTimer_->setInterval(200);

    auto* chooseButton = new QPushButton(tr("选择目录"), this);
    chooseButton->setObjectName(QStringLiteral("chooseSearchRoot"));
    chooseButton->setAccessibleDescription(tr("选择本地文件搜索的根目录"));

    auto* rootLayout = new QHBoxLayout;
    rootLayout->addWidget(rootPath_, 1);
    rootLayout->addWidget(chooseButton);
    rootLayout->addWidget(rebuildButton_);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(rootLayout);
    layout->addWidget(status_);
    layout->addWidget(resultList_, 1);

    connect(chooseButton, &QPushButton::clicked, this, [this] {
        const QString selected = QFileDialog::getExistingDirectory(
            this, tr("选择搜索目录"), rootPath_->text(), QFileDialog::ShowDirsOnly);
        if (!selected.isEmpty()) {
            setRootDirectory(selected);
        }
    });
    connect(rebuildButton_, &QPushButton::clicked, this,
            &LocalFileSearchWidget::rebuildIndex);
    connect(debounceTimer_, &QTimer::timeout, this,
            &LocalFileSearchWidget::startSearch);
    connect(updateTimer_, &QTimer::timeout, this,
            &LocalFileSearchWidget::applyPendingUpdates);
    connect(fileWatcher_, &QFileSystemWatcher::directoryChanged, this,
            &LocalFileSearchWidget::queueChangedPath);
    connect(fileWatcher_, &QFileSystemWatcher::fileChanged, this,
            [this](const QString& path) {
                queueChangedPath(QFileInfo(path).absolutePath());
            });

    QString initialRoot = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    if (initialRoot.isEmpty()) {
        initialRoot = QDir::homePath();
    }
    setRootDirectory(initialRoot);
}

LocalFileSearchWidget::~LocalFileSearchWidget()
{
    if (indexCancellation_) {
        indexCancellation_->store(true, std::memory_order_relaxed);
    }
    if (searchCancellation_) {
        searchCancellation_->store(true, std::memory_order_relaxed);
    }
}

void LocalFileSearchWidget::setQuery(const QString& query)
{
    if (query_ == query) {
        return;
    }
    query_ = query;
    scheduleSearch();
}

void LocalFileSearchWidget::setRootDirectory(const QString& path)
{
    const QString normalized = QDir::cleanPath(path);
    const QFileInfo info(normalized);
    if (!QDir::isAbsolutePath(normalized) || !info.exists() || !info.isDir()
        || info.isSymLink()) {
        status_->setText(tr("请选择有效的本地目录。"));
        return;
    }
    if (rootPath_->text() == normalized && (index_.ready || indexCancellation_)) {
        return;
    }
    rootPath_->setText(normalized);
    beginIndexBuild();
}

void LocalFileSearchWidget::rebuildIndex()
{
    if (!rootPath_->text().isEmpty()) {
        beginIndexBuild();
    }
}

void LocalFileSearchWidget::setIndexBuilder(IndexBuilder indexBuilder)
{
    indexBuilder_ = std::move(indexBuilder);
}

void LocalFileSearchWidget::setIndexUpdater(IndexUpdater indexUpdater)
{
    indexUpdater_ = std::move(indexUpdater);
}

void LocalFileSearchWidget::setSearchStarter(SearchStarter searchStarter)
{
    searchStarter_ = std::move(searchStarter);
}

void LocalFileSearchWidget::setUrlLauncher(UrlLauncher launcher)
{
    urlLauncher_ = std::move(launcher);
}

QList<LocalFileSearchResult> LocalFileSearchWidget::results() const
{
    return results_;
}

bool LocalFileSearchWidget::isIndexReady() const
{
    return index_.ready;
}

LocalFileSearchIndexStats LocalFileSearchWidget::indexStats() const
{
    return index_.stats;
}

void LocalFileSearchWidget::beginIndexBuild()
{
    ++indexGeneration_;
    ++searchGeneration_;
    if (indexCancellation_) {
        indexCancellation_->store(true, std::memory_order_relaxed);
    }
    if (searchCancellation_) {
        searchCancellation_->store(true, std::memory_order_relaxed);
    }
    debounceTimer_->stop();
    updateTimer_->stop();
    pendingChangedPaths_.clear();
    if (!fileWatcher_->directories().isEmpty()) {
        fileWatcher_->removePaths(fileWatcher_->directories());
    }
    if (!fileWatcher_->files().isEmpty()) {
        fileWatcher_->removePaths(fileWatcher_->files());
    }
    index_ = {};
    watchRegistrationComplete_ = false;
    results_.clear();
    resultList_->clear();
    status_->setText(tr("正在后台建立文件名和路径索引…"));
    emit resultsChanged();
    emit indexStateChanged();

    const quint64 generation = indexGeneration_;
    indexCancellation_ = std::make_shared<std::atomic_bool>(false);
    auto* watcher = new QFutureWatcher<LocalFileSearchIndexResult>(this);
    connect(watcher, &QFutureWatcher<LocalFileSearchIndexResult>::finished, this,
            [this, watcher, generation] {
                const LocalFileSearchIndexResult result = watcher->result();
                watcher->deleteLater();
                if (generation != indexGeneration_) {
                    return;
                }
                indexCancellation_.reset();
                if (!result.success) {
                    status_->setText(result.cancelled
                                         ? tr("索引建立已取消。")
                                         : tr("索引建立失败，请检查目录后手动重建。"));
                    emit indexStateChanged();
                    return;
                }
                index_ = result.index;
                configureWatcher();
                emit indexStateChanged();
                scheduleSearch();
            });
    watcher->setFuture(indexBuilder_(rootPath_->text(), indexCancellation_));
}

void LocalFileSearchWidget::configureWatcher()
{
    const QStringList oldDirectories = fileWatcher_->directories();
    if (!oldDirectories.isEmpty()) {
        fileWatcher_->removePaths(oldDirectories);
    }
    const QStringList oldFiles = fileWatcher_->files();
    if (!oldFiles.isEmpty()) {
        fileWatcher_->removePaths(oldFiles);
    }

    const QStringList failed = index_.watchedDirectories.isEmpty()
        ? QStringList{index_.rootPath}
        : fileWatcher_->addPaths(index_.watchedDirectories);
    watchRegistrationComplete_ = failed.isEmpty()
        && fileWatcher_->directories().size() == index_.watchedDirectories.size();
}

void LocalFileSearchWidget::queueChangedPath(const QString& path)
{
    if (!index_.ready || path.isEmpty()) {
        return;
    }
    pendingChangedPaths_.insert(QDir::cleanPath(path));
    updateTimer_->start();
}

void LocalFileSearchWidget::applyPendingUpdates()
{
    if (!index_.ready || pendingChangedPaths_.isEmpty()) {
        return;
    }

    QStringList paths(pendingChangedPaths_.cbegin(), pendingChangedPaths_.cend());
    pendingChangedPaths_.clear();
    std::sort(paths.begin(), paths.end(), [](const QString& left, const QString& right) {
        return left.size() < right.size();
    });
    QStringList coalesced;
    for (const QString& path : paths) {
        const bool covered = std::any_of(
            coalesced.cbegin(), coalesced.cend(), [&path](const QString& parent) {
                const QString relative = QDir::fromNativeSeparators(
                    QDir(parent).relativeFilePath(path));
                return relative == QStringLiteral(".")
                    || (!QDir::isAbsolutePath(relative)
                        && relative != QStringLiteral("..")
                        && !relative.startsWith(QStringLiteral("../")));
            });
        if (!covered) {
            coalesced.append(path);
        }
    }

    const quint64 generation = ++indexGeneration_;
    ++searchGeneration_;
    if (indexCancellation_) {
        indexCancellation_->store(true, std::memory_order_relaxed);
    }
    if (searchCancellation_) {
        searchCancellation_->store(true, std::memory_order_relaxed);
    }
    debounceTimer_->stop();
    status_->setText(tr("正在后台更新文件索引…"));
    indexCancellation_ = std::make_shared<std::atomic_bool>(false);

    auto* watcher = new QFutureWatcher<LocalFileSearchIndexResult>(this);
    connect(watcher, &QFutureWatcher<LocalFileSearchIndexResult>::finished, this,
            [this, watcher, generation] {
                const LocalFileSearchIndexResult result = watcher->result();
                watcher->deleteLater();
                if (generation != indexGeneration_) {
                    return;
                }
                indexCancellation_.reset();
                if (!result.success) {
                    watchRegistrationComplete_ = false;
                    status_->setText(tr("文件索引更新失败，请使用“重建索引”重新扫描。"));
                    emit indexStateChanged();
                    return;
                }
                index_ = result.index;
                configureWatcher();
                emit indexStateChanged();
                scheduleSearch();
                if (!pendingChangedPaths_.isEmpty()) {
                    updateTimer_->start();
                }
            });
    watcher->setFuture(indexUpdater_(index_, coalesced, indexCancellation_));
}

void LocalFileSearchWidget::scheduleSearch()
{
    ++searchGeneration_;
    if (searchCancellation_) {
        searchCancellation_->store(true, std::memory_order_relaxed);
    }
    debounceTimer_->stop();
    results_.clear();
    resultList_->clear();

    if (!index_.ready) {
        emit resultsChanged();
        return;
    }
    if (query_.trimmed().isEmpty()) {
        showReadyStatus();
        emit resultsChanged();
        return;
    }

    status_->setText(tr("等待内存索引搜索…"));
    debounceTimer_->start();
}

void LocalFileSearchWidget::startSearch()
{
    if (!index_.ready) {
        return;
    }
    const quint64 searchGeneration = searchGeneration_;
    const quint64 indexGeneration = indexGeneration_;
    searchCancellation_ = std::make_shared<std::atomic_bool>(false);
    status_->setText(tr("正在搜索内存索引…"));

    auto* watcher = new QFutureWatcher<QList<LocalFileSearchResult>>(this);
    connect(watcher, &QFutureWatcher<QList<LocalFileSearchResult>>::finished, this,
            [this, watcher, searchGeneration, indexGeneration] {
                const QList<LocalFileSearchResult> found = watcher->result();
                watcher->deleteLater();
                if (searchGeneration != searchGeneration_
                    || indexGeneration != indexGeneration_) {
                    return;
                }
                searchCancellation_.reset();
                displayResults(found);
            });
    watcher->setFuture(searchStarter_(index_, query_,
                                      LocalFileSearch::DefaultResultLimit,
                                      searchCancellation_));
}

void LocalFileSearchWidget::displayResults(
    const QList<LocalFileSearchResult>& results)
{
    results_ = results.mid(0, LocalFileSearch::DefaultResultLimit);
    resultList_->clear();
    for (const LocalFileSearchResult& result : results_) {
        addResultRow(result);
    }

    QString message;
    if (results_.isEmpty()) {
        message = tr("没有找到匹配的本地文件或目录。");
    } else if (results_.size() >= LocalFileSearch::DefaultResultLimit) {
        message = tr("显示前 %1 条结果，已达到结果上限。")
                      .arg(LocalFileSearch::DefaultResultLimit);
    } else {
        message = tr("找到 %1 条结果。").arg(results_.size());
    }
    status_->setText(message + watcherWarning());
    emit resultsChanged();
}

void LocalFileSearchWidget::addResultRow(const LocalFileSearchResult& result)
{
    auto* item = new QListWidgetItem(resultList_);
    auto* row = new QWidget(resultList_);
    row->setObjectName(QStringLiteral("localSearchResultRow"));
    row->setAccessibleName(tr("%1，名称或路径匹配").arg(result.path));

    auto* path = new QLabel(result.path, row);
    path->setTextInteractionFlags(Qt::TextSelectableByMouse);
    path->setToolTip(result.path);
    path->setAccessibleName(tr("结果路径"));
    auto* match = new QLabel(tr("名称或路径匹配"), row);
    match->setAccessibleName(tr("匹配方式"));
    auto* open = new QPushButton(result.isDirectory ? tr("打开目录") : tr("打开文件"), row);
    open->setObjectName(QStringLiteral("openResult"));
    open->setAccessibleDescription(tr("使用系统默认程序打开此结果"));
    connect(open, &QPushButton::clicked, this,
            [this, result] { launchPath(result.path, false); });

    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(6, 3, 6, 3);
    layout->addWidget(path, 1);
    layout->addWidget(match);
    layout->addWidget(open);
    if (!result.isDirectory) {
        auto* containing = new QPushButton(tr("打开所在目录"), row);
        containing->setObjectName(QStringLiteral("openContainingDirectory"));
        containing->setAccessibleDescription(tr("在文件管理器中打开此文件的所在目录"));
        connect(containing, &QPushButton::clicked, this,
                [this, result] { launchPath(result.path, true); });
        layout->addWidget(containing);
    }

    item->setSizeHint(row->sizeHint());
    item->setData(Qt::UserRole, result.path);
    resultList_->setItemWidget(item, row);
}

void LocalFileSearchWidget::showReadyStatus()
{
    status_->setText(
        tr("索引已就绪：%1 个文件，%2 个目录。输入名称或路径开始搜索。")
            .arg(index_.stats.fileCount)
            .arg(index_.stats.directoryCount)
        + watcherWarning());
}

QString LocalFileSearchWidget::watcherWarning() const
{
    return watchRegistrationComplete_
        ? QString()
        : tr(" 部分目录无法监控，变更可能不会自动显示；请使用“重建索引”手动刷新。");
}

bool LocalFileSearchWidget::launchPath(const QString& path,
                                       bool containingDirectory)
{
    const QFileInfo source(path);
    if (!source.exists() || (!source.isFile() && !source.isDir()) || source.isSymLink()) {
        status_->setText(tr("无法打开：结果已不存在或不是有效的本地路径。"));
        return false;
    }

    const QString targetPath = containingDirectory && source.isFile()
        ? source.absoluteDir().absolutePath()
        : source.absoluteFilePath();
    const QFileInfo target(targetPath);
    const QUrl url = QUrl::fromLocalFile(QDir::cleanPath(target.absoluteFilePath()));
    if (!target.exists() || !url.isValid() || !url.isLocalFile()
        || url.toLocalFile().isEmpty() || !urlLauncher_ || !urlLauncher_(url)) {
        status_->setText(tr("无法使用系统程序打开所选路径。"));
        return false;
    }
    return true;
}

}  // namespace mtb
