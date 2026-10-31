#include "ai/handwriting_removal/removal_widget.h"
#include "app/main_window.h"
#include "core/settings.h"
#include "core/tool_history.h"
#include "core/tool_registry.h"
#include "domain/tools/builtin_tools.h"
#include "ui/widgets/tool_card.h"
#include "ui/widgets/tool_grid.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTest>

using namespace mtb;
using namespace mtb::handwriting;

namespace {

QString fixtureExecutable()
{
#ifdef Q_OS_WIN
    return QDir(QCoreApplication::applicationDirPath()).filePath(
        QStringLiteral("fake_exam_restoration_worker.exe"));
#else
    return QDir(QCoreApplication::applicationDirPath()).filePath(
        QStringLiteral("fake_exam_restoration_worker"));
#endif
}

class ScopedEnvironment final {
public:
    ScopedEnvironment(const char* name, const QByteArray& value)
        : name_(name), existed_(qEnvironmentVariableIsSet(name)), old_(qgetenv(name))
    {
        qputenv(name_, value);
    }

    ~ScopedEnvironment()
    {
        if (existed_) qputenv(name_, old_);
        else qunsetenv(name_);
    }

private:
    const char* name_;
    bool existed_;
    QByteArray old_;
};

QListWidgetItem* itemWithText(QListWidget* list, const QString& text)
{
    for (int row = 0; row < list->count(); ++row) {
        if (list->item(row)->text().trimmed() == text) return list->item(row);
    }
    return nullptr;
}

}  // namespace

class ExamRestorationE2eTest final : public QObject {
    Q_OBJECT

private slots:
    void registryImportUsesResolvedFakeRuntimeAndExports();
};

void ExamRestorationE2eTest::registryImportUsesResolvedFakeRuntimeAndExports()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString models = directory.filePath(QStringLiteral("models"));
    QVERIFY(QDir().mkpath(models));
    QFile model(QDir(models).filePath(QStringLiteral("exam-restoration.onnx")));
    QVERIFY(model.open(QIODevice::WriteOnly));
    QCOMPARE(model.write("fixture-model"), qint64(13));
    model.close();
    const QString worker = fixtureExecutable();
    QVERIFY2(QFileInfo(worker).isExecutable(), qPrintable(worker));

    ScopedEnvironment workerEnvironment(
        "MINITREASUREBOX_EXAM_RESTORATION_WORKER", worker.toUtf8());
    ScopedEnvironment modelEnvironment(
        "MINITREASUREBOX_EXAM_RESTORATION_MODELS", models.toUtf8());
    ScopedEnvironment modeEnvironment("MINITREASUREBOX_FAKE_RESTORATION_MODE", QByteArray());

    const QString inputPath = directory.filePath(QStringLiteral("filled-exam.png"));
    QImage input(48, 32, QImage::Format_ARGB32);
    input.fill(QColor(244, 242, 235, 207));
    input.setPixelColor(input.width() / 2, input.height() / 2, QColor(20, 45, 180, 190));
    QVERIFY(input.save(inputPath, "PNG"));

    QSettings storage(directory.filePath(QStringLiteral("e2e.ini")), QSettings::IniFormat);
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
    QListWidgetItem* item = itemWithText(navigation, QStringLiteral("AI 工具"));
    QVERIFY(item);
    QTest::mouseClick(navigation->viewport(), Qt::LeftButton, Qt::NoModifier,
                      navigation->visualItemRect(item).center());
    QCoreApplication::processEvents();
    ToolCard* card = grid->cardForTool(QStringLiteral("exam-restoration"));
    QVERIFY(card);
    card->findChild<QPushButton*>(QStringLiteral("openButton"))->click();
    QCoreApplication::processEvents();
    QCOMPARE(pages->currentWidget()->objectName(), QStringLiteral("toolPage_exam-restoration"));

    auto* widget = pages->currentWidget()->findChild<HandwritingRemovalWidget*>(
        QStringLiteral("handwritingRemovalWidget"));
    QVERIFY(widget);
    auto* status = widget->findChild<QLabel*>(QStringLiteral("restorationStatus"));
    auto* selector = widget->findChild<QComboBox*>(QStringLiteral("restorationViewSelector"));
    auto* exportButton = widget->findChild<QPushButton*>(
        QStringLiteral("restorationExportButton"));
    QVERIFY(status);
    QVERIFY(selector);
    QVERIFY(exportButton);

    QVERIFY(widget->importImage(inputPath));
    QVERIFY(!exportButton->isEnabled());
    QTRY_VERIFY_WITH_TIMEOUT(status->text().contains(QStringLiteral("中置信度")), 5000);
    QVERIFY(status->text().contains(QStringLiteral("fixture confidence reason")));
    QCOMPARE(selector->currentText(), QStringLiteral("结果"));
    const QPoint center(input.width() / 2, input.height() / 2);
    QCOMPARE(qGray(widget->maskImage().pixel(center)), 255);
    QCOMPARE(widget->resultImage().pixelColor(center), QColor(Qt::white));
    QCOMPARE(widget->resultImage().pixelColor(0, 0), input.pixelColor(0, 0));
    QTRY_VERIFY_WITH_TIMEOUT(exportButton->isEnabled(), 2000);

    const QString exportPath = directory.filePath(QStringLiteral("restored-exam.png"));
    QString error;
    QVERIFY2(widget->exportPng(exportPath, &error), qPrintable(error));
    const QImage exported(exportPath);
    QVERIFY(!exported.isNull());
    QCOMPARE(exported, widget->resultImage());
    QVERIFY(!QFileInfo::exists(exportPath + QStringLiteral(".XXXXXX")));
}

QTEST_MAIN(ExamRestorationE2eTest)
#include "test_exam_restoration_e2e.moc"
