#include "app/main_window.h"
#include "core/settings.h"
#include "core/tool_history.h"
#include "core/tool_registry.h"
#include "domain/tools/builtin_tools.h"
#include "ui/widgets/tool_card.h"
#include "ui/widgets/tool_grid.h"

#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace mtb;

namespace {
const QStringList gameIds{QStringLiteral("guess-number"), QStringLiteral("game-2048"),
                          QStringLiteral("snake"), QStringLiteral("tetris"),
                          QStringLiteral("gomoku")};

QListWidgetItem* itemWithText(QListWidget* list, const QString& text)
{
    for (int row = 0; row < list->count(); ++row)
        if (list->item(row)->text().trimmed() == text) return list->item(row);
    return nullptr;
}
}

class GameWidgetsTest final : public QObject {
    Q_OBJECT

private slots:
    void registryContainsFiveOfflineGames();
    void everyFactoryProvidesAccessibleSharedControls();
    void keyboardChangesStateAndControlsPauseRestart();
    void gameNavigationOpensEveryFactory();
};

void GameWidgetsTest::registryContainsFiveOfflineGames()
{
    ToolRegistry registry;
    registerBuiltinTools(registry);
    QStringList actual;
    for (const auto& tool : registry.all()) {
        const ToolMetadata metadata = tool->metadata();
        if (metadata.category != QStringLiteral("小游戏")) continue;
        actual.append(metadata.id);
        QVERIFY(!metadata.requiresNetwork);
        QVERIFY(!metadata.keywords.isEmpty());
        QVERIFY(!metadata.aliases.isEmpty());
    }
    QCOMPARE(actual, gameIds);
}

void GameWidgetsTest::everyFactoryProvidesAccessibleSharedControls()
{
    ToolRegistry registry;
    registerBuiltinTools(registry);
    for (const QString& id : gameIds) {
        std::unique_ptr<QWidget> widget(registry.find(id)->createWidget());
        QVERIFY2(widget != nullptr, qPrintable(id));
        auto* surface = widget->findChild<QWidget*>(QStringLiteral("gameSurface"));
        auto* start = widget->findChild<QPushButton*>(QStringLiteral("startButton"));
        auto* pause = widget->findChild<QPushButton*>(QStringLiteral("pauseButton"));
        auto* restart = widget->findChild<QPushButton*>(QStringLiteral("restartButton"));
        auto* sound = widget->findChild<QCheckBox*>(QStringLiteral("soundToggle"));
        auto* status = widget->findChild<QLabel*>(QStringLiteral("gameStatus"));
        QVERIFY(surface);
        QVERIFY(start);
        QVERIFY(pause);
        QVERIFY(!pause->isHidden());
        QVERIFY(restart);
        QVERIFY(sound);
        QVERIFY(status);
        QCOMPARE(surface->focusPolicy(), Qt::StrongFocus);
        QVERIFY(!surface->accessibleName().isEmpty());
        QVERIFY(!start->toolTip().isEmpty());
        QVERIFY(!pause->accessibleDescription().isEmpty());
        QVERIFY(!restart->toolTip().isEmpty());
        QVERIFY(!sound->accessibleName().isEmpty());
        QVERIFY(widget->styleSheet().isEmpty());
    }
}

