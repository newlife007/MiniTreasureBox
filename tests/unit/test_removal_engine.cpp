#include "ai/handwriting_removal/removal_engine.h"

#include <QImage>
#include <QTest>

using namespace mtb::handwriting;

class RemovalEngineTest final : public QObject {
    Q_OBJECT

private slots:
    void proposesConservativeColorMaskAndFillsLocally();
    void editsMaskWithUndoAndRedo();
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
