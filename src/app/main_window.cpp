#include "app/main_window.h"

#include "core/tool_history.h"
#include "core/tool_registry.h"
#include "ui/pages/home_page.h"

#include <QApplication>
#include <QCloseEvent>
#include <QColor>
#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QLabel>
#include <QListWidget>
#include <QPalette>
#include <QPushButton>
#include <QSet>
#include <QShortcut>
#include <QStackedWidget>
#include <QStyle>
#include <QVBoxLayout>
#include <QHBoxLayout>

#include <algorithm>

namespace mtb {
namespace {
constexpr int kViewRole = Qt::UserRole;
constexpr int kCategoryRole = Qt::UserRole + 1;

QListWidgetItem* addNavigationItem(QListWidget* list, const QString& text,
                                   const QString& view, const QString& category = {})
{
    auto* item = new QListWidgetItem(text, list);
    item->setData(kViewRole, view);
    item->setData(kCategoryRole, category);
    return item;
}
}  // namespace

MainWindow::MainWindow(ToolRegistry& registry, Settings& settings, ToolHistory& history,
                       QWidget* parent)
    : QMainWindow(parent)
    , registry_(registry)
    , settings_(settings)
    , history_(history)
    , navigation_(new QListWidget(this))
    , pages_(new QStackedWidget(this))
    , homePage_(new HomePage(registry, settings, history, this))
{
    setWindowTitle(tr("小小百宝箱"));
    navigation_->setObjectName(QStringLiteral("mainNavigation"));
    navigation_->setMaximumWidth(190);
    navigation_->setMinimumWidth(150);
    navigation_->setSpacing(2);

    buildNavigation();
    settingsPage_ = createSettingsPage();
    pages_->addWidget(homePage_);
    pages_->addWidget(settingsPage_);

    auto* separator = new QFrame(this);
    separator->setFrameShape(QFrame::VLine);
    separator->setFrameShadow(QFrame::Sunken);

    auto* central = new QWidget(this);
    auto* layout = new QHBoxLayout(central);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->addWidget(navigation_);
    layout->addWidget(separator);
    layout->addWidget(pages_, 1);
    setCentralWidget(central);

    connect(navigation_, &QListWidget::itemClicked, this, &MainWindow::handleNavigation);
    connect(homePage_, &HomePage::toolOpenRequested, this, &MainWindow::openTool);
    connect(homePage_, &HomePage::favoriteChanged, this,
            [this] { homePage_->refresh(); });

    auto* searchShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_K), this);
    searchShortcut->setContext(Qt::ApplicationShortcut);
    connect(searchShortcut, &QShortcut::activated, this, [this] {
        pages_->setCurrentWidget(homePage_);
        navigation_->setCurrentRow(0);
        homePage_->showHome();
        homePage_->focusSearch();
    });
    auto* escapeShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escapeShortcut->setContext(Qt::ApplicationShortcut);
    connect(escapeShortcut, &QShortcut::activated, this, [this] {
        if (pages_->currentWidget() != homePage_) {
            returnHome();
        } else {
            homePage_->clearSearch();
            homePage_->showHome();
            navigation_->setCurrentRow(0);
        }
    });

    if (navigation_->count() > 0) {
        navigation_->setCurrentRow(0);
    }

    resize(1000, 680);
    if (!settings_.windowGeometry().isEmpty()) {
        restoreGeometry(settings_.windowGeometry());
    }
    if (!settings_.windowState().isEmpty()) {
        restoreState(settings_.windowState());
    }
    applyTheme(settings_.theme());
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    settings_.setWindowGeometry(saveGeometry());
    settings_.setWindowState(saveState());
    QMainWindow::closeEvent(event);
}

