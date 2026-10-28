#include "ui/pages/home_page.h"

#include "core/settings.h"
#include "core/tool_history.h"
#include "core/tool_registry.h"

#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>

#include <algorithm>

namespace mtb {
namespace {
constexpr int kToolIdRole = Qt::UserRole;

QListWidget* createSectionList(QWidget* parent)
{
    auto* list = new QListWidget(parent);
    list->setAlternatingRowColors(true);
    list->setMinimumHeight(110);
    return list;
}
}  // namespace

HomePage::HomePage(ToolRegistry& registry, Settings& settings, ToolHistory& history,
                   QWidget* parent)
    : QWidget(parent)
    , registry_(registry)
    , settings_(settings)
    , history_(history)
    , heading_(new QLabel(tr("工具首页"), this))
    , search_(new QLineEdit(this))
    , resultState_(new QLabel(this))
    , toolList_(new QTreeWidget(this))
    , favoritesList_(createSectionList(this))
    , recentList_(createSectionList(this))
{
    heading_->setObjectName(QStringLiteral("pageHeading"));
    search_->setObjectName(QStringLiteral("globalSearch"));
    resultState_->setObjectName(QStringLiteral("resultState"));
    toolList_->setObjectName(QStringLiteral("toolList"));
    favoritesList_->setObjectName(QStringLiteral("favoritesList"));
    recentList_->setObjectName(QStringLiteral("recentList"));
    QFont headingFont = heading_->font();
    headingFont.setPointSize(18);
    headingFont.setBold(true);
    heading_->setFont(headingFont);

    search_->setPlaceholderText(tr("搜索名称、分类、描述、关键词或别名"));
    search_->setClearButtonEnabled(true);
    search_->setAccessibleName(tr("全局工具搜索"));

    resultState_->setAlignment(Qt::AlignCenter);
    resultState_->setWordWrap(true);

    toolList_->setColumnCount(4);
    toolList_->setHeaderLabels({tr("工具"), tr("分类"), tr("说明"), tr("收藏")});
    toolList_->setRootIsDecorated(false);
    toolList_->setAlternatingRowColors(true);
    toolList_->setSelectionBehavior(QAbstractItemView::SelectRows);
    toolList_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    toolList_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    toolList_->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    toolList_->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);

    auto* favoritesBox = new QGroupBox(tr("收藏工具"), this);
    auto* favoritesLayout = new QVBoxLayout(favoritesBox);
    favoritesLayout->addWidget(favoritesList_);

    auto* recentBox = new QGroupBox(tr("最近使用"), this);
    auto* recentLayout = new QVBoxLayout(recentBox);
    recentLayout->addWidget(recentList_);

    auto* sectionsLayout = new QHBoxLayout;
    sectionsLayout->addWidget(favoritesBox);
    sectionsLayout->addWidget(recentBox);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(heading_);
    layout->addWidget(search_);
    layout->addWidget(resultState_);
    layout->addWidget(toolList_, 1);
    layout->addLayout(sectionsLayout);

    connect(search_, &QLineEdit::textChanged, this, &HomePage::refreshToolList);
    connect(toolList_, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem* item, int) {
                emit toolOpenRequested(item->data(0, kToolIdRole).toString());
            });
    connect(toolList_, &QTreeWidget::itemChanged, this,
            [this](QTreeWidgetItem* item, int column) {
                if (refreshing_ || column != 3) {
                    return;
                }
                const QString id = item->data(0, kToolIdRole).toString();
                const bool favorite = item->checkState(3) == Qt::Checked;
                settings_.setFavorite(id, favorite);
                refreshFavorites();
                emit favoriteChanged(id, favorite);
            });
    connect(favoritesList_, &QListWidget::itemActivated, this,
            [this](QListWidgetItem* item) { requestOpen(item); });
    connect(recentList_, &QListWidget::itemActivated, this,
            [this](QListWidgetItem* item) { requestOpen(item); });

    refresh();
}

void HomePage::showHome()
{
    view_ = View::All;
    categories_.clear();
    heading_->setText(tr("工具首页"));
    refreshToolList();
}

