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

#include <atomic>
#include <algorithm>
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

class LocalFileSearchTest final : public QObject {
    Q_OBJECT

private slots:
    void recursiveNameAndContentSearch();
    void skipsUnsafeContentAndBoundsResults();
    void honorsCancellationAndDirectorySymlinks();
    void modeSwitchAndAsynchronousStaleSuppression();
    void validatedOpenActionsUseLocalUrls();
};

void LocalFileSearchTest::recursiveNameAndContentSearch()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QDir root(temporary.path());
    QVERIFY(root.mkpath(QStringLiteral("Nested/资料目录")));
    const QString namedFile = root.filePath(QStringLiteral("Nested/资料目录/Résumé.TXT"));
    const QString contentFile = root.filePath(QStringLiteral("Nested/content-only.txt"));
    writeFile(namedFile, "ordinary text");
    writeFile(contentFile, QStringLiteral("这里包含 Needle 内容").toUtf8());

    LocalFileSearchRequest nameRequest{temporary.path(), QStringLiteral("RÉSUMÉ"), true,
                                       false, 20,
                                       LocalFileSearch::DefaultMaximumContentBytes, {}};
    const auto nameResults = LocalFileSearch::search(nameRequest);
    QCOMPARE(nameResults.size(), 1);
    QCOMPARE(nameResults.first().path, namedFile);
    QVERIFY(nameResults.first().nameMatched);
    QVERIFY(!nameResults.first().contentMatched);

    nameRequest.query = QStringLiteral("资料");
    const auto directoryResults = LocalFileSearch::search(nameRequest);
    QCOMPARE(directoryResults.size(), 1);
    QVERIFY(directoryResults.first().isDirectory);

    LocalFileSearchRequest contentRequest{temporary.path(), QStringLiteral("needle"), false,
                                          true, 20,
                                          LocalFileSearch::DefaultMaximumContentBytes, {}};
    const auto contentResults = LocalFileSearch::search(contentRequest);
    QCOMPARE(contentResults.size(), 1);
    QCOMPARE(contentResults.first().path, contentFile);
    QVERIFY(!contentResults.first().nameMatched);
    QVERIFY(contentResults.first().contentMatched);

    contentRequest.searchContents = false;
    QVERIFY(LocalFileSearch::search(contentRequest).isEmpty());
}

void LocalFileSearchTest::skipsUnsafeContentAndBoundsResults()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QDir root(temporary.path());
    writeFile(root.filePath(QStringLiteral("binary.bin")), QByteArray("needle\0data", 11));
    writeFile(root.filePath(QStringLiteral("oversized.txt")), QByteArray(65, 'x') + "needle");
#ifdef Q_OS_UNIX
    const QString unreadablePath = root.filePath(QStringLiteral("unreadable.txt"));
    writeFile(unreadablePath, "needle");
    QVERIFY(QFile::setPermissions(unreadablePath, QFileDevice::WriteOwner));
#endif
    for (int index = 0; index < 6; ++index) {
        writeFile(root.filePath(QStringLiteral("match-%1.txt").arg(index)), "safe");
    }

    LocalFileSearchRequest contentRequest{temporary.path(), QStringLiteral("needle"), false,
                                          true, 20, 64, {}};
    const auto unsafeResults = LocalFileSearch::search(contentRequest);
    QStringList unsafePaths;
    for (const auto& result : unsafeResults) {
        unsafePaths.append(result.path);
    }
    QVERIFY2(unsafeResults.isEmpty(), qPrintable(unsafePaths.join(QLatin1Char(','))));
#ifdef Q_OS_UNIX
    QVERIFY(QFile::setPermissions(unreadablePath, QFileDevice::ReadOwner
                                                      | QFileDevice::WriteOwner));
#endif

    LocalFileSearchRequest limitedRequest{temporary.path(), QStringLiteral("match"), true,
                                          false, 3, 64, {}};
    QCOMPARE(LocalFileSearch::search(limitedRequest).size(), 3);
}

void LocalFileSearchTest::honorsCancellationAndDirectorySymlinks()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QDir root(temporary.path());
    QVERIFY(root.mkpath(QStringLiteral("actual")));
    writeFile(root.filePath(QStringLiteral("actual/inside.txt")), "needle");
    const QString linkPath = root.filePath(QStringLiteral("linked"));
    const bool linkCreated = QFile::link(root.filePath(QStringLiteral("actual")), linkPath);

    LocalFileSearchRequest request{temporary.path(), QStringLiteral("inside"), true,
                                   false, 20, 64, {}};
    const auto results = LocalFileSearch::search(request);
    QCOMPARE(results.size(), 1);
    QVERIFY(!results.first().path.startsWith(linkPath));
    if (linkCreated) {
        QVERIFY(std::none_of(results.cbegin(), results.cend(), [&linkPath](const auto& result) {
            return result.path.startsWith(linkPath);
        }));
    }

    request.cancelled = std::make_shared<std::atomic_bool>(true);
    QVERIFY(LocalFileSearch::search(request).isEmpty());
}

