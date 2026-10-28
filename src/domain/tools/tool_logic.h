#pragma once

#include <QDateTime>
#include <QString>

namespace mtb::tools {

struct TextResult {
    bool ok{false};
    QString output;
    QString error;
};

class JsonLogic {
public:
    [[nodiscard]] static TextResult format(const QString& input);
    [[nodiscard]] static TextResult compact(const QString& input);
    [[nodiscard]] static TextResult validate(const QString& input);
};

class Base64Logic {
public:
    [[nodiscard]] static TextResult encode(const QString& input);
    [[nodiscard]] static TextResult decode(const QString& input);
};

class UrlLogic {
public:
    [[nodiscard]] static TextResult encode(const QString& input);
    [[nodiscard]] static TextResult decode(const QString& input);
};

enum class TimestampUnit {
    Seconds,
    Milliseconds,
};

struct TimestampResult {
    bool ok{false};
    QDateTime utc;
    QDateTime local;
    QString error;
};

class TimestampLogic {
public:
    [[nodiscard]] static TimestampResult convert(const QString& input, TimestampUnit unit);
};

class UuidLogic {
public:
    [[nodiscard]] static QString generate();
    [[nodiscard]] static bool isValid(const QString& value);
};

struct TextStatistics {
    qsizetype characters{0};
    qsizetype nonWhitespaceCharacters{0};
    qsizetype words{0};
    qsizetype lines{0};
    qsizetype utf8Bytes{0};
};

class TextStatisticsLogic {
public:
    [[nodiscard]] static TextStatistics calculate(const QString& input);
};

}  // namespace mtb::tools
