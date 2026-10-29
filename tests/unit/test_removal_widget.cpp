#include "ai/handwriting_removal/removal_widget.h"
#include "app/main_window.h"
#include "core/settings.h"
#include "core/tool_history.h"
#include "core/tool_registry.h"
#include "domain/tools/builtin_tools.h"
#include "ui/widgets/tool_card.h"
#include "ui/widgets/tool_grid.h"

#include <QApplication>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QMimeData>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

using namespace mtb;
using namespace mtb::handwriting;

namespace {

QString createPage(QTemporaryDir& directory, const QString& name)
{
    QImage image(80, 60, QImage::Format_RGB32);
    image.fill(QColor(248, 246, 240));
    for (int x = 20; x < 60; ++x) image.setPixelColor(x, 30, QColor(25, 70, 190));
    const QString path = directory.filePath(name);
    return image.save(path, "PNG") ? path : QString();
}

QListWidgetItem* itemWithText(QListWidget* list, const QString& text)
{
    for (int row = 0; row < list->count(); ++row) {
        if (list->item(row)->text().trimmed() == text) return list->item(row);
    }
    return nullptr;
}

class FailingRegenerationEngine final : public IHandwritingRemovalEngine {
public:
    RemovalResult detectAndRemove(const QImage& image, const RemovalOptions&,
                                  const CancellationCheck&) const override
    {
        QImage mask(image.size(), QImage::Format_Grayscale8);
        mask.fill(0);
        return {true, mask, image.copy(), RemovalError::None,
                QStringLiteral("initial result")};
    }

    RemovalResult removeWithMask(const QImage&, const QImage&, const RemovalOptions&,
                                 const CancellationCheck&) const override
    {
        return {false, {}, {}, RemovalError::ProcessingFailed,
                QStringLiteral("forced regeneration failure")};
    }
};

class PassthroughRegenerationEngine final : public IHandwritingRemovalEngine {
public:
    RemovalResult detectAndRemove(const QImage& image, const RemovalOptions&,
                                  const CancellationCheck&) const override
    {
        QImage mask(image.size(), QImage::Format_Grayscale8);
        mask.fill(0);
        return {true, mask, image.copy(), RemovalError::None, QStringLiteral("ready")};
    }

    RemovalResult removeWithMask(const QImage& image, const QImage& mask,
                                 const RemovalOptions&,
                                 const CancellationCheck&) const override
    {
        return {true, mask.copy(), image.copy(), RemovalError::None,
                QStringLiteral("updated")};
    }
};

}  // namespace

class RemovalWidgetTest final : public QObject {
    Q_OBJECT

private slots:
    void importsProcessesEditsViewsAndExports();
    void failedRegenerationKeepsStaleResultUnexportable();
    void productionSizeCanvasRendersAndEditsAtDisplayResolution();
    void acceptsLocalDropAndReportsInvalidInput();
    void registryNavigationOpensOfflineTool();
};

