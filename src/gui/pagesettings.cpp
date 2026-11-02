#include "pagesettings.hpp"
#include "theme.hpp"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QListView>
#include <QFontMetrics>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalBlocker>

#include <algorithm>
#include <QSpinBox>
#include <QTextBlock>
#include <QTextEdit>
#include <QVBoxLayout>

namespace inkwell {
namespace {

int read_int(const QString& text, int fallback) {
    bool ok = false;
    const int value = text.trimmed().toInt(&ok);
    return ok ? value : fallback;
}

QString hex_color(const QColor& color) {
    return color.name(QColor::HexRgb);
}

QColor read_color(const QString& text, const QColor& fallback) {
    const QColor color(text.trimmed());
    return color.isValid() ? color : fallback;
}

bool writing_face(const QString& family) {
    const QString lower = family.toLower();
    if (family.startsWith('.') || lower.contains("emoji") || lower.contains("icon") || lower.contains("awesome") ||
        lower.contains("symbol") || lower.contains("dingbat") || lower.contains("wingding") || lower.contains("fallback") ||
        lower.contains("lastresort") || lower.contains("math") || lower.contains("braille") || lower.contains("music") ||
        lower.contains("cjk") || lower.contains("condensed") || lower.contains("narrow") || lower.contains("extrabold") ||
        lower.contains("extralight") || lower.contains("semibold") || lower.contains("black") || lower.contains("bold") ||
        lower.contains("light") || lower.contains("thin") || lower.contains("medium") || lower.contains("demibold") ||
        lower.contains("heavy") || lower.contains("italic") || lower.contains("display") || lower.contains("caption") ||
        lower.contains("mono") || lower.contains(" wide") || lower.contains(" semi")) {
        return false;
    }
    const int bracket = family.indexOf('[');
    const QString visible = (bracket > 0 ? family.left(bracket) : family).simplified();
    int letters = 0;
    int digits = 0;
    for (const QChar ch : visible) {
        const ushort code = ch.unicode();
        if ((code >= 'A' && code <= 'Z') || (code >= 'a' && code <= 'z')) {
            ++letters;
        } else if (code >= '0' && code <= '9') {
            ++digits;
        }
    }
    if (digits > 0 && letters <= 4) {
        return false;
    }
    if (!QFontDatabase::isScalable(family) || QFontDatabase::isFixedPitch(family)) {
        return false;
    }
    const QList<QFontDatabase::WritingSystem> systems = QFontDatabase::writingSystems(family);
    if (systems.isEmpty() || (systems.size() == 1 && systems.first() == QFontDatabase::Symbol)) {
        return false;
    }
    return systems.contains(QFontDatabase::Latin);
}

int face_rank(const QString& family) {
    const QString lower = family.toLower();
    if (lower.contains("serif") || lower.contains("roman") || lower.contains("garamond") || lower.contains("baskerville") ||
        lower.contains("caslon") || lower.contains("bodoni") || lower.contains("didot") || lower.contains("georgia") ||
        lower.contains("times") || lower.contains("palatino") || lower.contains("cambria") || lower.contains("constantia") ||
        lower.contains("literata") || lower.contains("merriweather") || lower.contains("lora") || lower.contains("crimson") ||
        lower.contains("charter") || lower.contains("spectral") || lower.contains("newsreader") || lower.contains("slab")) {
        return 0;
    }
    if (lower.contains("sans") || lower.contains("gothic") || lower.contains("grotesk") || lower.contains("inter") ||
        lower.contains("helvetica") || lower.contains("arial") || lower.contains("verdana") || lower.contains("calibri") ||
        lower.contains("carlito")) {
        return 1;
    }
    return 2;
}

QStringList writing_faces() {
    QStringList faces;
    for (const QString& family : QFontDatabase::families()) {
        if (writing_face(family)) {
            faces.push_back(family);
        }
    }
    std::sort(faces.begin(), faces.end(), [](const QString& left, const QString& right) {
        const int rank = face_rank(left) - face_rank(right);
        if (rank != 0) {
            return rank < 0;
        }
        return left.localeAwareCompare(right) < 0;
    });
    return faces;
}

void fill_typefaces(QComboBox* box, const QString& selected) {
    box->clear();
    box->addItem("Desk serif", QString());
    const QStringList preferred = {"Literata", "Source Serif 4", "Liberation Serif", "Noto Serif", "DejaVu Serif",
                                   "Times New Roman", "Georgia"};
    QStringList added;
    auto add_face = [&](const QString& family) {
        const int bracket = family.indexOf('[');
        const QString plain = (bracket > 0 ? family.left(bracket) : family).simplified();
        if (plain.isEmpty() || added.contains(plain) || !QFontDatabase::families().contains(family)) {
            return;
        }
        box->addItem(plain, family);
        added.push_back(plain);
    };
    for (const QString& family : preferred) {
        add_face(family);
    }
    for (const QString& family : writing_faces()) {
        add_face(family);
    }
    if (!selected.trimmed().isEmpty() && box->findData(selected.trimmed()) < 0) {
        box->addItem(selected.trimmed(), selected.trimmed());
    }
    const int index = selected.trimmed().isEmpty() ? 0 : box->findData(selected.trimmed());
    box->setCurrentIndex(index < 0 ? 0 : index);
}

void readable_popup(QComboBox* box, const QColor& field, const QColor& ink, const QColor& selected, const QColor& selected_ink) {
    auto* view = new QListView(box);
    view->setFrameShape(QFrame::NoFrame);
    QPalette colors = view->palette();
    colors.setColor(QPalette::Base, field);
    colors.setColor(QPalette::Text, ink);
    colors.setColor(QPalette::Highlight, selected);
    colors.setColor(QPalette::HighlightedText, selected_ink);
    view->setPalette(colors);
    box->setView(view);
    box->setMaxVisibleItems(12);
}

QString installed_family(const QStringList& faces) {
    const QStringList installed = QFontDatabase::families();
    for (const QString& face : faces) {
        if (installed.contains(face)) {
            return face;
        }
    }
    QFont fallback;
    fallback.setStyleHint(QFont::Serif);
    return fallback.family();
}

}  // namespace

QColor themePaper(bool dark) {
    return deskTheme(activeTheme().id, dark).paper;
}

QColor themeInk(bool dark) {
    return deskTheme(activeTheme().id, dark).page_ink;
}

QStringList PageSettings::families() const {
    QStringList faces;
    const QString chosen = font_family.trimmed();
    if (!chosen.isEmpty()) {
        faces.push_back(chosen);
    }
    const QStringList stack = {"Literata", "Source Serif 4", "Liberation Serif", "Noto Serif", "DejaVu Serif",
                               "Times New Roman", "Georgia"};
    for (const QString& face : stack) {
        if (!faces.contains(face)) {
            faces.push_back(face);
        }
    }
    return faces;
}

QString PageSettings::resolvedFamily() const {
    return installed_family(families());
}

QColor PageSettings::paperFor(bool dark) const {
    if (custom_colors && paper.isValid()) {
        return paper;
    }
    return themePaper(dark);
}

QColor PageSettings::inkFor(bool dark) const {
    if (custom_colors && ink.isValid()) {
        return ink;
    }
    return themeInk(dark);
}

void PageSettings::clamp() {
    font_size = qBound(12, font_size, 32);
    line_percent = qBound(120, line_percent, 240);
    indent_ems = qBound(0, indent_ems, 8);
    column_width = qBound(420, column_width, 1100);
    if (!paper.isValid()) {
        paper = themePaper(false);
    }
    if (!ink.isValid()) {
        ink = themeInk(false);
    }
}

QString PageSettings::configPath() {
    return QDir::homePath() + "/.config/inkwell/page";
}

PageSettings PageSettings::load() {
    return loadFile(configPath());
}

PageSettings PageSettings::loadFile(const QString& path) {
    PageSettings settings;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return settings;
    }
    const QStringList lines = QString::fromUtf8(file.readAll()).split('\n');
    for (const QString& line : lines) {
        const int split = line.indexOf('=');
        if (split <= 0) {
            continue;
        }
        const QString key = line.left(split).trimmed();
        const QString value = line.mid(split + 1).trimmed();
        if (key == "font") {
            settings.font_family = value;
        } else if (key == "size") {
            settings.font_size = read_int(value, settings.font_size);
        } else if (key == "line") {
            settings.line_percent = read_int(value, settings.line_percent);
        } else if (key == "indent") {
            settings.indent_ems = read_int(value, settings.indent_ems);
        } else if (key == "width") {
            settings.column_width = read_int(value, settings.column_width);
        } else if (key == "align") {
            settings.justify = value != "left";
        } else if (key == "colors") {
            settings.custom_colors = value == "custom";
        } else if (key == "paper") {
            settings.paper = read_color(value, settings.paper);
        } else if (key == "ink") {
            settings.ink = read_color(value, settings.ink);
        }
    }
    settings.clamp();
    return settings;
}

