#include "core/local_file_search.h"
#include "core/settings.h"
#include "core/tool_history.h"
#include "core/tool_registry.h"
#include "domain/tools/builtin_tools.h"
#include "ui/pages/home_page.h"
#include "ui/widgets/local_file_search_widget.h"
#include "ui/widgets/tool_grid.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPromise>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <algorithm>
#include <atomic>
#include <memory>

using namespace mtb;

namespace {

void writeFile(const QString& path, const QByteArray& contents)
{
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.errorString()));
    QCOMPARE(file.write(contents), contents.size());
}

QListWidgetItem* itemForPath(QListWidget* list, const QString& path)
{
    for (int row = 0; row < list->count(); ++row) {
        if (list->item(row)->data(Qt::UserRole).toString() == path) {
            return list->item(row);
        }
    }
    return nullptr;
}

QStringList resultPaths(const QList<LocalFileSearchResult>& results)
{
    QStringList paths;
    for (const auto& result : results) {
        paths.append(result.path);
    }
    return paths;
}

class LocalFileSearchTest final : public QObject {
    Q_OBJECT

private slots:
    void constructionDoesNotIndexBeforeSelection();
    void buildsUnicodeMetadataIndexAndBoundsResults();
    void rejectsInvalidRootsSymlinksAndCancellation();
    void searchesPrebuiltSnapshotWithoutReadingDisk();
    void incrementallyRefreshesCreatedDeletedAndRenamedSubtrees();
    void watcherRefreshesWidgetAndReportsStatus();
    void serializesOverlappingWatcherRefreshes();
    void reconcilesChangesBeforeBuildCompletion();
    void reconcilesNewDirectoriesUntilWatchesAreStable();
    void rootAndQueryChangesSuppressStaleCompletions();
    void modeWordingAndValidatedOpenActionsUseLocalUrls();
};

void LocalFileSearchTest::constructionDoesNotIndexBeforeSelection()
{
    QPromise<LocalFileSearchIndexResult> unexpectedBuild;
    unexpectedBuild.start();
    int buildRequests = 0;
    LocalFileSearchWidget widget(
        LocalFileSearchWidget::IndexBuilder{
            [&unexpectedBuild, &buildRequests](
                const QString&, const std::shared_ptr<std::atomic_bool>&) {
                ++buildRequests;
                return unexpectedBuild.future();
            }});

    auto* rootPath = widget.findChild<QLineEdit*>(QStringLiteral("localSearchRoot"));
    auto* status = widget.findChild<QLabel*>(QStringLiteral("localSearchStatus"));
    auto* rebuild = widget.findChild<QPushButton*>(
        QStringLiteral("rebuildSearchIndex"));
    QVERIFY(rootPath);
    QVERIFY(status);
    QVERIFY(rebuild);
    QTest::qWait(300);
    unexpectedBuild.finish();

    QCOMPARE(buildRequests, 0);
    QVERIFY(rootPath->text().isEmpty());
    QVERIFY(!widget.isIndexReady());
    QVERIFY(!rebuild->isEnabled());
    QVERIFY(status->text().contains(QStringLiteral("请选择")));
}

