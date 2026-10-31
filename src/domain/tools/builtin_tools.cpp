#include "domain/tools/builtin_tools.h"

#include "ai/handwriting_removal/removal_widget.h"
#include "ai/ocr/ocr_widget.h"
#include "core/tool_registry.h"
#include "domain/games/game_widgets.h"
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

    add(registry,
        {QStringLiteral("image-ocr"),
         QStringLiteral("图片 OCR"),
         QStringLiteral("AI 工具"),
         QStringLiteral("使用应用内置的离线 OCR 组件识别简体中文和英文图片文字。"),
         {QStringLiteral("ocr"), QStringLiteral("图片"), QStringLiteral("文字识别"),
          QStringLiteral("paddleocr"), QStringLiteral("onnx"),
          QStringLiteral("image text")},
         {QStringLiteral("Image OCR"), QStringLiteral("光学字符识别")},
         QStringLiteral("insert-text"),
         false},
        ocr::createOcrWidget);

    add(registry,
        {QStringLiteral("exam-restoration"),
         QStringLiteral("试卷复原小工具"),
         QStringLiteral("AI 工具"),
         QStringLiteral("使用保守的本地图像基线提出手写 Mask，并支持人工修正和背景填充。"),
         {QStringLiteral("试卷复原"), QStringLiteral("去手写"), QStringLiteral("mask"),
          QStringLiteral("handwriting"), QStringLiteral("exam restoration")},
         {QStringLiteral("Handwriting removal"), QStringLiteral("试卷去手写")},
         QStringLiteral("draw-eraser"),
         false},
        handwriting::createHandwritingRemovalWidget);

    add(registry,
        {QStringLiteral("guess-number"),
         QStringLiteral("猜数字"),
         QStringLiteral("小游戏"),
         QStringLiteral("猜出 1 到 100 之间的本地随机数字。"),
         {QStringLiteral("猜数字"), QStringLiteral("数字"), QStringLiteral("guess"),
          QStringLiteral("number")},
         {QStringLiteral("Guess Number"), QStringLiteral("数字猜谜")},
         QStringLiteral("help-about"),
         false},
        games::createGuessNumberWidget);

    add(registry,
        {QStringLiteral("game-2048"),
         QStringLiteral("2048"),
         QStringLiteral("小游戏"),
         QStringLiteral("移动并合并数字方块，挑战 2048。"),
         {QStringLiteral("2048"), QStringLiteral("方块"), QStringLiteral("merge"),
          QStringLiteral("puzzle")},
         {QStringLiteral("2048 game"), QStringLiteral("数字方块")},
         QStringLiteral("view-grid"),
         false},
        games::create2048Widget);

    add(registry,
        {QStringLiteral("snake"),
         QStringLiteral("贪吃蛇"),
         QStringLiteral("小游戏"),
         QStringLiteral("使用键盘控制贪吃蛇成长并避开碰撞。"),
         {QStringLiteral("贪吃蛇"), QStringLiteral("snake"), QStringLiteral("方向键"),
          QStringLiteral("wasd")},
         {QStringLiteral("Snake Game"), QStringLiteral("蛇")},
         QStringLiteral("go-next"),
         false},
        games::createSnakeWidget);

    add(registry,
        {QStringLiteral("tetris"),
         QStringLiteral("俄罗斯方块"),
         QStringLiteral("小游戏"),
         QStringLiteral("移动、旋转和下落方块以消除整行。"),
         {QStringLiteral("俄罗斯方块"), QStringLiteral("tetris"), QStringLiteral("方块"),
          QStringLiteral("消除")},
         {QStringLiteral("Tetris Game"), QStringLiteral("落下方块")},
         QStringLiteral("view-grid"),
         false},
        games::createTetrisWidget);

    add(registry,
        {QStringLiteral("gomoku"),
         QStringLiteral("五子棋"),
         QStringLiteral("小游戏"),
         QStringLiteral("本地双人轮流落子，先连成五子者获胜。"),
         {QStringLiteral("五子棋"), QStringLiteral("gomoku"), QStringLiteral("棋盘"),
          QStringLiteral("five in a row")},
         {QStringLiteral("Gomoku"), QStringLiteral("Five in a Row")},
         QStringLiteral("applications-games"),
         false},
        games::createGomokuWidget);
}

}  // namespace mtb
