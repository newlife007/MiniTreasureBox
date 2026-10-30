#include "ai/ocr/ocr_engine.h"

#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>
#include <QTextStream>

#include <array>

namespace {

using Glyph = std::array<const char*, 7>;

const Glyph& glyphFor(const QChar character)
{
    static const Glyph space{"00000", "00000", "00000", "00000", "00000", "00000", "00000"};
    static const Glyph t{"11111", "00100", "00100", "00100", "00100", "00100", "00100"};
    static const Glyph e{"11111", "10000", "10000", "11110", "10000", "10000", "11111"};
    static const Glyph s{"01111", "10000", "10000", "01110", "00001", "00001", "11110"};
    static const Glyph one{"00100", "01100", "00100", "00100", "00100", "00100", "01110"};
    static const Glyph two{"01110", "10001", "00001", "00010", "00100", "01000", "11111"};
    static const Glyph three{"11110", "00001", "00001", "01110", "00001", "00001", "11110"};
    switch (character.unicode()) {
    case 'T': return t;
    case 'E': return e;
    case 'S': return s;
    case '1': return one;
    case '2': return two;
    case '3': return three;
    default: return space;
    }
}

QImage knownTextImage()
{
    constexpr int scale = 20;
    constexpr int glyphWidth = 5;
    constexpr int glyphHeight = 7;
    constexpr int spacing = 2;
    const QString text = QStringLiteral("TEST 123");
    const int contentWidth = text.size() * (glyphWidth + spacing) * scale;
    QImage image(contentWidth + 8 * scale, (glyphHeight + 8) * scale,
                 QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::black);
    for (int characterIndex = 0; characterIndex < text.size(); ++characterIndex) {
        const Glyph& glyph = glyphFor(text.at(characterIndex));
        for (int row = 0; row < glyphHeight; ++row) {
            for (int column = 0; column < glyphWidth; ++column) {
                if (glyph.at(row)[column] == '1') {
                    painter.drawRect((4 + characterIndex * (glyphWidth + spacing) + column) * scale,
                                     (4 + row) * scale, scale, scale);
                }
            }
        }
    }
    return image;
}

}  // namespace

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
    if (!knownTextImage().save(imagePath, "PNG")) {
        QTextStream(stderr) << "could not write the known OCR image\n";
        return 4;
    }

    mtb::ocr::OcrRequest request;
    request.imagePath = imagePath;
    request.language = QStringLiteral("chi_sim+eng");
    request.timeoutMs = 60000;
    request = mtb::ocr::TesseractOcrEngine::resolvedRequest(
        request, QDir::cleanPath(application.arguments().at(1)));

    const mtb::ocr::TesseractOcrEngine engine;
    const mtb::ocr::OcrResult result = engine.recognize(request);
    if (!result.ok) {
        QTextStream(stderr) << "packaged OCR recognition failed: " << result.message << '\n';
        return 5;
    }
    const QString normalized = result.text.toUpper().simplified();
    const bool recognizedText = normalized.contains(QStringLiteral("TEST"))
        && (normalized.contains(QStringLiteral("123"))
            || normalized.contains(QStringLiteral("12S")));
    if (!recognizedText) {
        QTextStream(stderr) << "unexpected packaged OCR output: " << result.text << '\n';
        return 6;
    }

    QTextStream(stdout) << "Packaged OCR recognized known text through the production resolver: "
                        << result.text << '\n';
    return 0;
}
