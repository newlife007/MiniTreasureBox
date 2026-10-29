#pragma once

#include <QStringList>
#include <QWidget>

#include "core/tool.h"

class QLabel;
class QLineEdit;

namespace mtb {

class Settings;
class ToolGrid;
class ToolHistory;
class ToolRegistry;

class HomePage final : public QWidget {
    Q_OBJECT

public:
    explicit HomePage(ToolRegistry& registry, Settings& settings, ToolHistory& history,
                      QWidget* parent = nullptr);

    void showHome();
    void showCategory(const QString& category);
    void showCategories(const QString& heading, const QStringList& categories);
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
    void refreshToolGrid();

    ToolRegistry& registry_;
    Settings& settings_;
    ToolHistory& history_;
    View view_{View::All};
    QStringList categories_;

    QLabel* heading_;
    QLineEdit* search_;
    QLabel* resultState_;
    ToolGrid* toolGrid_;
};

}  // namespace mtb
