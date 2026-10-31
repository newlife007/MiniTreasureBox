#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <atomic>

namespace mtb::ocr {

enum class OcrError {
    None,
    InvalidInput,
    UnsupportedImage,
    RuntimeMissing,
    ModelDataMissing,
    ProcessFailed,
    ProtocolError,
    TimedOut,
    Cancelled,
    EmptyOutput,
};

struct OcrRequest {
    QString imagePath;
    QString workerExecutable;
    QString modelPath;
    QString language{QStringLiteral("ch")};
    int timeoutMs{120000};
};

struct OcrResult {
    bool ok{false};
    QString text;
    OcrError error{OcrError::None};
    QString message;
};

class IOcrEngine {
public:
    virtual ~IOcrEngine() = default;
    [[nodiscard]] virtual OcrResult recognize(
        const OcrRequest& request,
        const std::atomic_bool* cancellation = nullptr) const = 0;
};

class PaddleOcrEngine final : public IOcrEngine {
public:
    [[nodiscard]] OcrResult recognize(
        const OcrRequest& request,
        const std::atomic_bool* cancellation = nullptr) const override;

    [[nodiscard]] static OcrRequest resolvedRequest(
        const OcrRequest& request,
        const QString& applicationDirPath = {});
    [[nodiscard]] static OcrResult validate(const OcrRequest& request);
    [[nodiscard]] static QStringList argumentsFor(const OcrRequest& request);
    [[nodiscard]] static OcrResult parseResponse(const QByteArray& output);
};

}  // namespace mtb::ocr
