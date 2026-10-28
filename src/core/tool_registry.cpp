#include "core/tool_registry.h"

namespace mtb {

bool ToolRegistry::registerTool(std::shared_ptr<ITool> tool)
{
    if (!tool) {
        return false;
    }

    const ToolMetadata metadata = tool->metadata();
    const QString id = metadata.id.trimmed();
    if (id.isEmpty() || id != metadata.id || find(id)) {
        return false;
    }

    tools_.append(std::move(tool));
    return true;
}

std::shared_ptr<ITool> ToolRegistry::find(const QString& id) const
{
    for (const auto& tool : tools_) {
        if (tool->metadata().id == id) {
            return tool;
        }
    }
    return {};
}

QList<std::shared_ptr<ITool>> ToolRegistry::all() const
{
    return tools_;
}

QList<ToolMetadata> ToolRegistry::search(const QString& query) const
{
    const QStringList terms = query.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QList<ToolMetadata> matches;

    for (const auto& tool : tools_) {
        const ToolMetadata metadata = tool->metadata();
        const QString searchable = QStringList{
            metadata.id,
            metadata.name,
            metadata.category,
            metadata.description,
            metadata.keywords.join(QLatin1Char(' ')),
            metadata.aliases.join(QLatin1Char(' ')),
        }.join(QLatin1Char(' '));

        bool allTermsMatch = true;
        for (const QString& term : terms) {
            if (!searchable.contains(term, Qt::CaseInsensitive)) {
                allTermsMatch = false;
                break;
            }
        }
        if (allTermsMatch) {
            matches.append(metadata);
        }
    }

    return matches;
}

}  // namespace mtb
