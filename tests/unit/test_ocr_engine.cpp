#include "ai/ocr/ocr_engine.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QTest>

#include <atomic>
#include <future>

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

QString createImage(QTemporaryDir& directory, const QString& name = QStringLiteral("sample.png"))
{
    const QString path = directory.filePath(name);
    QImage image(20, 20, QImage::Format_RGB32);
    image.fill(Qt::white);
    return image.save(path, "PNG") ? path : QString();
}

OcrRequest requestFor(const QString& imagePath, const QString& language = QStringLiteral("eng"))
{
    return {imagePath, fixtureExecutable(), {}, language, 2000};
}

}  // namespace

class OcrEngineTest final : public QObject {
    Q_OBJECT

private slots:
    void validatesRequestsAndBuildsArguments();
    void executesProductionAdapterSuccessAndFailures();
    void enforcesTimeoutAndCancellation();
};

void OcrEngineTest::validatesRequestsAndBuildsArguments()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString imagePath = createImage(directory);
    QVERIFY(!imagePath.isEmpty());

    OcrRequest valid = requestFor(imagePath, QStringLiteral("chi_sim+eng"));
    QVERIFY(TesseractOcrEngine::validate(valid).ok);
    QCOMPARE(TesseractOcrEngine::argumentsFor(valid),
             QStringList({imagePath, QStringLiteral("stdout"), QStringLiteral("-l"),
                          QStringLiteral("chi_sim+eng")}));

    OcrRequest withData = valid;
    withData.tessdataPath = directory.filePath(QStringLiteral("tessdata"));
    QVERIFY(QDir().mkpath(withData.tessdataPath));
    QFile(directory.filePath(QStringLiteral("tessdata/chi_sim.traineddata"))).open(QIODevice::WriteOnly);
    QFile(directory.filePath(QStringLiteral("tessdata/eng.traineddata"))).open(QIODevice::WriteOnly);
    QVERIFY(TesseractOcrEngine::validate(withData).ok);
    QCOMPARE(TesseractOcrEngine::argumentsFor(withData).last(),
             QDir::cleanPath(withData.tessdataPath));

    OcrRequest missingImage = valid;
    missingImage.imagePath = directory.filePath(QStringLiteral("absent.png"));
    QCOMPARE(TesseractOcrEngine::validate(missingImage).error, OcrError::InvalidInput);

    const QString textPath = directory.filePath(QStringLiteral("not-image.txt"));
    QFile text(textPath);
    QVERIFY(text.open(QIODevice::WriteOnly));
    text.write("not an image");
    text.close();
    OcrRequest unsupported = valid;
    unsupported.imagePath = textPath;
    QCOMPARE(TesseractOcrEngine::validate(unsupported).error, OcrError::UnsupportedImage);

    OcrRequest missingRuntime = valid;
    missingRuntime.executable = directory.filePath(QStringLiteral("missing-program"));
    QCOMPARE(TesseractOcrEngine::validate(missingRuntime).error, OcrError::RuntimeMissing);

    QFile::remove(directory.filePath(QStringLiteral("tessdata/eng.traineddata")));
    QCOMPARE(TesseractOcrEngine::validate(withData).error, OcrError::LanguageDataMissing);
}

void OcrEngineTest::executesProductionAdapterSuccessAndFailures()
{
    QTemporaryDir directory;
    const QString imagePath = createImage(directory, QStringLiteral("private-page.png"));
    QVERIFY(!imagePath.isEmpty());
    const TesseractOcrEngine engine;

    const OcrResult success = engine.recognize(requestFor(imagePath));
    QVERIFY(success.ok);
    QCOMPARE(success.text, QStringLiteral("fixture text for private-page"));

    const OcrResult processFailure = engine.recognize(requestFor(imagePath, QStringLiteral("fail")));
    QCOMPARE(processFailure.error, OcrError::ProcessFailed);
    QVERIFY(!processFailure.message.contains(imagePath));

    const OcrResult modelFailure = engine.recognize(requestFor(imagePath, QStringLiteral("missing")));
    QCOMPARE(modelFailure.error, OcrError::LanguageDataMissing);

    const OcrResult empty = engine.recognize(requestFor(imagePath, QStringLiteral("empty")));
    QCOMPARE(empty.error, OcrError::EmptyOutput);
    QVERIFY(!empty.ok);
}

void OcrEngineTest::enforcesTimeoutAndCancellation()
{
    QTemporaryDir directory;
    const QString imagePath = createImage(directory);
    QVERIFY(!imagePath.isEmpty());
    const TesseractOcrEngine engine;

    OcrRequest timed = requestFor(imagePath, QStringLiteral("slow"));
    timed.timeoutMs = 50;
    QElapsedTimer timer;
    timer.start();
    QCOMPARE(engine.recognize(timed).error, OcrError::TimedOut);
    QVERIFY(timer.elapsed() < 800);

    std::atomic_bool cancellation{false};
    OcrRequest cancellable = requestFor(imagePath, QStringLiteral("slow"));
    auto future = std::async(std::launch::async, [&] {
        return engine.recognize(cancellable, &cancellation);
    });
    QTest::qSleep(80);
    cancellation.store(true);
    const OcrResult cancelled = future.get();
    QCOMPARE(cancelled.error, OcrError::Cancelled);
}

QTEST_MAIN(OcrEngineTest)
#include "test_ocr_engine.moc"
