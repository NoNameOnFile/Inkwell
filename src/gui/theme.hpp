#pragma once

#include <QColor>
#include <QString>

namespace inkwell {

enum class ThemeId { Classic, Modern, Bubbly, Industrial };

struct DeskTheme {
    ThemeId id = ThemeId::Classic;
    bool dark = false;
    QString name;
    QColor desk;
    QColor panel;
    QColor field;
    QColor ink;
    QColor muted;
    QColor edge;
    QColor paper;
    QColor page_ink;
    QColor accent;
    QColor accent_ink;
    QColor accent_hover;
    QColor hover;
    QColor menu_hover;
    QColor field_tint;
    QColor tab_idle;
    QColor tab_idle_text;
    QColor rule;
    QColor disabled;
    int radius = 8;
    QString chrome_family;
};

struct ThemeChoice {
    ThemeId id = ThemeId::Classic;
    bool dark = false;
};

DeskTheme deskTheme(ThemeId id, bool dark);
DeskTheme activeTheme();
void setActiveTheme(const DeskTheme& theme);
ThemeChoice loadThemeChoice();
void saveThemeChoice(ThemeChoice choice);
QString themeIdName(ThemeId id);
ThemeId themeIdFromName(const QString& name);

}  // namespace inkwell
