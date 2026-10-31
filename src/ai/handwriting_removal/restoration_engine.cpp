#include "ai/handwriting_removal/restoration_engine.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>

#include <algorithm>
#include <utility>

namespace mtb::handwriting {
namespace {

constexpr int kProtocolVersion = 1;
constexpr qsizetype kMaximumProtocolBytes = 1024 * 1024;
constexpr int kTerminationWaitMs = 500;
constexpr qint64 kStaleTaskAgeSeconds = 24 * 60 * 60;
constexpr auto kTaskDirectoryPrefix = "minitreasurebox-restoration-";
constexpr auto kTaskMarkerName = ".minitreasurebox-restoration-task-v1";
constexpr auto kTaskMarkerContents = "MiniTreasureBox exam restoration task v1\n";

RemovalResult failure(RemovalError error, const QString& message,
                      const QString& requestId = {})
{
    RemovalResult result{false, {}, {}, error, message};
    result.metadata.requestId = requestId;
    result.metadata.stage = ProcessingStage::Completed;
    return result;
}

QString executablePath(const QString& configured)
{
    const QFileInfo info(configured.trimmed());
    if (info.isAbsolute()) {
        return info.exists() && info.isFile() && info.isExecutable()
            ? info.canonicalFilePath() : QString();
    }
    return QStandardPaths::findExecutable(configured.trimmed());
}

QStringList runtimeRoots(const QString& applicationDirPath)
{
    const QDir applicationDirectory(applicationDirPath);
    QStringList roots{
        applicationDirectory.filePath(QStringLiteral("exam-restoration-runtime")),
        applicationDirectory.filePath(QStringLiteral("../exam-restoration-runtime")),
        applicationDirectory.filePath(QStringLiteral("../Resources/exam-restoration-runtime")),
        applicationDirectory.filePath(QStringLiteral("../lib/MiniTreasureBox/exam-restoration-runtime")),
    };
    for (QString& root : roots) root = QDir::cleanPath(root);
    roots.removeDuplicates();
    return roots;
}

QString workerName()
{
#ifdef Q_OS_WIN
    return QStringLiteral("exam-restoration-worker.exe");
#else
    return QStringLiteral("exam-restoration-worker");
#endif
}

bool hasModel(const QString& root)
{
    if (root.trimmed().isEmpty()) return false;
    const QFileInfo model(QDir(root).filePath(QStringLiteral("exam-restoration.onnx")));
    return model.exists() && model.isFile() && model.isReadable()
        && !model.canonicalFilePath().isEmpty()
        && QFileInfo(root).canonicalFilePath() == model.dir().canonicalPath();
}

QString taskRoot(const RestorationRuntimeConfig& runtime)
{
    return runtime.temporaryRoot.isEmpty() ? QDir::tempPath() : runtime.temporaryRoot;
}

void cleanupStaleTaskDirectories(const QString& rootPath)
{
    const QFileInfo rootInfo(rootPath);
    if (!rootInfo.isDir() || rootInfo.isSymLink()) return;
    const QString canonicalRoot = rootInfo.canonicalFilePath();
    if (canonicalRoot.isEmpty()) return;

    const QDateTime cutoff = QDateTime::currentDateTimeUtc().addSecs(-kStaleTaskAgeSeconds);
    const QFileInfoList candidates = QDir(canonicalRoot).entryInfoList(
        {QString::fromLatin1(kTaskDirectoryPrefix) + QStringLiteral("*")},
        QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable);
    for (const QFileInfo& candidate : candidates) {
        if (candidate.isSymLink() || candidate.absoluteDir().canonicalPath() != canonicalRoot) {
            continue;
        }
        const QString suffix = candidate.fileName().mid(
            static_cast<qsizetype>(QString::fromLatin1(kTaskDirectoryPrefix).size()));
        if (suffix.size() < 6 || suffix.size() > 64) continue;
        bool safeSuffix = true;
        for (const QChar character : suffix) {
            if (!character.isLetterOrNumber() && character != u'_' && character != u'-') {
                safeSuffix = false;
                break;
            }
        }
        if (!safeSuffix) continue;

        const QFileInfo markerInfo(QDir(candidate.absoluteFilePath()).filePath(
            QString::fromLatin1(kTaskMarkerName)));
        if (!markerInfo.isFile() || markerInfo.isSymLink()
            || markerInfo.lastModified().toUTC() >= cutoff) {
            continue;
        }
        QFile marker(markerInfo.absoluteFilePath());
        if (!marker.open(QIODevice::ReadOnly)) continue;
        const bool owned = marker.readAll() == QByteArray(kTaskMarkerContents);
        marker.close();
        if (!owned) continue;
        QDir(candidate.absoluteFilePath()).removeRecursively();
    }
}

bool writeTaskMarker(const QTemporaryDir& directory)
{
    QFile marker(directory.filePath(QString::fromLatin1(kTaskMarkerName)));
    if (!marker.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return false;
    return marker.write(kTaskMarkerContents) == QByteArray(kTaskMarkerContents).size();
}

void report(const RestorationRequest& request, ProcessingStage stage, int percent)
{
    if (request.progress) request.progress({request.requestId, stage, percent});
}

void stopProcess(QProcess& process)
{
    if (process.state() == QProcess::NotRunning) return;
#ifdef Q_OS_WIN
    // QProcess::terminate() cannot stop console workers without a WM_CLOSE-aware event loop.
    process.kill();
    process.waitForFinished(kTerminationWaitMs);
#else
    process.terminate();
    if (!process.waitForFinished(kTerminationWaitMs)) {
        process.kill();
        process.waitForFinished(kTerminationWaitMs);
    }
#endif
}

struct ProcessResult {
    QByteArray output;
    RemovalError error{RemovalError::None};
    int exitCode{0};
};

ProcessResult runProcess(const QString& executable, const QStringList& arguments,
                         int timeoutMs, const CancellationCheck& cancelled)
{
    if (timeoutMs <= 0) return {{}, RemovalError::TimedOut};
    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.setStandardErrorFile(QProcess::nullDevice());
    process.setProgram(executable);
    process.setArguments(arguments);
    process.start(QIODevice::ReadOnly);

    QElapsedTimer timer;
    timer.start();
    QByteArray output;
    while (process.state() != QProcess::NotRunning) {
        if (cancelled && cancelled()) {
            stopProcess(process);
            return {{}, RemovalError::Cancelled};
        }
        if (timer.elapsed() >= timeoutMs) {
            stopProcess(process);
            return {{}, RemovalError::TimedOut};
        }
        process.waitForFinished(20);
        output.append(process.readAllStandardOutput());
        if (output.size() > kMaximumProtocolBytes) {
            stopProcess(process);
            return {{}, RemovalError::ProtocolError};
        }
        QThread::yieldCurrentThread();
    }
    output.append(process.readAllStandardOutput());
    if (output.size() > kMaximumProtocolBytes) return {{}, RemovalError::ProtocolError};
    if (process.error() == QProcess::FailedToStart) return {{}, RemovalError::RuntimeMissing};
    if (process.exitStatus() != QProcess::NormalExit) return {{}, RemovalError::ProcessCrashed};
    return {output, RemovalError::None, process.exitCode()};
}

bool parseObject(const QByteArray& data, QJsonObject* object)
{
    if (data.isEmpty() || data.size() > kMaximumProtocolBytes) return false;
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(data.trimmed(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return false;
    *object = document.object();
    return true;
}

RemovalError workerError(const QString& code)
{
    if (code == QStringLiteral("invalid_input")) return RemovalError::InvalidInput;
    if (code == QStringLiteral("model_missing")) return RemovalError::ModelDataMissing;
    if (code == QStringLiteral("resource_limit")) return RemovalError::ResourceLimit;
    if (code == QStringLiteral("cancelled")) return RemovalError::Cancelled;
    return RemovalError::ProcessingFailed;
}

ConfidenceLevel confidenceLevel(const QString& value)
{
    if (value == QStringLiteral("high")) return ConfidenceLevel::High;
    if (value == QStringLiteral("medium")) return ConfidenceLevel::Medium;
    if (value == QStringLiteral("low")) return ConfidenceLevel::Low;
    return ConfidenceLevel::Unknown;
}

QString safeFailureMessage(RemovalError error)
{
    switch (error) {
    case RemovalError::RuntimeMissing:
    case RemovalError::ModelDataMissing:
        return QStringLiteral("AI 还原组件不可用，已使用基础模式生成可编辑结果。");
    case RemovalError::TimedOut:
        return QStringLiteral("AI 还原处理超时。原图片未修改。");
    case RemovalError::ResourceLimit:
        return QStringLiteral("图片超出本地处理资源限制。原图片未修改。");
    case RemovalError::Cancelled:
        return QStringLiteral("处理已取消。原图片未修改。");
    case RemovalError::ProtocolError:
    case RemovalError::ProcessCrashed:
    case RemovalError::ProcessingFailed:
        return QStringLiteral("AI 还原处理失败，已使用基础模式生成可编辑结果。");
    default:
        return QStringLiteral("图片无法处理。原图片未修改。");
    }
}

int remainingTimeMs(const QElapsedTimer& timer, int timeoutMs)
{
    const qint64 remaining = static_cast<qint64>(timeoutMs) - timer.elapsed();
    return static_cast<int>(std::clamp<qint64>(remaining, 0, timeoutMs));
}

RemovalResult baselineFallback(const BaselineHandwritingRemovalEngine& baseline,
                               const RestorationRequest& request,
                               RemovalError reason,
                               const QElapsedTimer& deadlineTimer,
                               int timeoutMs,
                               const CancellationCheck& cancelled)
{
    if (remainingTimeMs(deadlineTimer, timeoutMs) <= 0) {
        return failure(RemovalError::TimedOut,
                       safeFailureMessage(RemovalError::TimedOut), request.requestId);
    }
    const CancellationCheck boundedCancellation = [&] {
        return (cancelled && cancelled())
            || remainingTimeMs(deadlineTimer, timeoutMs) <= 0;
    };
    RemovalResult result = baseline.detectAndRemove(request.correctedImage,
                                                    request.options,
                                                    boundedCancellation);
    if (!result.ok) {
        if (cancelled && cancelled()) {
            return failure(RemovalError::Cancelled,
                           safeFailureMessage(RemovalError::Cancelled), request.requestId);
        }
        if (remainingTimeMs(deadlineTimer, timeoutMs) <= 0) {
            return failure(RemovalError::TimedOut,
                           safeFailureMessage(RemovalError::TimedOut), request.requestId);
        }
        return result;
    }
    result.message = reason == RemovalError::TimedOut
        ? QStringLiteral("AI 还原处理超时，已使用基础模式生成可编辑结果。")
        : safeFailureMessage(reason);
    result.metadata.requestId = request.requestId;
    result.metadata.executionMode = ExecutionMode::Baseline;
    result.metadata.confidence = ConfidenceLevel::Low;
    result.metadata.confidenceReasons = {QStringLiteral("AI 组件未通过本地可用性检查")};
    result.metadata.fallbackReason = reason;
    result.metadata.stage = ProcessingStage::Completed;
    return result;
}

bool validOptions(const RemovalOptions& options)
{
    return options.colorDifferenceThreshold >= 0
        && options.colorDifferenceThreshold <= 255
        && options.maximumInkBrightness >= 0
        && options.maximumInkBrightness <= 255
        && options.fillRadius > 0 && options.fillRadius <= 128;
}

bool validImageBudget(const QImage& image)
{
    if (image.isNull() || image.width() <= 0 || image.height() <= 0) return false;
    return static_cast<qint64>(image.width())
        <= MaximumImagePixels / static_cast<qint64>(image.height());
}

}  // namespace

RestorationWorkerEngine::RestorationWorkerEngine(RestorationRuntimeConfig config)
    : config_(std::move(config))
{
}

RestorationRuntimeConfig RestorationWorkerEngine::resolvedConfig(
    const RestorationRuntimeConfig& config, const QString& applicationDirPath)
{
    RestorationRuntimeConfig resolved = config;
    const QString appDir = applicationDirPath.trimmed().isEmpty()
        ? QCoreApplication::applicationDirPath() : applicationDirPath;

    QString packagedWorker;
    QString packagedModels;
    for (const QString& root : runtimeRoots(appDir)) {
        const QString candidate = QDir(root).filePath(QStringLiteral("bin/") + workerName());
        const QString models = QDir(root).filePath(QStringLiteral("models"));
        if (!executablePath(candidate).isEmpty() && hasModel(models)) {
            packagedWorker = QFileInfo(candidate).canonicalFilePath();
            packagedModels = QFileInfo(models).canonicalFilePath();
            break;
        }
    }

    const QString configuredWorker = config.workerExecutable.trimmed();
    const QString configuredModels = config.modelRoot.trimmed();
    const QString environmentWorker = qEnvironmentVariable(
        "MINITREASUREBOX_EXAM_RESTORATION_WORKER").trimmed();
    const QString environmentModels = qEnvironmentVariable(
        "MINITREASUREBOX_EXAM_RESTORATION_MODELS").trimmed();

    resolved.workerExecutable.clear();
    resolved.modelRoot.clear();
    if (!configuredWorker.isEmpty() || !configuredModels.isEmpty()) {
        if (!configuredWorker.isEmpty() && !configuredModels.isEmpty()) {
            resolved.workerExecutable = configuredWorker;
            resolved.modelRoot = configuredModels;
        }
    } else if (!environmentWorker.isEmpty() || !environmentModels.isEmpty()) {
        if (!environmentWorker.isEmpty() && !environmentModels.isEmpty()) {
            resolved.workerExecutable = environmentWorker;
            resolved.modelRoot = environmentModels;
        }
    } else {
        resolved.workerExecutable = packagedWorker;
        resolved.modelRoot = packagedModels;
    }
    if (!resolved.modelRoot.isEmpty()) {
        resolved.modelRoot = QDir::cleanPath(resolved.modelRoot);
    }
    const QString configuredTemporaryRoot = config.temporaryRoot.trimmed();
    resolved.temporaryRoot = configuredTemporaryRoot.isEmpty()
        ? QString() : QDir::cleanPath(configuredTemporaryRoot);
    return resolved;
}

void RestorationWorkerEngine::cleanupStaleTasks(const QString& temporaryRoot)
{
    const QString requestedRoot = temporaryRoot.trimmed();
    cleanupStaleTaskDirectories(requestedRoot.isEmpty()
        ? QDir::tempPath() : QDir::cleanPath(requestedRoot));
}

RemovalResult RestorationWorkerEngine::detectAndRemove(
    const QImage& image, const RemovalOptions& options,
    const CancellationCheck& cancelled) const
{
    return detectAndRemoveWithProgress(image, options, {}, cancelled);
}

RemovalResult RestorationWorkerEngine::detectAndRemoveWithProgress(
    const QImage& image, const RemovalOptions& options,
    const ProgressCallback& progress,
    const CancellationCheck& cancelled) const
{
    RestorationRequest request{image, options,
                               QUuid::createUuid().toString(QUuid::WithoutBraces),
                               progress};
    return restore(request, cancelled);
}

RemovalResult RestorationWorkerEngine::removeWithMask(
    const QImage& image, const QImage& mask, const RemovalOptions& options,
    const CancellationCheck& cancelled) const
{
    return baseline_.removeWithMask(image, mask, options, cancelled);
}

RemovalResult RestorationWorkerEngine::restore(
    const RestorationRequest& suppliedRequest,
    const CancellationCheck& cancelled) const
{
    RestorationRequest request = suppliedRequest;
    if (request.requestId.trimmed().isEmpty()) {
        request.requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    report(request, ProcessingStage::Validating, 0);
    if (cancelled && cancelled()) {
        return failure(RemovalError::Cancelled, safeFailureMessage(RemovalError::Cancelled),
                       request.requestId);
    }
    if (request.correctedImage.isNull() || !validOptions(request.options)) {
        return failure(RemovalError::InvalidInput,
                       QStringLiteral("图片或处理参数无效。原图片未修改。"), request.requestId);
    }
    if (!validImageBudget(request.correctedImage)) {
        return failure(RemovalError::ResourceLimit,
                       safeFailureMessage(RemovalError::ResourceLimit), request.requestId);
    }

    const RestorationRuntimeConfig runtime = resolvedConfig(config_);
    if (runtime.timeoutMs <= 0) {
        return failure(RemovalError::InvalidInput,
                       QStringLiteral("本地处理超时时间无效。"), request.requestId);
    }
    QElapsedTimer deadlineTimer;
    deadlineTimer.start();
    cleanupStaleTaskDirectories(taskRoot(runtime));
    const QString executable = executablePath(runtime.workerExecutable);
    if (executable.isEmpty()) {
        return baselineFallback(baseline_, request, RemovalError::RuntimeMissing,
                                deadlineTimer, runtime.timeoutMs, cancelled);
    }
    if (!hasModel(runtime.modelRoot)) {
        return baselineFallback(baseline_, request, RemovalError::ModelDataMissing,
                                deadlineTimer, runtime.timeoutMs, cancelled);
    }

    report(request, ProcessingStage::Initializing, 5);
    const ProcessResult selfTest = runProcess(
        executable,
        {QStringLiteral("--models"), QFileInfo(runtime.modelRoot).canonicalFilePath(),
         QStringLiteral("--self-test")},
        remainingTimeMs(deadlineTimer, runtime.timeoutMs), cancelled);
    if (selfTest.error == RemovalError::Cancelled) {
        return failure(selfTest.error, safeFailureMessage(selfTest.error), request.requestId);
    }
    QJsonObject selfTestObject;
    if (selfTest.error != RemovalError::None
        || selfTest.exitCode != 0
        || !parseObject(selfTest.output, &selfTestObject)
        || !selfTestObject.value(QStringLiteral("ok")).toBool()
        || selfTestObject.value(QStringLiteral("schema_version")).toInt() != kProtocolVersion
        || selfTestObject.value(QStringLiteral("backend")).toString()
            != QStringLiteral("onnxruntime")) {
        const RemovalError reason = selfTest.error == RemovalError::None
            ? RemovalError::ProtocolError : selfTest.error;
        return baselineFallback(baseline_, request, reason,
                                deadlineTimer, runtime.timeoutMs, cancelled);
    }

    QString pattern = runtime.temporaryRoot.isEmpty()
        ? QDir::temp().filePath(QStringLiteral("minitreasurebox-restoration-XXXXXX"))
        : QDir(runtime.temporaryRoot).filePath(QStringLiteral("minitreasurebox-restoration-XXXXXX"));
    if (!runtime.temporaryRoot.isEmpty() && !QFileInfo(runtime.temporaryRoot).isDir()) {
        return baselineFallback(baseline_, request, RemovalError::ProcessingFailed,
                                deadlineTimer, runtime.timeoutMs, cancelled);
    }
    QTemporaryDir taskDirectory(pattern);
    taskDirectory.setAutoRemove(true);
    if (!taskDirectory.isValid() || !writeTaskMarker(taskDirectory)) {
        return baselineFallback(baseline_, request, RemovalError::ProcessingFailed,
                                deadlineTimer, runtime.timeoutMs, cancelled);
    }

    const QString inputPath = taskDirectory.filePath(QStringLiteral("input.png"));
    const QString maskPath = taskDirectory.filePath(QStringLiteral("mask.png"));
    const QString outputPath = taskDirectory.filePath(QStringLiteral("restored.png"));
    if (!request.correctedImage.save(inputPath, "PNG")) {
        return baselineFallback(baseline_, request, RemovalError::ProcessingFailed,
                                deadlineTimer, runtime.timeoutMs, cancelled);
    }

    report(request, ProcessingStage::Segmenting, 15);
    const ProcessResult execution = runProcess(
        executable,
        {QStringLiteral("--models"), QFileInfo(runtime.modelRoot).canonicalFilePath(),
         QStringLiteral("--image"), inputPath,
         QStringLiteral("--mask"), maskPath,
         QStringLiteral("--output"), outputPath,
         QStringLiteral("--request-id"), request.requestId},
        remainingTimeMs(deadlineTimer, runtime.timeoutMs), cancelled);
    if (execution.error == RemovalError::Cancelled) {
        return failure(execution.error, safeFailureMessage(execution.error), request.requestId);
    }
    if (execution.error != RemovalError::None) {
        return baselineFallback(baseline_, request, execution.error,
                                deadlineTimer, runtime.timeoutMs, cancelled);
    }

    QJsonObject response;
    if (!parseObject(execution.output, &response)
        || response.value(QStringLiteral("schema_version")).toInt() != kProtocolVersion
        || response.value(QStringLiteral("request_id")).toString() != request.requestId) {
        return baselineFallback(baseline_, request, RemovalError::ProtocolError,
                                deadlineTimer, runtime.timeoutMs, cancelled);
    }
    if (!response.value(QStringLiteral("ok")).toBool()) {
        const RemovalError reason = workerError(response.value(QStringLiteral("error")).toString());
        if (reason == RemovalError::Cancelled) {
            return failure(reason, safeFailureMessage(reason), request.requestId);
        }
        return baselineFallback(baseline_, request, reason,
                                deadlineTimer, runtime.timeoutMs, cancelled);
    }
    if (execution.exitCode != 0) {
        return baselineFallback(baseline_, request, RemovalError::ProcessingFailed,
                                deadlineTimer, runtime.timeoutMs, cancelled);
    }

    report(request, ProcessingStage::Fusing, 75);
    QImageReader maskReader(maskPath);
    QImageReader outputReader(outputPath);
    maskReader.setAutoTransform(false);
    outputReader.setAutoTransform(false);
    QImage mask = maskReader.read();
    QImage workerOutput = outputReader.read();
    if (mask.isNull() || workerOutput.isNull()
        || mask.size() != request.correctedImage.size()
        || workerOutput.size() != request.correctedImage.size()) {
        return baselineFallback(baseline_, request, RemovalError::ProtocolError,
                                deadlineTimer, runtime.timeoutMs, cancelled);
    }
    mask = mask.convertToFormat(QImage::Format_Grayscale8);
    workerOutput = workerOutput.convertToFormat(QImage::Format_ARGB32);
    QImage restored = request.correctedImage.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < restored.height(); ++y) {
        if (cancelled && cancelled()) {
            return failure(RemovalError::Cancelled,
                           safeFailureMessage(RemovalError::Cancelled), request.requestId);
        }
        if (remainingTimeMs(deadlineTimer, runtime.timeoutMs) <= 0) {
            return failure(RemovalError::TimedOut,
                           safeFailureMessage(RemovalError::TimedOut), request.requestId);
        }
        uchar* maskLine = mask.scanLine(y);
        QRgb* destination = reinterpret_cast<QRgb*>(restored.scanLine(y));
        const QRgb* source = reinterpret_cast<const QRgb*>(workerOutput.constScanLine(y));
        for (int x = 0; x < restored.width(); ++x) {
            maskLine[x] = maskLine[x] >= 128 ? 255 : 0;
            if (maskLine[x]) destination[x] = source[x];
        }
    }

    report(request, ProcessingStage::Restoring, 95);
    RemovalResult result{true, mask, restored, RemovalError::None,
                         QStringLiteral("AI 本地还原完成。")};
    result.metadata.requestId = request.requestId;
    result.metadata.executionMode = ExecutionMode::OfflineModel;
    result.metadata.stage = ProcessingStage::Completed;
    result.metadata.confidence = confidenceLevel(
        response.value(QStringLiteral("confidence")).toString());
    const QJsonArray reasons = response.value(QStringLiteral("confidence_reasons")).toArray();
    for (const QJsonValue& reason : reasons) {
        if (reason.isString() && result.metadata.confidenceReasons.size() < 8) {
            result.metadata.confidenceReasons.append(reason.toString().left(160));
        }
    }
    result.metadata.modelVersion = response.value(QStringLiteral("model_version"))
                                       .toString().left(80);
    report(request, ProcessingStage::Completed, 100);
    return result;
}

}  // namespace mtb::handwriting
