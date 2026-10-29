#include "ui/pages/home_page.h"

#include "core/settings.h"
#include "core/tool_history.h"
#include "core/tool_registry.h"
#include "ui/widgets/local_file_search_widget.h"
#include "ui/widgets/tool_grid.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSet>
#include <QVBoxLayout>

#include <algorithm>

namespace mtb {

HomePage::HomePage(ToolRegistry& registry, Settings& settings, ToolHistory& history,
                   QWidget* parent)
    : QWidget(parent)
    , registry_(registry)
    , settings_(settings)
    , history_(history)
    , heading_(new QLabel(tr("工具首页"), this))
    , searchMode_(new QComboBox(this))
    , search_(new QLineEdit(this))
    , resultState_(new QLabel(this))
    , toolGrid_(new ToolGrid(this))
    , localFileSearch_(new LocalFileSearchWidget(this))
{
    heading_->setObjectName(QStringLiteral("pageHeading"));
    searchMode_->setObjectName(QStringLiteral("searchModeSelector"));
    searchMode_->addItem(tr("工具搜索"), QStringLiteral("tools"));
    searchMode_->addItem(tr("本地文件"), QStringLiteral("files"));
    searchMode_->setAccessibleName(tr("搜索模式"));
    search_->setObjectName(QStringLiteral("globalSearch"));
    resultState_->setObjectName(QStringLiteral("resultState"));
    QFont headingFont = heading_->font();
    headingFont.setPointSize(18);
    headingFont.setBold(true);
    heading_->setFont(headingFont);

    search_->setPlaceholderText(tr("搜索名称、分类、描述、关键词或别名"));
    search_->setClearButtonEnabled(true);
    search_->setAccessibleName(tr("全局工具搜索"));
    search_->setAccessibleDescription(tr("输入文字以筛选当前页面中的工具卡片"));

    resultState_->setAlignment(Qt::AlignCenter);
    resultState_->setWordWrap(true);
    resultState_->setAccessibleName(tr("工具列表状态"));

    auto* searchLayout = new QHBoxLayout;
    searchLayout->setContentsMargins(0, 0, 0, 0);
    searchLayout->addWidget(searchMode_);
    searchLayout->addWidget(search_, 1);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(10);
    layout->addWidget(heading_);
    layout->addLayout(searchLayout);
    layout->addWidget(resultState_);
    layout->addWidget(toolGrid_, 1);
    layout->addWidget(localFileSearch_, 1);

    connect(search_, &QLineEdit::textChanged, this, [this](const QString& query) {
        if (searchMode_->currentData().toString() == QStringLiteral("files")) {
            localFileSearch_->setQuery(query);
        } else {
            refreshToolGrid();
        }
    });
    connect(searchMode_, &QComboBox::currentIndexChanged, this,
            &HomePage::updateSearchMode);
    connect(toolGrid_, &ToolGrid::toolOpenRequested,
            this, &HomePage::toolOpenRequested);
    connect(toolGrid_, &ToolGrid::favoriteToggleRequested, this,
            [this](const QString& id, bool favorite) {
                settings_.setFavorite(id, favorite);
                refreshToolGrid();
                emit favoriteChanged(id, favorite);
            });

    updateSearchMode();
    refresh();
}

void HomePage::showHome()
{
    view_ = View::All;
    categories_.clear();
    heading_->setText(tr("工具首页"));
    refreshToolGrid();
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
    refreshToolGrid();
}

void HomePage::showFavorites()
{
    view_ = View::Favorites;
    categories_.clear();
    heading_->setText(tr("收藏工具"));
    refreshToolGrid();
}

void HomePage::showRecent()
{
    view_ = View::Recent;
    categories_.clear();
    heading_->setText(tr("最近使用"));
    refreshToolGrid();
}

void HomePage::refresh()
{
    refreshToolGrid();
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

void HomePage::updateSearchMode()
{
    const bool fileMode = searchMode_->currentData().toString() == QStringLiteral("files");
    search_->setPlaceholderText(fileMode
                                    ? tr("搜索本地文件名、目录名或文件内容")
                                    : tr("搜索名称、分类、描述、关键词或别名"));
    search_->setAccessibleName(fileMode ? tr("本地文件搜索") : tr("全局工具搜索"));
    search_->setAccessibleDescription(
        fileMode ? tr("输入文字以搜索所选目录中的本地文件")
                 : tr("输入文字以筛选当前页面中的工具卡片"));
    localFileSearch_->setVisible(fileMode);
    resultState_->setVisible(false);
    toolGrid_->setVisible(false);
    if (fileMode) {
        localFileSearch_->setQuery(search_->text());
    } else {
        localFileSearch_->setQuery({});
        refreshToolGrid();
    }
}

void HomePage::refreshToolGrid()
{
    if (searchMode_->currentData().toString() == QStringLiteral("files")) {
        toolGrid_->hide();
        resultState_->hide();
        return;
    }

    const QList<ToolMetadata> tools = visibleTools();
    const QStringList favoriteIds = settings_.favoriteToolIds();
    toolGrid_->setTools(tools, QSet<QString>(favoriteIds.cbegin(), favoriteIds.cend()));

    const bool hasTools = !tools.isEmpty();
    toolGrid_->setVisible(hasTools);
    resultState_->setVisible(!hasTools);
    if (!hasTools) {
        resultState_->setText(registry_.all().isEmpty()
                                  ? tr("尚未注册工具。工具将在后续功能中显示在这里。")
                                  : tr("没有符合当前条件的工具。"));
    }
}

}  // namespace mtb