void LocalFileSearchTest::modeSwitchAndAsynchronousStaleSuppression()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QDir root(temporary.path());
    writeFile(root.filePath(QStringLiteral("old-result.txt")), "old");
    writeFile(root.filePath(QStringLiteral("new-unique-result.txt")), "new");

    QSettings storage(root.filePath(QStringLiteral("search.ini")), QSettings::IniFormat);
    Settings settings(storage);
    ToolHistory history(storage);
    ToolRegistry registry;
    registerBuiltinTools(registry);
    HomePage page(registry, settings, history);
    page.show();

    auto* mode = page.findChild<QComboBox*>(QStringLiteral("searchModeSelector"));
    auto* query = page.findChild<QLineEdit*>(QStringLiteral("globalSearch"));
    auto* grid = page.findChild<ToolGrid*>(QStringLiteral("toolGrid"));
    auto* local = page.findChild<LocalFileSearchWidget*>(
        QStringLiteral("localFileSearchWidget"));
    auto* list = page.findChild<QListWidget*>(QStringLiteral("localSearchResults"));
    QVERIFY(mode);
    QVERIFY(query);
    QVERIFY(grid);
    QVERIFY(local);
    QVERIFY(list);

    QCOMPARE(mode->currentData().toString(), QStringLiteral("tools"));
    QVERIFY(grid->isVisible());
    QVERIFY(!local->isVisible());
    local->setRootDirectory(temporary.path());
    mode->setCurrentIndex(mode->findData(QStringLiteral("files")));
    QVERIFY(local->isVisible());
    QVERIFY(!grid->isVisible());
    QVERIFY(query->placeholderText().contains(QStringLiteral("本地")));

    using SearchResults = QList<LocalFileSearchResult>;
    QPromise<SearchResults> oldCompletion;
    QPromise<SearchResults> newCompletion;
    oldCompletion.start();
    newCompletion.start();
    QStringList requestedQueries;
    local->setSearchStarter(
        [&oldCompletion, &newCompletion, &requestedQueries](
            const LocalFileSearchRequest& request) {
            requestedQueries.append(request.query);
            return request.query == QStringLiteral("old-result")
                ? oldCompletion.future()
                : newCompletion.future();
        });
    QSignalSpy resultsSpy(local, &LocalFileSearchWidget::resultsChanged);

    query->setText(QStringLiteral("old-result"));
    QTRY_VERIFY_WITH_TIMEOUT(requestedQueries.contains(QStringLiteral("old-result")),
                             2000);
    query->setText(QStringLiteral("new-unique"));
    QTRY_VERIFY_WITH_TIMEOUT(requestedQueries.contains(QStringLiteral("new-unique")),
                             2000);

    SearchResults newResults;
    newResults.append({root.filePath(QStringLiteral("new-unique-result.txt")), false,
                       true, false});
    newCompletion.addResult(newResults);
    newCompletion.finish();
    QTRY_COMPARE_WITH_TIMEOUT(list->count(), 1, 2000);
    QCOMPARE(list->item(0)->data(Qt::UserRole).toString(),
             root.filePath(QStringLiteral("new-unique-result.txt")));
    QCOMPARE(resultsSpy.count(), 1);

    SearchResults oldResults;
    oldResults.append({root.filePath(QStringLiteral("old-result.txt")), false, true,
                       false});
    oldCompletion.addResult(oldResults);
    oldCompletion.finish();
    QTRY_VERIFY_WITH_TIMEOUT(local->findChildren<QFutureWatcherBase*>().isEmpty(), 2000);
    QCOMPARE(list->count(), 1);
    QCOMPARE(list->item(0)->data(Qt::UserRole).toString(),
             root.filePath(QStringLiteral("new-unique-result.txt")));
    QCOMPARE(resultsSpy.count(), 1);

    mode->setCurrentIndex(mode->findData(QStringLiteral("tools")));
    QVERIFY(!local->isVisible());
    query->setText(QStringLiteral("JSON"));
    QVERIFY(grid->isVisible());
    QCOMPARE(grid->toolIds(), QStringList{QStringLiteral("json-formatter")});
    page.clearSearch();
    QVERIFY(query->text().isEmpty());
}

void LocalFileSearchTest::validatedOpenActionsUseLocalUrls()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QDir root(temporary.path());
    QVERIFY(root.mkpath(QStringLiteral("launch-directory")));
    const QString filePath = root.filePath(QStringLiteral("launch-file.txt"));
    const QString directoryPath = root.filePath(QStringLiteral("launch-directory"));
    writeFile(filePath, "data");

    LocalFileSearchWidget widget;
    widget.setRootDirectory(temporary.path());
    QList<QUrl> launched;
    widget.setUrlLauncher([&launched](const QUrl& url) {
        launched.append(url);
        return true;
    });
    widget.show();
    widget.setQuery(QStringLiteral("launch"));

    auto* list = widget.findChild<QListWidget*>(QStringLiteral("localSearchResults"));
    auto* status = widget.findChild<QLabel*>(QStringLiteral("localSearchStatus"));
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
