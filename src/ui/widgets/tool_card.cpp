#include "ui/widgets/tool_card.h"

#include <QEnterEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

namespace mtb {
namespace {
constexpr int kIconSize = 40;

QStyle::StandardPixmap fallbackPixmap(const ToolMetadata& metadata)
{
    const QString key = metadata.iconName.toLower();
    if (key.contains(QStringLiteral("clock"))) {
        return QStyle::SP_FileDialogDetailedView;
    }
    if (key.contains(QStringLiteral("refresh"))) {
        return QStyle::SP_BrowserReload;
    }
    if (key.contains(QStringLiteral("link"))) {
        return QStyle::SP_DirLinkIcon;
    }
    if (key.contains(QStringLiteral("statistics"))) {
        return QStyle::SP_ComputerIcon;
    }
    if (key.contains(QStringLiteral("code"))) {
        return QStyle::SP_FileDialogContentsView;
    }
    return QStyle::SP_FileIcon;
}
}  // namespace

ToolCard::ToolCard(const ToolMetadata& metadata, bool favorite, QWidget* parent)
    : QFrame(parent)
    , metadata_(metadata)
    , icon_(resolveIcon(metadata, this))
    , iconLabel_(new QLabel(this))
    , nameLabel_(new QLabel(metadata.name, this))
    , descriptionLabel_(new QLabel(metadata.description, this))
    , categoryLabel_(new QLabel(metadata.category, this))
    , networkLabel_(new QLabel(tr("需要网络连接"), this))
    , favoriteButton_(new QPushButton(this))
    , openButton_(new QPushButton(tr("打开"), this))
{
    setObjectName(QStringLiteral("toolCard_%1").arg(metadata.id));
    setFrameShape(QFrame::NoFrame);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMinimumHeight(184);
    setProperty("surfaceCard", true);
    setAccessibleName(metadata.name);
    setAccessibleDescription(tr("%1。分类：%2。%3")
                                 .arg(metadata.description, metadata.category,
                                      metadata.requiresNetwork ? tr("需要网络连接")
                                                               : tr("本地工具")));

    iconLabel_->setObjectName(QStringLiteral("toolIcon"));
    iconLabel_->setPixmap(icon_.pixmap(kIconSize, kIconSize));
    iconLabel_->setFixedSize(kIconSize, kIconSize);
    iconLabel_->setAlignment(Qt::AlignCenter);
    iconLabel_->setProperty("iconTile", true);
    iconLabel_->setAccessibleName(tr("%1 图标").arg(metadata.name));

    nameLabel_->setObjectName(QStringLiteral("toolName"));
    QFont nameFont = nameLabel_->font();
    nameFont.setBold(true);
    nameFont.setPointSize(nameFont.pointSize() + 1);
    nameLabel_->setFont(nameFont);

    descriptionLabel_->setObjectName(QStringLiteral("toolDescription"));
    descriptionLabel_->setWordWrap(true);
    descriptionLabel_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    descriptionLabel_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    categoryLabel_->setObjectName(QStringLiteral("toolCategory"));
    categoryLabel_->setText(tr("分类：%1").arg(metadata.category));
    categoryLabel_->setForegroundRole(QPalette::PlaceholderText);

    networkLabel_->setObjectName(QStringLiteral("toolNetworkState"));
    networkLabel_->setText(metadata.requiresNetwork ? tr("需要网络连接") : tr("本地运行"));
    networkLabel_->setVisible(true);
    networkLabel_->setForegroundRole(metadata.requiresNetwork ? QPalette::Highlight
                                                             : QPalette::Mid);

    favoriteButton_->setObjectName(QStringLiteral("favoriteButton"));
    favoriteButton_->setCheckable(true);
    favoriteButton_->setFocusPolicy(Qt::StrongFocus);
    favoriteButton_->setAccessibleDescription(tr("切换 %1 的收藏状态").arg(metadata.name));
    favoriteButton_->setToolTip(tr("切换收藏状态"));

    openButton_->setObjectName(QStringLiteral("openButton"));
    openButton_->setFocusPolicy(Qt::StrongFocus);
    openButton_->setAccessibleName(tr("打开 %1").arg(metadata.name));
    openButton_->setAccessibleDescription(metadata.description);

    auto* titleLayout = new QHBoxLayout;
    titleLayout->addWidget(iconLabel_);
    titleLayout->addWidget(nameLabel_, 1);
    titleLayout->addWidget(favoriteButton_);

    auto* footerLayout = new QHBoxLayout;
    footerLayout->addWidget(categoryLabel_);
    footerLayout->addWidget(networkLabel_);
    footerLayout->addStretch();
    footerLayout->addWidget(openButton_);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(9);
    layout->addLayout(titleLayout);
    layout->addWidget(descriptionLabel_, 1);
    layout->addLayout(footerLayout);

    setFavorite(favorite);
    connect(openButton_, &QPushButton::clicked, this,
            [this] { emit activationRequested(metadata_.id); });
    connect(favoriteButton_, &QPushButton::toggled, this, [this](bool checked) {
        updateFavoritePresentation();
        emit favoriteToggleRequested(metadata_.id, checked);
    });
}

QString ToolCard::toolId() const
{
    return metadata_.id;
}

const ToolMetadata& ToolCard::metadata() const
{
    return metadata_;
}

bool ToolCard::isFavorite() const
{
    return favoriteButton_->isChecked();
}

QIcon ToolCard::toolIcon() const
{
    return icon_;
}

void ToolCard::setFavorite(bool favorite)
{
    if (favoriteButton_->isChecked() != favorite) {
        favoriteButton_->blockSignals(true);
        favoriteButton_->setChecked(favorite);
        favoriteButton_->blockSignals(false);
    }
    updateFavoritePresentation();
}

void ToolCard::enterEvent(QEnterEvent* event)
{
    hovered_ = true;
    update();
    QFrame::enterEvent(event);
}

void ToolCard::leaveEvent(QEvent* event)
{
    hovered_ = false;
    update();
    QFrame::leaveEvent(event);
}

void ToolCard::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter
        || event->key() == Qt::Key_Space) {
        emit activationRequested(metadata_.id);
        event->accept();
        return;
    }
    QFrame::keyPressEvent(event);
}

