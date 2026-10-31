#include "ai/ocr/ocr_engine.h"

#include <QCoreApplication>
#include <QDir>
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
    return QDir(QCoreApplication::applicationDirPath()).filePath(
        QStringLiteral("fake_paddleocr_worker.exe"));
#else
    return QDir(QCoreApplication::applicationDirPath()).filePath(
        QStringLiteral("fake_paddleocr_worker"));
#endif
}

QString packagedExecutableName()
{
#ifdef Q_OS_WIN
    return QStringLiteral("paddleocr-worker.exe");
#else
    return QStringLiteral("paddleocr-worker");
#endif
}

QString createImage(QTemporaryDir& directory,
                    const QString& name = QStringLiteral("sample.png"))
{
    const QString path = directory.filePath(name);
    QImage image(20, 20, QImage::Format_RGB32);
    image.fill(Qt::white);
    return image.save(path, "PNG") ? path : QString();
}

void createEmptyFile(const QString& path)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
}

QString createModels(QTemporaryDir& directory, const QString& name = QStringLiteral("models"))
{
    const QString root = directory.filePath(name);
    for (const QString& model : {QStringLiteral("PP-OCRv5_mobile_det_onnx"),
                                 QStringLiteral("PP-OCRv5_mobile_rec_onnx")}) {
        const QString modelPath = QDir(root).filePath(model);
        if (!QDir().mkpath(modelPath)) return {};
        createEmptyFile(QDir(modelPath).filePath(QStringLiteral("inference.onnx")));
        createEmptyFile(QDir(modelPath).filePath(QStringLiteral("inference.yml")));
    }
    return root;
}

OcrRequest requestFor(const QString& imagePath, const QString& modelPath,
                      const QString& language = QStringLiteral("ch"))
{
    return {imagePath, fixtureExecutable(), modelPath, language, 2000};
}

class ScopedEnvironment final {
public:
    ScopedEnvironment(const char* name, const QByteArray& value)
        : name_(name), wasSet_(qEnvironmentVariableIsSet(name)), previous_(qgetenv(name))
    {
        qputenv(name, value);
    }
    ~ScopedEnvironment()
    {
        if (wasSet_) qputenv(name_.constData(), previous_);
        else qunsetenv(name_.constData());
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
    const QString models = createModels(directory);
    OcrRequest valid = requestFor(imagePath, models);
    QVERIFY(PaddleOcrEngine::validate(valid).ok);
    QCOMPARE(PaddleOcrEngine::argumentsFor(valid),
             QStringList({QStringLiteral("--models"), QDir::cleanPath(models),
                          QStringLiteral("--image"), imagePath,
                          QStringLiteral("--language"), QStringLiteral("ch")}));

    OcrRequest missingImage = valid;
    missingImage.imagePath = directory.filePath(QStringLiteral("absent.png"));
    QCOMPARE(PaddleOcrEngine::validate(missingImage).error, OcrError::InvalidInput);

    const QString textPath = directory.filePath(QStringLiteral("not-image.txt"));
    createEmptyFile(textPath);
    OcrRequest unsupported = valid;
    unsupported.imagePath = textPath;
    QCOMPARE(PaddleOcrEngine::validate(unsupported).error, OcrError::UnsupportedImage);

    OcrRequest invalidLanguage = valid;
    invalidLanguage.language = QStringLiteral("unsupported");
    QCOMPARE(PaddleOcrEngine::validate(invalidLanguage).error, OcrError::InvalidInput);

    OcrRequest missingRuntime = valid;
    missingRuntime.workerExecutable = directory.filePath(QStringLiteral("missing-worker"));
    const OcrResult runtimeFailure = PaddleOcrEngine::validate(missingRuntime);
    QCOMPARE(runtimeFailure.error, OcrError::RuntimeMissing);
    QVERIFY(runtimeFailure.message.contains(QStringLiteral("修复")));

    QFile::remove(QDir(models).filePath(
        QStringLiteral("PP-OCRv5_mobile_rec_onnx/inference.onnx")));
    const OcrResult modelFailure = PaddleOcrEngine::validate(valid);
    QCOMPARE(modelFailure.error, OcrError::ModelDataMissing);
    QVERIFY(modelFailure.message.contains(QStringLiteral("模型")));
}

void OcrEngineTest::resolvesPackagedRuntimeAndEnvironmentOverrides()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString imagePath = createImage(directory, QStringLiteral("packaged-page.png"));
    const QString applicationDir = directory.filePath(QStringLiteral("application/bin"));
    const QString runtimeRoot = directory.filePath(QStringLiteral("application/ocr-runtime"));
    const QString runtimeBin = QDir(runtimeRoot).filePath(QStringLiteral("bin"));
    QVERIFY(QDir().mkpath(applicationDir));
    QVERIFY(QDir().mkpath(runtimeBin));
    const QString packagedExecutable = QDir(runtimeBin).filePath(packagedExecutableName());
    QVERIFY(QFile::copy(fixtureExecutable(), packagedExecutable));
    QVERIFY(QFile::setPermissions(packagedExecutable, QFile::permissions(packagedExecutable)
                                                     | QFileDevice::ExeOwner
                                                     | QFileDevice::ExeGroup
                                                     | QFileDevice::ExeOther));
    QTemporaryDir modelSource;
    QVERIFY(modelSource.isValid());
    const QString sourceModels = createModels(modelSource);
    QVERIFY(QDir().mkpath(runtimeRoot));
    QVERIFY(QDir(sourceModels).rename(sourceModels,
                                      QDir(runtimeRoot).filePath(QStringLiteral("models"))));

