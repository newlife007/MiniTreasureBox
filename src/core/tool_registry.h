#pragma once

#include "core/tool.h"

#include <QList>
#include <QString>

#include <memory>

namespace mtb {

class ToolRegistry {
public:
    [[nodiscard]] bool registerTool(std::shared_ptr<ITool> tool);
    [[nodiscard]] std::shared_ptr<ITool> find(const QString& id) const;
    [[nodiscard]] QList<std::shared_ptr<ITool>> all() const;
    [[nodiscard]] QList<ToolMetadata> search(const QString& query) const;

private:
    QList<std::shared_ptr<ITool>> tools_;
};

}  // namespace mtb
