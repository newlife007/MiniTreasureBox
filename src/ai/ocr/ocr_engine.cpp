#include "ai/ocr/ocr_engine.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImageReader>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThread>

namespace mtb::ocr {
namespace {

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
    for (QString& root : roots) {
        root = QDir::cleanPath(root);
    }
    roots.removeDuplicates();
    return roots;
}

struct PackagedRuntime {
    QString executable;
    QString tessdataPath;
};

PackagedRuntime findPackagedRuntime(const QString& applicationDirPath)
{
#ifdef Q_OS_WIN
    const QString executableName = QStringLiteral("tesseract.exe");
#else
    const QString executableName = QStringLiteral("tesseract");
#endif
    for (const QString& root : packagedRuntimeRoots(applicationDirPath)) {
        const QDir runtime(root);
        const QStringList executableCandidates{
            runtime.filePath(QStringLiteral("bin/") + executableName),
            runtime.filePath(QStringLiteral("Library/bin/") + executableName),
        };
        QString executable;
        for (const QString& candidate : executableCandidates) {
            if (!resolvedExecutable(candidate).isEmpty()) {
                executable = QFileInfo(candidate).canonicalFilePath();
                break;
            }
        }
        if (executable.isEmpty()) {
            continue;
        }

        const QStringList tessdataCandidates{
            runtime.filePath(QStringLiteral("share/tessdata")),
            runtime.filePath(QStringLiteral("Library/share/tessdata")),
        };
        QString tessdataPath = QDir::cleanPath(tessdataCandidates.constFirst());
        for (const QString& candidate : tessdataCandidates) {
            if (QFileInfo(candidate).isDir()) {
                tessdataPath = QDir(candidate).canonicalPath();
                break;
            }
        }
        return {executable, tessdataPath};
    }
    return {};
}

bool languageFilesExist(const OcrRequest& request)
{
    if (request.tessdataPath.trimmed().isEmpty()) {
        return true;
    }
    const QDir directory(request.tessdataPath.trimmed());
    if (!directory.exists()) {
        return false;
    }
    const QStringList languages = request.language.split(QLatin1Char('+'), Qt::SkipEmptyParts);
    for (const QString& language : languages) {
        if (!QFileInfo::exists(directory.filePath(language + QStringLiteral(".traineddata")))) {
            return false;
        }
    }
    return true;
}

bool reportsMissingLanguageData(const QByteArray& standardError)
{
    return standardError.contains("Error opening data file")
        || standardError.contains("Failed loading language")
        || standardError.contains("Could not initialize tesseract");
}

}  // namespace

OcrRequest TesseractOcrEngine::resolvedRequest(const OcrRequest& request,
                                                const QString& applicationDirPath)
{
    OcrRequest resolved = request;
    const QString effectiveApplicationDir = applicationDirPath.trimmed().isEmpty()
        ? QCoreApplication::applicationDirPath()
        : applicationDirPath;
    const PackagedRuntime packaged = findPackagedRuntime(effectiveApplicationDir);

    resolved.executable = request.executable.trimmed();
    if (resolved.executable.isEmpty()) {
        resolved.executable = qEnvironmentVariable("MINITREASUREBOX_TESSERACT").trimmed();
    }
    if (resolved.executable.isEmpty()) {
        resolved.executable = packaged.executable;
    }
    if (resolved.executable.isEmpty()) {
        resolved.executable = QStringLiteral("tesseract");
    }

    resolved.tessdataPath = request.tessdataPath.trimmed();
    if (resolved.tessdataPath.isEmpty()) {
        resolved.tessdataPath = qEnvironmentVariable("TESSDATA_PREFIX").trimmed();
    }
    if (resolved.tessdataPath.isEmpty()) {
        resolved.tessdataPath = packaged.tessdataPath;
    }
    if (!resolved.tessdataPath.isEmpty()) {
        resolved.tessdataPath = QDir::cleanPath(resolved.tessdataPath);
    }
    resolved.language = request.language.trimmed();
    return resolved;
}

