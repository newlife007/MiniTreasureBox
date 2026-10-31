#include <QByteArray>
#include <QCoreApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <chrono>
#include <iostream>
#include <thread>

namespace {

QString argumentValue(const QStringList& arguments, const QString& name)
{
    const qsizetype index = arguments.indexOf(name);
    return index >= 0 && index + 1 < arguments.size() ? arguments.at(index + 1) : QString();
}

void writeObject(const QJsonObject& object)
{
    std::cout << QJsonDocument(object).toJson(QJsonDocument::Compact).constData();
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    const QStringList arguments = application.arguments();
    const QByteArray mode = qgetenv("MINITREASUREBOX_FAKE_RESTORATION_MODE");
    if (arguments.contains(QStringLiteral("--self-test"))) {
        if (mode == "self-test-fail") {
            writeObject({{QStringLiteral("ok"), false},
                         {QStringLiteral("schema_version"), 1},
                         {QStringLiteral("error"), QStringLiteral("model_missing")}});
            return 1;
        }
        writeObject({{QStringLiteral("ok"), true},
                     {QStringLiteral("schema_version"), 1},
                     {QStringLiteral("backend"), QStringLiteral("onnxruntime")}});
        return 0;
    }

    const QString requestId = argumentValue(arguments, QStringLiteral("--request-id"));
    if (mode == "timeout") {
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
    if (mode == "crash") {
        std::abort();
    }
    if (mode == "oversized") {
        std::cout << std::string(1024 * 1024 + 1, 'x');
        return 0;
    }
    if (mode == "malformed") {
        std::cout << "not-json";
        return 0;
    }
    if (mode == "missing") {
        writeObject({{QStringLiteral("ok"), false},
                     {QStringLiteral("schema_version"), 1},
                     {QStringLiteral("request_id"), requestId},
                     {QStringLiteral("error"), QStringLiteral("model_missing")}});
        return 1;
    }

    const QString inputPath = argumentValue(arguments, QStringLiteral("--image"));
    const QString maskPath = argumentValue(arguments, QStringLiteral("--mask"));
    const QString outputPath = argumentValue(arguments, QStringLiteral("--output"));
    QImage input(inputPath);
    if (input.isNull() || maskPath.isEmpty() || outputPath.isEmpty()) return 4;
    QImage mask(input.size(), QImage::Format_Grayscale8);
    mask.fill(0);
    const int centerX = input.width() / 2;
    const int centerY = input.height() / 2;
    mask.scanLine(centerY)[centerX] = 255;
    QImage output = input.convertToFormat(QImage::Format_ARGB32);
    output.fill(Qt::black);
    output.setPixelColor(centerX, centerY, Qt::white);
    if (!mask.save(maskPath, "PNG") || !output.save(outputPath, "PNG")) return 5;

    writeObject({{QStringLiteral("ok"), true},
                 {QStringLiteral("schema_version"), mode == "schema" ? 2 : 1},
                 {QStringLiteral("request_id"), mode == "mismatch"
                      ? QStringLiteral("other-request") : requestId},
                 {QStringLiteral("confidence"), QStringLiteral("medium")},
                 {QStringLiteral("confidence_reasons"),
                  QJsonArray{QStringLiteral("fixture confidence reason")}},
                 {QStringLiteral("model_version"), QStringLiteral("fixture-v1")}});
    return 0;
}
