#pragma once

#include <QMainWindow>

#include "core/settings.h"

class QCloseEvent;
class QComboBox;
class QListWidget;
class QListWidgetItem;
class QStackedWidget;

namespace mtb {

class HomePage;
class ToolHistory;
class ToolRegistry;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(ToolRegistry& registry, Settings& settings, ToolHistory& history,
               QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void buildNavigation();
    QWidget* createSettingsPage();
    void handleNavigation(QListWidgetItem* item);
    void openTool(const QString& toolId);
    void showToolOverview(const QString& toolId);
    void returnHome();
    void applyTheme(Theme theme);

    ToolRegistry& registry_;
    Settings& settings_;
    ToolHistory& history_;
    QListWidget* navigation_;
    QStackedWidget* pages_;
    HomePage* homePage_;
    QWidget* settingsPage_{nullptr};
    QWidget* toolPage_{nullptr};
    QComboBox* themeSelector_{nullptr};
};

}  // namespace mtb