void HomePage::showCategory(const QString& category)
{
    showCategories(category, {category});
}

void HomePage::showCategories(const QString& heading, const QStringList& categories)
{
    view_ = View::Category;
    categories_ = categories;
    heading_->setText(tr("分类：%1").arg(heading));
    refreshToolList();
}

void HomePage::showFavorites()
{
    view_ = View::Favorites;
    categories_.clear();
    heading_->setText(tr("收藏工具"));
    refreshToolList();
}

void HomePage::showRecent()
{
    view_ = View::Recent;
    categories_.clear();
    heading_->setText(tr("最近使用"));
    refreshToolList();
}

void HomePage::refresh()
{
    refreshToolList();
    refreshFavorites();
    refreshRecent();
}

void HomePage::focusSearch()
{
    search_->setFocus(Qt::ShortcutFocusReason);
    search_->selectAll();
}

void HomePage::clearSearch()
{
    search_->clear();
}

QList<ToolMetadata> HomePage::visibleTools() const
{
    const QList<ToolMetadata> candidates = registry_.search(search_->text().trimmed());
    QList<ToolMetadata> filtered;
    const QStringList favorites = settings_.favoriteToolIds();
    const QStringList recent = history_.recentToolIds();
    for (const ToolMetadata& metadata : candidates) {
        const bool include = view_ == View::All
            || (view_ == View::Category && categories_.contains(metadata.category))
            || (view_ == View::Favorites && favorites.contains(metadata.id))
            || (view_ == View::Recent && recent.contains(metadata.id));
        if (include) {
            filtered.append(metadata);
        }
    }

    if (view_ == View::Recent) {
        std::sort(filtered.begin(), filtered.end(), [&recent](const auto& left, const auto& right) {
            return recent.indexOf(left.id) < recent.indexOf(right.id);
        });
    }
    return filtered;
}

void HomePage::refreshToolList()
{
    const QSignalBlocker blocker(toolList_);
    refreshing_ = true;
    toolList_->clear();

    const QList<ToolMetadata> tools = visibleTools();
    for (const ToolMetadata& metadata : tools) {
        auto* item = new QTreeWidgetItem(toolList_);
        item->setText(0, metadata.name);
        item->setText(1, metadata.category);
        item->setText(2, metadata.description);
        item->setData(0, kToolIdRole, metadata.id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(3, settings_.isFavorite(metadata.id) ? Qt::Checked : Qt::Unchecked);
        if (metadata.requiresNetwork) {
            item->setToolTip(0, tr("此工具需要网络连接"));
        }
    }

    const bool hasTools = !tools.isEmpty();
    toolList_->setVisible(hasTools);
    resultState_->setVisible(!hasTools);
    if (!hasTools) {
        resultState_->setText(registry_.all().isEmpty()
                                  ? tr("尚未注册工具。工具将在后续功能中显示在这里。")
                                  : tr("没有符合当前条件的工具。"));
    }
    refreshing_ = false;
}

void HomePage::refreshFavorites()
{
    addStoredTools(favoritesList_, settings_.favoriteToolIds(), tr("还没有收藏工具。"));
}

void HomePage::refreshRecent()
{
    addStoredTools(recentList_, history_.recentToolIds(), tr("还没有最近使用记录。"));
}

void HomePage::addStoredTools(QListWidget* list, const QStringList& ids, const QString& emptyText)
{
    list->clear();
    for (const QString& id : ids) {
        const auto tool = registry_.find(id);
        if (!tool) {
            continue;
        }
        auto* item = new QListWidgetItem(tool->metadata().name, list);
        item->setData(kToolIdRole, id);
        item->setToolTip(tool->metadata().description);
    }
    if (list->count() == 0) {
        auto* item = new QListWidgetItem(emptyText, list);
        item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
    }
}

void HomePage::requestOpen(QListWidgetItem* item)
{
    const QString id = item->data(kToolIdRole).toString();
    if (!id.isEmpty()) {
        emit toolOpenRequested(id);
    }
}

}  // namespace mtb