void LocalFileSearchTest::buildsUnicodeMetadataIndexAndBoundsResults()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QDir root(temporary.path());
    QVERIFY(root.mkpath(QStringLiteral("Nested/资料目录")));
    const QString namedFile = root.filePath(QStringLiteral("Nested/资料目录/Résumé.TXT"));
    writeFile(namedFile, QStringLiteral("正文中的 secret-token 不应被搜索").toUtf8());
    for (int index = 0; index < 210; ++index) {
        writeFile(root.filePath(QStringLiteral("match-%1.txt").arg(index)), "data");
    }

    const auto built = LocalFileSearch::buildIndex(temporary.path());
    QVERIFY2(built.success, qPrintable(built.error));
    QVERIFY(built.index.ready);
    QCOMPARE(built.index.rootPath, temporary.path());
    QCOMPARE(built.index.stats.fileCount, 211);
    QCOMPARE(built.index.stats.directoryCount, 2);
    QCOMPARE(built.index.stats.recordCount(), 213);
    QVERIFY(built.index.watchedDirectories.contains(temporary.path()));
    QVERIFY(built.index.watchedDirectories.contains(
        root.filePath(QStringLiteral("Nested/资料目录"))));

    const auto unicode = LocalFileSearch::search(
        built.index, QStringLiteral("RÉSUMÉ"), 20);
    QCOMPARE(unicode.size(), 1);
    QCOMPARE(unicode.first().path, namedFile);

    const auto directoryPathMatches = LocalFileSearch::search(
        built.index, QStringLiteral("资料"), 20);
    QCOMPARE(directoryPathMatches.size(), 2);
    QVERIFY(resultPaths(directoryPathMatches).contains(namedFile));
    QVERIFY(std::any_of(directoryPathMatches.cbegin(), directoryPathMatches.cend(),
                        [](const auto& result) { return result.isDirectory; }));

    QVERIFY(LocalFileSearch::search(built.index, QStringLiteral("secret-token"), 20)
                .isEmpty());
    QCOMPARE(LocalFileSearch::search(built.index, QStringLiteral("match"), 500).size(),
             LocalFileSearch::DefaultResultLimit);
}

void LocalFileSearchTest::rejectsInvalidRootsSymlinksAndCancellation()
{
    QVERIFY(!LocalFileSearch::buildIndex(QStringLiteral("relative/path")).success);
    QVERIFY(!LocalFileSearch::buildIndex(QStringLiteral("/path/that/does/not/exist"))
                 .success);

    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QDir root(temporary.path());
    QVERIFY(root.mkpath(QStringLiteral("actual")));
    writeFile(root.filePath(QStringLiteral("actual/inside.txt")), "data");
    const QString directoryLink = root.filePath(QStringLiteral("linked-directory"));
    const QString fileLink = root.filePath(QStringLiteral("linked-file.txt"));
    const bool directoryLinkCreated = QFile::link(
        root.filePath(QStringLiteral("actual")), directoryLink);
    const bool fileLinkCreated = QFile::link(
        root.filePath(QStringLiteral("actual/inside.txt")), fileLink);

    const auto built = LocalFileSearch::buildIndex(temporary.path());
    QVERIFY(built.success);
    const QStringList paths = resultPaths(
        LocalFileSearch::search(built.index, QStringLiteral("inside")));
    QCOMPARE(paths, QStringList{root.filePath(QStringLiteral("actual/inside.txt"))});
    if (directoryLinkCreated) {
        QVERIFY(std::none_of(built.index.records.cbegin(), built.index.records.cend(),
                             [&directoryLink](const auto& record) {
                                 return record.path.startsWith(directoryLink);
                             }));
    }
    if (fileLinkCreated) {
        QVERIFY(std::none_of(built.index.records.cbegin(), built.index.records.cend(),
                             [&fileLink](const auto& record) {
                                 return record.path == fileLink;
                             }));
    }

    auto cancelled = std::make_shared<std::atomic_bool>(true);
    const auto cancelledBuild = LocalFileSearch::buildIndex(temporary.path(), cancelled);
    QVERIFY(!cancelledBuild.success);
    QVERIFY(cancelledBuild.cancelled);
    QVERIFY(LocalFileSearch::search(built.index, QStringLiteral("inside"), 200,
                                    cancelled)
                .isEmpty());
    const auto outside = LocalFileSearch::refreshSubtree(
        built.index, QDir(temporary.path()).absoluteFilePath(QStringLiteral("../outside")));
    QVERIFY(!outside.success);
}

void LocalFileSearchTest::searchesPrebuiltSnapshotWithoutReadingDisk()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QDir root(temporary.path());
    const QString oldPath = root.filePath(QStringLiteral("old-snapshot.txt"));
    const QString newPath = root.filePath(QStringLiteral("new-on-disk.txt"));
    writeFile(oldPath, "data");

    const auto built = LocalFileSearch::buildIndex(temporary.path());
    QVERIFY(built.success);
    QVERIFY(QFile::rename(oldPath, newPath));

    const auto oldResults = LocalFileSearch::search(
        built.index, QStringLiteral("old-snapshot"));
    QCOMPARE(oldResults.size(), 1);
    QCOMPARE(oldResults.first().path, oldPath);
    QVERIFY(LocalFileSearch::search(built.index, QStringLiteral("new-on-disk"))
                .isEmpty());
}