void RemovalWidgetTest::importsProcessesEditsViewsAndExports()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = createPage(directory, QStringLiteral("private-exam.png"));
    QVERIFY(!path.isEmpty());

    HandwritingRemovalWidget widget;
    widget.resize(900, 600);
    widget.show();
    auto* status = widget.findChild<QLabel*>(QStringLiteral("restorationStatus"));
    auto* view = widget.findChild<QComboBox*>(QStringLiteral("restorationViewSelector"));
    auto* automatic = widget.findChild<QPushButton*>(QStringLiteral("restorationAutoButton"));
    auto* clear = widget.findChild<QPushButton*>(QStringLiteral("restorationClearButton"));
    auto* undo = widget.findChild<QPushButton*>(QStringLiteral("restorationUndoButton"));
    auto* redo = widget.findChild<QPushButton*>(QStringLiteral("restorationRedoButton"));
    auto* exportButton = widget.findChild<QPushButton*>(QStringLiteral("restorationExportButton"));
    auto* brush = widget.findChild<QPushButton*>(QStringLiteral("restorationBrushButton"));
    auto* eraser = widget.findChild<QPushButton*>(QStringLiteral("restorationEraserButton"));
    auto* rectangle = widget.findChild<QPushButton*>(QStringLiteral("restorationRectangleButton"));
    auto* canvas = widget.findChild<QWidget*>(QStringLiteral("restorationCanvas"));
    QVERIFY(status);
    QVERIFY(view);
    QVERIFY(automatic);
    QVERIFY(clear);
    QVERIFY(undo);
    QVERIFY(redo);
    QVERIFY(exportButton);
    QVERIFY(brush);
    QVERIFY(eraser);
    QVERIFY(rectangle);
    QVERIFY(canvas);

    QVERIFY(widget.importImage(path));
    const QImage original = widget.originalImage().copy();
    QVERIFY(automatic->isEnabled());
    widget.startAutomaticProcessing();
    QVERIFY(status->text().contains(QStringLiteral("正在")));
    QTRY_VERIFY_WITH_TIMEOUT(!widget.resultImage().isNull()
                             && qGray(widget.maskImage().pixel(40, 30)) == 255, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(automatic->isEnabled(), 3000);
    QCOMPARE(widget.originalImage(), original);

    QCOMPARE(view->count(), 3);
    view->setCurrentIndex(0);
    QCOMPARE(view->currentText(), QStringLiteral("原图"));
    view->setCurrentIndex(2);
    QCOMPARE(view->currentText(), QStringLiteral("结果"));
    QVERIFY(widget.findChild<QPushButton*>(QStringLiteral("restorationBrushButton")));
    QVERIFY(widget.findChild<QPushButton*>(QStringLiteral("restorationEraserButton")));
    QVERIFY(widget.findChild<QPushButton*>(QStringLiteral("restorationRectangleButton")));
    QVERIFY(widget.findChild<QPushButton*>(QStringLiteral("restorationCompareButton")));
    QVERIFY(widget.findChild<QSpinBox*>(QStringLiteral("restorationBrushSize")));
    QVERIFY(widget.findChild<QSlider*>(QStringLiteral("restorationMaskOpacity")));

    const QImage automaticMask = widget.maskImage().copy();
    QVERIFY(brush->isChecked());
    eraser->click();
    QVERIFY(eraser->isChecked());
    const QPoint center = canvas->rect().center();
    QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, center - QPoint(60, 0));
    QTest::mouseMove(canvas, center);
    QTest::mouseMove(canvas, center + QPoint(60, 0));
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, center + QPoint(60, 0));
    const QImage erasedMask = widget.maskImage().copy();
    QVERIFY(erasedMask != automaticMask);
    const QSize imageSize = automaticMask.size();
    const QSize scaled = imageSize.scaled(canvas->size(), Qt::KeepAspectRatio);
    const QRect imageArea(QPoint((canvas->width() - scaled.width()) / 2,
                                 (canvas->height() - scaled.height()) / 2), scaled);
    const auto imagePoint = [&](const QPoint& point) {
        return QPoint((point.x() - imageArea.left()) * imageSize.width() / imageArea.width(),
                      (point.y() - imageArea.top()) * imageSize.height() / imageArea.height());
    };
    for (const QPoint& point : {center - QPoint(60, 0), center, center + QPoint(60, 0)}) {
        const QPoint mapped = imagePoint(point);
        QCOMPARE(qGray(automaticMask.pixel(mapped)), 255);
        QCOMPARE(qGray(erasedMask.pixel(mapped)), 0);
    }
    QVERIFY(undo->isEnabled());
    undo->click();
    QCOMPARE(widget.maskImage(), automaticMask);
    QVERIFY(redo->isEnabled());
    redo->click();
    QCOMPARE(widget.maskImage(), erasedMask);
    brush->click();
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, canvas->rect().center());
    QVERIFY(widget.maskImage() != erasedMask);
    rectangle->click();
    QVERIFY(rectangle->isChecked());

    const QImage editedMask = widget.maskImage().copy();
    clear->click();
    QVERIFY(widget.maskImage() != editedMask);
    undo->click();
    QCOMPARE(widget.maskImage(), editedMask);

    const QString exportPath = directory.filePath(QStringLiteral("restored.png"));
    QTRY_VERIFY_WITH_TIMEOUT(exportButton->isEnabled(), 3000);
    QString error;
    QVERIFY(widget.exportPng(exportPath, &error));
    QImage exported(exportPath);
    QVERIFY(!exported.isNull());
    QCOMPARE(exported.size(), original.size());
    QCOMPARE(widget.originalImage(), original);
}

