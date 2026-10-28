#include "core/tool_registry.h"

#include <QtTest>

#include <memory>

namespace {

class TestTool final : public mtb::ITool {
public:
    explicit TestTool(mtb::ToolMetadata metadata)
        : metadata_(std::move(metadata))
    {
    }

    mtb::ToolMetadata metadata() const override { return metadata_; }

private:
    mtb::ToolMetadata metadata_;
};

std::shared_ptr<TestTool> makeJsonTool()
{
    return std::make_shared<TestTool>(mtb::ToolMetadata{
        QStringLiteral("json.formatter"),
        QStringLiteral("JSON 格式化"),
        QStringLiteral("开发工具"),
        QStringLiteral("格式化并验证 JSON 文档"),
        {QStringLiteral("pretty"), QStringLiteral("validate")},
        {QStringLiteral("美化"), QStringLiteral("json beautifier")},
        QStringLiteral("code-json"),
        false,
    });
}

class ToolRegistryTest final : public QObject {
    Q_OBJECT

private slots:
    void registersAndFindsTools();
    void rejectsInvalidAndDuplicateIds();
    void searchesEveryMetadataField_data();
    void searchesEveryMetadataField();
    void matchesMultipleTermsCaseInsensitively();
};

void ToolRegistryTest::registersAndFindsTools()
{
    mtb::ToolRegistry registry;
    const auto tool = makeJsonTool();

    QVERIFY(registry.registerTool(tool));
    QCOMPARE(registry.all().size(), 1);
    QCOMPARE(registry.find(QStringLiteral("json.formatter")), tool);
    QVERIFY(!registry.find(QStringLiteral("missing")));
}

void ToolRegistryTest::rejectsInvalidAndDuplicateIds()
{
    mtb::ToolRegistry registry;
    QVERIFY(!registry.registerTool(nullptr));
    QVERIFY(!registry.registerTool(std::make_shared<TestTool>(mtb::ToolMetadata{})));

    auto whitespaceId = mtb::ToolMetadata{};
    whitespaceId.id = QStringLiteral("   ");
    QVERIFY(!registry.registerTool(std::make_shared<TestTool>(whitespaceId)));

    auto paddedId = mtb::ToolMetadata{};
    paddedId.id = QStringLiteral(" padded.id ");
    QVERIFY(!registry.registerTool(std::make_shared<TestTool>(paddedId)));

    QVERIFY(registry.registerTool(makeJsonTool()));
    QVERIFY(!registry.registerTool(makeJsonTool()));
    QCOMPARE(registry.all().size(), 1);
}

void ToolRegistryTest::searchesEveryMetadataField_data()
{
    QTest::addColumn<QString>("query");
    QTest::newRow("id") << QStringLiteral("formatter");
    QTest::newRow("name") << QStringLiteral("json 格式");
    QTest::newRow("category") << QStringLiteral("开发工具");
    QTest::newRow("description") << QStringLiteral("验证");
    QTest::newRow("keyword") << QStringLiteral("PRETTY");
    QTest::newRow("alias") << QStringLiteral("BEAUTIFIER");
}

void ToolRegistryTest::searchesEveryMetadataField()
{
    QFETCH(QString, query);
    mtb::ToolRegistry registry;
    QVERIFY(registry.registerTool(makeJsonTool()));

    const auto matches = registry.search(query);
    QCOMPARE(matches.size(), 1);
    QCOMPARE(matches.first().id, QStringLiteral("json.formatter"));
}

void ToolRegistryTest::matchesMultipleTermsCaseInsensitively()
{
    mtb::ToolRegistry registry;
    QVERIFY(registry.registerTool(makeJsonTool()));

    QCOMPARE(registry.search(QStringLiteral("JSON VALIDATE")).size(), 1);
    QCOMPARE(registry.search(QStringLiteral("json missing")).size(), 0);
    QCOMPARE(registry.search(QString()).size(), 1);
}

}  // namespace

QTEST_APPLESS_MAIN(ToolRegistryTest)
#include "test_tool_registry.moc"