void GameWidgetsTest::keyboardChangesStateAndControlsPauseRestart()
{
    ToolRegistry registry;
    registerBuiltinTools(registry);

    std::unique_ptr<QWidget> guessWidget(registry.find(QStringLiteral("guess-number"))->createWidget());
    guessWidget->show();
    QCoreApplication::processEvents();
    auto* guessPause = guessWidget->findChild<QPushButton*>(QStringLiteral("pauseButton"));
    auto* guessStart = guessWidget->findChild<QPushButton*>(QStringLiteral("startButton"));
    auto* guessInput = guessWidget->findChild<QLineEdit*>(QStringLiteral("guessInput"));
    auto* guessStatus = guessWidget->findChild<QLabel*>(QStringLiteral("gameStatus"));
    QVERIFY(guessPause);
    QVERIFY(guessStart);
    QVERIFY(guessPause->isVisible());
    guessStart->click();
    QTest::keyClick(guessInput, Qt::Key_Space);
    QCOMPARE(guessStatus->text(), QStringLiteral("已暂停"));
    QVERIFY(!guessInput->isEnabled());
    guessPause->click();
    QVERIFY(guessInput->isEnabled());

    std::unique_ptr<QWidget> widget(registry.find(QStringLiteral("game-2048"))->createWidget());
    widget->show();
    QCoreApplication::processEvents();
    auto* surface = widget->findChild<QWidget*>(QStringLiteral("gameSurface"));
    auto* status = widget->findChild<QLabel*>(QStringLiteral("gameStatus"));
    auto* score = widget->findChild<QLabel*>(QStringLiteral("scoreLabel"));
    QVERIFY(surface);
    surface->setFocus();
    QCOMPARE(surface->property("tileCount").toInt(), 2);
    const QString initialBoard = surface->property("boardState").toString();
    for (int key : {Qt::Key_Left, Qt::Key_Right, Qt::Key_Up, Qt::Key_Down}) {
        if (surface->property("boardState").toString() != initialBoard) break;
        QTest::keyClick(surface, static_cast<Qt::Key>(key));
    }
    QVERIFY(surface->property("boardState").toString() != initialBoard);
    QVERIFY(score->text().contains(QString::number(surface->property("score").toInt())));
    QTest::keyClick(surface, Qt::Key_Space);
    QCOMPARE(status->text(), QStringLiteral("已暂停"));
    const int pausedScore = surface->property("score").toInt();
    QTest::keyClick(surface, Qt::Key_Right);
    QCOMPARE(surface->property("score").toInt(), pausedScore);
    QTest::keyClick(surface, Qt::Key_R);
    QCOMPARE(surface->property("score").toInt(), 0);
    QCOMPARE(status->text(), QStringLiteral("游戏中"));
}

void GameWidgetsTest::gameNavigationOpensEveryFactory()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings storage(directory.filePath(QStringLiteral("navigation.ini")), QSettings::IniFormat);
    Settings settings(storage);
    ToolHistory history(storage);
    ToolRegistry registry;
    registerBuiltinTools(registry);
    MainWindow window(registry, settings, history);
    window.show();
    QCoreApplication::processEvents();

    auto* navigation = window.findChild<QListWidget*>(QStringLiteral("mainNavigation"));
    auto* grid = window.findChild<ToolGrid*>(QStringLiteral("toolGrid"));
    auto* pages = window.findChild<QStackedWidget*>(QStringLiteral("mainPages"));
    QVERIFY(navigation);
    QVERIFY(grid);
    QVERIFY(pages);
    auto* gamesItem = itemWithText(navigation, QStringLiteral("小游戏"));
    QVERIFY(gamesItem);
    QTest::mouseClick(navigation->viewport(), Qt::LeftButton, Qt::NoModifier,
                      navigation->visualItemRect(gamesItem).center());
    QCoreApplication::processEvents();
    QCOMPARE(grid->toolIds(), gameIds);

    for (const QString& id : gameIds) {
        ToolCard* card = grid->cardForTool(id);
        QVERIFY(card);
        card->findChild<QPushButton*>(QStringLiteral("openButton"))->click();
        QCoreApplication::processEvents();
        QCOMPARE(pages->currentWidget()->objectName(), QStringLiteral("toolPage_%1").arg(id));
        QVERIFY(pages->currentWidget()->findChild<QWidget*>(QStringLiteral("gameSurface")));
        QTest::keyClick(&window, Qt::Key_Escape);
        QCoreApplication::processEvents();
        gamesItem = itemWithText(navigation, QStringLiteral("小游戏"));
        QTest::mouseClick(navigation->viewport(), Qt::LeftButton, Qt::NoModifier,
                          navigation->visualItemRect(gamesItem).center());
        QCoreApplication::processEvents();
    }
}

QTEST_MAIN(GameWidgetsTest)
#include "test_game_widgets.moc"