void MainWindow::buildNavigation()
{
    addNavigationItem(navigation_, tr("首页"), QStringLiteral("home"));

    QStringList categories{tr("AI 工具"), tr("实用工具"), tr("小游戏")};
    QSet<QString> categorySet(categories.begin(), categories.end());
    for (const auto& tool : registry_.all()) {
        if (!tool->metadata().category.trimmed().isEmpty()) {
            categorySet.insert(tool->metadata().category);
        }
    }
    categories = QStringList(categorySet.begin(), categorySet.end());
    categories.sort(Qt::CaseInsensitive);
    for (const QString& category : categories) {
        addNavigationItem(navigation_, category, QStringLiteral("category"), category);
    }

    addNavigationItem(navigation_, tr("收藏"), QStringLiteral("favorites"));
    addNavigationItem(navigation_, tr("最近使用"), QStringLiteral("recent"));
    addNavigationItem(navigation_, tr("设置"), QStringLiteral("settings"));
}

QWidget* MainWindow::createSettingsPage()
{
    auto* page = new QWidget(this);
    auto* heading = new QLabel(tr("设置"), page);
    QFont font = heading->font();
    font.setPointSize(18);
    font.setBold(true);
    heading->setFont(font);

    themeSelector_ = new QComboBox(page);
    themeSelector_->addItem(tr("跟随系统"), static_cast<int>(Theme::System));
    themeSelector_->addItem(tr("浅色"), static_cast<int>(Theme::Light));
    themeSelector_->addItem(tr("深色"), static_cast<int>(Theme::Dark));
    const int currentIndex = themeSelector_->findData(static_cast<int>(settings_.theme()));
    themeSelector_->setCurrentIndex(currentIndex >= 0 ? currentIndex : 0);

    auto* description = new QLabel(
        tr("主题更改会立即生效，并在下次启动时恢复。所有偏好仅保存在本机。"), page);
    description->setWordWrap(true);

    auto* form = new QFormLayout;
    form->addRow(tr("外观主题"), themeSelector_);

    auto* layout = new QVBoxLayout(page);
    layout->addWidget(heading);
    layout->addLayout(form);
    layout->addWidget(description);
    layout->addStretch();

    connect(themeSelector_, &QComboBox::currentIndexChanged, this, [this](int index) {
        const Theme theme = static_cast<Theme>(themeSelector_->itemData(index).toInt());
        settings_.setTheme(theme);
        applyTheme(theme);
    });
    return page;
}

void MainWindow::handleNavigation(QListWidgetItem* item)
{
    const QString view = item->data(kViewRole).toString();
    if (view == QStringLiteral("settings")) {
        pages_->setCurrentWidget(settingsPage_);
        return;
    }

    pages_->setCurrentWidget(homePage_);
    if (view == QStringLiteral("category")) {
        homePage_->showCategory(item->data(kCategoryRole).toString());
    } else if (view == QStringLiteral("favorites")) {
        homePage_->showFavorites();
    } else if (view == QStringLiteral("recent")) {
        homePage_->showRecent();
    } else {
        homePage_->showHome();
    }
}

void MainWindow::openTool(const QString& toolId)
{
    if (!registry_.find(toolId)) {
        return;
    }
    history_.recordToolOpened(toolId);
    homePage_->refresh();
    showToolOverview(toolId);
}

