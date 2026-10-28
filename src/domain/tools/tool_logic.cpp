#include "domain/tools/tool_logic.h"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QTimeZone>
#include <QUrl>
#include <QUuid>

namespace mtb::tools {
namespace {

TextResult parseJson(const QString& input, QJsonDocument::JsonFormat format)
{
    if (input.trimmed().isEmpty()) {
        return {false, {}, QStringLiteral("请输入 JSON 内容。")};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(input.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return {false,
                {},
                QStringLiteral("JSON 解析失败（字节位置 %1）：%2")
                    .arg(parseError.offset)
                    .arg(parseError.errorString())};
    }
    return {true, QString::fromUtf8(document.toJson(format)), {}};
}

bool isHex(char value)
{
    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f')
        || (value >= 'A' && value <= 'F');
}

TextResult utf8Text(const QByteArray& bytes, const QString& invalidMessage)
{
    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString decoded = decoder.decode(bytes);
    if (decoder.hasError()) {
        return {false, {}, invalidMessage};
    }
    return {true, decoded, {}};
}

}  // namespace

TextResult JsonLogic::format(const QString& input)
{
    return parseJson(input, QJsonDocument::Indented);
}

TextResult JsonLogic::compact(const QString& input)
{
    return parseJson(input, QJsonDocument::Compact);
}

TextResult JsonLogic::validate(const QString& input)
{
    TextResult result = parseJson(input, QJsonDocument::Compact);
    if (result.ok) {
        result.output = QStringLiteral("JSON 有效。");
    }
    return result;
}

TextResult Base64Logic::encode(const QString& input)
{
    return {true, QString::fromLatin1(input.toUtf8().toBase64()), {}};
}

TextResult Base64Logic::decode(const QString& input)
{
    const QByteArray encoded = input.trimmed().toLatin1();
    const auto result = QByteArray::fromBase64Encoding(
        encoded, QByteArray::AbortOnBase64DecodingErrors);
    if (!result) {
        return {false, {}, QStringLiteral("Base64 输入无效，请检查字符和填充。")};
    }
    return utf8Text(*result, QStringLiteral("解码结果不是有效的 UTF-8 文本。"));
}

TextResult UrlLogic::encode(const QString& input)
{
    return {true, QString::fromLatin1(QUrl::toPercentEncoding(input)), {}};
}

TextResult UrlLogic::decode(const QString& input)
{
    const QByteArray encoded = input.toUtf8();
    for (qsizetype index = 0; index < encoded.size(); ++index) {
        if (encoded.at(index) != '%') {
            continue;
        }
        if (index + 2 >= encoded.size() || !isHex(encoded.at(index + 1))
            || !isHex(encoded.at(index + 2))) {
            return {false,
                    {},
                    QStringLiteral("URL 编码无效：位置 %1 的百分号后需要两个十六进制字符。")
                        .arg(index)};
        }
        index += 2;
    }

    return utf8Text(QByteArray::fromPercentEncoding(encoded),
                    QStringLiteral("URL 解码结果不是有效的 UTF-8 文本。"));
}

TimestampResult TimestampLogic::convert(const QString& input, TimestampUnit unit)
{
    const QString normalized = input.trimmed();
    if (normalized.isEmpty()) {
        return {false, {}, {}, QStringLiteral("请输入 Unix 时间戳。")};
    }

    bool parsed = false;
    const qint64 value = normalized.toLongLong(&parsed);
    if (!parsed) {
        return {false, {}, {}, QStringLiteral("时间戳必须是整数。")};
    }

    const QDateTime utc = unit == TimestampUnit::Seconds
        ? QDateTime::fromSecsSinceEpoch(value, QTimeZone::UTC)
        : QDateTime::fromMSecsSinceEpoch(value, QTimeZone::UTC);
    if (!utc.isValid()) {
        return {false, {}, {}, QStringLiteral("时间戳超出 Qt 支持的日期范围。")};
    }
    return {true, utc, utc.toLocalTime(), {}};
}

QString UuidLogic::generate()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
}

bool UuidLogic::isValid(const QString& value)
{
    static const QRegularExpression pattern(
        QStringLiteral("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[1-5][0-9a-fA-F]{3}-"
                       "[89aAbB][0-9a-fA-F]{3}-[0-9a-fA-F]{12}$"));
    QString normalized = value.trimmed();
    const bool startsWithBrace = normalized.startsWith(QLatin1Char('{'));
    const bool endsWithBrace = normalized.endsWith(QLatin1Char('}'));
    if (startsWithBrace != endsWithBrace) {
        return false;
    }
    if (startsWithBrace) {
        normalized = normalized.mid(1, normalized.size() - 2);
    }
    if (!pattern.match(normalized).hasMatch()) {
        return false;
    }
    return !QUuid::fromString(normalized).isNull();
}

TextStatistics TextStatisticsLogic::calculate(const QString& input)
{
    TextStatistics statistics;
    const QList<uint> codePoints = input.toUcs4();
    statistics.characters = codePoints.size();
    for (const uint codePoint : codePoints) {
        if (!QChar::isSpace(codePoint)) {
            ++statistics.nonWhitespaceCharacters;
        }
    }

    static const QRegularExpression wordPattern(QStringLiteral("\\S+"));
    auto matches = wordPattern.globalMatch(input);
    while (matches.hasNext()) {
        matches.next();
        ++statistics.words;
    }

    if (!input.isEmpty()) {
        statistics.lines = 1;
        for (qsizetype index = 0; index < input.size(); ++index) {
            if (input.at(index) == QLatin1Char('\r')) {
                ++statistics.lines;
                if (index + 1 < input.size() && input.at(index + 1) == QLatin1Char('\n')) {
                    ++index;
                }
            } else if (input.at(index) == QLatin1Char('\n')) {
                ++statistics.lines;
            }
        }
    }
    statistics.utf8Bytes = input.toUtf8().size();
    return statistics;
}

}  // namespace mtb::tools
