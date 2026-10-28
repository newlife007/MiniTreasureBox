#include "core/settings.h"
#include "core/tool_history.h"

#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

namespace {

class SettingsHistoryTest final : public QObject {
    Q_OBJECT

private slots:
    void persistsThemeFavoritesAndWindowState();
    void favoritesAreUniqueAndClearable();
    void recentToolsAreMostRecentFirstUniqueAndBounded();
    void recentToolsPersistAndClear();
};

void SettingsHistoryTest::persistsThemeFavoritesAndWindowState()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("settings.ini"));

    {
        QSettings storage(path, QSettings::IniFormat);
        mtb::Settings settings(storage);
        settings.setTheme(mtb::Theme::Dark);
        settings.setFavorite(QStringLiteral("json.formatter"), true);
        settings.setWindowGeometry(QByteArrayLiteral("geometry"));
        settings.setWindowState(QByteArrayLiteral("state"));
    }

    QSettings restoredStorage(path, QSettings::IniFormat);
    mtb::Settings restored(restoredStorage);
    QCOMPARE(restored.theme(), mtb::Theme::Dark);
    QVERIFY(restored.isFavorite(QStringLiteral("json.formatter")));
    QCOMPARE(restored.windowGeometry(), QByteArrayLiteral("geometry"));
    QCOMPARE(restored.windowState(), QByteArrayLiteral("state"));
}

void SettingsHistoryTest::favoritesAreUniqueAndClearable()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings storage(directory.filePath(QStringLiteral("favorites.ini")), QSettings::IniFormat);
    mtb::Settings settings(storage);

    settings.setFavorite(QStringLiteral("json.formatter"), true);
    settings.setFavorite(QStringLiteral("base64.codec"), true);
    settings.setFavorite(QStringLiteral("json.formatter"), true);
    QCOMPARE(settings.favoriteToolIds(),
             QStringList({QStringLiteral("base64.codec"), QStringLiteral("json.formatter")}));

    settings.setFavorite(QStringLiteral("base64.codec"), false);
    QCOMPARE(settings.favoriteToolIds(), QStringList({QStringLiteral("json.formatter")}));
    settings.clearFavorites();
    QVERIFY(settings.favoriteToolIds().isEmpty());
}

void SettingsHistoryTest::recentToolsAreMostRecentFirstUniqueAndBounded()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings storage(directory.filePath(QStringLiteral("history.ini")), QSettings::IniFormat);
    mtb::ToolHistory history(storage, 3);

    history.recordToolOpened(QStringLiteral("one"));
    history.recordToolOpened(QStringLiteral("two"));
    history.recordToolOpened(QStringLiteral("three"));
    history.recordToolOpened(QStringLiteral("two"));
    history.recordToolOpened(QStringLiteral("four"));

    QCOMPARE(history.recentToolIds(),
             QStringList({QStringLiteral("four"), QStringLiteral("two"), QStringLiteral("three")}));
}

void SettingsHistoryTest::recentToolsPersistAndClear()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("history-persistence.ini"));

    {
        QSettings storage(path, QSettings::IniFormat);
        mtb::ToolHistory history(storage);
        history.recordToolOpened(QStringLiteral("json.formatter"));
        history.recordToolOpened(QStringLiteral("base64.codec"));
    }

    QSettings restoredStorage(path, QSettings::IniFormat);
    mtb::ToolHistory restored(restoredStorage);
    QCOMPARE(restored.recentToolIds(),
             QStringList({QStringLiteral("base64.codec"), QStringLiteral("json.formatter")}));
    restored.clearRecent();
    QVERIFY(restored.recentToolIds().isEmpty());
}

}  // namespace

QTEST_APPLESS_MAIN(SettingsHistoryTest)
#include "test_settings_history.moc"
