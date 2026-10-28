#include "app/main_window.h"
#include "core/settings.h"
#include "core/tool_history.h"
#include "core/tool_registry.h"
#include "domain/tools/builtin_tools.h"
#include "domain/tools/tool_logic.h"
#include "domain/tools/tool_widgets.h"
#include "ui/pages/home_page.h"

#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QTest>
#include <QTreeWidget>

#include <memory>

using namespace mtb;

namespace {

QListWidgetItem* itemWithText(QListWidget* list, const QString& text)
{
    for (int row = 0; row < list->count(); ++row) {
        if (list->item(row)->text().trimmed() == text) {
            return list->item(row);
        }
    }
    return nullptr;
}

void clickItem(QListWidget* list, QListWidgetItem* item)
{
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier,
                      list->visualItemRect(item).center());
    QCoreApplication::processEvents();
}

QPushButton* buttonWithText(QWidget* widget, const QString& text)
{
    const auto buttons = widget->findChildren<QPushButton*>();
    for (QPushButton* button : buttons) {
        if (button->text() == text) {
            return button;
        }
    }
    return nullptr;
}

class ShellInteractionsTest final : public QObject {
    Q_OBJECT

private slots:
    void searchIntersectsActiveView();
    void practicalNavigationShortcutsRecentsAndTheme();
    void toolWidgetActions();
};

void ShellInteractionsTest::searchIntersectsActiveView()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings storage(directory.filePath(QStringLiteral("views.ini")), QSettings::IniFormat);
    Settings settings(storage);
    ToolHistory history(storage);
    ToolRegistry registry;
    registerBuiltinTools(registry);
    settings.setFavorite(QStringLiteral("base64-codec"), true);
    history.recordToolOpened(QStringLiteral("json-formatter"));

    HomePage page(registry, settings, history);
    auto* search = page.findChild<QLineEdit*>(QStringLiteral("globalSearch"));
    auto* tools = page.findChild<QTreeWidget*>(QStringLiteral("toolList"));
    QVERIFY(search);
    QVERIFY(tools);

    page.showFavorites();
    search->setText(QStringLiteral("JSON"));
    QCOMPARE(tools->topLevelItemCount(), 0);
    search->setText(QStringLiteral("Base64"));
    QCOMPARE(tools->topLevelItemCount(), 1);
    QCOMPARE(tools->topLevelItem(0)->data(0, Qt::UserRole).toString(),
             QStringLiteral("base64-codec"));

    page.showCategory(QStringLiteral("开发工具"));
    search->setText(QStringLiteral("Base64"));
    QCOMPARE(tools->topLevelItemCount(), 0);
    search->setText(QStringLiteral("UUID"));
    QCOMPARE(tools->topLevelItemCount(), 1);

    page.showRecent();
    search->setText(QStringLiteral("Base64"));
    QCOMPARE(tools->topLevelItemCount(), 0);
    search->setText(QStringLiteral("JSON"));
    QCOMPARE(tools->topLevelItemCount(), 1);
}

