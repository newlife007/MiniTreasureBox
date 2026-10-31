#include "ai/handwriting_removal/restoration_engine.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QTest>

#include <atomic>
#include <future>

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

class ScopedMode final {
public:
    explicit ScopedMode(const QByteArray& mode)
        : existed_(qEnvironmentVariableIsSet("MINITREASUREBOX_FAKE_RESTORATION_MODE")),
          old_(qgetenv("MINITREASUREBOX_FAKE_RESTORATION_MODE"))
    {
        qputenv("MINITREASUREBOX_FAKE_RESTORATION_MODE", mode);
    }
    ~ScopedMode()
    {
        if (existed_) qputenv("MINITREASUREBOX_FAKE_RESTORATION_MODE", old_);
        else qunsetenv("MINITREASUREBOX_FAKE_RESTORATION_MODE");
    }
private:
    bool existed_;
    QByteArray old_;
};

RestorationRuntimeConfig runtime(QTemporaryDir& directory, int timeoutMs = 2000)
{
    const QString modelRoot = directory.filePath(QStringLiteral("models"));
    if (!QDir().mkpath(modelRoot)) return {};
    QFile model(QDir(modelRoot).filePath(QStringLiteral("exam-restoration.onnx")));
    if (!model.open(QIODevice::WriteOnly)) return {};
    model.write("fixture");
    model.close();
    const QString tasks = directory.filePath(QStringLiteral("tasks"));
    if (!QDir().mkpath(tasks)) return {};
    return {fixtureExecutable(), modelRoot, tasks, timeoutMs};
}

QImage page()
{
    QImage image(16, 12, QImage::Format_ARGB32);
    image.fill(QColor(245, 243, 237, 211));
    image.setPixelColor(1, 1, QColor(10, 20, 180, 170));
    return image;
}

}  // namespace

class ExamRestorationEngineTest final : public QObject {
    Q_OBJECT
private slots:
    void executesWorkerAndHardCopiesOutsideMask();
    void fallsBackForUnavailableOrInvalidWorker();
    void enforcesTimeoutCancellationPixelBudgetAndCleanup();
};

void ExamRestorationEngineTest::executesWorkerAndHardCopiesOutsideMask()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const RestorationRuntimeConfig config = runtime(directory);
    QVERIFY(!config.workerExecutable.isEmpty());
    RestorationWorkerEngine engine(config);
    const QImage input = page();
    QList<RemovalProgress> progress;
    RestorationRequest request{input, {}, QStringLiteral("request-17"),
                               [&](const RemovalProgress& update) { progress.append(update); }};
    const RemovalResult result = engine.restore(request);
    QVERIFY2(result.ok, qPrintable(result.message));
    QCOMPARE(result.metadata.executionMode, ExecutionMode::OfflineModel);
    QCOMPARE(result.metadata.requestId, QStringLiteral("request-17"));
    QCOMPARE(result.metadata.confidence, ConfidenceLevel::Medium);
    QCOMPARE(result.metadata.modelVersion, QStringLiteral("fixture-v1"));
    QVERIFY(!progress.isEmpty());
    QCOMPARE(progress.constLast().stage, ProcessingStage::Completed);
    QCOMPARE(progress.constLast().percent, 100);

    const QPoint center(input.width() / 2, input.height() / 2);
    QCOMPARE(qGray(result.mask.pixel(center)), 255);
    QCOMPARE(result.restored.pixelColor(center), QColor(Qt::white));
    for (int y = 0; y < input.height(); ++y) {
        for (int x = 0; x < input.width(); ++x) {
            if (QPoint(x, y) != center) {
                QCOMPARE(result.restored.pixelColor(x, y), input.pixelColor(x, y));
            }
        }
    }
    QCOMPARE(QDir(config.temporaryRoot).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size(), 0);
}

void ExamRestorationEngineTest::fallsBackForUnavailableOrInvalidWorker()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    RestorationRuntimeConfig config = runtime(directory);
    const QImage input = page();

    config.workerExecutable = directory.filePath(QStringLiteral("absent-worker"));
    RemovalResult result = RestorationWorkerEngine(config).restore(
        {input, {}, QStringLiteral("missing-runtime"), {}});
    QVERIFY(result.ok);
    QCOMPARE(result.metadata.executionMode, ExecutionMode::Baseline);
    QCOMPARE(result.metadata.fallbackReason, RemovalError::RuntimeMissing);
    QVERIFY(result.message.contains(QStringLiteral("基础模式")));

    config = runtime(directory);
    for (const QByteArray mode : {QByteArray("self-test-fail"), QByteArray("malformed"),
                                  QByteArray("oversized"), QByteArray("mismatch"),
                                  QByteArray("schema"), QByteArray("missing"),
                                  QByteArray("crash")}) {
        ScopedMode scoped(mode);
        result = RestorationWorkerEngine(config).restore(
            {input, {}, QString::fromLatin1(mode), {}});
        QVERIFY2(result.ok, mode.constData());
        QCOMPARE(result.metadata.executionMode, ExecutionMode::Baseline);
        QVERIFY(result.metadata.fallbackReason != RemovalError::None);
        QVERIFY(!result.message.contains(directory.path()));
    }
}

void ExamRestorationEngineTest::enforcesTimeoutCancellationPixelBudgetAndCleanup()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const RestorationRuntimeConfig config = runtime(directory, 50);
    const QImage input = page();
    RestorationWorkerEngine engine(config);
    {
        ScopedMode mode("timeout");
        const RemovalResult timed = engine.restore(
            {input, {}, QStringLiteral("timed"), {}});
        QVERIFY(timed.ok);
        QCOMPARE(timed.metadata.fallbackReason, RemovalError::TimedOut);
    }

    std::atomic_bool cancel{false};
    {
        ScopedMode mode("timeout");
        RestorationRuntimeConfig longConfig = config;
        longConfig.timeoutMs = 5000;
        RestorationWorkerEngine cancellable(longConfig);
        auto future = std::async(std::launch::async, [&] {
            return cancellable.restore({input, {}, QStringLiteral("cancelled"), {}},
                                       [&] { return cancel.load(); });
        });
        QTest::qWait(80);
        cancel.store(true);
        QCOMPARE(future.get().error, RemovalError::Cancelled);
    }

    QImage oversized(10000, 4001, QImage::Format_Grayscale8);
    QVERIFY(!oversized.isNull());
    const RemovalResult limited = engine.restore(
        {oversized, {}, QStringLiteral("large"), {}});
    QVERIFY(!limited.ok);
    QCOMPARE(limited.error, RemovalError::ResourceLimit);
    QCOMPARE(QDir(config.temporaryRoot).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size(), 0);
}

QTEST_GUILESS_MAIN(ExamRestorationEngineTest)
#include "test_exam_restoration_engine.moc"
