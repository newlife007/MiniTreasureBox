#include "ui/widgets/local_file_search_widget.h"

#include <QCheckBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
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

#include <utility>

namespace mtb {

LocalFileSearchWidget::LocalFileSearchWidget(QWidget* parent)
    : QWidget(parent)
    , rootPath_(new QLineEdit(this))
    , searchNames_(new QCheckBox(tr("文件名和目录名"), this))
    , searchContents_(new QCheckBox(tr("文本文件内容"), this))
    , status_(new QLabel(this))
    , resultList_(new QListWidget(this))
    , debounceTimer_(new QTimer(this))
    , searchStarter_([](const LocalFileSearchRequest& request) {
        return QtConcurrent::run([request] {
            return LocalFileSearch::search(request);
        });
    })
    , urlLauncher_([](const QUrl& url) { return QDesktopServices::openUrl(url); })
{
    setObjectName(QStringLiteral("localFileSearchWidget"));
    rootPath_->setObjectName(QStringLiteral("localSearchRoot"));
    searchNames_->setObjectName(QStringLiteral("searchNamesCheck"));
    searchContents_->setObjectName(QStringLiteral("searchContentsCheck"));
    status_->setObjectName(QStringLiteral("localSearchStatus"));
    resultList_->setObjectName(QStringLiteral("localSearchResults"));

    rootPath_->setReadOnly(true);
    rootPath_->setAccessibleName(tr("本地搜索目录"));
    searchNames_->setChecked(true);
    searchNames_->setAccessibleDescription(tr("按文件名和目录名搜索"));
    searchContents_->setAccessibleDescription(tr("同时搜索不超过 1 MiB 的 UTF-8 文本文件"));
    status_->setWordWrap(true);
    status_->setAccessibleName(tr("本地文件搜索状态"));
    resultList_->setAccessibleName(tr("本地文件搜索结果"));
    debounceTimer_->setSingleShot(true);
    debounceTimer_->setInterval(250);

    auto* chooseButton = new QPushButton(tr("选择目录"), this);
    chooseButton->setObjectName(QStringLiteral("chooseSearchRoot"));
    chooseButton->setAccessibleDescription(tr("选择本地文件搜索的根目录"));

    auto* rootLayout = new QHBoxLayout;
    rootLayout->addWidget(rootPath_, 1);
    rootLayout->addWidget(chooseButton);
    auto* optionsLayout = new QHBoxLayout;
    optionsLayout->addWidget(searchNames_);
    optionsLayout->addWidget(searchContents_);
    optionsLayout->addStretch();
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(rootLayout);
    layout->addLayout(optionsLayout);
    layout->addWidget(status_);
    layout->addWidget(resultList_, 1);

    QString initialRoot = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    if (initialRoot.isEmpty()) {
        initialRoot = QDir::homePath();
    }
    setRootDirectory(initialRoot);

    connect(chooseButton, &QPushButton::clicked, this, [this] {
        const QString selected = QFileDialog::getExistingDirectory(
            this, tr("选择搜索目录"), rootPath_->text(), QFileDialog::ShowDirsOnly);
        if (!selected.isEmpty()) {
            setRootDirectory(selected);
        }
    });
    connect(searchNames_, &QCheckBox::toggled, this,
            &LocalFileSearchWidget::scheduleSearch);
    connect(searchContents_, &QCheckBox::toggled, this,
            &LocalFileSearchWidget::scheduleSearch);
    connect(debounceTimer_, &QTimer::timeout, this,
            &LocalFileSearchWidget::startSearch);
}

LocalFileSearchWidget::~LocalFileSearchWidget()
{
    if (cancellation_) {
        cancellation_->store(true, std::memory_order_relaxed);
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
    const QFileInfo info(path);
    const QString normalized = QDir::cleanPath(info.absoluteFilePath());
    if (!info.exists() || !info.isDir() || info.isSymLink()) {
        status_->setText(tr("请选择有效的本地目录。"));
        return;
    }
    rootPath_->setText(normalized);
    scheduleSearch();
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

void LocalFileSearchWidget::scheduleSearch()
{
    ++generation_;
    if (cancellation_) {
        cancellation_->store(true, std::memory_order_relaxed);
    }
    debounceTimer_->stop();
    results_.clear();
    resultList_->clear();

    if (query_.trimmed().isEmpty()) {
        status_->setText(tr("输入文件名、目录名或文件内容开始搜索。"));
        emit resultsChanged();
        return;
    }
    if (!searchNames_->isChecked() && !searchContents_->isChecked()) {
        status_->setText(tr("请至少选择一种搜索方式。"));
        emit resultsChanged();
        return;
    }

    status_->setText(tr("等待搜索…"));
    debounceTimer_->start();
}

void LocalFileSearchWidget::startSearch()
{
    const quint64 generation = generation_;
    cancellation_ = std::make_shared<std::atomic_bool>(false);
    LocalFileSearchRequest request{
        rootPath_->text(), query_, searchNames_->isChecked(),
        searchContents_->isChecked(), LocalFileSearch::DefaultResultLimit,
        LocalFileSearch::DefaultMaximumContentBytes, cancellation_};
    status_->setText(tr("正在搜索…"));

    auto* watcher = new QFutureWatcher<QList<LocalFileSearchResult>>(this);
    connect(watcher, &QFutureWatcher<QList<LocalFileSearchResult>>::finished, this,
            [this, watcher, generation] {
                const QList<LocalFileSearchResult> found = watcher->result();
                watcher->deleteLater();
                if (generation != generation_) {
                    return;
                }
                displayResults(found);
            });
    watcher->setFuture(searchStarter_(request));
}

void LocalFileSearchWidget::displayResults(const QList<LocalFileSearchResult>& results)
{
    results_ = results;
    resultList_->clear();
    for (const LocalFileSearchResult& result : results_) {
        addResultRow(result);
    }

    if (results_.isEmpty()) {
        status_->setText(tr("没有找到匹配的本地文件或目录。"));
    } else if (results_.size() >= LocalFileSearch::DefaultResultLimit) {
        status_->setText(tr("显示前 %1 条结果，已达到结果上限。")
                             .arg(LocalFileSearch::DefaultResultLimit));
    } else {
        status_->setText(tr("找到 %1 条结果。").arg(results_.size()));
    }
    emit resultsChanged();
}

void LocalFileSearchWidget::addResultRow(const LocalFileSearchResult& result)
{
    auto* item = new QListWidgetItem(resultList_);
    auto* row = new QWidget(resultList_);
    row->setObjectName(QStringLiteral("localSearchResultRow"));
    const QString matchText = result.nameMatched && result.contentMatched
        ? tr("名称和内容匹配")
        : (result.nameMatched ? tr("名称匹配") : tr("内容匹配"));
    row->setAccessibleName(tr("%1，%2").arg(result.path, matchText));

    auto* path = new QLabel(result.path, row);
    path->setTextInteractionFlags(Qt::TextSelectableByMouse);
    path->setToolTip(result.path);
    path->setAccessibleName(tr("结果路径"));
    auto* match = new QLabel(matchText, row);
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

bool LocalFileSearchWidget::launchPath(const QString& path, bool containingDirectory)
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
