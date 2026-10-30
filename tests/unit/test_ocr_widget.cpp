#include "ai/ocr/ocr_widget.h"
#include "app/main_window.h"
#include "core/settings.h"
#include "core/tool_history.h"
#include "core/tool_registry.h"
#include "domain/tools/builtin_tools.h"
#include "ui/widgets/tool_card.h"
#include "ui/widgets/tool_grid.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeData>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTextEdit>
#include <QUrl>

using namespace mtb;
using namespace mtb::ocr;

namespace {

QString fixtureExecutable()
{
#ifdef Q_OS_WIN
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("fake_tesseract.exe"));
#else
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("fake_tesseract"));
#endif
}

QString createImage(QTemporaryDir& directory, const QString& name)
{
    const QString path = directory.filePath(name);
    QImage image(30, 20, QImage::Format_RGB32);
    image.fill(Qt::white);
    return image.save(path, "PNG") ? path : QString();
}

QListWidgetItem* itemWithText(QListWidget* list, const QString& text)
{
    for (int row = 0; row < list->count(); ++row) {
        if (list->item(row)->text().trimmed() == text) {
            return list->item(row);
        }
    }
    return nullptr;
}

class ScopedEnvironment final {
public:
    ScopedEnvironment(const char* name, const QByteArray& value)
        : name_(name)
        , wasSet_(qEnvironmentVariableIsSet(name))
        , previous_(qgetenv(name))
    {
        qputenv(name, value);
    }

    ~ScopedEnvironment()
    {
        if (wasSet_) {
            qputenv(name_.constData(), previous_);
        } else {
            qunsetenv(name_.constData());
        }
    }

private:
    QByteArray name_;
    bool wasSet_;
    QByteArray previous_;
};

}  // namespace

class OcrWidgetTest final : public QObject {
    Q_OBJECT

private slots:
    void importsRecognizesCopiesAndExports();
    void reportsMissingPackagedComponentAsRepairProblem();
    void acceptsClipboardAndLocalDrop();
    void registryNavigationOpensOfflineTool();
};

void OcrWidgetTest::importsRecognizesCopiesAndExports()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString imagePath = createImage(directory, QStringLiteral("exam-page.png"));
    const QString replacementPath = createImage(directory, QStringLiteral("replacement-page.png"));
    QVERIFY(!imagePath.isEmpty());
    QVERIFY(!replacementPath.isEmpty());

    ScopedEnvironment executableOverride("MINITREASUREBOX_TESSERACT",
                                         fixtureExecutable().toUtf8());
    ScopedEnvironment dataOverride("TESSDATA_PREFIX", QByteArray());
    OcrWidget widget;
    widget.show();
    auto* language = widget.findChild<QLineEdit*>(QStringLiteral("ocrLanguage"));
    auto* preview = widget.findChild<QLabel*>(QStringLiteral("ocrPreview"));
    auto* status = widget.findChild<QLabel*>(QStringLiteral("ocrStatus"));
    auto* result = widget.findChild<QTextEdit*>(QStringLiteral("ocrResult"));
    auto* copy = widget.findChild<QPushButton*>(QStringLiteral("ocrCopyButton"));
    auto* exportButton = widget.findChild<QPushButton*>(QStringLiteral("ocrExportButton"));
    auto* cancel = widget.findChild<QPushButton*>(QStringLiteral("ocrCancelButton"));
    QVERIFY(language);
    QVERIFY(!widget.findChild<QLineEdit*>(QStringLiteral("ocrExecutable")));
    QVERIFY(!widget.findChild<QLineEdit*>(QStringLiteral("ocrTessdata")));
    QVERIFY(preview);
    QVERIFY(status);
    QVERIFY(result);
    QVERIFY(copy);
    QVERIFY(exportButton);
    QVERIFY(cancel);

    language->setText(QStringLiteral("eng"));
    QVERIFY(widget.importImage(imagePath));
    QVERIFY(!preview->pixmap().isNull());
    widget.startRecognition();
    QVERIFY(status->text().contains(QStringLiteral("正在")));
    QVERIFY(cancel->isEnabled());
    QTRY_COMPARE_WITH_TIMEOUT(result->toPlainText(), QStringLiteral("fixture text for exam-page"), 3000);
    QCOMPARE(status->text(), QStringLiteral("识别完成。"));

    result->setPlainText(QStringLiteral("edited result"));
    copy->click();
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("edited result"));

    const QString textExport = directory.filePath(QStringLiteral("result.txt"));
    QString error;
    QVERIFY(widget.exportText(textExport, &error));
    QFile exported(textExport);
    QVERIFY(exported.open(QIODevice::ReadOnly));
    QCOMPARE(QString::fromUtf8(exported.readAll()), QStringLiteral("edited result"));

    const QString markdownExport = directory.filePath(QStringLiteral("result.md"));
    QVERIFY(widget.exportText(markdownExport, &error));

    QVERIFY(widget.importImage(replacementPath));
    QVERIFY(result->toPlainText().isEmpty());
    QVERIFY(!copy->isEnabled());
    QVERIFY(!exportButton->isEnabled());
    QVERIFY(!widget.exportText(directory.filePath(QStringLiteral("stale.txt")), &error));

    language->setText(QStringLiteral("fail"));
    widget.startRecognition();
    QVERIFY(result->toPlainText().isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(status->text().contains(QStringLiteral("失败")), 3000);
    QVERIFY(result->toPlainText().isEmpty());
    QVERIFY(!copy->isEnabled());
    QVERIFY(!exportButton->isEnabled());
    QVERIFY(!widget.exportText(directory.filePath(QStringLiteral("failed.txt")), &error));

    widget.clearAll();
    QVERIFY(result->toPlainText().isEmpty());
    QVERIFY(status->text().contains(QStringLiteral("已清空")));
}

