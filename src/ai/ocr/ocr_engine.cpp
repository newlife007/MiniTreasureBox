#include "ai/ocr/ocr_engine.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QThread>

namespace mtb::ocr {
namespace {

constexpr qsizetype kMaximumCapturedOutput = 1024 * 1024;

OcrResult failure(OcrError error, const QString& message)
{
    return {false, {}, error, message};
}

QString resolvedExecutable(const QString& configured)
{
    const QString value = configured.trimmed();
    const QFileInfo info(value);
    if (info.isAbsolute()) {
        return info.exists() && info.isFile() && info.isExecutable()
            ? info.canonicalFilePath()
            : QString();
    }
    return QStandardPaths::findExecutable(value);
}

QStringList packagedRuntimeRoots(const QString& applicationDirPath)
{
    const QDir applicationDirectory(applicationDirPath);
    QStringList roots{
        applicationDirectory.filePath(QStringLiteral("ocr-runtime")),
        applicationDirectory.filePath(QStringLiteral("../ocr-runtime")),
        applicationDirectory.filePath(QStringLiteral("../Resources/ocr-runtime")),
        applicationDirectory.filePath(QStringLiteral("../lib/MiniTreasureBox/ocr-runtime")),
    };
    for (QString& root : roots) root = QDir::cleanPath(root);
    roots.removeDuplicates();
    return roots;
}

struct PackagedRuntime {
    QString executable;
    QString modelPath;
};

PackagedRuntime findPackagedRuntime(const QString& applicationDirPath)
{
#ifdef Q_OS_WIN
    const QString executableName = QStringLiteral("paddleocr-worker.exe");
#else
    const QString executableName = QStringLiteral("paddleocr-worker");
#endif
    for (const QString& root : packagedRuntimeRoots(applicationDirPath)) {
        const QDir runtime(root);
        const QString executable = runtime.filePath(QStringLiteral("bin/") + executableName);
        const QString models = runtime.filePath(QStringLiteral("models"));
        if (!resolvedExecutable(executable).isEmpty() && QFileInfo(models).isDir()) {
            return {QFileInfo(executable).canonicalFilePath(), QDir(models).canonicalPath()};
        }
    }
    return {};
}

bool modelFilesExist(const QString& modelPath)
{
    const QDir root(modelPath);
    if (!root.exists()) return false;
    for (const QString& model : {QStringLiteral("PP-OCRv5_mobile_det_onnx"),
                                 QStringLiteral("PP-OCRv5_mobile_rec_onnx")}) {
        const QDir directory(root.filePath(model));
        if (!QFileInfo::exists(directory.filePath(QStringLiteral("inference.onnx")))
            || !QFileInfo::exists(directory.filePath(QStringLiteral("inference.yml")))) {
            return false;
        }
    }
    return true;
}

void appendCapped(QByteArray& destination, const QByteArray& data)
{
    destination.append(data);
    if (destination.size() > kMaximumCapturedOutput) {
        destination.remove(0, destination.size() - kMaximumCapturedOutput);
    }
}

OcrError workerError(const QString& code)
{
    if (code == QStringLiteral("invalid_input")) return OcrError::InvalidInput;
    if (code == QStringLiteral("model_missing")) return OcrError::ModelDataMissing;
    if (code == QStringLiteral("empty_output")) return OcrError::EmptyOutput;
    return OcrError::ProcessFailed;
}

QByteArray normalizedWorkerOutput(const QByteArray& output)
{
    const qsizetype start = qMax<qsizetype>(0, output.size() - kMaximumCapturedOutput);
    QByteArray normalized;
    normalized.reserve(output.size() - start);
    for (qsizetype index = start; index < output.size(); ++index) {
        const char character = output.at(index);
        if (character != '\0') normalized.append(character);
    }
    return normalized;
}

bool findLastWorkerObject(const QByteArray& output, QJsonObject* object)
{
    const QByteArray marker("{\"ok\"");
    const qsizetype start = output.lastIndexOf(marker);
    if (start < 0) return false;

    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (qsizetype index = start; index < output.size(); ++index) {
        const char character = output.at(index);
        if (inString) {
            if (escaped) escaped = false;
            else if (character == '\\') escaped = true;
            else if (character == '"') inString = false;
            continue;
        }
        if (character == '"') {
            inString = true;
        } else if (character == '{') {
            ++depth;
        } else if (character == '}') {
            --depth;
            if (depth == 0) {
                QJsonParseError parseError;
                const QJsonDocument document = QJsonDocument::fromJson(
                    output.mid(start, index - start + 1), &parseError);
                if (parseError.error == QJsonParseError::NoError
                    && document.isObject()) {
                    *object = document.object();
                    return true;
                }
                return false;
            }
        }
    }
    return false;
}

OcrResult parseWorkerResponse(const QByteArray& output)
{
    const QByteArray normalized = normalizedWorkerOutput(output);
    QJsonObject object;
    if (!findLastWorkerObject(normalized, &object)) {
        return failure(OcrError::ProtocolError,
                       QStringLiteral("应用内置 PaddleOCR 组件返回了无效响应，请修复或重新安装应用。"));
    }
    if (!object.value(QStringLiteral("ok")).toBool(false)) {
        const OcrError error = workerError(object.value(QStringLiteral("error")).toString());
        if (error == OcrError::ModelDataMissing) {
            return failure(error,
                           QStringLiteral("应用内置 PaddleOCR 模型缺失或损坏，请修复或重新安装应用。"));
        }
        if (error == OcrError::EmptyOutput) {
            return failure(error,
                           QStringLiteral("识别完成但没有检测到文字。请尝试更清晰的图片。"));
        }
        return failure(error,
                       QStringLiteral("本地 PaddleOCR 处理失败。原图片未修改，请稍后重试。"));
    }
    const QString text = object.value(QStringLiteral("text")).toString().trimmed();
    if (text.isEmpty()) {
        return failure(OcrError::EmptyOutput,
                       QStringLiteral("识别完成但没有检测到文字。请尝试更清晰的图片。"));
    }
    return {true, text, OcrError::None, QStringLiteral("识别完成。")};
}

}  // namespace

OcrRequest PaddleOcrEngine::resolvedRequest(const OcrRequest& request,
                                             const QString& applicationDirPath)
{
    OcrRequest resolved = request;
    const QString effectiveApplicationDir = applicationDirPath.trimmed().isEmpty()
        ? QCoreApplication::applicationDirPath()
        : applicationDirPath;
    const PackagedRuntime packaged = findPackagedRuntime(effectiveApplicationDir);

    resolved.workerExecutable = request.workerExecutable.trimmed();
    if (resolved.workerExecutable.isEmpty()) {
        resolved.workerExecutable = qEnvironmentVariable(
            "MINITREASUREBOX_PADDLEOCR_WORKER").trimmed();
    }
    if (resolved.workerExecutable.isEmpty()) resolved.workerExecutable = packaged.executable;
    if (resolved.workerExecutable.isEmpty()) {
#ifdef Q_OS_WIN
        resolved.workerExecutable = QStringLiteral("paddleocr-worker.exe");
#else
        resolved.workerExecutable = QStringLiteral("paddleocr-worker");
#endif
    }

    resolved.modelPath = request.modelPath.trimmed();
    if (resolved.modelPath.isEmpty()) {
        resolved.modelPath = qEnvironmentVariable("MINITREASUREBOX_PADDLEOCR_MODELS").trimmed();
    }
    if (resolved.modelPath.isEmpty()) resolved.modelPath = packaged.modelPath;
    if (!resolved.modelPath.isEmpty()) resolved.modelPath = QDir::cleanPath(resolved.modelPath);
    resolved.language = request.language.trimmed();
    return resolved;
}

OcrResult PaddleOcrEngine::validate(const OcrRequest& request)
{
    const OcrRequest resolved = resolvedRequest(request);
    if (resolved.timeoutMs <= 0) {
        return failure(OcrError::InvalidInput,
                       QStringLiteral("识别超时时间无效，请设置大于 0 的毫秒数。"));
    }
    const QFileInfo image(resolved.imagePath.trimmed());
    if (resolved.imagePath.trimmed().isEmpty() || !image.exists() || !image.isFile()
        || !image.isReadable()) {
        return failure(OcrError::InvalidInput,
                       QStringLiteral("图片无法读取。原文件未修改，请重新选择本地图片。"));
    }
    QImageReader reader(image.absoluteFilePath());
    if (!reader.canRead()) {
        return failure(OcrError::UnsupportedImage,
                       QStringLiteral("图片格式不受支持或文件已损坏。原文件未修改，请更换图片。"));
    }
    if (resolved.language != QStringLiteral("ch")
        && resolved.language != QStringLiteral("en")) {
        return failure(OcrError::InvalidInput,
                       QStringLiteral("PaddleOCR 识别语言设置无效。"));
    }
    if (resolvedExecutable(resolved.workerExecutable).isEmpty()) {
        return failure(OcrError::RuntimeMissing,
                       QStringLiteral("应用内置 PaddleOCR 组件缺失或损坏，请修复或重新安装应用。"));
    }
    if (!modelFilesExist(resolved.modelPath)) {
        return failure(OcrError::ModelDataMissing,
                       QStringLiteral("应用内置 PaddleOCR 模型缺失或损坏，请修复或重新安装应用。"));
    }
    return {true, {}, OcrError::None, {}};
}

QStringList PaddleOcrEngine::argumentsFor(const OcrRequest& request)
{
    return {QStringLiteral("--models"), QDir::cleanPath(request.modelPath),
            QStringLiteral("--image"), request.imagePath,
            QStringLiteral("--language"), request.language};
}

OcrResult PaddleOcrEngine::parseResponse(const QByteArray& output)
{
    return parseWorkerResponse(output);
}

OcrResult PaddleOcrEngine::recognize(const OcrRequest& request,
                                     const std::atomic_bool* cancellation) const
{
    if (cancellation && cancellation->load()) {
        return failure(OcrError::Cancelled, QStringLiteral("识别已取消。原图片未修改。"));
    }
    OcrRequest normalized = resolvedRequest(request);
    const OcrResult validation = validate(normalized);
    if (!validation.ok) return validation;

    normalized.imagePath = QFileInfo(normalized.imagePath).canonicalFilePath();
    normalized.workerExecutable = resolvedExecutable(normalized.workerExecutable);
    normalized.modelPath = QDir(normalized.modelPath).canonicalPath();

    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.setStandardErrorFile(QProcess::nullDevice());
    process.setProgram(normalized.workerExecutable);
    process.setArguments(argumentsFor(normalized));
    process.start(QIODevice::ReadOnly);

    QElapsedTimer elapsed;
    elapsed.start();
    while (process.state() == QProcess::Starting) {
        if (process.waitForStarted(20)) break;
        if (cancellation && cancellation->load()) {
            process.kill();
            process.waitForFinished(1000);
            return failure(OcrError::Cancelled, QStringLiteral("识别已取消。原图片未修改。"));
        }
        if (elapsed.elapsed() >= normalized.timeoutMs) {
            process.kill();
            process.waitForFinished(1000);
            return failure(OcrError::TimedOut,
                           QStringLiteral("本地 PaddleOCR 启动超时。原图片未修改，可以稍后重试。"));
        }
        if (process.error() == QProcess::FailedToStart) {
            return failure(OcrError::RuntimeMissing,
                           QStringLiteral("应用内置 PaddleOCR 组件无法启动，请修复或重新安装应用。"));
        }
    }
    if (process.state() == QProcess::NotRunning
        && process.error() == QProcess::FailedToStart) {
        return failure(OcrError::RuntimeMissing,
                       QStringLiteral("应用内置 PaddleOCR 组件无法启动，请修复或重新安装应用。"));
    }

    QByteArray standardOutput;
    while (process.state() != QProcess::NotRunning) {
        if (cancellation && cancellation->load()) {
            process.kill();
            process.waitForFinished(1000);
            return failure(OcrError::Cancelled, QStringLiteral("识别已取消。原图片未修改。"));
        }
        if (elapsed.elapsed() >= normalized.timeoutMs) {
            process.kill();
            process.waitForFinished(1000);
            return failure(OcrError::TimedOut,
                           QStringLiteral("本地 PaddleOCR 识别超时。原图片未修改，可以稍后重试。"));
        }
        process.waitForFinished(20);
        appendCapped(standardOutput, process.readAllStandardOutput());
        QThread::yieldCurrentThread();
    }
    appendCapped(standardOutput, process.readAllStandardOutput());

    if (standardOutput.trimmed().isEmpty()) {
        return failure(OcrError::ProcessFailed,
                       QStringLiteral("本地 PaddleOCR 组件未返回结果。原图片未修改，请稍后重试。"));
    }
    const OcrResult response = parseResponse(standardOutput);
    if (!response.ok) return response;
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        return failure(OcrError::ProcessFailed,
                       QStringLiteral("本地 PaddleOCR 进程异常退出。原图片未修改，请稍后重试。"));
    }
    return response;
}

}  // namespace mtb::ocr
