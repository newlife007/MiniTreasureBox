#pragma once

#include <QWidget>

#include "core/tool.h"

class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QTreeWidget;
class QTreeWidgetItem;

namespace mtb {

class Settings;
class ToolHistory;
class ToolRegistry;

class HomePage final : public QWidget {
    Q_OBJECT

public:
    explicit HomePage(ToolRegistry& registry, Settings& settings, ToolHistory& history,
                      QWidget* parent = nullptr);

    void showHome();
    void showCategory(const QString& category);
    void showFavorites();
    void showRecent();
    void refresh();
    void focusSearch();
    void clearSearch();

signals:
    void toolOpenRequested(const QString& toolId);
    void favoriteChanged(const QString& toolId, bool favorite);

private:
    enum class View {
        All,
        Category,
        Favorites,
        Recent,
    };

    [[nodiscard]] QList<ToolMetadata> visibleTools() const;
    void refreshToolList();
    void refreshFavorites();
    void refreshRecent();
    void addStoredTools(QListWidget* list, const QStringList& ids, const QString& emptyText);
    void requestOpen(QListWidgetItem* item);

    ToolRegistry& registry_;
    Settings& settings_;
    ToolHistory& history_;
    View view_{View::All};
    QString category_;
    bool refreshing_{false};

    QLabel* heading_;
    QLineEdit* search_;
    QLabel* resultState_;
    QTreeWidget* toolList_;
    QListWidget* favoritesList_;
    QListWidget* recentList_;
};

}  // namespace mtb