void OcrWidgetTest::reportsMissingPackagedComponentAsRepairProblem()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString imagePath = createImage(directory, QStringLiteral("missing-runtime.png"));
    QVERIFY(!imagePath.isEmpty());

    ScopedEnvironment missingExecutable("MINITREASUREBOX_TESSERACT",
                                        directory.filePath(QStringLiteral("missing-tesseract")).toUtf8());
    ScopedEnvironment noDataOverride("TESSDATA_PREFIX", QByteArray());
    ScopedEnvironment restrictedPath("PATH", directory.filePath(QStringLiteral("empty-path")).toUtf8());
    OcrWidget widget;
    widget.show();
    QVERIFY(widget.importImage(imagePath));
    widget.startRecognition();

    auto* status = widget.findChild<QLabel*>(QStringLiteral("ocrStatus"));
    QVERIFY(status);
    QVERIFY(status->text().contains(QStringLiteral("修复"))
            || status->text().contains(QStringLiteral("重新安装应用")));
    QVERIFY(!status->text().contains(QStringLiteral("安装 Tesseract")));
    QVERIFY(!status->text().contains(QStringLiteral("配置可执行文件")));
}

void OcrWidgetTest::acceptsClipboardAndLocalDrop()
{
    QTemporaryDir directory;
    const QString imagePath = createImage(directory, QStringLiteral("dropped.png"));
    QVERIFY(!imagePath.isEmpty());
    OcrWidget widget;
    widget.show();

    QImage clipboardImage(12, 12, QImage::Format_RGB32);
    clipboardImage.fill(Qt::black);
    QApplication::clipboard()->setImage(clipboardImage);
    QVERIFY(widget.pasteImage());
    QVERIFY(!widget.findChild<QLabel*>(QStringLiteral("ocrPreview"))->pixmap().isNull());

    auto* mime = new QMimeData;
    mime->setUrls({QUrl::fromLocalFile(imagePath)});
    QDragEnterEvent enter(QPoint(10, 10), Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &enter);
    QVERIFY(enter.isAccepted());
    QDropEvent drop(QPointF(10, 10), Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &drop);
    QVERIFY(drop.isAccepted());
    delete mime;
}

void OcrWidgetTest::registryNavigationOpensOfflineTool()
{
    QTemporaryDir directory;
    QSettings storage(directory.filePath(QStringLiteral("ocr-navigation.ini")), QSettings::IniFormat);
    Settings settings(storage);
    ToolHistory history(storage);
    ToolRegistry registry;
    registerBuiltinTools(registry);
    const auto tool = registry.find(QStringLiteral("image-ocr"));
    QVERIFY(tool);
    QCOMPARE(tool->metadata().name, QStringLiteral("图片 OCR"));
    QCOMPARE(tool->metadata().category, QStringLiteral("AI 工具"));
    QVERIFY(!tool->metadata().requiresNetwork);

    MainWindow window(registry, settings, history);
    window.show();
    QCoreApplication::processEvents();
    auto* navigation = window.findChild<QListWidget*>(QStringLiteral("mainNavigation"));
    auto* grid = window.findChild<ToolGrid*>(QStringLiteral("toolGrid"));
    auto* pages = window.findChild<QStackedWidget*>(QStringLiteral("mainPages"));
    QVERIFY(navigation);
    QVERIFY(grid);
    QVERIFY(pages);
    QListWidgetItem* aiItem = itemWithText(navigation, QStringLiteral("AI 工具"));
    QVERIFY(aiItem);
    QTest::mouseClick(navigation->viewport(), Qt::LeftButton, Qt::NoModifier,
                      navigation->visualItemRect(aiItem).center());
    QCoreApplication::processEvents();
    QCOMPARE(grid->toolIds(),
             QStringList({QStringLiteral("image-ocr"), QStringLiteral("exam-restoration")}));
    ToolCard* card = grid->cardForTool(QStringLiteral("image-ocr"));
    QVERIFY(card);
    card->findChild<QPushButton*>(QStringLiteral("openButton"))->click();
    QCoreApplication::processEvents();
    QCOMPARE(pages->currentWidget()->objectName(), QStringLiteral("toolPage_image-ocr"));
    QVERIFY(pages->currentWidget()->findChild<OcrWidget*>(QStringLiteral("ocrWidget")));
}

QTEST_MAIN(OcrWidgetTest)
#include "test_ocr_widget.moc"
