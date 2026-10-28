#include "core/tool_history.h"

#include <QSettings>

#include <algorithm>

namespace mtb {
namespace {
constexpr auto kRecentToolsKey = "tools/recent";
}  // namespace

ToolHistory::ToolHistory(QSettings& storage, int maximumRecentTools)
    : storage_(&storage)
    , maximumRecentTools_(std::max(0, maximumRecentTools))
{
}

QStringList ToolHistory::recentToolIds() const
{
    return storage_->value(kRecentToolsKey).toStringList();
}

void ToolHistory::recordToolOpened(const QString& toolId)
{
    const QString normalizedId = toolId.trimmed();
    if (normalizedId.isEmpty()) {
        return;
    }

    QStringList recent = recentToolIds();
    recent.removeAll(normalizedId);
    recent.prepend(normalizedId);
    while (recent.size() > maximumRecentTools_) {
        recent.removeLast();
    }
    storage_->setValue(kRecentToolsKey, recent);
    storage_->sync();
}

void ToolHistory::clearRecent()
{
    storage_->remove(kRecentToolsKey);
    storage_->sync();
}

}  // namespace mtb
