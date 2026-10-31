#include "ai/ocr/ocr_engine.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    if (application.arguments().size() != 2) {
        QTextStream(stderr) << "usage: ocr_runtime_smoke <installed-application-directory>\n";
        return 2;
    }

    QTemporaryDir temporaryDirectory;
    if (!temporaryDirectory.isValid()) {
        QTextStream(stderr) << "could not create a temporary directory\n";
        return 3;
    }

    const QString imagePath = temporaryDirectory.filePath(QStringLiteral("known-text.png"));
    if (!QFile::copy(QStringLiteral(":/fixtures/paddleocr-smoke.png"), imagePath)) {
        QTextStream(stderr) << "could not copy the embedded OCR fixture\n";
        return 4;
    }

    mtb::ocr::OcrRequest request;
    request.imagePath = imagePath;
    request.language = QStringLiteral("ch");
    request.timeoutMs = 120000;
    request = mtb::ocr::PaddleOcrEngine::resolvedRequest(
        request, QDir::cleanPath(application.arguments().at(1)));

    const mtb::ocr::PaddleOcrEngine engine;
    const mtb::ocr::OcrResult result = engine.recognize(request);
    if (!result.ok) {
        QTextStream(stderr) << "packaged PaddleOCR recognition failed: "
                            << result.message << '\n';
        return 5;
    }
    const QString normalized = result.text.toUpper().simplified();
    if (!normalized.contains(QStringLiteral("TEST"))
        || !normalized.contains(QStringLiteral("123"))) {
        QTextStream(stderr) << "unexpected packaged PaddleOCR output: "
                            << result.text << '\n';
        return 6;
    }

    QTextStream(stdout) << "Bundled PaddleOCR recognized known text through the production resolver: "
                        << result.text << '\n';
    return 0;
}
