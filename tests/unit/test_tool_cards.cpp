#include "core/settings.h"
#include "core/tool_history.h"
#include "core/tool_registry.h"
#include "domain/tools/builtin_tools.h"
#include "ui/pages/home_page.h"
#include "ui/widgets/tool_card.h"
#include "ui/widgets/tool_grid.h"

#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace mtb;

namespace {

ToolMetadata sampleMetadata(const QString& id, bool requiresNetwork = false)
{
    return {id,
            QStringLiteral("示例工具 %1").arg(id),
            QStringLiteral("示例分类"),
            QStringLiteral("用于验证工具卡片元数据和交互的描述。"),
            {},
            {},
            QStringLiteral("definitely-missing-theme-icon-%1").arg(id),
            requiresNetwork};
}

class ToolCardsTest final : public QObject {
    Q_OBJECT

private slots:
    void cardRendersMetadataAndSupportsKeyboard();
    void gridRespondsToWidthAndPreservesOrder();
    void homeFiltersOrdersAndSynchronizesFavorites();
    void everyBuiltinHasVisibleFallbackIcon();
};

void ToolCardsTest::cardRendersMetadataAndSupportsKeyboard()
{
    const ToolMetadata metadata = sampleMetadata(QStringLiteral("network-tool"), true);
    ToolCard card(metadata, false);
    card.show();
    QCoreApplication::processEvents();

    QCOMPARE(card.toolId(), metadata.id);
    QVERIFY(!card.toolIcon().isNull());
    QVERIFY(!card.accessibleName().isEmpty());
    QVERIFY(card.accessibleDescription().contains(metadata.description));
    QCOMPARE(card.findChild<QLabel*>(QStringLiteral("toolName"))->text(), metadata.name);
    QCOMPARE(card.findChild<QLabel*>(QStringLiteral("toolDescription"))->text(),
             metadata.description);
    QVERIFY(card.findChild<QLabel*>(QStringLiteral("toolCategory"))->text()
                .contains(metadata.category));
    QVERIFY(card.findChild<QLabel*>(QStringLiteral("toolNetworkState"))->isVisible());

    auto* favorite = card.findChild<QPushButton*>(QStringLiteral("favoriteButton"));
    auto* open = card.findChild<QPushButton*>(QStringLiteral("openButton"));
    QVERIFY(favorite);
    QVERIFY(open);
    QCOMPARE(favorite->focusPolicy(), Qt::StrongFocus);
    QCOMPARE(open->focusPolicy(), Qt::StrongFocus);
    QVERIFY(!favorite->accessibleName().isEmpty());
    QVERIFY(!open->accessibleDescription().isEmpty());

    QSignalSpy activationSpy(&card, &ToolCard::activationRequested);
    card.setFocus();
    QTest::keyClick(&card, Qt::Key_Return);
    QCOMPARE(activationSpy.count(), 1);
    QCOMPARE(activationSpy.takeFirst().at(0).toString(), metadata.id);

    QSignalSpy favoriteSpy(&card, &ToolCard::favoriteToggleRequested);
    favorite->setFocus();
    QTest::keyClick(favorite, Qt::Key_Space);
    QCOMPARE(favoriteSpy.count(), 1);
    QCOMPARE(favoriteSpy.takeFirst().at(1).toBool(), true);
    QVERIFY(card.isFavorite());
}

void ToolCardsTest::gridRespondsToWidthAndPreservesOrder()
{
    QList<ToolMetadata> tools;
    for (int index = 0; index < 5; ++index) {
        tools.append(sampleMetadata(QString::number(index)));
    }

    ToolGrid grid;
    grid.resize(300, 500);
    grid.setTools(tools);
    grid.show();
    QCoreApplication::processEvents();
    QCOMPARE(grid.cardCount(), 5);
    QCOMPARE(grid.columnCount(), 1);
    QCOMPARE(grid.toolIds(),
             QStringList({QStringLiteral("0"), QStringLiteral("1"), QStringLiteral("2"),
                          QStringLiteral("3"), QStringLiteral("4")}));
    QCOMPARE(grid.horizontalScrollBar()->maximum(), 0);

    grid.resize(900, 500);
    QCoreApplication::processEvents();
    QTRY_VERIFY(grid.columnCount() >= 3);
    QCOMPARE(grid.horizontalScrollBar()->maximum(), 0);

    grid.resize(520, 500);
    QCoreApplication::processEvents();
    QTRY_VERIFY(grid.columnCount() < 3);
    QCOMPARE(grid.toolIds(),
             QStringList({QStringLiteral("0"), QStringLiteral("1"), QStringLiteral("2"),
                          QStringLiteral("3"), QStringLiteral("4")}));
}

void ToolCardsTest::homeFiltersOrdersAndSynchronizesFavorites()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings storage(directory.filePath(QStringLiteral("cards.ini")), QSettings::IniFormat);
    Settings settings(storage);
    ToolHistory history(storage);
    ToolRegistry registry;
    registerBuiltinTools(registry);
    history.recordToolOpened(QStringLiteral("json-formatter"));
    history.recordToolOpened(QStringLiteral("base64-codec"));

    HomePage page(registry, settings, history);
    page.resize(900, 600);
    page.show();
    auto* grid = page.findChild<ToolGrid*>(QStringLiteral("toolGrid"));
    QVERIFY(grid);
    QCOMPARE(grid->cardCount(), 12);

    page.showRecent();
    QCOMPARE(grid->toolIds().mid(0, 2),
             QStringList({QStringLiteral("base64-codec"), QStringLiteral("json-formatter")}));

    page.showCategory(QStringLiteral("开发工具"));
    QCOMPARE(grid->toolIds(),
             QStringList({QStringLiteral("json-formatter"), QStringLiteral("uuid-generator")}));

    page.showHome();
    ToolCard* jsonCard = grid->cardForTool(QStringLiteral("json-formatter"));
    QVERIFY(jsonCard);
    auto* favorite = jsonCard->findChild<QPushButton*>(QStringLiteral("favoriteButton"));
    QVERIFY(favorite);
    QSignalSpy favoriteSpy(&page, &HomePage::favoriteChanged);
    favorite->click();
    QCOMPARE(favoriteSpy.count(), 1);
    QVERIFY(settings.isFavorite(QStringLiteral("json-formatter")));
    QVERIFY(grid->cardForTool(QStringLiteral("json-formatter"))->isFavorite());

    page.showFavorites();
    QCOMPARE(grid->toolIds(), QStringList{QStringLiteral("json-formatter")});
    auto* favoriteViewButton = grid->cardForTool(QStringLiteral("json-formatter"))
                                   ->findChild<QPushButton*>(QStringLiteral("favoriteButton"));
    favoriteViewButton->click();
    QCOMPARE(grid->cardCount(), 0);
    QVERIFY(!settings.isFavorite(QStringLiteral("json-formatter")));
}

void ToolCardsTest::everyBuiltinHasVisibleFallbackIcon()
{
    ToolRegistry registry;
    registerBuiltinTools(registry);
    for (const auto& tool : registry.all()) {
        ToolMetadata metadata = tool->metadata();
        metadata.iconName = QStringLiteral("missing-%1").arg(metadata.id);
        ToolCard card(metadata, false);
        QVERIFY2(!card.toolIcon().isNull(), qPrintable(metadata.id));
        const QPixmap pixmap = card.toolIcon().pixmap(32, 32);
        QVERIFY2(!pixmap.isNull(), qPrintable(metadata.id));
    }
}

}  // namespace

QTEST_MAIN(ToolCardsTest)
#include "test_tool_cards.moc"