void ShellInteractionsTest::practicalNavigationShortcutsRecentsAndTheme()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings storage(directory.filePath(QStringLiteral("shell.ini")), QSettings::IniFormat);
    Settings settings(storage);
    ToolHistory history(storage);
    ToolRegistry registry;
    registerBuiltinTools(registry);
    settings.setFavorite(QStringLiteral("json-formatter"), true);

    MainWindow window(registry, settings, history);
    window.show();
    QCoreApplication::processEvents();

    auto* navigation = window.findChild<QListWidget*>(QStringLiteral("mainNavigation"));
    auto* tools = window.findChild<QTreeWidget*>(QStringLiteral("toolList"));
    auto* pages = window.findChild<QStackedWidget*>(QStringLiteral("mainPages"));
    auto* search = window.findChild<QLineEdit*>(QStringLiteral("globalSearch"));
    QVERIFY(navigation);
    QVERIFY(tools);
    QVERIFY(pages);
    QVERIFY(search);

    QListWidgetItem* practical = itemWithText(navigation, QStringLiteral("实用工具"));
    QVERIFY(practical);
    clickItem(navigation, practical);
    QCOMPARE(tools->topLevelItemCount(), 6);

    QListWidgetItem* favorites = itemWithText(navigation, QStringLiteral("收藏"));
    QVERIFY(favorites);
    clickItem(navigation, favorites);
    QCOMPARE(tools->topLevelItemCount(), 1);
    QCOMPARE(tools->topLevelItem(0)->data(0, Qt::UserRole).toString(),
             QStringLiteral("json-formatter"));

    QTreeWidgetItem* favoriteTool = tools->topLevelItem(0);
    tools->setCurrentItem(favoriteTool);
    tools->itemDoubleClicked(favoriteTool, 0);
    QCoreApplication::processEvents();
    QCOMPARE(history.recentToolIds().value(0), QStringLiteral("json-formatter"));
    QCOMPARE(pages->currentWidget()->objectName(), QStringLiteral("toolPage_json-formatter"));

    QTest::keyClick(&window, Qt::Key_Escape);
    QCoreApplication::processEvents();
    QCOMPARE(navigation->currentRow(), 0);
    QVERIFY(pages->currentWidget()->objectName() != QStringLiteral("toolPage_json-formatter"));

    QListWidgetItem* recent = itemWithText(navigation, QStringLiteral("最近使用"));
    QVERIFY(recent);
    clickItem(navigation, recent);
    QCOMPARE(tools->topLevelItemCount(), 1);
    QCOMPARE(tools->topLevelItem(0)->data(0, Qt::UserRole).toString(),
             QStringLiteral("json-formatter"));

    QListWidgetItem* settingsItem = itemWithText(navigation, QStringLiteral("设置"));
    QVERIFY(settingsItem);
    clickItem(navigation, settingsItem);
    QCOMPARE(pages->currentWidget()->objectName(), QStringLiteral("settingsPage"));
    auto* themeSelector = window.findChild<QComboBox*>(QStringLiteral("themeSelector"));
    QVERIFY(themeSelector);
    themeSelector->setCurrentIndex(themeSelector->findData(static_cast<int>(Theme::Dark)));
    QCOMPARE(settings.theme(), Theme::Dark);
    QCOMPARE(QApplication::palette().color(QPalette::Window), QColor(45, 45, 48));

    QTest::keyClick(&window, Qt::Key_K, Qt::ControlModifier);
    QCoreApplication::processEvents();
    QCOMPARE(navigation->currentRow(), 0);
    QVERIFY(search->hasFocus());

    themeSelector->setCurrentIndex(themeSelector->findData(static_cast<int>(Theme::System)));
    QCOMPARE(settings.theme(), Theme::System);
    window.close();
}

