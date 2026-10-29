#include "ai/ocr/ocr_engine.h"

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

OcrResult TesseractOcrEngine::validate(const OcrRequest& request)
{
    if (request.timeoutMs <= 0) {
        return failure(OcrError::InvalidInput,
                       QStringLiteral("识别超时时间无效，请设置大于 0 的毫秒数。"));
    }
    const QFileInfo image(request.imagePath.trimmed());
    if (request.imagePath.trimmed().isEmpty() || !image.exists() || !image.isFile()
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
    if (!languagePattern.match(request.language.trimmed()).hasMatch()) {
        return failure(OcrError::InvalidInput,
                       QStringLiteral("识别语言设置无效，请使用例如 chi_sim+eng 的语言代码。"));
    }
    if (resolvedExecutable(request.executable).isEmpty()) {
        return failure(OcrError::RuntimeMissing,
                       QStringLiteral("未找到本地 Tesseract 程序。请先安装并配置可执行文件路径。"));
    }
    if (!languageFilesExist(request)) {
        return failure(OcrError::LanguageDataMissing,
                       QStringLiteral("未找到所选语言模型。请检查 tessdata 目录和语言设置。"));
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
    const OcrResult validation = validate(request);
    if (!validation.ok) {
        return validation;
    }

    OcrRequest normalized = request;
    normalized.imagePath = QFileInfo(request.imagePath.trimmed()).canonicalFilePath();
    normalized.executable = resolvedExecutable(request.executable);
    normalized.language = request.language.trimmed();

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
                           QStringLiteral("本地 OCR 启动超时。原图片未修改，可以检查配置后重试。"));
        }
        if (process.error() == QProcess::FailedToStart) {
            return failure(OcrError::RuntimeMissing,
                           QStringLiteral("本地 Tesseract 无法启动。请检查可执行文件和权限。"));
        }
    }
    if (process.state() == QProcess::NotRunning && process.error() == QProcess::FailedToStart) {
        return failure(OcrError::RuntimeMissing,
                       QStringLiteral("本地 Tesseract 无法启动。请检查可执行文件和权限。"));
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
                           QStringLiteral("本地 OCR 识别超时。原图片未修改，可以重试或增加超时时间。"));
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
                       QStringLiteral("Tesseract 无法加载所选语言模型。请检查 tessdata 配置。"));
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        return failure(OcrError::ProcessFailed,
                       QStringLiteral("本地 OCR 处理失败。原图片未修改，请检查运行时配置后重试。"));
    }

    const QString text = QString::fromUtf8(standardOutput).trimmed();
    if (text.isEmpty()) {
        return failure(OcrError::EmptyOutput,
                       QStringLiteral("识别完成但没有检测到文字。请尝试更清晰的图片或其他语言模型。"));
    }
    return {true, text, OcrError::None, QStringLiteral("识别完成。")};
}

}  // namespace mtb::ocr