void LocalFileSearchTest::incrementallyRefreshesCreatedDeletedAndRenamedSubtrees()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QDir root(temporary.path());
    QVERIFY(root.mkpath(QStringLiteral("branch/old-child")));
    const QString oldPath = root.filePath(QStringLiteral("branch/old.txt"));
    writeFile(oldPath, "old");

    auto current = LocalFileSearch::buildIndex(temporary.path());
    QVERIFY(current.success);
    const QString createdPath = root.filePath(QStringLiteral("branch/old-child/created.txt"));
    writeFile(createdPath, "created");
    current = LocalFileSearch::refreshSubtree(
        current.index, root.filePath(QStringLiteral("branch/old-child")));
    QVERIFY(current.success);
    QCOMPARE(LocalFileSearch::search(current.index, QStringLiteral("created")).size(), 1);

    const QString renamedPath = root.filePath(QStringLiteral("branch/renamed.txt"));
    QVERIFY(QFile::rename(oldPath, renamedPath));
    current = LocalFileSearch::refreshSubtree(
        current.index, root.filePath(QStringLiteral("branch")));
    QVERIFY(current.success);
    QVERIFY(LocalFileSearch::search(current.index, QStringLiteral("old.txt")).isEmpty());
    QCOMPARE(LocalFileSearch::search(current.index, QStringLiteral("renamed")).size(), 1);

    QVERIFY(QFile::remove(createdPath));
    QVERIFY(root.rmdir(QStringLiteral("branch/old-child")));
    current = LocalFileSearch::refreshSubtree(
        current.index, root.filePath(QStringLiteral("branch")));
    QVERIFY(current.success);
    QVERIFY(LocalFileSearch::search(current.index, QStringLiteral("created")).isEmpty());
    QVERIFY(!current.index.watchedDirectories.contains(
        root.filePath(QStringLiteral("branch/old-child"))));
}

void LocalFileSearchTest::watcherRefreshesWidgetAndReportsStatus()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QDir root(temporary.path());
    writeFile(root.filePath(QStringLiteral("initial.txt")), "data");

    LocalFileSearchWidget widget;
    QSignalSpy indexSpy(&widget, &LocalFileSearchWidget::indexStateChanged);
    widget.setRootDirectory(temporary.path());
    widget.show();
    auto* status = widget.findChild<QLabel*>(QStringLiteral("localSearchStatus"));
    auto* list = widget.findChild<QListWidget*>(QStringLiteral("localSearchResults"));
    auto* rebuild = widget.findChild<QPushButton*>(QStringLiteral("rebuildSearchIndex"));
    QVERIFY(status);
    QVERIFY(list);
    QVERIFY(rebuild);
    QVERIFY(status->text().contains(QStringLiteral("建立")));
    QTRY_VERIFY_WITH_TIMEOUT(widget.isIndexReady(), 5000);
    QVERIFY(indexSpy.count() >= 2);
    QVERIFY(status->text().contains(QStringLiteral("就绪")));
    QCOMPARE(widget.indexStats().fileCount, 1);
    QVERIFY(widget.findChildren<QCheckBox*>().isEmpty());

    widget.setQuery(QStringLiteral("watch-created"));
    QTRY_VERIFY_WITH_TIMEOUT(status->text().contains(QStringLiteral("没有找到")), 3000);
    const QString created = root.filePath(QStringLiteral("watch-created.txt"));
    writeFile(created, "contents are irrelevant");
    QTRY_COMPARE_WITH_TIMEOUT(list->count(), 1, 7000);
    QCOMPARE(list->item(0)->data(Qt::UserRole).toString(), created);

    const QString renamed = root.filePath(QStringLiteral("watch-renamed.txt"));
    QVERIFY(QFile::rename(created, renamed));
    widget.setQuery(QStringLiteral("watch-renamed"));
    QTRY_COMPARE_WITH_TIMEOUT(list->count(), 1, 7000);
    QCOMPARE(list->item(0)->data(Qt::UserRole).toString(), renamed);

    QVERIFY(QFile::remove(renamed));
    QTRY_COMPARE_WITH_TIMEOUT(list->count(), 0, 7000);
    rebuild->click();
    QVERIFY(status->text().contains(QStringLiteral("建立")));
    QTRY_VERIFY_WITH_TIMEOUT(widget.isIndexReady(), 5000);
}

