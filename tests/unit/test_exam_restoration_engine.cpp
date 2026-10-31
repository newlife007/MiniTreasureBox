#include "ai/handwriting_removal/restoration_engine.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
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

class ScopedEnvironment final {
public:
    ScopedEnvironment(const char* name, const QByteArray& value)
        : name_(name), existed_(qEnvironmentVariableIsSet(name)), old_(qgetenv(name))
    {
        if (value.isNull()) qunsetenv(name_);
        else qputenv(name_, value);
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

class ScopedMode final {
public:
    explicit ScopedMode(const QByteArray& mode)
        : environment_("MINITREASUREBOX_FAKE_RESTORATION_MODE", mode)
    {
    }
private:
    ScopedEnvironment environment_;
};

class ScopedCurrentDirectory final {
public:
    explicit ScopedCurrentDirectory(const QString& path)
        : previous_(QDir::currentPath())
    {
        changed_ = QDir::setCurrent(path);
    }
    ~ScopedCurrentDirectory()
    {
        if (changed_) QDir::setCurrent(previous_);
    }
    [[nodiscard]] bool changed() const { return changed_; }
private:
    QString previous_;
    bool changed_{false};
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

bool createTaskDirectory(const QString& root, const QString& name,
                         const QByteArray& markerContents, bool stale)
{
    const QString path = QDir(root).filePath(name);
    if (!QDir().mkpath(path)) return false;
    QFile input(QDir(path).filePath(QStringLiteral("input.png")));
    if (!input.open(QIODevice::WriteOnly) || input.write("sensitive fixture") < 0) return false;
    input.close();
    QFile marker(QDir(path).filePath(
        QStringLiteral(".minitreasurebox-restoration-task-v1")));
    if (!marker.open(QIODevice::WriteOnly) || marker.write(markerContents) < 0
        || !marker.flush()) {
        return false;
    }
    if (stale && !marker.setFileTime(QDateTime::currentDateTimeUtc().addDays(-2),
                                     QFileDevice::FileModificationTime)) {
        return false;
    }
    return true;
}

}  // namespace

class ExamRestorationEngineTest final : public QObject {
    Q_OBJECT
private slots:
    void executesWorkerAndHardCopiesOutsideMask();
    void fallsBackForUnavailableOrInvalidWorker();
    void failsClosedAgainstImplicitRuntimeDiscovery();
    void enforcesTimeoutCancellationPixelBudgetAndCleanup();
    void removesOnlyOwnedStaleTaskDirectories();
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

void ExamRestorationEngineTest::failsClosedAgainstImplicitRuntimeDiscovery()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString hostile = directory.filePath(QStringLiteral("hostile"));
    const QString application = directory.filePath(QStringLiteral("application"));
    QVERIFY(QDir().mkpath(hostile));
    QVERIFY(QDir().mkpath(application));
#ifdef Q_OS_WIN
    const QString worker = QDir(hostile).filePath(QStringLiteral("exam-restoration-worker.exe"));
#else
    const QString worker = QDir(hostile).filePath(QStringLiteral("exam-restoration-worker"));
#endif
    QVERIFY(QFile::copy(fixtureExecutable(), worker));
    QVERIFY(QFile::setPermissions(worker, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                  | QFileDevice::ExeOwner | QFileDevice::ReadGroup
                                  | QFileDevice::ExeGroup | QFileDevice::ReadOther
                                  | QFileDevice::ExeOther));
    QFile model(QDir(hostile).filePath(QStringLiteral("exam-restoration.onnx")));
    QVERIFY(model.open(QIODevice::WriteOnly));
    QCOMPARE(model.write("fixture"), 7);
    model.close();

    ScopedEnvironment workerOverride("MINITREASUREBOX_EXAM_RESTORATION_WORKER", {});
    ScopedEnvironment modelOverride("MINITREASUREBOX_EXAM_RESTORATION_MODELS", {});
    QByteArray hostilePath = hostile.toLocal8Bit();
    const QByteArray originalPath = qgetenv("PATH");
    if (!originalPath.isEmpty()) {
        hostilePath.append(QDir::listSeparator().toLatin1());
        hostilePath.append(originalPath);
    }
    ScopedEnvironment path("PATH", hostilePath);
    ScopedCurrentDirectory current(hostile);
    QVERIFY(current.changed());

    const RestorationRuntimeConfig unresolved =
        RestorationWorkerEngine::resolvedConfig({}, application);
    QVERIFY(unresolved.workerExecutable.isEmpty());
    QVERIFY(unresolved.modelRoot.isEmpty());
    const RemovalResult fallback = RestorationWorkerEngine().restore(
        {page(), {}, QStringLiteral("hostile-search-path"), {}});
    QVERIFY(fallback.ok);
    QCOMPARE(fallback.metadata.executionMode, ExecutionMode::Baseline);
    QCOMPARE(fallback.metadata.fallbackReason, RemovalError::RuntimeMissing);

    RestorationRuntimeConfig partial;
    partial.workerExecutable = QStringLiteral("exam-restoration-worker");
    const RestorationRuntimeConfig rejected =
        RestorationWorkerEngine::resolvedConfig(partial, application);
    QVERIFY(rejected.workerExecutable.isEmpty());
    QVERIFY(rejected.modelRoot.isEmpty());

    RestorationRuntimeConfig explicitPair;
    explicitPair.workerExecutable = QStringLiteral("exam-restoration-worker");
    explicitPair.modelRoot = hostile;
    const RemovalResult explicitResult = RestorationWorkerEngine(explicitPair).restore(
        {page(), {}, QStringLiteral("explicit-pair"), {}});
    QVERIFY2(explicitResult.ok, qPrintable(explicitResult.message));
    QCOMPARE(explicitResult.metadata.executionMode, ExecutionMode::OfflineModel);
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
        QVERIFY(!timed.ok);
        QCOMPARE(timed.error, RemovalError::TimedOut);
    }
    {
        ScopedMode mode("cumulative-timeout");
        RestorationRuntimeConfig cumulativeConfig = config;
        cumulativeConfig.timeoutMs = 180;
        QElapsedTimer elapsed;
        elapsed.start();
        const RemovalResult timed = RestorationWorkerEngine(cumulativeConfig).restore(
            {input, {}, QStringLiteral("cumulative-timed"), {}});
        QVERIFY(!timed.ok);
        QCOMPARE(timed.error, RemovalError::TimedOut);
        QVERIFY2(elapsed.elapsed() < 320,
                 "Self-test and inference must share one operation deadline");
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

void ExamRestorationEngineTest::removesOnlyOwnedStaleTaskDirectories()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const RestorationRuntimeConfig config = runtime(directory);
    const QByteArray ownerMarker("MiniTreasureBox exam restoration task v1\n");
    QVERIFY(createTaskDirectory(config.temporaryRoot,
                                QStringLiteral("minitreasurebox-restoration-stale01"),
                                ownerMarker, true));
    QVERIFY(createTaskDirectory(config.temporaryRoot,
                                QStringLiteral("minitreasurebox-restoration-active01"),
                                ownerMarker, false));
    QVERIFY(createTaskDirectory(config.temporaryRoot,
                                QStringLiteral("minitreasurebox-restoration-foreign01"),
                                QByteArray("another application\n"), true));
    QVERIFY(QDir().mkpath(QDir(config.temporaryRoot).filePath(QStringLiteral("unrelated"))));

    const RemovalResult result = RestorationWorkerEngine(config).restore(
        {page(), {}, QStringLiteral("stale-cleanup"), {}});
    QVERIFY2(result.ok, qPrintable(result.message));
    QVERIFY(!QFileInfo::exists(QDir(config.temporaryRoot).filePath(
        QStringLiteral("minitreasurebox-restoration-stale01"))));
    QVERIFY(QFileInfo::exists(QDir(config.temporaryRoot).filePath(
        QStringLiteral("minitreasurebox-restoration-active01/input.png"))));
    QVERIFY(QFileInfo::exists(QDir(config.temporaryRoot).filePath(
        QStringLiteral("minitreasurebox-restoration-foreign01/input.png"))));
    QVERIFY(QFileInfo::exists(QDir(config.temporaryRoot).filePath(
        QStringLiteral("unrelated"))));
}

QTEST_GUILESS_MAIN(ExamRestorationEngineTest)
#include "test_exam_restoration_engine.moc"