void ShellInteractionsTest::toolWidgetActions()
{
    std::unique_ptr<QWidget> json(tools::createJsonWidget());
    auto* jsonInput = json->findChild<QTextEdit*>(QStringLiteral("toolInput"));
    auto* jsonOutput = json->findChild<QTextEdit*>(QStringLiteral("toolOutput"));
    auto* jsonError = json->findChild<QLabel*>(QStringLiteral("errorLabel"));
    QVERIFY(jsonInput);
    QVERIFY(jsonOutput);
    QVERIFY(jsonError);
    jsonInput->setPlainText(QStringLiteral("{\"answer\":42}"));
    QVERIFY(buttonWithText(json.get(), QStringLiteral("格式化")));
    buttonWithText(json.get(), QStringLiteral("格式化"))->click();
    QVERIFY(jsonOutput->toPlainText().contains(QLatin1Char('\n')));
    jsonInput->setPlainText(QStringLiteral("{"));
    buttonWithText(json.get(), QStringLiteral("校验"))->click();
    QVERIFY(!jsonError->text().isEmpty());
    QVERIFY(!jsonError->isHidden());
    buttonWithText(json.get(), QStringLiteral("清空"))->click();
    QVERIFY(jsonInput->toPlainText().isEmpty());
    QVERIFY(jsonError->isHidden());

    std::unique_ptr<QWidget> base64(tools::createBase64Widget());
    auto* base64Input = base64->findChild<QTextEdit*>(QStringLiteral("toolInput"));
    auto* base64Output = base64->findChild<QTextEdit*>(QStringLiteral("toolOutput"));
    auto* base64Error = base64->findChild<QLabel*>(QStringLiteral("errorLabel"));
    QVERIFY(base64Input);
    QVERIFY(base64Output);
    QVERIFY(base64Error);
    base64Input->setPlainText(QStringLiteral("你好"));
    buttonWithText(base64.get(), QStringLiteral("编码"))->click();
    QCOMPARE(base64Output->toPlainText(), QStringLiteral("5L2g5aW9"));
    base64Input->setPlainText(QStringLiteral("not @ base64"));
    buttonWithText(base64.get(), QStringLiteral("解码"))->click();
    QVERIFY(!base64Error->text().isEmpty());

    std::unique_ptr<QWidget> url(tools::createUrlWidget());
    auto* urlInput = url->findChild<QTextEdit*>(QStringLiteral("toolInput"));
    auto* urlOutput = url->findChild<QTextEdit*>(QStringLiteral("toolOutput"));
    QVERIFY(urlInput);
    QVERIFY(urlOutput);
    urlInput->setPlainText(QStringLiteral("a b"));
    buttonWithText(url.get(), QStringLiteral("URL 编码"))->click();
    QCOMPARE(urlOutput->toPlainText(), QStringLiteral("a%20b"));

    std::unique_ptr<QWidget> timestamp(tools::createTimestampWidget());
    auto* timestampInput = timestamp->findChild<QLineEdit*>(QStringLiteral("timestampInput"));
    auto* utcOutput = timestamp->findChild<QLineEdit*>(QStringLiteral("utcOutput"));
    auto* timestampError = timestamp->findChild<QLabel*>(QStringLiteral("errorLabel"));
    QVERIFY(timestampInput);
    QVERIFY(utcOutput);
    QVERIFY(timestampError);
    timestampInput->setText(QStringLiteral("0"));
    buttonWithText(timestamp.get(), QStringLiteral("转换"))->click();
    QVERIFY(utcOutput->text().startsWith(QStringLiteral("1970-01-01T00:00:00")));
    timestampInput->setText(QStringLiteral("invalid"));
    buttonWithText(timestamp.get(), QStringLiteral("转换"))->click();
    QVERIFY(utcOutput->text().isEmpty());
    QVERIFY(!timestampError->text().isEmpty());

    std::unique_ptr<QWidget> uuid(tools::createUuidWidget());
    auto* uuidOutput = uuid->findChild<QLineEdit*>(QStringLiteral("uuidOutput"));
    QVERIFY(uuidOutput);
    buttonWithText(uuid.get(), QStringLiteral("生成 UUID"))->click();
    QVERIFY(tools::UuidLogic::isValid(uuidOutput->text()));
    buttonWithText(uuid.get(), QStringLiteral("清空"))->click();
    QVERIFY(uuidOutput->text().isEmpty());

    std::unique_ptr<QWidget> statistics(tools::createTextStatisticsWidget());
    auto* statisticsInput = statistics->findChild<QTextEdit*>(QStringLiteral("toolInput"));
    auto* characterCount = statistics->findChild<QLabel*>(QStringLiteral("characterCount"));
    auto* wordCount = statistics->findChild<QLabel*>(QStringLiteral("wordCount"));
    auto* byteCount = statistics->findChild<QLabel*>(QStringLiteral("byteCount"));
    QVERIFY(statisticsInput);
    QVERIFY(characterCount);
    QVERIFY(wordCount);
    QVERIFY(byteCount);
    statisticsInput->setPlainText(QStringLiteral("你好 a"));
    QCOMPARE(characterCount->text(), QStringLiteral("4"));
    QCOMPARE(wordCount->text(), QStringLiteral("2"));
    QCOMPARE(byteCount->text(), QStringLiteral("8"));
    buttonWithText(statistics.get(), QStringLiteral("清空"))->click();
    QCOMPARE(characterCount->text(), QStringLiteral("0"));
}

}  // namespace

QTEST_MAIN(ShellInteractionsTest)
#include "test_shell_interactions.moc"