void LocalFileSearchTest::serializesOverlappingWatcherRefreshes()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QDir root(temporary.path());
    QVERIFY(root.mkpath(QStringLiteral("branch-a")));
    QVERIFY(root.mkpath(QStringLiteral("branch-b")));
    const QString branchA = root.filePath(QStringLiteral("branch-a"));
    const QString branchB = root.filePath(QStringLiteral("branch-b"));

    LocalFileSearchWidget widget;
    widget.setRootDirectory(temporary.path());
    QTRY_VERIFY_WITH_TIMEOUT(widget.isIndexReady(), 5000);

    QPromise<LocalFileSearchIndexResult> firstUpdate;
    QPromise<LocalFileSearchIndexResult> secondUpdate;
    firstUpdate.start();
    secondUpdate.start();
    QList<LocalFileSearchIndex> updateBases;
    QList<QStringList> updatePaths;
    QList<std::shared_ptr<std::atomic_bool>> cancellations;
    widget.setIndexUpdater(
        [&firstUpdate, &secondUpdate, &updateBases, &updatePaths, &cancellations](
            const LocalFileSearchIndex& current, const QStringList& paths,
            const std::shared_ptr<std::atomic_bool>& cancelled) {
            updateBases.append(current);
            updatePaths.append(paths);
            cancellations.append(cancelled);
            return updateBases.size() == 1 ? firstUpdate.future()
                                           : secondUpdate.future();
        });

    const QString firstPath = QDir(branchA).filePath(
        QStringLiteral("first-added.txt"));
    writeFile(firstPath, "first");
    QVERIFY(QMetaObject::invokeMethod(&widget, "queueChangedPath",
                                      Qt::DirectConnection,
                                      Q_ARG(QString, branchA)));
    QVERIFY(QMetaObject::invokeMethod(&widget, "applyPendingUpdates",
                                      Qt::DirectConnection));
    QTRY_COMPARE_WITH_TIMEOUT(updateBases.size(), 1, 3000);
    QVERIFY(updatePaths.first().contains(branchA)
            || updatePaths.first().contains(temporary.path()));

    const QString secondPath = QDir(branchB).filePath(
        QStringLiteral("second-added.txt"));
    writeFile(secondPath, "second");
    QVERIFY(QMetaObject::invokeMethod(&widget, "queueChangedPath",
                                      Qt::DirectConnection,
                                      Q_ARG(QString, branchB)));
    QVERIFY(QMetaObject::invokeMethod(&widget, "applyPendingUpdates",
                                      Qt::DirectConnection));
    QTest::qWait(350);
    QCOMPARE(updateBases.size(), 1);
    QVERIFY(!cancellations.first()->load(std::memory_order_relaxed));

    auto firstResult = LocalFileSearch::refreshSubtree(
        updateBases.first(), updatePaths.first().first());
    QVERIFY(firstResult.success);
    firstUpdate.addResult(firstResult);
    firstUpdate.finish();

    QTRY_COMPARE_WITH_TIMEOUT(updateBases.size(), 2, 3000);
    QVERIFY(updatePaths.at(1).contains(branchB)
            || updatePaths.at(1).contains(temporary.path()));
    QCOMPARE(LocalFileSearch::search(updateBases.at(1),
                                     QStringLiteral("first-added")).size(),
             1);
    QVERIFY(!cancellations.first()->load(std::memory_order_relaxed));

    auto secondResult = LocalFileSearchIndexResult{updateBases.at(1), true, false, {}};
    for (const QString& path : updatePaths.at(1)) {
        secondResult = LocalFileSearch::refreshSubtree(secondResult.index, path);
        QVERIFY(secondResult.success);
    }
    secondUpdate.addResult(secondResult);
    secondUpdate.finish();
    QTRY_VERIFY_WITH_TIMEOUT(widget.isIndexReady(), 3000);

    widget.setQuery(QStringLiteral("added"));
    auto* list = widget.findChild<QListWidget*>(QStringLiteral("localSearchResults"));
    QVERIFY(list);
    QTRY_COMPARE_WITH_TIMEOUT(list->count(), 2, 3000);
    QVERIFY(itemForPath(list, firstPath));
    QVERIFY(itemForPath(list, secondPath));
}

