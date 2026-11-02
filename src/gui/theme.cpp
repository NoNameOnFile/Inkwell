#include "theme.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QSaveFile>

#include <initializer_list>

namespace inkwell {
namespace {

DeskTheme g_theme;
bool g_ready = false;

QString installed_face(std::initializer_list<const char*> names) {
    const QStringList families = QFontDatabase::families();
    for (const char* name : names) {
        const QString face = QString::fromLatin1(name);
        if (families.contains(face)) {
            return face;
        }
    }
    return {};
}

DeskTheme paint(ThemeId id, bool dark, const QString& name, QColor desk, QColor panel, QColor field, QColor ink, QColor muted, QColor edge,
                QColor paper, QColor page_ink, QColor accent, QColor accent_ink, QColor accent_hover, QColor hover, QColor menu_hover,
                QColor field_tint, QColor tab_idle, QColor tab_idle_text, QColor rule, QColor disabled, int radius, const QString& chrome) {
    DeskTheme theme;
    theme.id = id;
    theme.dark = dark;
    theme.name = name;
    theme.desk = desk;
    theme.panel = panel;
    theme.field = field;
    theme.ink = ink;
    theme.muted = muted;
    theme.edge = edge;
    theme.paper = paper;
    theme.page_ink = page_ink;
    theme.accent = accent;
    theme.accent_ink = accent_ink;
    theme.accent_hover = accent_hover;
    theme.hover = hover;
    theme.menu_hover = menu_hover;
    theme.field_tint = field_tint;
    theme.tab_idle = tab_idle;
    theme.tab_idle_text = tab_idle_text;
    theme.rule = rule;
    theme.disabled = disabled;
    theme.radius = radius;
    theme.chrome_family = chrome;
    return theme;
}

}  // namespace

DeskTheme deskTheme(ThemeId id, bool dark) {
    switch (id) {
        case ThemeId::Modern: {
            const QString sans = installed_face({"Inter", "Source Sans 3", "Liberation Sans", "Nimbus Sans"});
            return dark ? paint(id, true, "Modern", QColor(16, 18, 22), QColor(28, 32, 38), QColor(22, 25, 30), QColor(236, 239, 244),
                                QColor(148, 156, 168), QColor(58, 64, 74), QColor(22, 25, 30), QColor(236, 239, 244), QColor(126, 168, 240),
                                QColor(16, 18, 22), QColor(154, 190, 246), QColor(36, 42, 52), QColor(36, 42, 52), QColor(32, 40, 54),
                                QColor(72, 86, 112), QColor(214, 224, 240), QColor(48, 56, 68), QColor(40, 46, 56), 10, sans)
                        : paint(id, false, "Modern", QColor(236, 238, 242), QColor(255, 255, 255), QColor(255, 255, 255), QColor(23, 27, 34),
                                QColor(110, 118, 130), QColor(214, 219, 227), QColor(255, 255, 255), QColor(23, 27, 34), QColor(36, 86, 168),
                                QColor(255, 255, 255), QColor(24, 68, 140), QColor(244, 246, 250), QColor(226, 230, 238), QColor(232, 238, 248),
                                QColor(120, 136, 160), QColor(255, 255, 255), QColor(206, 212, 222), QColor(226, 230, 236), 10, sans);
        }
        case ThemeId::Bubbly: {
            const QString round = installed_face({"Nunito", "Quicksand", "Varela Round", "Liberation Sans"});
            return dark ? paint(id, true, "Bubbly", QColor(42, 24, 32), QColor(58, 34, 44), QColor(48, 28, 38), QColor(255, 236, 228),
                                QColor(196, 150, 160), QColor(92, 58, 70), QColor(48, 30, 38), QColor(255, 236, 228), QColor(240, 120, 140),
                                QColor(42, 24, 32), QColor(246, 156, 170), QColor(72, 40, 52), QColor(72, 40, 52), QColor(78, 42, 54),
                                QColor(120, 70, 86), QColor(255, 220, 214), QColor(86, 52, 64), QColor(70, 40, 50), 18, round)
                        : paint(id, false, "Bubbly", QColor(255, 236, 228), QColor(255, 248, 244), QColor(255, 255, 255), QColor(62, 36, 48),
                                QColor(166, 112, 124), QColor(244, 196, 186), QColor(255, 250, 246), QColor(62, 36, 48), QColor(214, 72, 96),
                                QColor(255, 255, 255), QColor(184, 52, 76), QColor(255, 228, 220), QColor(255, 214, 204), QColor(255, 228, 220),
                                QColor(214, 120, 132), QColor(255, 248, 244), QColor(236, 176, 168), QColor(248, 210, 202), 18, round);
        }
        case ThemeId::Industrial: {
            const QString condensed = installed_face({"Barlow Condensed", "Oswald", "Liberation Sans Narrow", "Liberation Sans"});
            return dark ? paint(id, true, "Industrial", QColor(22, 24, 26), QColor(36, 38, 40), QColor(28, 30, 32), QColor(230, 230, 226),
                                QColor(140, 142, 138), QColor(72, 74, 76), QColor(32, 34, 36), QColor(230, 230, 226), QColor(232, 146, 48),
                                QColor(22, 24, 26), QColor(240, 170, 80), QColor(44, 46, 48), QColor(44, 46, 48), QColor(48, 44, 36),
                                QColor(96, 98, 92), QColor(232, 220, 196), QColor(58, 60, 62), QColor(48, 50, 52), 2, condensed)
                        : paint(id, false, "Industrial", QColor(214, 214, 210), QColor(236, 236, 232), QColor(245, 245, 242), QColor(28, 30, 32),
                                QColor(96, 100, 104), QColor(168, 168, 162), QColor(244, 242, 234), QColor(28, 30, 32), QColor(196, 98, 16),
                                QColor(255, 248, 240), QColor(160, 78, 10), QColor(228, 228, 224), QColor(196, 196, 190), QColor(236, 228, 210),
                                QColor(120, 122, 118), QColor(255, 248, 236), QColor(176, 176, 170), QColor(204, 204, 198), 2, condensed);
        }
        case ThemeId::Classic:
            break;
    }
    return dark ? paint(id, true, "Classic", QColor(18, 16, 14), QColor(32, 29, 25), QColor(36, 31, 27), QColor(243, 239, 230),
                        QColor(200, 187, 168), QColor(74, 67, 58), QColor(28, 25, 22), QColor(243, 239, 230), QColor(243, 239, 230),
                        QColor(28, 25, 21), QColor(231, 224, 212), QColor(42, 38, 34), QColor(42, 38, 34), QColor(46, 40, 32), QColor(110, 88, 64),
                        QColor(255, 248, 236), QColor(54, 48, 40), QColor(58, 52, 44), 8, {})
                : paint(id, false, "Classic", QColor(232, 226, 214), QColor(255, 253, 248), QColor(255, 255, 255), QColor(28, 25, 21),
                        QColor(111, 103, 94), QColor(224, 217, 204), QColor(255, 253, 248), QColor(28, 25, 21), QColor(28, 25, 21),
                        QColor(255, 253, 248), QColor(58, 52, 44), QColor(246, 241, 231), QColor(221, 212, 196), QColor(243, 236, 220),
                        QColor(154, 115, 68), QColor(255, 248, 236), QColor(228, 221, 208), QColor(228, 221, 208), 8, {});
}

DeskTheme activeTheme() {
    if (!g_ready) {
        g_theme = deskTheme(ThemeId::Classic, false);
        g_ready = true;
    }
    return g_theme;
}

void setActiveTheme(const DeskTheme& theme) {
    g_theme = theme;
    g_ready = true;
}

QString themeIdName(ThemeId id) {
    switch (id) {
        case ThemeId::Modern:
            return "modern";
        case ThemeId::Bubbly:
            return "bubbly";
        case ThemeId::Industrial:
            return "industrial";
        case ThemeId::Classic:
            break;
    }
    return "classic";
}

ThemeId themeIdFromName(const QString& name) {
    const QString key = name.trimmed().toLower();
    if (key == "modern") {
        return ThemeId::Modern;
    }
    if (key == "bubbly") {
        return ThemeId::Bubbly;
    }
    if (key == "industrial") {
        return ThemeId::Industrial;
    }
    return ThemeId::Classic;
}

ThemeChoice loadThemeChoice() {
    ThemeChoice choice;
    QFile file(QDir::homePath() + "/.config/inkwell/theme");
    if (!file.open(QIODevice::ReadOnly)) {
        return choice;
    }
    const QStringList lines = QString::fromUtf8(file.readAll()).split('\n', Qt::SkipEmptyParts);
    if (lines.isEmpty()) {
        return choice;
    }
    const QString first = lines.first().trimmed().toLower();
    if (first == "dark" || first == "light") {
        choice.dark = first == "dark";
        return choice;
    }
    QString name = first;
    if (name.endsWith("-dark")) {
        choice.dark = true;
        name.chop(5);
    } else if (name.endsWith("-light")) {
        name.chop(6);
    }
    choice.id = themeIdFromName(name);
    if (lines.size() > 1) {
        choice.dark = lines.at(1).trimmed().toLower() == "dark";
    }
    return choice;
}

void saveThemeChoice(ThemeChoice choice) {
    const QString path = QDir::homePath() + "/.config/inkwell/theme";
    QDir().mkpath(QFileInfo(path).path());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return;
    }
    const QString body = themeIdName(choice.id) + "\n" + (choice.dark ? "dark\n" : "light\n");
    file.write(body.toUtf8());
    file.commit();
}

}  // namespace inkwell
