#include "app/main_window.h"

#include "core/tool_history.h"
#include "core/tool_registry.h"
#include "ui/pages/home_page.h"

#include <QApplication>
#include <QCloseEvent>
#include <QColor>
#include <QComboBox>
#include <QFont>
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
#include <QVariant>
#include <QVBoxLayout>
#include <QHBoxLayout>

#include <algorithm>

namespace mtb {
namespace {
constexpr int kViewRole = Qt::UserRole;
constexpr int kCategoryRole = Qt::UserRole + 1;
constexpr int kPracticalSubcategoryRole = Qt::UserRole + 2;

QListWidgetItem* addNavigationItem(QListWidget* list, const QString& text,
                                   const QString& view, const QVariant& category = {})
{
    auto* item = new QListWidgetItem(text, list);
    item->setData(kViewRole, view);
    item->setData(kCategoryRole, category);
    return item;
}

QString themeStyleSheet(Theme theme)
{
    const bool dark = theme == Theme::Dark
        || (theme == Theme::System
            && QApplication::style()->standardPalette().color(QPalette::Window).lightness() < 128);
    const QString background = dark ? QStringLiteral("#111521") : QStringLiteral("#F5F6FA");
    const QString surface = dark ? QStringLiteral("#1B2130") : QStringLiteral("#FFFFFF");
    const QString secondarySurface = dark ? QStringLiteral("#232A3B") : QStringLiteral("#F5F6FA");
    const QString text = dark ? QStringLiteral("#F4F6FA") : QStringLiteral("#172033");
    const QString muted = dark ? QStringLiteral("#A6AEC0") : QStringLiteral("#667085");
    const QString border = dark ? QStringLiteral("#30384B") : QStringLiteral("#E6E8EF");
    const QString primary = dark ? QStringLiteral("#8B83FF") : QStringLiteral("#665CF2");
    const QString primarySurface = dark ? QStringLiteral("#302C5B") : QStringLiteral("#EEECFF");
    const QString success = dark ? QStringLiteral("#55D0AE") : QStringLiteral("#19A982");

    return QStringLiteral(R"(
        QWidget#mainCentral { background: %1; }
        QWidget#sidebar { background: %2; }
        QStackedWidget#mainPages { background: %1; border: none; }
            background: %2;
            border: none;
            outline: none;
            padding: 8px 8px;
            color: %3;
        }
        QListWidget#mainNavigation::item {
            border-radius: 10px;
            padding: 10px 12px;
            margin: 2px 0;
            color: %4;
        }
        QListWidget#mainNavigation::item:hover { background: %5; }
        QListWidget#mainNavigation::item:selected {
            background: %6;
            color: %7;
            border-left: 3px solid %7;
        }
        QLabel#brandMark { background: %7; border-radius: 12px; color: white; font-size: 22px; font-weight: 700; }
        QLabel#brandTitle { color: %3; font-size: 15px; font-weight: 700; }
        QLabel#brandSubtitle { color: %8; font-size: 11px; }
        QFrame#localStatusCard { background: %5; border: 1px solid %9; border-radius: 12px; }
        QLabel#localStatusTitle { color: %3; font-size: 12px; font-weight: 600; }
        QLabel#localStatusDescription { color: %8; font-size: 11px; }
        QLabel#pageHeading { color: %3; font-size: 26px; font-weight: 700; }
        QLabel#pageSubtitle { color: %8; font-size: 14px; }
        QFrame#searchSurface {
            background: %2;
            border: 1px solid %9;
            border-radius: 15px;
        }
        QComboBox#searchModeSelector {
            background: %10;
            border: none;
            border-radius: 8px;
            color: %7;
            padding: 7px 10px;
            font-weight: 600;
        }
        QLineEdit#globalSearch {
            background: transparent;
            border: none;
            color: %3;
            padding: 8px 12px;
            selection-background-color: %7;
        }
        QLabel#resultState { color: %8; padding: 18px; }
        QLabel#privacyHint { color: %8; font-size: 12px; }
        QScrollArea#toolGrid { background: transparent; border: none; }
        QPushButton#favoriteButton {
            background: transparent;
            border: none;
            color: #F0B84B;
            font-size: 19px;
            padding: 2px;
        }
        QPushButton#favoriteButton:hover { color: #D99A24; }
        QPushButton#openButton {
            background: %7;
            border: none;
            border-radius: 8px;
            color: white;
            padding: 7px 12px;
            font-weight: 600;
        }
        QPushButton#openButton:hover { background: %11; }
        QLabel[iconTile="true"] { background: %10; border-radius: 12px; }
        QLabel#toolName { color: %3; font-size: 16px; font-weight: 700; }
        QLabel#toolDescription { color: %4; }
        QLabel#toolCategory { color: %8; }
        QLabel#toolNetworkState { color: %12; }
        QLineEdit#localSearchRoot, QListWidget#localSearchResults {
            background: %2;
            border: 1px solid %9;
            border-radius: 10px;
            color: %3;
        }
        QLabel#localSearchStatus { color: %8; padding: 8px 0; }
        QPushButton#chooseSearchRoot, QPushButton#rebuildSearchIndex {
            background: %10;
            border: 1px solid %9;
            border-radius: 8px;
            color: %7;
            padding: 8px 12px;
            font-weight: 600;
        }
    )")
        .arg(background)
        .arg(surface)
        .arg(text)
        .arg(muted)
        .arg(secondarySurface)
        .arg(primarySurface)
        .arg(primary)
        .arg(muted)
        .arg(border)
        .arg(primarySurface)
        .arg(dark ? QStringLiteral("#756CF0") : QStringLiteral("#554CD8"))
        .arg(success);
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
    pages_->setObjectName(QStringLiteral("mainPages"));
    navigation_->setMaximumWidth(198);
    navigation_->setMinimumWidth(178);
    navigation_->setSpacing(2);

    buildNavigation();
    settingsPage_ = createSettingsPage();
    pages_->addWidget(homePage_);
    pages_->addWidget(settingsPage_);

    auto* separator = new QFrame(this);
    separator->setFrameShape(QFrame::VLine);
    separator->setFrameShadow(QFrame::Plain);

    auto* sidebar = new QWidget(this);
    sidebar->setObjectName(QStringLiteral("sidebar"));
    sidebar->setMinimumWidth(220);
    sidebar->setMaximumWidth(230);
    auto* sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(12, 18, 12, 18);
    sidebarLayout->setSpacing(10);

    auto* brandRow = new QHBoxLayout;
    brandRow->setContentsMargins(4, 0, 4, 8);
    auto* brandMark = new QLabel(QStringLiteral("◈"), sidebar);
    brandMark->setObjectName(QStringLiteral("brandMark"));
    brandMark->setAlignment(Qt::AlignCenter);
    brandMark->setFixedSize(38, 38);
    auto* brandText = new QVBoxLayout;
    auto* brandTitle = new QLabel(tr("小小百宝箱"), sidebar);
    brandTitle->setObjectName(QStringLiteral("brandTitle"));
    auto* brandSubtitle = new QLabel(QStringLiteral("MiniTreasureBox"), sidebar);
    brandSubtitle->setObjectName(QStringLiteral("brandSubtitle"));
    brandText->addWidget(brandTitle);
    brandText->addWidget(brandSubtitle);
    brandRow->addWidget(brandMark);
    brandRow->addLayout(brandText, 1);
    sidebarLayout->addLayout(brandRow);
    sidebarLayout->addWidget(navigation_, 1);

    auto* localStatus = new QFrame(sidebar);
    localStatus->setObjectName(QStringLiteral("localStatusCard"));
    auto* localStatusLayout = new QVBoxLayout(localStatus);
    localStatusLayout->setContentsMargins(12, 10, 12, 10);
    localStatusLayout->setSpacing(2);
    auto* localStatusTitle = new QLabel(tr("●  全部在本机运行"), localStatus);
    localStatusTitle->setObjectName(QStringLiteral("localStatusTitle"));
    auto* localStatusDescription = new QLabel(tr("不会自动上传你的内容"), localStatus);
    localStatusDescription->setObjectName(QStringLiteral("localStatusDescription"));
    localStatusLayout->addWidget(localStatusTitle);
    localStatusLayout->addWidget(localStatusDescription);
    sidebarLayout->addWidget(localStatus);

    auto* central = new QWidget(this);
    central->setObjectName(QStringLiteral("mainCentral"));
    auto* layout = new QHBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(sidebar);
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
        setPracticalSubcategoriesVisible(false);
        homePage_->showHome();
        homePage_->focusSearch();
    });
    auto* escapeShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escapeShortcut->setContext(Qt::ApplicationShortcut);
    connect(escapeShortcut, &QShortcut::activated, this, [this] {
        if (pages_->currentWidget() == toolPage_) {
            returnToPreviousView();
        } else if (pages_->currentWidget() == homePage_) {
            homePage_->clearSearch();
            homePage_->showHome();
            navigation_->setCurrentRow(0);
            setPracticalSubcategoriesVisible(false);
        } else {
            pages_->setCurrentWidget(homePage_);
            homePage_->showHome();
            navigation_->setCurrentRow(0);
            setPracticalSubcategoriesVisible(false);
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

    QSet<QString> categorySet;
    for (const auto& tool : registry_.all()) {
        if (!tool->metadata().category.trimmed().isEmpty()) {
            categorySet.insert(tool->metadata().category);
        }
    }
    QStringList categories(categorySet.begin(), categorySet.end());
    categories.removeAll(tr("AI 工具"));
    categories.removeAll(tr("小游戏"));
    categories.sort(Qt::CaseInsensitive);

    addNavigationItem(navigation_, tr("AI 工具"), QStringLiteral("category"),
                      tr("AI 工具"));
    addNavigationItem(navigation_, tr("实用工具"), QStringLiteral("categories"),
                      categories);
    for (const QString& category : categories) {
        auto* item = addNavigationItem(navigation_, QStringLiteral("  %1").arg(category),
                                       QStringLiteral("category"), category);
        item->setData(kPracticalSubcategoryRole, true);
        item->setHidden(true);
    }
    addNavigationItem(navigation_, tr("小游戏"), QStringLiteral("category"),
                      tr("小游戏"));

    addNavigationItem(navigation_, tr("收藏"), QStringLiteral("favorites"));
    addNavigationItem(navigation_, tr("最近使用"), QStringLiteral("recent"));
    addNavigationItem(navigation_, tr("设置"), QStringLiteral("settings"));
}

void MainWindow::setPracticalSubcategoriesVisible(bool visible)
{
    for (int row = 0; row < navigation_->count(); ++row) {
        auto* item = navigation_->item(row);
        if (item->data(kPracticalSubcategoryRole).toBool()) {
            item->setHidden(!visible);
        }
    }
}

QWidget* MainWindow::createSettingsPage()
{
    auto* page = new QWidget(this);
    page->setObjectName(QStringLiteral("settingsPage"));
    auto* heading = new QLabel(tr("设置"), page);
    QFont font = heading->font();
    font.setPointSize(18);
    font.setBold(true);
    heading->setFont(font);

    themeSelector_ = new QComboBox(page);
    themeSelector_->setObjectName(QStringLiteral("themeSelector"));
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
    const bool showPracticalSubcategories = view == QStringLiteral("categories")
        || item->data(kPracticalSubcategoryRole).toBool();
    setPracticalSubcategoriesVisible(showPracticalSubcategories);
    if (view == QStringLiteral("settings")) {
        pages_->setCurrentWidget(settingsPage_);
        return;
    }

    pages_->setCurrentWidget(homePage_);
    if (view == QStringLiteral("categories")) {
        homePage_->showCategories(item->text().trimmed(),
                                  item->data(kCategoryRole).toStringList());
    } else if (view == QStringLiteral("category")) {
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

    auto* backButton = new QPushButton(tr("返回上一页"), toolPage_);
    backButton->setObjectName(QStringLiteral("backButton"));
    backButton->setToolTip(tr("返回打开此工具前的工具列表（Esc）"));
    backButton->setAccessibleName(tr("返回上一页"));
    backButton->setAccessibleDescription(tr("返回打开此工具前的工具列表"));
    connect(backButton, &QPushButton::clicked, this, &MainWindow::returnToPreviousView);

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

void MainWindow::returnToPreviousView()
{
    pages_->setCurrentWidget(homePage_);
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
    if (centralWidget()) {
        centralWidget()->setStyleSheet(themeStyleSheet(theme));
    }
}

}  // namespace mtb
