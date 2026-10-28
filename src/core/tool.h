#pragma once

#include <QString>
#include <QStringList>

namespace mtb {

struct ToolMetadata {
    QString id;
    QString name;
    QString category;
    QString description;
    QStringList keywords;
    QStringList aliases;
    QString iconName;
    bool requiresNetwork{false};
};

class ITool {
public:
    virtual ~ITool() = default;
    [[nodiscard]] virtual ToolMetadata metadata() const = 0;
};

}  // namespace mtb
