#pragma once

#include <QList>
#include <QScrollArea>
#include <QSet>
#include <QStringList>

#include "core/tool.h"

class QGridLayout;
class QWidget;

namespace mtb {

class ToolCard;

class ToolGrid final : public QScrollArea {
    Q_OBJECT
    Q_PROPERTY(int cardCount READ cardCount NOTIFY layoutChanged)
    Q_PROPERTY(int columnCount READ columnCount NOTIFY layoutChanged)

public:
    explicit ToolGrid(QWidget* parent = nullptr);

    void setTools(const QList<ToolMetadata>& tools, const QSet<QString>& favoriteIds = {});
    [[nodiscard]] int cardCount() const;
    [[nodiscard]] int columnCount() const;
    [[nodiscard]] QStringList toolIds() const;
    [[nodiscard]] ToolCard* cardForTool(const QString& toolId) const;

signals:
    void toolOpenRequested(const QString& toolId);
    void favoriteToggleRequested(const QString& toolId, bool favorite);
    void layoutChanged();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void relayout();

    QWidget* content_;
    QGridLayout* layout_;
    QList<ToolCard*> cards_;
    int columnCount_{1};
};

}  // namespace mtb