void ToolCard::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        emit activationRequested(metadata_.id);
        event->accept();
        return;
    }
    QFrame::mouseDoubleClickEvent(event);
}

void ToolCard::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QPalette currentPalette = palette();
    QColor background = currentPalette.color(QPalette::Base);
    QColor border = currentPalette.color(QPalette::Mid);
    if (hovered_ || hasFocus()) {
        background = currentPalette.color(QPalette::AlternateBase);
        border = currentPalette.color(QPalette::Highlight);
    }
    painter.setBrush(background);
    painter.setPen(QPen(border, hasFocus() ? 2.0 : 1.0));
    painter.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 14, 14);
}

QIcon ToolCard::resolveIcon(const ToolMetadata& metadata, QWidget* widget)
{
    const QIcon fallback = widget->style()->standardIcon(fallbackPixmap(metadata));
    const QIcon themed = QIcon::fromTheme(metadata.iconName, fallback);
    return themed.isNull() ? fallback : themed;
}

void ToolCard::updateFavoritePresentation()
{
    const bool favorite = favoriteButton_->isChecked();
    favoriteButton_->setText(favorite ? QStringLiteral("★") : QStringLiteral("☆"));
    favoriteButton_->setAccessibleName(
        favorite ? tr("取消收藏 %1").arg(metadata_.name)
                 : tr("收藏 %1").arg(metadata_.name));
    favoriteButton_->setToolTip(
        favorite ? tr("取消收藏 %1").arg(metadata_.name)
                 : tr("收藏 %1").arg(metadata_.name));
}

}  // namespace mtb