OcrResult TesseractOcrEngine::validate(const OcrRequest& request)
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
    static const QRegularExpression languagePattern(QStringLiteral("^[A-Za-z0-9_]+(?:\\+[A-Za-z0-9_]+)*$"));
    if (!languagePattern.match(resolved.language).hasMatch()) {
        return failure(OcrError::InvalidInput,
                       QStringLiteral("识别语言设置无效，请使用例如 chi_sim+eng 的语言代码。"));
    }
    if (resolvedExecutable(resolved.executable).isEmpty()) {
        return failure(OcrError::RuntimeMissing,
                       QStringLiteral("应用内置 OCR 组件缺失或损坏，请修复或重新安装应用。"));
    }
    if (!languageFilesExist(resolved)) {
        return failure(OcrError::LanguageDataMissing,
                       QStringLiteral("应用内置 OCR 语言数据缺失或损坏，请修复或重新安装应用。"));
    }
    return {true, {}, OcrError::None, {}};
}

QStringList TesseractOcrEngine::argumentsFor(const OcrRequest& request)
{
    QStringList arguments{request.imagePath, QStringLiteral("stdout"),
                          QStringLiteral("-l"), request.language.trimmed()};
    if (!request.tessdataPath.trimmed().isEmpty()) {
        arguments << QStringLiteral("--tessdata-dir")
                  << QDir::cleanPath(request.tessdataPath.trimmed());
    }
    return arguments;
}

OcrResult TesseractOcrEngine::recognize(const OcrRequest& request,
                                        const std::atomic_bool* cancellation) const
{
    if (cancellation && cancellation->load()) {
        return failure(OcrError::Cancelled, QStringLiteral("识别已取消。原图片未修改。"));
    }
    OcrRequest normalized = resolvedRequest(request);
    const OcrResult validation = validate(normalized);
    if (!validation.ok) {
        return validation;
    }

    normalized.imagePath = QFileInfo(normalized.imagePath.trimmed()).canonicalFilePath();
    normalized.executable = resolvedExecutable(normalized.executable);

    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.setProgram(normalized.executable);
    process.setArguments(argumentsFor(normalized));
    process.start(QIODevice::ReadOnly);

    QElapsedTimer elapsed;
    elapsed.start();
    while (process.state() == QProcess::Starting) {
        if (process.waitForStarted(20)) {
            break;
        }
        if (cancellation && cancellation->load()) {
            process.kill();
            process.waitForFinished(1000);
            return failure(OcrError::Cancelled, QStringLiteral("识别已取消。原图片未修改。"));
        }
        if (elapsed.elapsed() >= normalized.timeoutMs) {
            process.kill();
            process.waitForFinished(1000);
            return failure(OcrError::TimedOut,
                           QStringLiteral("本地 OCR 启动超时。原图片未修改，可以稍后重试。"));
        }
        if (process.error() == QProcess::FailedToStart) {
            return failure(OcrError::RuntimeMissing,
                           QStringLiteral("应用内置 OCR 组件无法启动，请修复或重新安装应用。"));
        }
    }
    if (process.state() == QProcess::NotRunning && process.error() == QProcess::FailedToStart) {
        return failure(OcrError::RuntimeMissing,
                       QStringLiteral("应用内置 OCR 组件无法启动，请修复或重新安装应用。"));
    }

    QByteArray standardOutput;
    QByteArray standardError;
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
                           QStringLiteral("本地 OCR 识别超时。原图片未修改，可以稍后重试。"));
        }
        process.waitForFinished(20);
        standardOutput.append(process.readAllStandardOutput());
        standardError.append(process.readAllStandardError());
        QThread::yieldCurrentThread();
    }
    standardOutput.append(process.readAllStandardOutput());
    standardError.append(process.readAllStandardError());

    if (reportsMissingLanguageData(standardError)) {
        return failure(OcrError::LanguageDataMissing,
                       QStringLiteral("应用内置 OCR 语言数据无法加载，请修复或重新安装应用。"));
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        return failure(OcrError::ProcessFailed,
                       QStringLiteral("本地 OCR 处理失败。原图片未修改，请稍后重试。"));
    }

    const QString text = QString::fromUtf8(standardOutput).trimmed();
    if (text.isEmpty()) {
        return failure(OcrError::EmptyOutput,
                       QStringLiteral("识别完成但没有检测到文字。请尝试更清晰的图片或其他语言模型。"));
    }
    return {true, text, OcrError::None, QStringLiteral("识别完成。")};
}

}  // namespace mtb::ocr
