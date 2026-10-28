#include "core/settings.h"

#include <QSettings>

namespace mtb {
namespace {
constexpr auto kThemeKey = "appearance/theme";
constexpr auto kFavoritesKey = "tools/favorites";
constexpr auto kWindowGeometryKey = "window/geometry";
constexpr auto kWindowStateKey = "window/state";
}  // namespace

Settings::Settings(QSettings& storage)
    : storage_(&storage)
{
}

Theme Settings::theme() const
{
    return themeFromName(storage_->value(kThemeKey, QStringLiteral("system")).toString());
}

void Settings::setTheme(Theme theme)
{
    storage_->setValue(kThemeKey, themeName(theme));
    storage_->sync();
}

QStringList Settings::favoriteToolIds() const
{
    return storage_->value(kFavoritesKey).toStringList();
}

bool Settings::isFavorite(const QString& toolId) const
{
    return favoriteToolIds().contains(toolId);
}

void Settings::setFavorite(const QString& toolId, bool favorite)
{
    const QString normalizedId = toolId.trimmed();
    if (normalizedId.isEmpty()) {
        return;
    }

    QStringList favorites = favoriteToolIds();
    favorites.removeAll(normalizedId);
    if (favorite) {
        favorites.append(normalizedId);
    }
    storage_->setValue(kFavoritesKey, favorites);
    storage_->sync();
}

void Settings::clearFavorites()
{
    storage_->remove(kFavoritesKey);
    storage_->sync();
}

QByteArray Settings::windowGeometry() const
{
    return storage_->value(kWindowGeometryKey).toByteArray();
}

void Settings::setWindowGeometry(const QByteArray& geometry)
{
    storage_->setValue(kWindowGeometryKey, geometry);
    storage_->sync();
}

QByteArray Settings::windowState() const
{
    return storage_->value(kWindowStateKey).toByteArray();
}

void Settings::setWindowState(const QByteArray& state)
{
    storage_->setValue(kWindowStateKey, state);
    storage_->sync();
}

QString themeName(Theme theme)
{
    switch (theme) {
    case Theme::Light:
        return QStringLiteral("light");
    case Theme::Dark:
        return QStringLiteral("dark");
    case Theme::System:
    default:
        return QStringLiteral("system");
    }
}

Theme themeFromName(const QString& name)
{
    if (name.compare(QStringLiteral("light"), Qt::CaseInsensitive) == 0) {
        return Theme::Light;
    }
    if (name.compare(QStringLiteral("dark"), Qt::CaseInsensitive) == 0) {
        return Theme::Dark;
    }
    return Theme::System;
}

}  // namespace mtb