void RemovalWidgetTest::failedRegenerationKeepsStaleResultUnexportable()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = createPage(directory, QStringLiteral("failure-page.png"));
    auto engine = std::make_shared<FailingRegenerationEngine>();
    HandwritingRemovalWidget widget(nullptr, engine);
    widget.resize(900, 600);
    widget.show();

    auto* canvas = widget.findChild<QWidget*>(QStringLiteral("restorationCanvas"));
    auto* automatic = widget.findChild<QPushButton*>(QStringLiteral("restorationAutoButton"));
    auto* exportButton = widget.findChild<QPushButton*>(QStringLiteral("restorationExportButton"));
    auto* status = widget.findChild<QLabel*>(QStringLiteral("restorationStatus"));
    QVERIFY(canvas);
    QVERIFY(automatic);
    QVERIFY(exportButton);
    QVERIFY(status);
    QVERIFY(widget.importImage(path));
    widget.startAutomaticProcessing();
    QTRY_VERIFY_WITH_TIMEOUT(automatic->isEnabled(), 3000);
    QVERIFY(exportButton->isEnabled());
    const QImage previousResult = widget.resultImage().copy();

    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, canvas->rect().center());
    QVERIFY(!exportButton->isEnabled());
    QTRY_COMPARE_WITH_TIMEOUT(status->text(), QStringLiteral("forced regeneration failure"), 3000);
    QCOMPARE(widget.resultImage(), previousResult);
    QVERIFY(!exportButton->isEnabled());
    QString error;
    QVERIFY(!widget.exportPng(directory.filePath(QStringLiteral("stale.png")), &error));
    QVERIFY(error.contains(QStringLiteral("最新结果")));
}

void RemovalWidgetTest::productionSizeCanvasRendersAndEditsAtDisplayResolution()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("production-page.png"));
    QImage image(4000, 3000, QImage::Format_RGB32);
    image.fill(Qt::white);
    QVERIFY(image.save(path, "PNG"));

    auto engine = std::make_shared<PassthroughRegenerationEngine>();
    HandwritingRemovalWidget widget(nullptr, engine);
    widget.resize(900, 600);
    widget.show();
    QVERIFY(widget.importImage(path));
    auto* canvas = widget.findChild<QWidget*>(QStringLiteral("restorationCanvas"));
    QVERIFY(canvas);
    QTRY_VERIFY_WITH_TIMEOUT(canvas->property("maskOverlayPixelCount").toInt() > 0, 3000);
    const int overlayPixels = canvas->property("maskOverlayPixelCount").toInt();
    QVERIFY(overlayPixels <= canvas->width() * canvas->height());
    QVERIFY(overlayPixels < image.width() * image.height());

    const QPoint center = canvas->rect().center();
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, center);
    QVERIFY(qGray(widget.maskImage().pixel(widget.maskImage().rect().center())) > 0);
    QTRY_COMPARE_WITH_TIMEOUT(
        widget.findChild<QLabel*>(QStringLiteral("restorationStatus"))->text(),
        QStringLiteral("updated"), 3000);
}

void RemovalWidgetTest::acceptsLocalDropAndReportsInvalidInput()
{
    QTemporaryDir directory;
    const QString path = createPage(directory, QStringLiteral("dropped.png"));
    HandwritingRemovalWidget widget;
    widget.show();

    QVERIFY(!widget.importImage(directory.filePath(QStringLiteral("missing.png"))));
    QVERIFY(widget.findChild<QLabel*>(QStringLiteral("restorationStatus"))->text()
                .contains(QStringLiteral("无法读取")));

    auto* mime = new QMimeData;
    mime->setUrls({QUrl::fromLocalFile(path)});
    QDragEnterEvent enter(QPoint(5, 5), Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &enter);
    QVERIFY(enter.isAccepted());
    QDropEvent drop(QPointF(5, 5), Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &drop);
    QVERIFY(drop.isAccepted());
    QVERIFY(!widget.originalImage().isNull());
    delete mime;
}

void RemovalWidgetTest::registryNavigationOpensOfflineTool()
{
    QTemporaryDir directory;
    QSettings storage(directory.filePath(QStringLiteral("restoration-navigation.ini")),
                      QSettings::IniFormat);
    Settings settings(storage);
    ToolHistory history(storage);
    ToolRegistry registry;
    registerBuiltinTools(registry);
    const auto tool = registry.find(QStringLiteral("exam-restoration"));
    QVERIFY(tool);
    QCOMPARE(tool->metadata().name, QStringLiteral("试卷复原小工具"));
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
    QListWidgetItem* item = itemWithText(navigation, QStringLiteral("AI 工具"));
    QVERIFY(item);
    QTest::mouseClick(navigation->viewport(), Qt::LeftButton, Qt::NoModifier,
                      navigation->visualItemRect(item).center());
    QCoreApplication::processEvents();
    QVERIFY(grid->toolIds().contains(QStringLiteral("exam-restoration")));
    ToolCard* card = grid->cardForTool(QStringLiteral("exam-restoration"));
    QVERIFY(card);
    card->findChild<QPushButton*>(QStringLiteral("openButton"))->click();
    QCoreApplication::processEvents();
    QCOMPARE(pages->currentWidget()->objectName(), QStringLiteral("toolPage_exam-restoration"));
    QVERIFY(pages->currentWidget()->findChild<HandwritingRemovalWidget*>(
        QStringLiteral("handwritingRemovalWidget")));
}

QTEST_MAIN(RemovalWidgetTest)
#include "test_removal_widget.moc"
