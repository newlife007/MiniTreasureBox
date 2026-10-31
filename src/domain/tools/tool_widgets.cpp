#include "domain/tools/tool_widgets.h"

#include "domain/tools/tool_logic.h"
#include "ui/widgets/tool_workspace/metric_card.h"
#include "ui/widgets/tool_workspace/responsive_pair.h"
#include "ui/widgets/tool_workspace/tool_status_strip.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QFontDatabase>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QTextEdit>
#include <QVBoxLayout>

#include <functional>

namespace mtb::tools {
namespace {

using mtb::ui::MetricCard;
using mtb::ui::ResponsivePair;
using mtb::ui::ToolStatusStrip;
using mtb::ui::ToolUiState;

QPushButton* actionButton(const QString& text, const QString& tooltip,
                          const QString& role, QWidget* parent)
{
    auto* button = new QPushButton(text, parent);
    button->setToolTip(tooltip);
    button->setAccessibleName(text);
    button->setAccessibleDescription(tooltip);
    button->setProperty("toolButtonRole", role);
    return button;
}

QFrame* editorPanel(const QString& title, const QString& description,
                    QTextEdit* editor, QWidget* parent)
{
    auto* panel = new QFrame(parent);
    panel->setObjectName(QStringLiteral("toolEditorPanel"));
    auto* titleLabel = new QLabel(title, panel);
    titleLabel->setObjectName(QStringLiteral("toolSectionTitle"));
    auto* descriptionLabel = new QLabel(description, panel);
    descriptionLabel->setObjectName(QStringLiteral("toolSectionDescription"));
    descriptionLabel->setWordWrap(true);
    editor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(14, 12, 14, 14);
    layout->setSpacing(7);
    layout->addWidget(titleLabel);
    layout->addWidget(descriptionLabel);
    layout->addWidget(editor, 1);
    return panel;
}

ToolStatusStrip* createStatus(QWidget* parent, QLabel** message)
{
    *message = new QLabel(parent);
    (*message)->setObjectName(QStringLiteral("errorLabel"));
    auto* strip = new ToolStatusStrip(*message, parent);
    strip->setObjectName(QStringLiteral("toolStatusStrip"));
    return strip;
}

void copyText(const QString& text, ToolStatusStrip* status)
{
    if (text.isEmpty()) return;
    QApplication::clipboard()->setText(text);
    status->setState(ToolUiState::Success, QWidget::tr("结果已复制到剪贴板。"));
}

void showResult(const TextResult& result, QTextEdit* output,
                ToolStatusStrip* status, QPushButton* copyButton)
{
    if (result.ok) {
        output->setPlainText(result.output);
        copyButton->setEnabled(!result.output.isEmpty());
        status->setState(ToolUiState::Success, QWidget::tr("处理完成。"));
    } else {
        output->clear();
        copyButton->setEnabled(false);
        status->setState(ToolUiState::Error, result.error);
    }
}

using Transform = std::function<TextResult(const QString&)>;

QWidget* createCodecWidget(const QString& inputTitle, const QString& inputHint,
                           const QString& outputTitle, const QString& outputHint,
                           const QString& encodeLabel, const QString& decodeLabel,
                           const Transform& encode, const Transform& decode,
                           QWidget* parent)
{
    auto* page = new QWidget(parent);
    page->setObjectName(QStringLiteral("textTransformWorkspace"));
    auto* input = new QTextEdit(page);
    input->setObjectName(QStringLiteral("toolInput"));
    input->setPlaceholderText(inputHint);
    auto* output = new QTextEdit(page);
    output->setObjectName(QStringLiteral("toolOutput"));
    output->setReadOnly(true);
    output->setPlaceholderText(outputHint);

    QLabel* statusLabel = nullptr;
    auto* status = createStatus(page, &statusLabel);
    auto* encodeButton = actionButton(encodeLabel, QWidget::tr("编码输入文本"),
                                      QStringLiteral("primary"), page);
    auto* decodeButton = actionButton(decodeLabel, QWidget::tr("解码输入文本"),
                                      QStringLiteral("secondary"), page);
    auto* swapButton = actionButton(QWidget::tr("结果转为输入"),
                                    QWidget::tr("将当前结果移到输入区"),
                                    QStringLiteral("secondary"), page);
    auto* copyButton = actionButton(QWidget::tr("复制结果"),
                                    QWidget::tr("复制结果到剪贴板"),
                                    QStringLiteral("secondary"), page);
    auto* clearButton = actionButton(QWidget::tr("清空"),
                                     QWidget::tr("清空输入、结果和状态"),
                                     QStringLiteral("quiet"), page);
    copyButton->setEnabled(false);
    swapButton->setEnabled(false);

    const auto execute = [=](const Transform& transform) {
        showResult(transform(input->toPlainText()), output, status, copyButton);
        swapButton->setEnabled(!output->toPlainText().isEmpty());
    };
    QObject::connect(encodeButton, &QPushButton::clicked, page,
                     [=] { execute(encode); });
    QObject::connect(decodeButton, &QPushButton::clicked, page,
                     [=] { execute(decode); });
    QObject::connect(copyButton, &QPushButton::clicked, page,
                     [=] { copyText(output->toPlainText(), status); });
    QObject::connect(swapButton, &QPushButton::clicked, page, [=] {
        if (output->toPlainText().isEmpty()) return;
        input->setPlainText(output->toPlainText());
        output->clear();
        copyButton->setEnabled(false);
        swapButton->setEnabled(false);
        status->setState(ToolUiState::Ready, QWidget::tr("结果已移到输入区。"));
        input->setFocus();
    });
    QObject::connect(clearButton, &QPushButton::clicked, page, [=] {
        input->clear();
        output->clear();
        copyButton->setEnabled(false);
        swapButton->setEnabled(false);
        status->setState(ToolUiState::Idle, {});
        input->setFocus();
    });
    QObject::connect(input, &QTextEdit::textChanged, page, [=] {
        if (!output->toPlainText().isEmpty()) {
            copyButton->setEnabled(false);
            swapButton->setEnabled(false);
            status->setState(ToolUiState::Warning,
                             QWidget::tr("输入已变化，请重新处理后再复制结果。"));
        }
    });

    auto* pair = new ResponsivePair(
        editorPanel(inputTitle, QWidget::tr("输入按 UTF-8 文本处理。"), input, page),
        editorPanel(outputTitle, outputHint, output, page), 660, page);
    auto* actions = new QHBoxLayout;
    actions->setSpacing(8);
    actions->addWidget(encodeButton);
    actions->addWidget(decodeButton);
    actions->addWidget(swapButton);
    actions->addStretch();
    actions->addWidget(copyButton);
    actions->addWidget(clearButton);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    layout->addWidget(pair, 1);
    layout->addLayout(actions);
    layout->addWidget(status);
    return page;
}

}  // namespace

QWidget* createJsonWidget(QWidget* parent)
{
    auto* page = new QWidget(parent);
    page->setObjectName(QStringLiteral("textTransformWorkspace"));
    auto* input = new QTextEdit(page);
    input->setObjectName(QStringLiteral("toolInput"));
    input->setPlaceholderText(QWidget::tr("输入需要处理的 JSON"));
    auto* output = new QTextEdit(page);
    output->setObjectName(QStringLiteral("toolOutput"));
    output->setReadOnly(true);
    output->setPlaceholderText(QWidget::tr("格式化或压缩结果会显示在这里"));

    QLabel* statusLabel = nullptr;
    auto* status = createStatus(page, &statusLabel);
    auto* formatButton = actionButton(QWidget::tr("格式化"), QWidget::tr("缩进并格式化 JSON"),
                                      QStringLiteral("primary"), page);
    auto* compactButton = actionButton(QWidget::tr("压缩"), QWidget::tr("移除 JSON 中不必要的空白"),
                                       QStringLiteral("secondary"), page);
    auto* validateButton = actionButton(QWidget::tr("校验"), QWidget::tr("检查 JSON 是否有效"),
                                        QStringLiteral("secondary"), page);
    auto* swapButton = actionButton(QWidget::tr("结果转为输入"), QWidget::tr("将结果移到输入区"),
                                    QStringLiteral("secondary"), page);
    auto* copyButton = actionButton(QWidget::tr("复制结果"), QWidget::tr("复制结果到剪贴板"),
                                    QStringLiteral("secondary"), page);
    auto* clearButton = actionButton(QWidget::tr("清空"), QWidget::tr("清空输入、结果和状态"),
                                     QStringLiteral("quiet"), page);
    copyButton->setEnabled(false);
    swapButton->setEnabled(false);

    const auto execute = [=](const TextResult& result) {
        showResult(result, output, status, copyButton);
        swapButton->setEnabled(result.ok && !result.output.isEmpty());
    };
    QObject::connect(formatButton, &QPushButton::clicked, page,
                     [=] { execute(JsonLogic::format(input->toPlainText())); });
    QObject::connect(compactButton, &QPushButton::clicked, page,
                     [=] { execute(JsonLogic::compact(input->toPlainText())); });
    QObject::connect(validateButton, &QPushButton::clicked, page, [=] {
        const TextResult result = JsonLogic::validate(input->toPlainText());
        execute(result);
        if (result.ok) status->setState(ToolUiState::Success, QWidget::tr("JSON 有效。"));
    });
    QObject::connect(copyButton, &QPushButton::clicked, page,
                     [=] { copyText(output->toPlainText(), status); });
    QObject::connect(swapButton, &QPushButton::clicked, page, [=] {
        if (output->toPlainText().isEmpty()) return;
        input->setPlainText(output->toPlainText());
        output->clear();
        copyButton->setEnabled(false);
        swapButton->setEnabled(false);
        status->setState(ToolUiState::Ready, QWidget::tr("结果已移到输入区。"));
        input->setFocus();
    });
    QObject::connect(clearButton, &QPushButton::clicked, page, [=] {
        input->clear();
        output->clear();
        copyButton->setEnabled(false);
        swapButton->setEnabled(false);
        status->setState(ToolUiState::Idle, {});
        input->setFocus();
    });
    QObject::connect(input, &QTextEdit::textChanged, page, [=] {
        if (!output->toPlainText().isEmpty()) {
            copyButton->setEnabled(false);
            swapButton->setEnabled(false);
            status->setState(ToolUiState::Warning,
                             QWidget::tr("输入已变化，当前结果已过期。"));
        }
    });

    auto* pair = new ResponsivePair(
        editorPanel(QWidget::tr("JSON 输入"), QWidget::tr("支持对象、数组和标准 JSON 数值。"), input, page),
        editorPanel(QWidget::tr("处理结果"), QWidget::tr("结果为只读文本，可复制或转回输入区。"), output, page),
        660, page);
    auto* actions = new QHBoxLayout;
    actions->setSpacing(8);
    for (QPushButton* button : {formatButton, compactButton, validateButton, swapButton})
        actions->addWidget(button);
    actions->addStretch();
    actions->addWidget(copyButton);
    actions->addWidget(clearButton);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    layout->addWidget(pair, 1);
    layout->addLayout(actions);
    layout->addWidget(status);
    return page;
}

QWidget* createBase64Widget(QWidget* parent)
{
    return createCodecWidget(QWidget::tr("UTF-8 文本"),
                             QWidget::tr("输入 UTF-8 文本或 Base64 内容"),
                             QWidget::tr("Base64 文本"),
                             QWidget::tr("严格解码；非 UTF-8 二进制内容会被拒绝。"),
                             QWidget::tr("编码"), QWidget::tr("解码"),
                             Base64Logic::encode, Base64Logic::decode, parent);
}

QWidget* createUrlWidget(QWidget* parent)
{
    return createCodecWidget(QWidget::tr("原始 UTF-8 文本"),
                             QWidget::tr("输入需要按整段处理的文本或百分号编码内容"),
                             QWidget::tr("百分号编码文本"),
                             QWidget::tr("按整段 UTF-8 文本处理，不拆分 URL 路径和查询参数。"),
                             QWidget::tr("URL 编码"), QWidget::tr("URL 解码"),
                             UrlLogic::encode, UrlLogic::decode, parent);
}

QWidget* createTimestampWidget(QWidget* parent)
{
    auto* page = new QWidget(parent);
    page->setObjectName(QStringLiteral("singleValueWorkspace"));
    auto* input = new QLineEdit(page);
    input->setObjectName(QStringLiteral("timestampInput"));
    input->setPlaceholderText(QWidget::tr("例如：1704067200"));
    input->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("^-?\\d*$")), input));
    auto* unit = new QComboBox(page);
    unit->setObjectName(QStringLiteral("timestampUnit"));
    unit->addItem(QWidget::tr("秒"), static_cast<int>(TimestampUnit::Seconds));
    unit->addItem(QWidget::tr("毫秒"), static_cast<int>(TimestampUnit::Milliseconds));
    auto* utcOutput = new QLineEdit(page);
    utcOutput->setObjectName(QStringLiteral("utcOutput"));
    utcOutput->setReadOnly(true);
    auto* localOutput = new QLineEdit(page);
    localOutput->setObjectName(QStringLiteral("localOutput"));
    localOutput->setReadOnly(true);

    QLabel* statusLabel = nullptr;
    auto* status = createStatus(page, &statusLabel);
    auto* convertButton = actionButton(QWidget::tr("转换"), QWidget::tr("转换 Unix 时间戳"),
                                       QStringLiteral("primary"), page);
    auto* nowButton = actionButton(QWidget::tr("使用当前时间"), QWidget::tr("填入当前 Unix 时间戳"),
                                   QStringLiteral("secondary"), page);
    auto* copyUtcButton = actionButton(QWidget::tr("复制 UTC"), QWidget::tr("复制 UTC 时间"),
                                       QStringLiteral("secondary"), page);
    auto* copyLocalButton = actionButton(QWidget::tr("复制本地时间"), QWidget::tr("复制本地时间"),
                                         QStringLiteral("secondary"), page);
    auto* clearButton = actionButton(QWidget::tr("清空"), QWidget::tr("清空时间戳和结果"),
                                     QStringLiteral("quiet"), page);
    copyUtcButton->setEnabled(false);
    copyLocalButton->setEnabled(false);

    const auto convert = [=] {
        const auto selectedUnit = static_cast<TimestampUnit>(unit->currentData().toInt());
        const TimestampResult result = TimestampLogic::convert(input->text(), selectedUnit);
        if (result.ok) {
            utcOutput->setText(result.utc.toString(Qt::ISODateWithMs));
            localOutput->setText(result.local.toString(Qt::ISODateWithMs));
            copyUtcButton->setEnabled(true);
            copyLocalButton->setEnabled(true);
            status->setState(ToolUiState::Success,
                             QWidget::tr("转换完成。本地时间使用系统当前时区。"));
        } else {
            utcOutput->clear();
            localOutput->clear();
            copyUtcButton->setEnabled(false);
            copyLocalButton->setEnabled(false);
            status->setState(ToolUiState::Error, result.error);
        }
    };
    QObject::connect(convertButton, &QPushButton::clicked, page, convert);
    QObject::connect(nowButton, &QPushButton::clicked, page, [=] {
        const bool milliseconds = unit->currentData().toInt()
            == static_cast<int>(TimestampUnit::Milliseconds);
        input->setText(QString::number(milliseconds
                                           ? QDateTime::currentMSecsSinceEpoch()
                                           : QDateTime::currentSecsSinceEpoch()));
        convert();
    });
    QObject::connect(copyUtcButton, &QPushButton::clicked, page,
                     [=] { copyText(utcOutput->text(), status); });
    QObject::connect(copyLocalButton, &QPushButton::clicked, page,
                     [=] { copyText(localOutput->text(), status); });
    QObject::connect(clearButton, &QPushButton::clicked, page, [=] {
        input->clear();
        utcOutput->clear();
        localOutput->clear();
        copyUtcButton->setEnabled(false);
        copyLocalButton->setEnabled(false);
        status->setState(ToolUiState::Idle, {});
        input->setFocus();
    });

    auto* inputCard = new QFrame(page);
    inputCard->setObjectName(QStringLiteral("toolInputCard"));
    auto* inputLayout = new QGridLayout(inputCard);
    inputLayout->setContentsMargins(16, 14, 16, 14);
    inputLayout->setHorizontalSpacing(10);
    inputLayout->setVerticalSpacing(8);
    inputLayout->addWidget(new QLabel(QWidget::tr("Unix 时间戳"), inputCard), 0, 0);
    inputLayout->addWidget(input, 0, 1);
    inputLayout->addWidget(unit, 0, 2);
    inputLayout->addWidget(nowButton, 1, 1);
    inputLayout->addWidget(convertButton, 1, 2);

    auto* utcCard = new QFrame(page);
    utcCard->setObjectName(QStringLiteral("toolResultCard"));
    auto* utcLayout = new QVBoxLayout(utcCard);
    utcLayout->setContentsMargins(16, 14, 16, 14);
    utcLayout->addWidget(new QLabel(QWidget::tr("UTC"), utcCard));
    utcLayout->addWidget(utcOutput);
    utcLayout->addWidget(copyUtcButton, 0, Qt::AlignRight);
    auto* localCard = new QFrame(page);
    localCard->setObjectName(QStringLiteral("toolResultCard"));
    auto* localLayout = new QVBoxLayout(localCard);
    localLayout->setContentsMargins(16, 14, 16, 14);
    localLayout->addWidget(new QLabel(QWidget::tr("本地时间"), localCard));
    localLayout->addWidget(localOutput);
    localLayout->addWidget(copyLocalButton, 0, Qt::AlignRight);

    auto* pair = new ResponsivePair(utcCard, localCard, 620, page);
    auto* bottom = new QHBoxLayout;
    bottom->addStretch();
    bottom->addWidget(clearButton);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    layout->addWidget(inputCard);
    layout->addWidget(pair);
    layout->addWidget(status);
    layout->addLayout(bottom);
    layout->addStretch();
    return page;
}

