#pragma once

#include <QColor>
#include <QString>
#include <QStringList>

#include <functional>

class QWidget;

namespace inkwell {

struct PageSettings {
    QString font_family;
    int font_size = 17;
    int line_percent = 170;
    int indent_ems = 3;
    int column_width = 680;
    bool justify = true;
    bool custom_colors = false;
    QColor paper = QColor(255, 253, 248);
    QColor ink = QColor(28, 25, 21);

    QStringList families() const;
    QString resolvedFamily() const;
    QColor paperFor(bool dark) const;
    QColor inkFor(bool dark) const;
    void clamp();
    bool save(QString* error = nullptr) const;
    bool saveFile(const QString& path, QString* error = nullptr) const;
    static PageSettings load();
    static PageSettings loadFile(const QString& path);
    static QString configPath();
};

QColor themePaper(bool dark);
QColor themeInk(bool dark);

bool editPageSettings(QWidget* parent, PageSettings& settings, bool dark,
                      const std::function<void(const PageSettings&, bool restyle)>& apply);

}  // namespace inkwell