void LocalFileSearchTest::reconcilesChangesBeforeBuildCompletion()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QDir root(temporary.path());
    writeFile(root.filePath(QStringLiteral("before-build.txt")), "before");
    const auto scannedBeforeMutation = LocalFileSearch::buildIndex(temporary.path());
    QVERIFY(scannedBeforeMutation.success);

    QPromise<LocalFileSearchIndexResult> delayedBuild;
    delayedBuild.start();
    int buildRequests = 0;
    LocalFileSearchWidget widget(
        LocalFileSearchWidget::IndexBuilder{
            [&delayedBuild, &buildRequests](
                const QString&, const std::shared_ptr<std::atomic_bool>&) {
                ++buildRequests;
                return delayedBuild.future();
            }});
    widget.setRootDirectory(temporary.path());
    QCOMPARE(buildRequests, 1);

    const QString duringBuild = root.filePath(
        QStringLiteral("created-during-build.txt"));
    writeFile(duringBuild, "during");
    delayedBuild.addResult(scannedBeforeMutation);
    delayedBuild.finish();

    QTRY_VERIFY_WITH_TIMEOUT(widget.isIndexReady(), 5000);
    widget.setQuery(QStringLiteral("created-during-build"));
    auto* list = widget.findChild<QListWidget*>(QStringLiteral("localSearchResults"));
    QVERIFY(list);
    QTRY_COMPARE_WITH_TIMEOUT(list->count(), 1, 3000);
    QCOMPARE(list->item(0)->data(Qt::UserRole).toString(), duringBuild);
}

void LocalFileSearchTest::reconcilesNewDirectoriesUntilWatchesAreStable()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QDir root(temporary.path());
    const auto staleBuild = LocalFileSearch::buildIndex(temporary.path());
    QVERIFY(staleBuild.success);

    QPromise<LocalFileSearchIndexResult> delayedBuild;
    delayedBuild.start();
    LocalFileSearchWidget widget(
        LocalFileSearchWidget::IndexBuilder{
            [&delayedBuild](const QString&,
                            const std::shared_ptr<std::atomic_bool>&) {
                return delayedBuild.future();
            }});

    QList<QStringList> updatePaths;
    bool lateWriteSucceeded = false;
    const QString newDirectory = root.filePath(
        QStringLiteral("new-during-reconciliation"));
    const QString lateFile = QDir(newDirectory).filePath(
        QStringLiteral("created-after-subtree-scan.txt"));
    widget.setIndexUpdater(
        [&updatePaths, &lateWriteSucceeded, &lateFile](
            const LocalFileSearchIndex& current, const QStringList& paths,
            const std::shared_ptr<std::atomic_bool>& cancelled) {
            updatePaths.append(paths);
            const LocalFileSearchIndexResult refreshed =
                LocalFileSearch::refreshSubtree(current, paths.first(), cancelled);
            if (updatePaths.size() == 1 && refreshed.success) {
                QFile file(lateFile);
                lateWriteSucceeded = file.open(QIODevice::WriteOnly)
                    && file.write("late") == 4;
            }

            QPromise<LocalFileSearchIndexResult> completed;
            const auto future = completed.future();
            completed.start();
            completed.addResult(refreshed);
            completed.finish();
            return future;
        });

    widget.setRootDirectory(temporary.path());
    QVERIFY(root.mkpath(QStringLiteral("new-during-reconciliation")));
    writeFile(QDir(newDirectory).filePath(QStringLiteral("already-scanned.txt")),
              "initial");
    delayedBuild.addResult(staleBuild);
    delayedBuild.finish();

    QTRY_VERIFY_WITH_TIMEOUT(widget.isIndexReady(), 5000);
    QVERIFY(lateWriteSucceeded);
    QVERIFY(updatePaths.size() >= 2);
    QCOMPARE(updatePaths.at(0), QStringList{temporary.path()});
    QCOMPARE(updatePaths.at(1), QStringList{temporary.path()});

    widget.setQuery(QStringLiteral("created-after-subtree-scan"));
    auto* list = widget.findChild<QListWidget*>(QStringLiteral("localSearchResults"));
    QVERIFY(list);
    QTRY_COMPARE_WITH_TIMEOUT(list->count(), 1, 3000);
    QCOMPARE(list->item(0)->data(Qt::UserRole).toString(), lateFile);
}

