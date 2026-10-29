#pragma once

#include <QFrame>
#include <QIcon>

#include "core/tool.h"

class QLabel;
class QPushButton;

namespace mtb {

class ToolCard final : public QFrame {
    Q_OBJECT
    Q_PROPERTY(QString toolId READ toolId CONSTANT)
    Q_PROPERTY(bool favorite READ isFavorite WRITE setFavorite)

public:
    explicit ToolCard(const ToolMetadata& metadata, bool favorite,
                      QWidget* parent = nullptr);

    [[nodiscard]] QString toolId() const;
    [[nodiscard]] const ToolMetadata& metadata() const;
    [[nodiscard]] bool isFavorite() const;
    [[nodiscard]] QIcon toolIcon() const;

    void setFavorite(bool favorite);

signals:
    void activationRequested(const QString& toolId);
    void favoriteToggleRequested(const QString& toolId, bool favorite);

protected:
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    static QIcon resolveIcon(const ToolMetadata& metadata, QWidget* widget);
    void updateFavoritePresentation();

    ToolMetadata metadata_;
    bool hovered_{false};
    QIcon icon_;
    QLabel* iconLabel_;
    QLabel* nameLabel_;
    QLabel* descriptionLabel_;
    QLabel* categoryLabel_;
    QLabel* networkLabel_;
    QPushButton* favoriteButton_;
    QPushButton* openButton_;
};

}  // namespace mtb
