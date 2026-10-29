#include "ai/handwriting_removal/removal_engine.h"

#include <QElapsedTimer>
#include <QImage>
#include <QTest>

using namespace mtb::handwriting;

class RemovalEngineTest final : public QObject {
    Q_OBJECT

private slots:
    void proposesConservativeColorMaskAndFillsLocally();
    void editsMaskWithUndoAndRedo();
    void coalescesGesturesAndBoundsLargeImageHistory();
    void processesProductionSizedImageInLinearTime();
    void rejectsInvalidInputsAndPreservesOriginal();
};

void RemovalEngineTest::proposesConservativeColorMaskAndFillsLocally()
{
    QImage page(31, 31, QImage::Format_RGB32);
    page.fill(QColor(246, 244, 238));
    page.setPixelColor(5, 5, Qt::black);
    page.setPixelColor(15, 15, QColor(20, 60, 190));
    const QImage before = page.copy();

    const BaselineHandwritingRemovalEngine engine;
    const RemovalResult result = engine.detectAndRemove(page);
    QVERIFY(result.ok);
    QCOMPARE(result.mask.size(), page.size());
    QCOMPARE(qGray(result.mask.pixel(15, 15)), 255);
    QCOMPARE(qGray(result.mask.pixel(5, 5)), 0);
    QVERIFY(qGray(result.restored.pixel(15, 15)) > 220);
    QCOMPARE(result.restored.pixelColor(5, 5), QColor(Qt::black));
    QCOMPARE(page, before);
    QVERIFY(result.message.contains(QStringLiteral("保守")));
}

void RemovalEngineTest::editsMaskWithUndoAndRedo()
{
    QImage page(40, 30, QImage::Format_RGB32);
    page.fill(Qt::white);
    MaskDocument document;
    QVERIFY(document.load(page));
    QCOMPARE(qGray(document.mask().pixel(10, 10)), 0);

    QVERIFY(document.paint(QPoint(10, 10), 3, true));
    QCOMPARE(qGray(document.mask().pixel(10, 10)), 255);
    QVERIFY(document.fillRectangle(QRect(20, 10, 5, 5), true));
    QCOMPARE(qGray(document.mask().pixel(22, 12)), 255);
    QVERIFY(document.paint(QPoint(10, 10), 3, false));
    QCOMPARE(qGray(document.mask().pixel(10, 10)), 0);

    QVERIFY(document.undo());
    QCOMPARE(qGray(document.mask().pixel(10, 10)), 255);
    QVERIFY(document.undo());
    QCOMPARE(qGray(document.mask().pixel(22, 12)), 0);
    QVERIFY(document.redo());
    QCOMPARE(qGray(document.mask().pixel(22, 12)), 255);
}

void RemovalEngineTest::coalescesGesturesAndBoundsLargeImageHistory()
{
    QImage page(4000, 3000, QImage::Format_RGB32);
    page.fill(Qt::white);
    MaskDocument document;
    QVERIFY(document.load(page));

    QVERIFY(document.beginEdit());
    for (int x = 100; x <= 900; x += 100) {
        QVERIFY(document.paint(QPoint(x, 500), 30, true));
    }
    QVERIFY(document.endEdit());
    QCOMPARE(document.historySize(), std::size_t{2});
    QVERIFY(document.undo());
    for (int x = 100; x <= 900; x += 100) {
        QCOMPARE(qGray(document.mask().pixel(x, 500)), 0);
    }
    QVERIFY(document.redo());

    for (int edit = 0; edit < 12; ++edit) {
        QVERIFY(document.paint(QPoint(1200 + edit * 100, 1000), 20, true));
    }
    QVERIFY(document.historyBytes() <= MaskDocument::HistoryByteLimit);
    QVERIFY(document.historySize() <= std::size_t{5});
}

void RemovalEngineTest::processesProductionSizedImageInLinearTime()
{
    QImage page(4000, 3000, QImage::Format_RGB32);
    page.fill(QColor(246, 244, 238));
    QImage mask(page.size(), QImage::Format_Grayscale8);
    mask.fill(0);
    for (int y = 100; y < page.height(); y += 300) {
        for (int x = 0; x < page.width(); ++x) mask.scanLine(y)[x] = 255;
    }

    QElapsedTimer timer;
    timer.start();
    const QImage result = BaselineHandwritingRemovalEngine::conservativeFill(page, mask);
    QVERIFY(!result.isNull());
    QCOMPARE(result.size(), page.size());
    QVERIFY2(timer.elapsed() < 8000,
             qPrintable(QStringLiteral("12-megapixel fill took %1 ms").arg(timer.elapsed())));
}

void RemovalEngineTest::rejectsInvalidInputsAndPreservesOriginal()
{
    const BaselineHandwritingRemovalEngine engine;
    QCOMPARE(engine.detectAndRemove({}).error, RemovalError::InvalidInput);

    QImage page(8, 8, QImage::Format_ARGB32);
    page.fill(QColor(250, 250, 245));
    const QImage original = page.copy();
    QImage wrongMask(3, 3, QImage::Format_Grayscale8);
    wrongMask.fill(255);
    QCOMPARE(engine.removeWithMask(page, wrongMask).error, RemovalError::InvalidInput);
    QImage validMask(page.size(), QImage::Format_Grayscale8);
    validMask.fill(255);
    QCOMPARE(engine.removeWithMask(page, validMask, {}, [] { return true; }).error,
             RemovalError::Cancelled);
    QCOMPARE(page, original);

    MaskDocument document;
    QVERIFY(!document.load({}));
    QVERIFY(document.load(page));
    QVERIFY(!document.setMask(wrongMask));
    QVERIFY(!document.paint(QPoint(-1, -1), 3, true));
    QCOMPARE(document.original(), original);
}

QTEST_MAIN(RemovalEngineTest)
#include "test_removal_engine.moc"