void LocalFileSearchTest::rootAndQueryChangesSuppressStaleCompletions()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QDir root(temporary.path());
    QVERIFY(root.mkpath(QStringLiteral("old-root")));
    QVERIFY(root.mkpath(QStringLiteral("new-root")));
    const QString oldRoot = root.filePath(QStringLiteral("old-root"));
    const QString newRoot = root.filePath(QStringLiteral("new-root"));
    writeFile(QDir(oldRoot).filePath(QStringLiteral("old-result.txt")), "old");
    writeFile(QDir(newRoot).filePath(QStringLiteral("new-result.txt")), "new");
    const auto oldIndex = LocalFileSearch::buildIndex(oldRoot);
    const auto newIndex = LocalFileSearch::buildIndex(newRoot);
    QVERIFY(oldIndex.success);
    QVERIFY(newIndex.success);

    LocalFileSearchWidget widget;
    QPromise<LocalFileSearchIndexResult> oldBuild;
    QPromise<LocalFileSearchIndexResult> newBuild;
    oldBuild.start();
    newBuild.start();
    QStringList requestedRoots;
    widget.setIndexBuilder(
        [&oldBuild, &newBuild, &requestedRoots](
            const QString& path, const std::shared_ptr<std::atomic_bool>&) {
            requestedRoots.append(path);
            return path.endsWith(QStringLiteral("old-root"))
                ? oldBuild.future()
                : newBuild.future();
        });
    widget.setRootDirectory(oldRoot);
    widget.setRootDirectory(newRoot);
    QCOMPARE(requestedRoots, QStringList({oldRoot, newRoot}));

    newBuild.addResult(newIndex);
    newBuild.finish();
    QTRY_VERIFY_WITH_TIMEOUT(widget.isIndexReady(), 2000);
    QCOMPARE(widget.indexStats().fileCount, 1);
    oldBuild.addResult(oldIndex);
    oldBuild.finish();
    QTest::qWait(50);
    QCOMPARE(widget.indexStats().fileCount, 1);

    using SearchResults = QList<LocalFileSearchResult>;
    QPromise<SearchResults> oldSearch;
    QPromise<SearchResults> newSearch;
    oldSearch.start();
    newSearch.start();
    QStringList requestedQueries;
    widget.setSearchStarter(
        [&oldSearch, &newSearch, &requestedQueries](
            const LocalFileSearchIndex&, const QString& query, int,
            const std::shared_ptr<std::atomic_bool>&) {
            requestedQueries.append(query);
            return query == QStringLiteral("old") ? oldSearch.future()
                                                    : newSearch.future();
        });
    widget.setQuery(QStringLiteral("old"));
    QTRY_VERIFY_WITH_TIMEOUT(requestedQueries.contains(QStringLiteral("old")), 2000);
    widget.setQuery(QStringLiteral("new"));
    QTRY_VERIFY_WITH_TIMEOUT(requestedQueries.contains(QStringLiteral("new")), 2000);

    newSearch.addResult(SearchResults{{QDir(newRoot).filePath(
                                             QStringLiteral("new-result.txt")),
                                         false, true}});
    newSearch.finish();
    auto* list = widget.findChild<QListWidget*>(QStringLiteral("localSearchResults"));
    QTRY_COMPARE_WITH_TIMEOUT(list->count(), 1, 2000);
    oldSearch.addResult(SearchResults{{QDir(oldRoot).filePath(
                                             QStringLiteral("old-result.txt")),
                                         false, true}});
    oldSearch.finish();
    QTRY_VERIFY_WITH_TIMEOUT(widget.findChildren<QFutureWatcherBase*>().isEmpty(), 2000);
    QCOMPARE(list->count(), 1);
    QCOMPARE(list->item(0)->data(Qt::UserRole).toString(),
             QDir(newRoot).filePath(QStringLiteral("new-result.txt")));
}