QWidget* createUuidWidget(QWidget* parent)
{
    auto* page = new QWidget(parent);
    page->setObjectName(QStringLiteral("singleValueWorkspace"));
    auto* output = new QLineEdit(page);
    output->setObjectName(QStringLiteral("uuidOutput"));
    output->setReadOnly(true);
    output->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    output->setPlaceholderText(QWidget::tr("点击“生成 UUID”创建随机 UUID v4"));
    QLabel* statusLabel = nullptr;
    auto* status = createStatus(page, &statusLabel);
    auto* generateButton = actionButton(QWidget::tr("生成 UUID"), QWidget::tr("生成随机 UUID v4"),
                                        QStringLiteral("primary"), page);
    auto* copyButton = actionButton(QWidget::tr("复制"), QWidget::tr("复制 UUID 到剪贴板"),
                                    QStringLiteral("secondary"), page);
    auto* clearButton = actionButton(QWidget::tr("清空"), QWidget::tr("清空 UUID"),
                                     QStringLiteral("quiet"), page);
    copyButton->setEnabled(false);
    QObject::connect(generateButton, &QPushButton::clicked, page, [=] {
        output->setText(UuidLogic::generate());
        copyButton->setEnabled(true);
        status->setState(ToolUiState::Success, QWidget::tr("已生成符合规范的随机 UUID v4。"));
    });
    QObject::connect(copyButton, &QPushButton::clicked, page,
                     [=] { copyText(output->text(), status); });
    QObject::connect(clearButton, &QPushButton::clicked, page, [=] {
        output->clear();
        copyButton->setEnabled(false);
        status->setState(ToolUiState::Idle, {});
    });

    auto* card = new QFrame(page);
    card->setObjectName(QStringLiteral("toolResultCard"));
    auto* cardTitle = new QLabel(QWidget::tr("随机 UUID v4"), card);
    cardTitle->setObjectName(QStringLiteral("toolSectionTitle"));
    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(18, 16, 18, 16);
    cardLayout->setSpacing(10);
    cardLayout->addWidget(cardTitle);
    cardLayout->addWidget(output);
    auto* actions = new QHBoxLayout;
    actions->addWidget(generateButton);
    actions->addWidget(copyButton);
    actions->addStretch();
    actions->addWidget(clearButton);
    cardLayout->addLayout(actions);

    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    layout->addWidget(card);
    layout->addWidget(status);
    layout->addStretch();
    return page;
}

