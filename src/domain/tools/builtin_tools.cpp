#include "domain/tools/builtin_tools.h"

#include "core/tool_registry.h"
#include "domain/tools/tool_widgets.h"

#include <QWidget>

#include <memory>
#include <utility>

namespace mtb {
namespace {

using WidgetFactory = QWidget* (*)(QWidget*);

class BuiltinTool final : public ITool {
public:
    BuiltinTool(ToolMetadata metadata, WidgetFactory factory)
        : metadata_(std::move(metadata))
        , factory_(factory)
    {
    }

    [[nodiscard]] ToolMetadata metadata() const override { return metadata_; }
    [[nodiscard]] QWidget* createWidget(QWidget* parent) const override
    {
        return factory_(parent);
    }

private:
    ToolMetadata metadata_;
    WidgetFactory factory_;
};

void add(ToolRegistry& registry, ToolMetadata metadata, WidgetFactory factory)
{
    const bool registered = registry.registerTool(
        std::make_shared<BuiltinTool>(std::move(metadata), factory));
    Q_ASSERT(registered);
    Q_UNUSED(registered);
}

}  // namespace

void registerBuiltinTools(ToolRegistry& registry)
{
    add(registry,
        {QStringLiteral("json-formatter"),
         QStringLiteral("JSON 格式化与校验"),
         QStringLiteral("开发工具"),
         QStringLiteral("格式化、压缩并校验 JSON，错误会显示具体字节位置。"),
         {QStringLiteral("json"), QStringLiteral("格式化"), QStringLiteral("校验"),
          QStringLiteral("compact"), QStringLiteral("validate")},
         {QStringLiteral("JSON formatter"), QStringLiteral("JSON validator")},
         QStringLiteral("document-properties"),
         false},
        tools::createJsonWidget);

    add(registry,
        {QStringLiteral("base64-codec"),
         QStringLiteral("Base64 编解码"),
         QStringLiteral("编码转换"),
         QStringLiteral("在 UTF-8 文本与 Base64 之间安全转换。"),
         {QStringLiteral("base64"), QStringLiteral("编码"), QStringLiteral("解码"),
          QStringLiteral("utf-8")},
         {QStringLiteral("Base64 encoder"), QStringLiteral("Base64 decoder")},
         QStringLiteral("code-context"),
         false},
        tools::createBase64Widget);

    add(registry,
        {QStringLiteral("url-codec"),
         QStringLiteral("URL 百分号编解码"),
         QStringLiteral("编码转换"),
         QStringLiteral("对 Unicode 文本进行 URL 百分号编码和解码。"),
         {QStringLiteral("url"), QStringLiteral("percent"), QStringLiteral("百分号"),
          QStringLiteral("encode"), QStringLiteral("decode")},
         {QStringLiteral("URL encoder"), QStringLiteral("URL decoder")},
         QStringLiteral("insert-link"),
         false},
        tools::createUrlWidget);

    add(registry,
        {QStringLiteral("unix-timestamp"),
         QStringLiteral("Unix 时间戳转换"),
         QStringLiteral("时间工具"),
         QStringLiteral("将秒或毫秒 Unix 时间戳转换为本地时间和 UTC。"),
         {QStringLiteral("unix"), QStringLiteral("timestamp"), QStringLiteral("时间戳"),
          QStringLiteral("utc"), QStringLiteral("毫秒")},
         {QStringLiteral("epoch converter"), QStringLiteral("时间转换")},
         QStringLiteral("clock"),
         false},
        tools::createTimestampWidget);

    add(registry,
        {QStringLiteral("uuid-generator"),
         QStringLiteral("UUID 生成器"),
         QStringLiteral("开发工具"),
         QStringLiteral("在本地生成符合规范的随机 UUID v4。"),
         {QStringLiteral("uuid"), QStringLiteral("guid"), QStringLiteral("随机"),
          QStringLiteral("生成")},
         {QStringLiteral("GUID generator"), QStringLiteral("UUID v4")},
         QStringLiteral("view-refresh"),
         false},
        tools::createUuidWidget);

    add(registry,
        {QStringLiteral("text-statistics"),
         QStringLiteral("文本统计"),
         QStringLiteral("文本工具"),
         QStringLiteral("统计 Unicode 字符、非空白字符、词、行和 UTF-8 字节。"),
         {QStringLiteral("text"), QStringLiteral("statistics"), QStringLiteral("字数"),
          QStringLiteral("字符"), QStringLiteral("行数"), QStringLiteral("bytes")},
         {QStringLiteral("word count"), QStringLiteral("character count")},
         QStringLiteral("view-statistics"),
         false},
        tools::createTextStatisticsWidget);
}

}  // namespace mtb