    ScopedEnvironment noWorker("MINITREASUREBOX_PADDLEOCR_WORKER", QByteArray());
    ScopedEnvironment noModels("MINITREASUREBOX_PADDLEOCR_MODELS", QByteArray());
    const OcrRequest unresolved{imagePath, {}, {}, QStringLiteral("ch"), 2000};
    const OcrRequest packaged = PaddleOcrEngine::resolvedRequest(unresolved, applicationDir);
    QCOMPARE(packaged.workerExecutable, QFileInfo(packagedExecutable).canonicalFilePath());
    QCOMPARE(packaged.modelPath,
             QFileInfo(QDir(runtimeRoot).filePath(QStringLiteral("models"))).canonicalFilePath());

    QTemporaryDir overrides;
    QVERIFY(overrides.isValid());
    const QString overrideModels = createModels(overrides);
    ScopedEnvironment workerOverride("MINITREASUREBOX_PADDLEOCR_WORKER",
                                     fixtureExecutable().toUtf8());
    ScopedEnvironment modelOverride("MINITREASUREBOX_PADDLEOCR_MODELS",
                                    overrideModels.toUtf8());
    const OcrRequest overridden = PaddleOcrEngine::resolvedRequest(unresolved, applicationDir);
    QCOMPARE(overridden.workerExecutable, fixtureExecutable());
    QCOMPARE(overridden.modelPath, QDir::cleanPath(overrideModels));
}

void OcrEngineTest::executesProductionAdapterSuccessAndFailures()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString models = createModels(directory);
    const QString imagePath = createImage(directory, QStringLiteral("ocr-page.png"));
    const PaddleOcrEngine engine;

    const OcrResult success = engine.recognize(requestFor(imagePath, models));
    QVERIFY2(success.ok, qPrintable(success.message));
    QCOMPARE(success.text, QStringLiteral("fixture text for ocr-page"));
    {
        ScopedEnvironment mode("MINITREASUREBOX_FAKE_PADDLEOCR_MODE", "noisy");
        const OcrResult noisy = engine.recognize(requestFor(imagePath, models));
        QVERIFY2(noisy.ok, qPrintable(noisy.message));
        QCOMPARE(noisy.text, QStringLiteral("fixture text for ocr-page"));
    }

    {
        ScopedEnvironment mode("MINITREASUREBOX_FAKE_PADDLEOCR_MODE", "missing");
        QCOMPARE(engine.recognize(requestFor(imagePath, models)).error,
                 OcrError::ModelDataMissing);
    }
    {
        ScopedEnvironment mode("MINITREASUREBOX_FAKE_PADDLEOCR_MODE", "fail");
        QCOMPARE(engine.recognize(requestFor(imagePath, models)).error,
                 OcrError::ProcessFailed);
    }
    {
        ScopedEnvironment mode("MINITREASUREBOX_FAKE_PADDLEOCR_MODE", "protocol");
        QCOMPARE(engine.recognize(requestFor(imagePath, models)).error,
                 OcrError::ProtocolError);
    }
    {
        ScopedEnvironment mode("MINITREASUREBOX_FAKE_PADDLEOCR_MODE", "empty");
        QCOMPARE(engine.recognize(requestFor(imagePath, models)).error,
                 OcrError::EmptyOutput);
    }
}

void OcrEngineTest::enforcesTimeoutAndCancellation()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString models = createModels(directory);
    const QString imagePath = createImage(directory, QStringLiteral("slow.png"));
    PaddleOcrEngine engine;

    OcrRequest timed = requestFor(imagePath, models);
    timed.timeoutMs = 50;
    ScopedEnvironment slow("MINITREASUREBOX_FAKE_PADDLEOCR_MODE", "slow");
    QCOMPARE(engine.recognize(timed).error, OcrError::TimedOut);

    std::atomic_bool cancelled{true};
    QCOMPARE(engine.recognize(requestFor(imagePath, models), &cancelled).error,
             OcrError::Cancelled);

    cancelled.store(false);
    auto future = std::async(std::launch::async, [&] {
        return engine.recognize(requestFor(imagePath, models), &cancelled);
    });
    QTest::qWait(50);
    cancelled.store(true);
    QCOMPARE(future.get().error, OcrError::Cancelled);
}

QTEST_GUILESS_MAIN(OcrEngineTest)
#include "test_ocr_engine.moc"