void LocalFileSearchTest::modeWordingAndValidatedOpenActionsUseLocalUrls()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QDir root(temporary.path());
    QVERIFY(root.mkpath(QStringLiteral("launch-directory")));
    const QString filePath = root.filePath(QStringLiteral("launch-file.txt"));
    const QString directoryPath = root.filePath(QStringLiteral("launch-directory"));
    writeFile(filePath, "data");

    QSettings storage(root.filePath(QStringLiteral("search.ini")), QSettings::IniFormat);
    Settings settings(storage);
    ToolHistory history(storage);
    ToolRegistry registry;
    registerBuiltinTools(registry);
    HomePage page(registry, settings, history);
    auto* mode = page.findChild<QComboBox*>(QStringLiteral("searchModeSelector"));
    auto* query = page.findChild<QLineEdit*>(QStringLiteral("globalSearch"));
    auto* local = page.findChild<LocalFileSearchWidget*>(
        QStringLiteral("localFileSearchWidget"));
    QVERIFY(mode);
    QVERIFY(query);
    QVERIFY(local);
    auto* localRoot = local->findChild<QLineEdit*>(
        QStringLiteral("localSearchRoot"));
    QVERIFY(localRoot);
    QVERIFY(localRoot->text().isEmpty());
    QVERIFY(!local->isIndexReady());
    local->setRootDirectory(temporary.path());
    mode->setCurrentIndex(mode->findData(QStringLiteral("files")));
    QVERIFY(query->placeholderText().contains(QStringLiteral("文件名")));
    QVERIFY(!query->placeholderText().contains(QStringLiteral("内容")));

    QList<QUrl> launched;
    local->setUrlLauncher([&launched](const QUrl& url) {
        launched.append(url);
        return true;
    });
    page.show();
    query->setText(QStringLiteral("launch"));
    auto* list = local->findChild<QListWidget*>(QStringLiteral("localSearchResults"));
    auto* status = local->findChild<QLabel*>(QStringLiteral("localSearchStatus"));
    QVERIFY(list);
    QVERIFY(status);
    QTRY_COMPARE_WITH_TIMEOUT(list->count(), 2, 5000);

    QListWidgetItem* fileItem = itemForPath(list, filePath);
    QListWidgetItem* directoryItem = itemForPath(list, directoryPath);
    QVERIFY(fileItem);
    QVERIFY(directoryItem);
    auto* fileRow = list->itemWidget(fileItem);
    auto* directoryRow = list->itemWidget(directoryItem);
    QVERIFY(fileRow);
    QVERIFY(directoryRow);
    fileRow->findChild<QPushButton*>(QStringLiteral("openResult"))->click();
    QCOMPARE(launched.last(), QUrl::fromLocalFile(filePath));
    fileRow->findChild<QPushButton*>(QStringLiteral("openContainingDirectory"))->click();
    QCOMPARE(launched.last(), QUrl::fromLocalFile(temporary.path()));
    directoryRow->findChild<QPushButton*>(QStringLiteral("openResult"))->click();
    QCOMPARE(launched.last(), QUrl::fromLocalFile(directoryPath));
    for (const QUrl& url : launched) {
        QVERIFY(url.isLocalFile());
    }

    QVERIFY(QFile::remove(filePath));
    const int launchCount = launched.size();
    fileRow->findChild<QPushButton*>(QStringLiteral("openResult"))->click();
    QCOMPARE(launched.size(), launchCount);
    QVERIFY(status->text().contains(QStringLiteral("无法打开")));
}

}  // namespace

QTEST_MAIN(LocalFileSearchTest)
#include "test_local_file_search.moc"
