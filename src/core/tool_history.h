#pragma once

#include <QStringList>

class QSettings;

namespace mtb {

class ToolHistory {
public:
    explicit ToolHistory(QSettings& storage, int maximumRecentTools = 10);

    [[nodiscard]] QStringList recentToolIds() const;
    void recordToolOpened(const QString& toolId);
    void clearRecent();

private:
    QSettings* storage_;
    int maximumRecentTools_;
};

}  // namespace mtb
