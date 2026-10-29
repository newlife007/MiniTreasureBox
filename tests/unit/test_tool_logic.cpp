#include "core/tool_registry.h"
#include "domain/tools/builtin_tools.h"
#include "domain/tools/tool_logic.h"

#include <QJsonDocument>
#include <QTest>
#include <QWidget>

using namespace mtb;
using namespace mtb::tools;

Q_DECLARE_METATYPE(TimestampUnit)

class ToolLogicTest final : public QObject {
    Q_OBJECT

private slots:
    void json_data();
    void json();
    void base64_data();
    void base64();
    void url_data();
    void url();
    void urlLiteralUnicode();
    void timestamp_data();
    void timestamp();
    void uuid();
    void textStatistics_data();
    void textStatistics();
    void builtinRegistration();
};

void ToolLogicTest::json_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<bool>("valid");
    QTest::addColumn<QString>("compact");
    QTest::newRow("object") << QStringLiteral("{\"answer\": 42}") << true
                            << QStringLiteral("{\"answer\":42}");
    QTest::newRow("unicode") << QStringLiteral("{\"文字\":\"你好 😀\"}") << true
                             << QStringLiteral("{\"文字\":\"你好 😀\"}");
    QTest::newRow("empty") << QString() << false << QString();
    QTest::newRow("malformed") << QStringLiteral("{\"missing\":}") << false << QString();
    QTest::newRow("nested-boundary")
        << QStringLiteral("[{\"n\":-9223372036854775808},[],{}]") << true
        << QStringLiteral("[{\"n\":-9223372036854775808},[],{}]");
}

void ToolLogicTest::json()
{
    QFETCH(QString, input);
    QFETCH(bool, valid);
    QFETCH(QString, compact);
    const TextResult result = JsonLogic::compact(input);
    QCOMPARE(result.ok, valid);
    if (valid) {
        QCOMPARE(result.output, compact);
        QVERIFY(JsonLogic::format(input).output.contains(QLatin1Char('\n')));
        QCOMPARE(JsonLogic::validate(input).output, QStringLiteral("JSON 有效。"));
    } else {
        QVERIFY(!result.error.isEmpty());
    }
}

void ToolLogicTest::base64_data()
{
    QTest::addColumn<QString>("plain");
    QTest::addColumn<QString>("encoded");
    QTest::addColumn<bool>("decodable");
    QTest::newRow("ascii") << QStringLiteral("treasure") << QStringLiteral("dHJlYXN1cmU=") << true;
    QTest::newRow("unicode") << QStringLiteral("你好 😀") << QStringLiteral("5L2g5aW9IPCfmIA=") << true;
    QTest::newRow("empty") << QString() << QString() << true;
    QTest::newRow("malformed") << QString() << QStringLiteral("not @ base64") << false;
    QTest::newRow("invalid-utf8") << QString() << QStringLiteral("/w==") << false;
}

void ToolLogicTest::base64()
{
    QFETCH(QString, plain);
    QFETCH(QString, encoded);
    QFETCH(bool, decodable);
    if (decodable) {
        QCOMPARE(Base64Logic::encode(plain).output, encoded);
        const TextResult decoded = Base64Logic::decode(encoded);
        QVERIFY(decoded.ok);
        QCOMPARE(decoded.output, plain);
    } else {
        const TextResult decoded = Base64Logic::decode(encoded);
        QVERIFY(!decoded.ok);
        QVERIFY(!decoded.error.isEmpty());
    }
}

void ToolLogicTest::url_data()
{
    QTest::addColumn<QString>("plain");
    QTest::addColumn<QString>("encoded");
    QTest::addColumn<bool>("decodable");
    QTest::newRow("ascii") << QStringLiteral("a b/c?") << QStringLiteral("a%20b%2Fc%3F") << true;
    QTest::newRow("unicode") << QStringLiteral("你好 😀")
                             << QStringLiteral("%E4%BD%A0%E5%A5%BD%20%F0%9F%98%80") << true;
    QTest::newRow("empty") << QString() << QString() << true;
    QTest::newRow("truncated") << QString() << QStringLiteral("abc%2") << false;
    QTest::newRow("invalid-hex") << QString() << QStringLiteral("%GG") << false;
    QTest::newRow("invalid-utf8") << QString() << QStringLiteral("%FF") << false;
}

void ToolLogicTest::url()
{
    QFETCH(QString, plain);
    QFETCH(QString, encoded);
    QFETCH(bool, decodable);
    if (decodable) {
        QCOMPARE(UrlLogic::encode(plain).output, encoded);
        const TextResult decoded = UrlLogic::decode(encoded);
        QVERIFY(decoded.ok);
        QCOMPARE(decoded.output, plain);
    } else {
        const TextResult decoded = UrlLogic::decode(encoded);
        QVERIFY(!decoded.ok);
        QVERIFY(!decoded.error.isEmpty());
    }
}

void ToolLogicTest::urlLiteralUnicode()
{
    const TextResult result = UrlLogic::decode(QStringLiteral("路径%20一"));
    QVERIFY(result.ok);
    QCOMPARE(result.output, QStringLiteral("路径 一"));
}