bool PageSettings::save(QString* error) const {
    return saveFile(configPath(), error);
}

bool PageSettings::saveFile(const QString& path, QString* error) const {
    PageSettings stored = *this;
    stored.clamp();
    QDir().mkpath(QFileInfo(path).path());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error != nullptr) {
            *error = "Could not save the page settings";
        }
        return false;
    }
    QString body;
    body += "font=" + stored.font_family + "\n";
    body += "size=" + QString::number(stored.font_size) + "\n";
    body += "line=" + QString::number(stored.line_percent) + "\n";
    body += "indent=" + QString::number(stored.indent_ems) + "\n";
    body += "width=" + QString::number(stored.column_width) + "\n";
    body += QString("align=") + (stored.justify ? "justify" : "left") + "\n";
    body += QString("colors=") + (stored.custom_colors ? "custom" : "theme") + "\n";
    body += "paper=" + hex_color(stored.paper) + "\n";
    body += "ink=" + hex_color(stored.ink) + "\n";
    if (file.write(body.toUtf8()) < 0 || !file.commit()) {
        if (error != nullptr) {
            *error = "Could not save the page settings";
        }
        return false;
    }
    return true;
}

bool editPageSettings(QWidget* parent, PageSettings& settings, bool dark,
                      const std::function<void(const PageSettings&, bool restyle)>& apply) {
    PageSettings original = settings;
    original.clamp();
    QDialog dialog(parent);
    dialog.setObjectName("pageSettings");
    dialog.setWindowTitle("Page");
    dialog.setMinimumWidth(460);
    const DeskTheme theme = activeTheme();
    const QColor desk = parent != nullptr ? parent->palette().color(QPalette::Window) : theme.desk;
    const QColor ink = parent != nullptr ? parent->palette().color(QPalette::WindowText) : theme.ink;
    const QColor field = theme.field;
    const QColor edge = theme.edge;
    QPalette dialog_palette = dialog.palette();
    dialog_palette.setColor(QPalette::Window, desk);
    dialog_palette.setColor(QPalette::WindowText, ink);
    dialog_palette.setColor(QPalette::Base, field);
    dialog_palette.setColor(QPalette::Text, ink);
    dialog_palette.setColor(QPalette::Button, field);
    dialog_palette.setColor(QPalette::ButtonText, ink);
    dialog.setPalette(dialog_palette);
    dialog.setAutoFillBackground(true);
    dialog.setStyleSheet(QStringLiteral("QDialog { background: %1; color: %2; }"
                                        "QLabel { color: %2; }"
                                        "QLabel#hint { color: %3; }"
                                        "QSpinBox, QComboBox, QTextEdit {"
                                        " background: %4; color: %2; border: 1px solid %5; border-radius: %8px; padding: 4px 8px; }"
                                        "QComboBox QAbstractItemView {"
                                        " background: %4; color: %2; selection-background-color: %6; selection-color: %7; outline: 0; }"
                                        "QSpinBox::up-button, QSpinBox::down-button { width: 18px; }")
                             .arg(desk.name(), ink.name(), theme.muted.name(), field.name(), edge.name(), theme.accent.name(),
                                  theme.accent_ink.name())
                             .arg(qMin(theme.radius, 12)));

    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(20, 18, 20, 16);
    layout->setSpacing(12);
    auto* intro = new QLabel("Type, measure, and the colors of the writing page.");
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto* form = new QFormLayout();
    form->setSpacing(10);
    form->setLabelAlignment(Qt::AlignLeft);
    auto* font_box = new QComboBox(&dialog);
    font_box->setObjectName("pageFont");
    const QColor selected = theme.accent;
    const QColor selected_ink = theme.accent_ink;
    readable_popup(font_box, field, ink, selected, selected_ink);
    fill_typefaces(font_box, original.font_family);
    auto* size_box = new QSpinBox(&dialog);
    size_box->setObjectName("pageSize");
    size_box->setRange(12, 32);
    size_box->setSuffix(" pt");
    size_box->setValue(original.font_size);
    auto* line_box = new QSpinBox(&dialog);
    line_box->setObjectName("pageLine");
    line_box->setRange(120, 240);
    line_box->setSuffix("%");
    line_box->setValue(original.line_percent);
    auto* indent_box = new QSpinBox(&dialog);
    indent_box->setObjectName("pageIndent");
    indent_box->setRange(0, 8);
    indent_box->setSuffix(" letters");
    indent_box->setValue(original.indent_ems);
    auto* align_box = new QComboBox(&dialog);
    align_box->setObjectName("pageAlign");
    align_box->addItem("Justified");
    align_box->addItem("Ragged right");
    align_box->setCurrentIndex(original.justify ? 0 : 1);
    readable_popup(align_box, field, ink, selected, selected_ink);
    auto* width_box = new QSpinBox(&dialog);
    width_box->setObjectName("pageWidth");
    width_box->setRange(420, 1100);
    width_box->setSingleStep(20);
    width_box->setSuffix(" px");
    width_box->setValue(original.column_width);
    form->addRow("Typeface", font_box);
    form->addRow("Size", size_box);
    form->addRow("Line spacing", line_box);
    form->addRow("First-line indent", indent_box);
    form->addRow("Alignment", align_box);
    form->addRow("Column width", width_box);
    layout->addLayout(form);

    auto* width_hint = new QLabel("The column stays centered. A narrower window uses a shorter measure.");
    width_hint->setObjectName("hint");
    width_hint->setWordWrap(true);
    layout->addWidget(width_hint);

    auto* theme_colors = new QCheckBox("Follow the window theme", &dialog);
    theme_colors->setObjectName("pageThemeColors");
    theme_colors->setChecked(!original.custom_colors);
    layout->addWidget(theme_colors);

    QColor paper = original.paper;
    QColor text_ink = original.ink;
    auto* paper_button = new QPushButton(&dialog);
    paper_button->setObjectName("pagePaper");
    auto* ink_button = new QPushButton(&dialog);
    ink_button->setObjectName("pageInk");
    for (QPushButton* button : {paper_button, ink_button}) {
        button->setFixedHeight(28);
        button->setCursor(Qt::PointingHandCursor);
    }
    auto paint_swatch = [&](QPushButton* button, const QColor& color) {
        const QColor label = color.lightness() > 140 ? QColor(28, 25, 21) : QColor(243, 239, 230);
        button->setText(color.name(QColor::HexRgb));
        button->setStyleSheet(QStringLiteral("QPushButton { background: %1; color: %2; border: 1px solid %3; border-radius: 6px; }")
                                   .arg(color.name(QColor::HexRgb), label.name(), edge.name()));
    };
    auto* color_row = new QHBoxLayout();
    color_row->addWidget(new QLabel("Paper"));
    color_row->addWidget(paper_button, 1);
    color_row->addSpacing(12);
    color_row->addWidget(new QLabel("Ink"));
    color_row->addWidget(ink_button, 1);
    layout->addLayout(color_row);

    auto* sample = new QTextEdit(&dialog);
    sample->setObjectName("pageSample");
    sample->setReadOnly(true);
    sample->setFrameStyle(QFrame::NoFrame);
    sample->setFixedHeight(118);
    sample->setPlainText("The tent held against the wind. She waited at the door while the fire took.");
    layout->addWidget(sample);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    auto* reset = buttons->addButton("Reset", QDialogButtonBox::ResetRole);
    layout->addWidget(buttons);

    auto drafted = [&]() {
        PageSettings next = original;
        next.font_family = font_box->currentData().toString();
        next.font_size = size_box->value();
        next.line_percent = line_box->value();
        next.indent_ems = indent_box->value();
        next.justify = align_box->currentIndex() == 0;
        next.column_width = width_box->value();
        next.custom_colors = !theme_colors->isChecked();
        next.paper = paper;
        next.ink = text_ink;
        next.clamp();
        return next;
    };
    auto show_sample = [&]() {
        const PageSettings next = drafted();
        QFont face(next.resolvedFamily());
        face.setPointSize(next.font_size);
        sample->setFont(face);
        const QColor page = next.paperFor(dark);
        const QColor type = next.inkFor(dark);
        QPalette colors = sample->palette();
        colors.setColor(QPalette::Base, page);
        colors.setColor(QPalette::Text, type);
        sample->setPalette(colors);
        QTextBlockFormat format;
        format.setAlignment(next.justify ? Qt::AlignJustify : Qt::AlignLeft);
        format.setTextIndent(QFontMetricsF(face).horizontalAdvance(QStringLiteral("M")) * next.indent_ems);
        format.setLineHeight(next.line_percent, QTextBlockFormat::ProportionalHeight);
        QTextCursor cursor(sample->document());
        cursor.select(QTextCursor::Document);
        cursor.setBlockFormat(format);
        QTextCharFormat chars;
        chars.setFont(face);
        chars.setForeground(type);
        cursor.setCharFormat(chars);
    };
    auto push = [&](bool restyle) {
        paint_swatch(paper_button, theme_colors->isChecked() ? themePaper(dark) : paper);
        paint_swatch(ink_button, theme_colors->isChecked() ? themeInk(dark) : text_ink);
        paper_button->setEnabled(!theme_colors->isChecked());
        ink_button->setEnabled(!theme_colors->isChecked());
        show_sample();
        if (apply) {
            apply(drafted(), restyle);
        }
    };

    QObject::connect(font_box, &QComboBox::currentIndexChanged, &dialog, [&](int) { push(false); });
    QObject::connect(size_box, &QSpinBox::valueChanged, &dialog, [&](int) { push(false); });
    QObject::connect(line_box, &QSpinBox::valueChanged, &dialog, [&](int) { push(false); });
    QObject::connect(indent_box, &QSpinBox::valueChanged, &dialog, [&](int) { push(false); });
    QObject::connect(align_box, &QComboBox::currentIndexChanged, &dialog, [&](int) { push(false); });
    QObject::connect(width_box, &QSpinBox::valueChanged, &dialog, [&](int) { push(false); });
    QObject::connect(theme_colors, &QCheckBox::toggled, &dialog, [&](bool) { push(false); });
    QObject::connect(paper_button, &QPushButton::clicked, &dialog, [&] {
        const QColor chosen = QColorDialog::getColor(paper, &dialog, "Paper");
        if (!chosen.isValid()) {
            return;
        }
        paper = chosen;
        push(false);
    });
    QObject::connect(ink_button, &QPushButton::clicked, &dialog, [&] {
        const QColor chosen = QColorDialog::getColor(text_ink, &dialog, "Ink");
        if (!chosen.isValid()) {
            return;
        }
        text_ink = chosen;
        push(false);
    });
    QObject::connect(reset, &QPushButton::clicked, &dialog, [&] {
        const PageSettings fresh;
        {
            const QSignalBlocker block_font(font_box);
            const QSignalBlocker block_size(size_box);
            const QSignalBlocker block_line(line_box);
            const QSignalBlocker block_indent(indent_box);
            const QSignalBlocker block_align(align_box);
            const QSignalBlocker block_width(width_box);
            const QSignalBlocker block_theme(theme_colors);
            font_box->setCurrentIndex(0);
            size_box->setValue(fresh.font_size);
            line_box->setValue(fresh.line_percent);
            indent_box->setValue(fresh.indent_ems);
            align_box->setCurrentIndex(0);
            width_box->setValue(fresh.column_width);
            theme_colors->setChecked(true);
        }
        paper = fresh.paper;
        text_ink = fresh.ink;
        push(false);
    });
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        PageSettings next = drafted();
        QString error;
        if (!next.save(&error)) {
            QMessageBox box(QMessageBox::Warning, "Page", error, QMessageBox::Ok, &dialog);
            box.setOption(QMessageBox::Option::DontUseNativeDialog);
            box.exec();
            return;
        }
        settings = next;
        if (apply) {
            apply(next, true);
        }
        dialog.accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(&dialog, &QDialog::rejected, &dialog, [&] {
        if (apply) {
            apply(original, false);
        }
    });

    push(false);
    return dialog.exec() == QDialog::Accepted;
}

}  // namespace inkwell