QWidget* createTextStatisticsWidget(QWidget* parent)
{
    auto* page = new QWidget(parent);
    page->setObjectName(QStringLiteral("statisticsWorkspace"));
    auto* input = new QTextEdit(page);
    input->setObjectName(QStringLiteral("toolInput"));
    input->setPlaceholderText(QWidget::tr("输入需要统计的文本，统计结果会实时更新"));
    auto* characters = new QLabel(QStringLiteral("0"), page);
    characters->setObjectName(QStringLiteral("characterCount"));
    auto* nonWhitespace = new QLabel(QStringLiteral("0"), page);
    nonWhitespace->setObjectName(QStringLiteral("nonWhitespaceCount"));
    auto* words = new QLabel(QStringLiteral("0"), page);
    words->setObjectName(QStringLiteral("wordCount"));
    auto* lines = new QLabel(QStringLiteral("0"), page);
    lines->setObjectName(QStringLiteral("lineCount"));
    auto* bytes = new QLabel(QStringLiteral("0"), page);
    bytes->setObjectName(QStringLiteral("byteCount"));

    auto* metrics = new QGridLayout;
    metrics->setSpacing(10);
    metrics->addWidget(new MetricCard(QWidget::tr("Unicode 字符"), characters, page), 0, 0);
    metrics->addWidget(new MetricCard(QWidget::tr("非空白字符"), nonWhitespace, page), 0, 1);
    metrics->addWidget(new MetricCard(QWidget::tr("词数"), words, page), 0, 2);
    metrics->addWidget(new MetricCard(QWidget::tr("行数"), lines, page), 0, 3);
    metrics->addWidget(new MetricCard(QWidget::tr("UTF-8 字节"), bytes, page), 0, 4);

    auto update = [=] {
        const TextStatistics result = TextStatisticsLogic::calculate(input->toPlainText());
        characters->setText(QString::number(result.characters));
        nonWhitespace->setText(QString::number(result.nonWhitespaceCharacters));
        words->setText(QString::number(result.words));
        lines->setText(QString::number(result.lines));
        bytes->setText(QString::number(result.utf8Bytes));
    };
    QObject::connect(input, &QTextEdit::textChanged, page, update);
    auto* copyButton = actionButton(QWidget::tr("复制统计摘要"), QWidget::tr("复制五项统计结果"),
                                    QStringLiteral("secondary"), page);
    auto* clearButton = actionButton(QWidget::tr("清空"), QWidget::tr("清空文本和统计结果"),
                                     QStringLiteral("quiet"), page);
    QObject::connect(copyButton, &QPushButton::clicked, page, [=] {
        QApplication::clipboard()->setText(
            QWidget::tr("Unicode 字符：%1\n非空白字符：%2\n词数：%3\n行数：%4\nUTF-8 字节：%5")
                .arg(characters->text(), nonWhitespace->text(), words->text(),
                     lines->text(), bytes->text()));
    });
    QObject::connect(clearButton, &QPushButton::clicked, page,
                     [=] { input->clear(); input->setFocus(); });

    auto* inputPanel = editorPanel(
        QWidget::tr("输入文本"),
        QWidget::tr("字符按 Unicode code point 统计；词数按空白分隔。"), input, page);
    auto* actions = new QHBoxLayout;
    actions->addWidget(copyButton);
    actions->addStretch();
    actions->addWidget(clearButton);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    layout->addWidget(inputPanel, 1);
    layout->addLayout(metrics);
    layout->addLayout(actions);
    update();
    return page;
}

}  // namespace mtb::tools