void MainWindow::showToolOverview(const QString& toolId)
{
    const auto tool = registry_.find(toolId);
    if (!tool) {
        return;
    }

    if (toolPage_) {
        pages_->removeWidget(toolPage_);
        toolPage_->deleteLater();
    }

    const ToolMetadata metadata = tool->metadata();
    toolPage_ = new QWidget(pages_);
    toolPage_->setObjectName(QStringLiteral("toolPage_%1").arg(metadata.id));
    auto* heading = new QLabel(metadata.name, toolPage_);
    QFont font = heading->font();
    font.setPointSize(18);
    font.setBold(true);
    heading->setFont(font);
    auto* description = new QLabel(metadata.description, toolPage_);
    description->setWordWrap(true);
    auto* category = new QLabel(tr("分类：%1").arg(metadata.category), toolPage_);
    auto* localState = new QLabel(metadata.requiresNetwork ? tr("需要网络连接") : tr("完全本地运行"), toolPage_);

    auto* favoriteButton = new QPushButton(toolPage_);
    favoriteButton->setToolTip(tr("切换此工具的收藏状态"));
    const auto updateFavoriteLabel = [this, favoriteButton, toolId] {
        favoriteButton->setText(settings_.isFavorite(toolId) ? tr("取消收藏") : tr("收藏工具"));
    };
    updateFavoriteLabel();
    connect(favoriteButton, &QPushButton::clicked, this,
            [this, favoriteButton, toolId, updateFavoriteLabel] {
                settings_.setFavorite(toolId, !settings_.isFavorite(toolId));
                updateFavoriteLabel();
                homePage_->refresh();
            });

    auto* backButton = new QPushButton(tr("返回首页"), toolPage_);
    backButton->setToolTip(tr("返回工具首页（Esc）"));
    connect(backButton, &QPushButton::clicked, this, &MainWindow::returnHome);

    auto* headerActions = new QHBoxLayout;
    headerActions->addWidget(category);
    headerActions->addWidget(localState);
    headerActions->addStretch();
    headerActions->addWidget(favoriteButton);

    auto* layout = new QVBoxLayout(toolPage_);
    layout->addWidget(heading);
    layout->addWidget(description);
    layout->addLayout(headerActions);
    if (QWidget* content = tool->createWidget(toolPage_)) {
        layout->addWidget(content, 1);
    } else {
        auto* unavailable = new QLabel(tr("此工具暂时没有可用界面。"), toolPage_);
        unavailable->setWordWrap(true);
        layout->addWidget(unavailable, 1);
    }
    layout->addWidget(backButton, 0, Qt::AlignLeft);

    pages_->addWidget(toolPage_);
    pages_->setCurrentWidget(toolPage_);
}

void MainWindow::returnHome()
{
    pages_->setCurrentWidget(homePage_);
    navigation_->setCurrentRow(0);
    homePage_->showHome();
}

void MainWindow::applyTheme(Theme theme)
{
    QPalette palette = QApplication::style()->standardPalette();
    if (theme == Theme::Dark) {
        palette.setColor(QPalette::Window, QColor(45, 45, 48));
        palette.setColor(QPalette::WindowText, Qt::white);
        palette.setColor(QPalette::Base, QColor(30, 30, 30));
        palette.setColor(QPalette::AlternateBase, QColor(55, 55, 58));
        palette.setColor(QPalette::ToolTipBase, QColor(30, 30, 30));
        palette.setColor(QPalette::ToolTipText, Qt::white);
        palette.setColor(QPalette::Text, Qt::white);
        palette.setColor(QPalette::Button, QColor(55, 55, 58));
        palette.setColor(QPalette::ButtonText, Qt::white);
        palette.setColor(QPalette::Highlight, QColor(42, 130, 218));
        palette.setColor(QPalette::HighlightedText, Qt::white);
        palette.setColor(QPalette::PlaceholderText, QColor(170, 170, 170));
    } else if (theme == Theme::Light) {
        palette.setColor(QPalette::Window, QColor(245, 245, 245));
        palette.setColor(QPalette::WindowText, Qt::black);
        palette.setColor(QPalette::Base, Qt::white);
        palette.setColor(QPalette::AlternateBase, QColor(235, 235, 235));
        palette.setColor(QPalette::Text, Qt::black);
        palette.setColor(QPalette::Button, QColor(240, 240, 240));
        palette.setColor(QPalette::ButtonText, Qt::black);
        palette.setColor(QPalette::Highlight, QColor(42, 130, 218));
        palette.setColor(QPalette::HighlightedText, Qt::white);
    }
    QApplication::setPalette(palette);
}

}  // namespace mtb
