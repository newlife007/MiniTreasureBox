#include "domain/tools/tool_widgets.h"

#include "domain/tools/tool_logic.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>

#include <functional>

namespace mtb::tools {
namespace {

QLabel* createErrorLabel(QWidget* parent)
{
    auto* label = new QLabel(parent);
    label->setObjectName(QStringLiteral("errorLabel"));
    label->setStyleSheet(QStringLiteral("color: #c62828;"));
    label->setWordWrap(true);
    label->hide();
    return label;
}

void showResult(const TextResult& result, QTextEdit* output, QLabel* error)
{
    error->setText(result.error);
    error->setVisible(!result.ok);
    if (result.ok) {
        output->setPlainText(result.output);
    } else {
        output->clear();
    }
}

void copyText(const QString& text)
{
    QApplication::clipboard()->setText(text);
}

QPushButton* actionButton(const QString& text, const QString& tooltip, QWidget* parent)
{
    auto* button = new QPushButton(text, parent);
    button->setToolTip(tooltip);
    return button;
}

using Transform = std::function<TextResult(const QString&)>;

QWidget* createCodecWidget(const QString& inputHint, const QString& encodeLabel,
                           const QString& decodeLabel, const Transform& encode,
                           const Transform& decode, QWidget* parent)
{
    auto* page = new QWidget(parent);
    auto* input = new QTextEdit(page);
    input->setObjectName(QStringLiteral("toolInput"));
    input->setPlaceholderText(inputHint);
    auto* output = new QTextEdit(page);
    output->setObjectName(QStringLiteral("toolOutput"));
    output->setReadOnly(true);
    output->setPlaceholderText(QWidget::tr("结果显示在这里"));
    auto* error = createErrorLabel(page);

    auto* encodeButton = actionButton(encodeLabel, QWidget::tr("编码输入文本"), page);
    auto* decodeButton = actionButton(decodeLabel, QWidget::tr("解码输入文本"), page);
    auto* copyButton = actionButton(QWidget::tr("复制结果"), QWidget::tr("复制结果到剪贴板"), page);
    auto* clearButton = actionButton(QWidget::tr("清空"), QWidget::tr("清空输入、结果和错误"), page);

    QObject::connect(encodeButton, &QPushButton::clicked, page,
                     [=] { showResult(encode(input->toPlainText()), output, error); });
    QObject::connect(decodeButton, &QPushButton::clicked, page,
                     [=] { showResult(decode(input->toPlainText()), output, error); });
    QObject::connect(copyButton, &QPushButton::clicked, page,
                     [=] { copyText(output->toPlainText()); });
    QObject::connect(clearButton, &QPushButton::clicked, page, [=] {
        input->clear();
        output->clear();
        error->clear();
        error->hide();
        input->setFocus();
    });

    auto* actions = new QHBoxLayout;
    actions->addWidget(encodeButton);
    actions->addWidget(decodeButton);
    actions->addWidget(copyButton);
    actions->addWidget(clearButton);
    actions->addStretch();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(new QLabel(QWidget::tr("输入"), page));
    layout->addWidget(input, 1);
    layout->addLayout(actions);
    layout->addWidget(error);
    layout->addWidget(new QLabel(QWidget::tr("结果"), page));
    layout->addWidget(output, 1);
    return page;
}

}  // namespace

QWidget* createJsonWidget(QWidget* parent)
{
    auto* page = new QWidget(parent);
    auto* input = new QTextEdit(page);
    input->setObjectName(QStringLiteral("toolInput"));
    input->setPlaceholderText(QWidget::tr("输入需要处理的 JSON"));
    auto* output = new QTextEdit(page);
    output->setObjectName(QStringLiteral("toolOutput"));
    output->setReadOnly(true);
    auto* error = createErrorLabel(page);

    auto* formatButton = actionButton(QWidget::tr("格式化"), QWidget::tr("缩进并格式化 JSON"), page);
    auto* compactButton = actionButton(QWidget::tr("压缩"), QWidget::tr("移除 JSON 中不必要的空白"), page);
    auto* validateButton = actionButton(QWidget::tr("校验"), QWidget::tr("检查 JSON 是否有效"), page);
    auto* copyButton = actionButton(QWidget::tr("复制结果"), QWidget::tr("复制结果到剪贴板"), page);
    auto* clearButton = actionButton(QWidget::tr("清空"), QWidget::tr("清空输入、结果和错误"), page);

    QObject::connect(formatButton, &QPushButton::clicked, page,
                     [=] { showResult(JsonLogic::format(input->toPlainText()), output, error); });
    QObject::connect(compactButton, &QPushButton::clicked, page,
                     [=] { showResult(JsonLogic::compact(input->toPlainText()), output, error); });
    QObject::connect(validateButton, &QPushButton::clicked, page,
                     [=] { showResult(JsonLogic::validate(input->toPlainText()), output, error); });
    QObject::connect(copyButton, &QPushButton::clicked, page,
                     [=] { copyText(output->toPlainText()); });
    QObject::connect(clearButton, &QPushButton::clicked, page, [=] {
        input->clear();
        output->clear();
        error->clear();
        error->hide();
        input->setFocus();
    });

    auto* actions = new QHBoxLayout;
    for (QPushButton* button : {formatButton, compactButton, validateButton, copyButton, clearButton}) {
        actions->addWidget(button);
    }
    actions->addStretch();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(new QLabel(QWidget::tr("JSON 输入"), page));
    layout->addWidget(input, 1);
    layout->addLayout(actions);
    layout->addWidget(error);
    layout->addWidget(new QLabel(QWidget::tr("处理结果"), page));
    layout->addWidget(output, 1);
    return page;
}

QWidget* createBase64Widget(QWidget* parent)
{
    return createCodecWidget(QWidget::tr("输入 UTF-8 文本或 Base64 内容"), QWidget::tr("编码"),
                             QWidget::tr("解码"), Base64Logic::encode, Base64Logic::decode,
                             parent);
}

QWidget* createUrlWidget(QWidget* parent)
{
    return createCodecWidget(QWidget::tr("输入文本或百分号编码内容"), QWidget::tr("URL 编码"),
                             QWidget::tr("URL 解码"), UrlLogic::encode, UrlLogic::decode,
                             parent);
}

QWidget* createTimestampWidget(QWidget* parent)
{
    auto* page = new QWidget(parent);
    auto* input = new QLineEdit(page);
    input->setObjectName(QStringLiteral("timestampInput"));
    input->setPlaceholderText(QWidget::tr("例如：1704067200"));
    auto* unit = new QComboBox(page);
    unit->addItem(QWidget::tr("秒"), static_cast<int>(TimestampUnit::Seconds));
    unit->addItem(QWidget::tr("毫秒"), static_cast<int>(TimestampUnit::Milliseconds));
    auto* utcOutput = new QLineEdit(page);
    utcOutput->setReadOnly(true);
    auto* localOutput = new QLineEdit(page);
    localOutput->setReadOnly(true);
    auto* error = createErrorLabel(page);
    auto* convertButton = actionButton(QWidget::tr("转换"), QWidget::tr("转换 Unix 时间戳"), page);
    auto* copyButton = actionButton(QWidget::tr("复制 UTC"), QWidget::tr("复制 UTC 时间到剪贴板"), page);
    auto* clearButton = actionButton(QWidget::tr("清空"), QWidget::tr("清空时间戳和结果"), page);

    QObject::connect(convertButton, &QPushButton::clicked, page, [=] {
        const auto selectedUnit = static_cast<TimestampUnit>(unit->currentData().toInt());
        const TimestampResult result = TimestampLogic::convert(input->text(), selectedUnit);
        error->setText(result.error);
        error->setVisible(!result.ok);
        if (result.ok) {
            utcOutput->setText(result.utc.toString(Qt::ISODateWithMs));
            localOutput->setText(result.local.toString(Qt::ISODateWithMs));
        } else {
            utcOutput->clear();
            localOutput->clear();
        }
    });
    QObject::connect(copyButton, &QPushButton::clicked, page, [=] { copyText(utcOutput->text()); });
    QObject::connect(clearButton, &QPushButton::clicked, page, [=] {
        input->clear();
        utcOutput->clear();
        localOutput->clear();
        error->hide();
        input->setFocus();
    });

    auto* form = new QFormLayout;
    form->addRow(QWidget::tr("Unix 时间戳"), input);
    form->addRow(QWidget::tr("单位"), unit);
    form->addRow(QWidget::tr("UTC"), utcOutput);
    form->addRow(QWidget::tr("本地时间"), localOutput);
    auto* actions = new QHBoxLayout;
    actions->addWidget(convertButton);
    actions->addWidget(copyButton);
    actions->addWidget(clearButton);
    actions->addStretch();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(form);
    layout->addLayout(actions);
    layout->addWidget(error);
    layout->addStretch();
    return page;
}

QWidget* createUuidWidget(QWidget* parent)
{
    auto* page = new QWidget(parent);
    auto* output = new QLineEdit(page);
    output->setObjectName(QStringLiteral("uuidOutput"));
    output->setReadOnly(true);
    auto* generateButton = actionButton(QWidget::tr("生成 UUID"), QWidget::tr("生成随机 UUID v4"), page);
    auto* copyButton = actionButton(QWidget::tr("复制"), QWidget::tr("复制 UUID 到剪贴板"), page);
    auto* clearButton = actionButton(QWidget::tr("清空"), QWidget::tr("清空 UUID"), page);
    QObject::connect(generateButton, &QPushButton::clicked, page,
                     [=] { output->setText(UuidLogic::generate()); });
    QObject::connect(copyButton, &QPushButton::clicked, page, [=] { copyText(output->text()); });
    QObject::connect(clearButton, &QPushButton::clicked, page, [=] { output->clear(); });
    auto* actions = new QHBoxLayout;
    actions->addWidget(generateButton);
    actions->addWidget(copyButton);
    actions->addWidget(clearButton);
    actions->addStretch();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(new QLabel(QWidget::tr("随机 UUID v4"), page));
    layout->addWidget(output);
    layout->addLayout(actions);
    layout->addStretch();
    return page;
}

QWidget* createTextStatisticsWidget(QWidget* parent)
{
    auto* page = new QWidget(parent);
    auto* input = new QTextEdit(page);
    input->setObjectName(QStringLiteral("toolInput"));
    input->setPlaceholderText(QWidget::tr("输入需要统计的文本"));
    auto* characters = new QLabel(QStringLiteral("0"), page);
    auto* nonWhitespace = new QLabel(QStringLiteral("0"), page);
    auto* words = new QLabel(QStringLiteral("0"), page);
    auto* lines = new QLabel(QStringLiteral("0"), page);
    auto* bytes = new QLabel(QStringLiteral("0"), page);
    auto update = [=] {
        const TextStatistics result = TextStatisticsLogic::calculate(input->toPlainText());
        characters->setText(QString::number(result.characters));
        nonWhitespace->setText(QString::number(result.nonWhitespaceCharacters));
        words->setText(QString::number(result.words));
        lines->setText(QString::number(result.lines));
        bytes->setText(QString::number(result.utf8Bytes));
    };
    QObject::connect(input, &QTextEdit::textChanged, page, update);
    auto* clearButton = actionButton(QWidget::tr("清空"), QWidget::tr("清空文本和统计结果"), page);
    QObject::connect(clearButton, &QPushButton::clicked, page, [=] { input->clear(); });
    auto* statistics = new QGridLayout;
    statistics->addWidget(new QLabel(QWidget::tr("Unicode 字符"), page), 0, 0);
    statistics->addWidget(characters, 0, 1);
    statistics->addWidget(new QLabel(QWidget::tr("非空白字符"), page), 1, 0);
    statistics->addWidget(nonWhitespace, 1, 1);
    statistics->addWidget(new QLabel(QWidget::tr("词数"), page), 2, 0);
    statistics->addWidget(words, 2, 1);
    statistics->addWidget(new QLabel(QWidget::tr("行数"), page), 3, 0);
    statistics->addWidget(lines, 3, 1);
    statistics->addWidget(new QLabel(QWidget::tr("UTF-8 字节"), page), 4, 0);
    statistics->addWidget(bytes, 4, 1);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(input, 1);
    layout->addLayout(statistics);
    layout->addWidget(clearButton, 0, Qt::AlignLeft);
    update();
    return page;
}

}  // namespace mtb::tools
