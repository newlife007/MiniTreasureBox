#pragma once

#include <QString>
#include <QStringList>

#include <atomic>

namespace mtb::ocr {

enum class OcrError {
    None,
    InvalidInput,
    UnsupportedImage,
    RuntimeMissing,
    LanguageDataMissing,
    ProcessFailed,
    TimedOut,
    Cancelled,
    EmptyOutput,
};

struct OcrRequest {
    QString imagePath;
    QString executable{QStringLiteral("tesseract")};
    QString tessdataPath;
    QString language{QStringLiteral("chi_sim+eng")};
    int timeoutMs{60000};
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

class TesseractOcrEngine final : public IOcrEngine {
public:
    [[nodiscard]] OcrResult recognize(
        const OcrRequest& request,
        const std::atomic_bool* cancellation = nullptr) const override;

    [[nodiscard]] static OcrResult validate(const OcrRequest& request);
    [[nodiscard]] static QStringList argumentsFor(const OcrRequest& request);
};

}  // namespace mtb::ocr
