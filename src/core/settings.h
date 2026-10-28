#pragma once

#include <QByteArray>
#include <QStringList>

class QSettings;

namespace mtb {

enum class Theme {
    System,
    Light,
    Dark,
};

class Settings {
public:
    explicit Settings(QSettings& storage);

    [[nodiscard]] Theme theme() const;
    void setTheme(Theme theme);

    [[nodiscard]] QStringList favoriteToolIds() const;
    [[nodiscard]] bool isFavorite(const QString& toolId) const;
    void setFavorite(const QString& toolId, bool favorite);
    void clearFavorites();

    [[nodiscard]] QByteArray windowGeometry() const;
    void setWindowGeometry(const QByteArray& geometry);
    [[nodiscard]] QByteArray windowState() const;
    void setWindowState(const QByteArray& state);

private:
    QSettings* storage_;
};

[[nodiscard]] QString themeName(Theme theme);
[[nodiscard]] Theme themeFromName(const QString& name);

}  // namespace mtb