void ToolLogicTest::timestamp_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<TimestampUnit>("unit");
    QTest::addColumn<bool>("valid");
    QTest::addColumn<qint64>("milliseconds");
    QTest::newRow("epoch-seconds") << QStringLiteral("0") << TimestampUnit::Seconds << true << qint64(0);
    QTest::newRow("negative-seconds") << QStringLiteral("-1") << TimestampUnit::Seconds << true << qint64(-1000);
    QTest::newRow("milliseconds") << QStringLiteral("1704067200123")
                                  << TimestampUnit::Milliseconds << true << qint64(1704067200123);
    QTest::newRow("empty") << QString() << TimestampUnit::Seconds << false << qint64(0);
    QTest::newRow("fraction") << QStringLiteral("1.5") << TimestampUnit::Seconds << false << qint64(0);
    QTest::newRow("overflow") << QStringLiteral("9223372036854775808")
                              << TimestampUnit::Milliseconds << false << qint64(0);
    QTest::newRow("seconds-out-of-range") << QStringLiteral("9223372036854775807")
                                         << TimestampUnit::Seconds << false << qint64(0);
}

void ToolLogicTest::timestamp()
{
    QFETCH(QString, input);
    QFETCH(TimestampUnit, unit);
    QFETCH(bool, valid);
    QFETCH(qint64, milliseconds);
    const TimestampResult result = TimestampLogic::convert(input, unit);
    QCOMPARE(result.ok, valid);
    if (valid) {
        QCOMPARE(result.utc.toMSecsSinceEpoch(), milliseconds);
        QCOMPARE(result.utc.timeSpec(), Qt::UTC);
        QCOMPARE(result.local.toMSecsSinceEpoch(), milliseconds);
    } else {
        QVERIFY(!result.error.isEmpty());
    }
}

void ToolLogicTest::uuid()
{
    const QString first = UuidLogic::generate();
    const QString second = UuidLogic::generate();
    QVERIFY(UuidLogic::isValid(first));
    QVERIFY(UuidLogic::isValid(QStringLiteral("{%1}").arg(first)));
    QVERIFY(!UuidLogic::isValid(QStringLiteral("{%1").arg(first)));
    QVERIFY(!UuidLogic::isValid(QStringLiteral("%1}").arg(first)));
    QVERIFY(first != second);
    QVERIFY(!UuidLogic::isValid(QString()));
    QVERIFY(!UuidLogic::isValid(QStringLiteral("not-a-uuid")));
    QVERIFY(!UuidLogic::isValid(QStringLiteral("00000000-0000-0000-0000-000000000000")));
}

void ToolLogicTest::textStatistics_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<qsizetype>("characters");
    QTest::addColumn<qsizetype>("nonWhitespace");
    QTest::addColumn<qsizetype>("words");
    QTest::addColumn<qsizetype>("lines");
    QTest::addColumn<qsizetype>("bytes");
    QTest::newRow("empty") << QString() << qsizetype(0) << qsizetype(0) << qsizetype(0)
                           << qsizetype(0) << qsizetype(0);
    QTest::newRow("unicode") << QStringLiteral("你好 😀\nQt") << qsizetype(7) << qsizetype(5)
                             << qsizetype(3) << qsizetype(2) << qsizetype(14);
    QTest::newRow("whitespace") << QStringLiteral(" \t\n") << qsizetype(3) << qsizetype(0)
                                << qsizetype(0) << qsizetype(2) << qsizetype(3);
    QTest::newRow("line-boundaries") << QStringLiteral("a\r\nb\r") << qsizetype(5)
                                     << qsizetype(2) << qsizetype(2) << qsizetype(3)
                                     << qsizetype(5);
}

void ToolLogicTest::textStatistics()
{
    QFETCH(QString, input);
    QFETCH(qsizetype, characters);
    QFETCH(qsizetype, nonWhitespace);
    QFETCH(qsizetype, words);
    QFETCH(qsizetype, lines);
    QFETCH(qsizetype, bytes);
    const TextStatistics result = TextStatisticsLogic::calculate(input);
    QCOMPARE(result.characters, characters);
    QCOMPARE(result.nonWhitespaceCharacters, nonWhitespace);
    QCOMPARE(result.words, words);
    QCOMPARE(result.lines, lines);
    QCOMPARE(result.utf8Bytes, bytes);
}

void ToolLogicTest::builtinRegistration()
{
    ToolRegistry registry;
    registerBuiltinTools(registry);
    QCOMPARE(registry.all().size(), 11);
    for (const auto& tool : registry.all()) {
        const ToolMetadata metadata = tool->metadata();
        QVERIFY(!metadata.id.isEmpty());
        QVERIFY(!metadata.name.isEmpty());
        QVERIFY(!metadata.category.isEmpty());
        QVERIFY(!metadata.description.isEmpty());
        QVERIFY(!metadata.keywords.isEmpty());
        QVERIFY(!metadata.aliases.isEmpty());
        QVERIFY(!metadata.requiresNetwork);
        QWidget* widget = tool->createWidget();
        QVERIFY(widget != nullptr);
        delete widget;
    }
    QCOMPARE(registry.search(QStringLiteral("JSON formatter")).size(), 1);
    QCOMPARE(registry.search(QStringLiteral("编码转换")).size(), 2);
    QCOMPARE(registry.search(QStringLiteral("word count")).size(), 1);
}

QTEST_MAIN(ToolLogicTest)
#include "test_tool_logic.moc"
