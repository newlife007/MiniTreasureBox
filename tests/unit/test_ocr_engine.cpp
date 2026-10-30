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

QString packagedExecutableName()
{
#ifdef Q_OS_WIN
    return QStringLiteral("tesseract.exe");
#else
    return QStringLiteral("tesseract");
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

void createEmptyFile(const QString& path)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
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

class OcrEngineTest final : public QObject {
    Q_OBJECT

private slots:
    void validatesRequestsAndBuildsArguments();
    void resolvesPackagedRuntimeAndEnvironmentOverrides();
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
    createEmptyFile(directory.filePath(QStringLiteral("tessdata/chi_sim.traineddata")));
    createEmptyFile(directory.filePath(QStringLiteral("tessdata/eng.traineddata")));
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
    const OcrResult runtimeFailure = TesseractOcrEngine::validate(missingRuntime);
    QCOMPARE(runtimeFailure.error, OcrError::RuntimeMissing);
    QVERIFY(runtimeFailure.message.contains(QStringLiteral("修复")));
    QVERIFY(!runtimeFailure.message.contains(QStringLiteral("安装 Tesseract")));

    QFile::remove(directory.filePath(QStringLiteral("tessdata/eng.traineddata")));
    const OcrResult dataFailure = TesseractOcrEngine::validate(withData);
    QCOMPARE(dataFailure.error, OcrError::LanguageDataMissing);
    QVERIFY(dataFailure.message.contains(QStringLiteral("修复")));
}

void OcrEngineTest::resolvesPackagedRuntimeAndEnvironmentOverrides()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString imagePath = createImage(directory, QStringLiteral("packaged-page.png"));
    QVERIFY(!imagePath.isEmpty());

    const QString applicationDir = directory.filePath(QStringLiteral("application/bin"));
    const QString runtimeRoot = directory.filePath(QStringLiteral("application/ocr-runtime"));
    const QString runtimeBin = QDir(runtimeRoot).filePath(QStringLiteral("bin"));
    const QString runtimeData = QDir(runtimeRoot).filePath(QStringLiteral("share/tessdata"));
    QVERIFY(QDir().mkpath(applicationDir));
    QVERIFY(QDir().mkpath(runtimeBin));
    QVERIFY(QDir().mkpath(runtimeData));
    const QString packagedExecutable = QDir(runtimeBin).filePath(packagedExecutableName());
    QVERIFY(QFile::copy(fixtureExecutable(), packagedExecutable));
    QVERIFY(QFile::setPermissions(packagedExecutable, QFile::permissions(packagedExecutable)
                                                     | QFileDevice::ExeOwner
                                                     | QFileDevice::ExeGroup
                                                     | QFileDevice::ExeOther));
    createEmptyFile(QDir(runtimeData).filePath(QStringLiteral("chi_sim.traineddata")));
    createEmptyFile(QDir(runtimeData).filePath(QStringLiteral("eng.traineddata")));

    ScopedEnvironment noExecutableOverride("MINITREASUREBOX_TESSERACT", QByteArray());
    ScopedEnvironment noDataOverride("TESSDATA_PREFIX", QByteArray());
#ifndef Q_OS_WIN
    ScopedEnvironment restrictedPath("PATH", directory.filePath(QStringLiteral("empty-path")).toUtf8());
#endif

    const OcrRequest unresolved{imagePath, {}, {}, QStringLiteral("chi_sim+eng"), 2000};
    const OcrRequest packaged = TesseractOcrEngine::resolvedRequest(unresolved, applicationDir);
    QCOMPARE(packaged.executable, QFileInfo(packagedExecutable).canonicalFilePath());
    QCOMPARE(packaged.tessdataPath, QFileInfo(runtimeData).canonicalFilePath());

#ifndef Q_OS_WIN
    // The Windows installer smoke test exercises the packaged OCR runtime with a sanitized PATH.
    // The unit test only checks Windows runtime resolution because its copied fixture is not the
    // same native OCR payload used by the installer.
    const QString argumentCapture = directory.filePath(QStringLiteral("arguments.txt"));
    ScopedEnvironment capture("MINITREASUREBOX_FAKE_TESSERACT_ARGUMENTS",
                              argumentCapture.toUtf8());
    const TesseractOcrEngine engine;
    const OcrResult packagedResult = engine.recognize(packaged);
    QVERIFY2(packagedResult.ok, qPrintable(packagedResult.message));
    QCOMPARE(packagedResult.text, QStringLiteral("fixture text for packaged-page"));
    QFile captured(argumentCapture);
    QVERIFY(captured.open(QIODevice::ReadOnly));
    const QStringList passedArguments = QString::fromUtf8(captured.readAll()).split(QLatin1Char('\n'));
    QVERIFY(passedArguments.contains(QStringLiteral("--tessdata-dir")));
    QVERIFY(passedArguments.contains(QFileInfo(runtimeData).canonicalFilePath()));
#else
    const TesseractOcrEngine engine;
#endif

    const QString overrideData = directory.filePath(QStringLiteral("override-data"));
    QVERIFY(QDir().mkpath(overrideData));
    createEmptyFile(QDir(overrideData).filePath(QStringLiteral("chi_sim.traineddata")));
    createEmptyFile(QDir(overrideData).filePath(QStringLiteral("eng.traineddata")));
    {
        ScopedEnvironment executableOverride("MINITREASUREBOX_TESSERACT",
                                             fixtureExecutable().toUtf8());
        ScopedEnvironment dataOverride("TESSDATA_PREFIX", overrideData.toUtf8());
        const OcrRequest overridden = TesseractOcrEngine::resolvedRequest(unresolved,
                                                                           applicationDir);
        QCOMPARE(overridden.executable, fixtureExecutable());
        QCOMPARE(overridden.tessdataPath, QDir::cleanPath(overrideData));
        QVERIFY(engine.recognize(overridden).ok);
    }

    const QString fallbackDirectory = directory.filePath(QStringLiteral("path-runtime"));
    QVERIFY(QDir().mkpath(fallbackDirectory));
    const QString fallbackExecutable = QDir(fallbackDirectory).filePath(packagedExecutableName());
    QVERIFY(QFile::copy(fixtureExecutable(), fallbackExecutable));
    QVERIFY(QFile::setPermissions(fallbackExecutable, QFile::permissions(fallbackExecutable)
                                                     | QFileDevice::ExeOwner
                                                     | QFileDevice::ExeGroup
                                                     | QFileDevice::ExeOther));
    {
#ifdef Q_OS_WIN
        const QByteArray fallbackPath = fallbackDirectory.toUtf8() + ";" + qgetenv("PATH");
#else
        const QByteArray fallbackPath = fallbackDirectory.toUtf8();
#endif
        ScopedEnvironment fallbackPathEnvironment("PATH", fallbackPath);
        const OcrRequest fallback = TesseractOcrEngine::resolvedRequest(
            unresolved, directory.filePath(QStringLiteral("unpackaged/bin")));
        QCOMPARE(fallback.executable, QStringLiteral("tesseract"));
        const OcrResult fallbackResult = engine.recognize(fallback);
        QVERIFY2(fallbackResult.ok, qPrintable(fallbackResult.message));
    }
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
