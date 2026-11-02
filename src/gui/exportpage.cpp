#include "export.hpp"
#include "theme.hpp"

#include <QAbstractItemView>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace inkwell {
namespace {

class InkCheck : public QCheckBox {
public:
    explicit InkCheck(const QString& label, QWidget* parent) : QCheckBox(label, parent) {}

    QSize sizeHint() const override { return QSize(fontMetrics().horizontalAdvance(text()) + 32, 28); }

protected:
    void paintEvent(QPaintEvent* event) override {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QColor ink = palette().color(QPalette::WindowText);
        const QColor fill = palette().color(QPalette::Base);
        const QColor border = palette().color(QPalette::Mid);
        const QColor mark = palette().color(QPalette::HighlightedText);
        const int side = 16;
        const int top = qMax(0, (height() - side) / 2);
        painter.setPen(QPen(isChecked() ? ink : border, 1));
        painter.setBrush(isChecked() ? ink : fill);
        painter.drawRoundedRect(QRectF(1, top, side, side), 3, 3);
        if (isChecked()) {
            painter.setPen(QPen(mark, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawLine(QPointF(4, top + 8.5), QPointF(7, top + 11.5));
            painter.drawLine(QPointF(7, top + 11.5), QPointF(13, top + 5));
        }
        painter.setPen(ink);
        painter.setFont(font());
        painter.drawText(QRect(side + 8, 0, width() - side - 8, height()), Qt::AlignVCenter | Qt::AlignLeft, text());
    }
};

class PageShape : public QWidget {
public:
    explicit PageShape(QWidget* parent) : QWidget(parent) { setFixedSize(210, 280); }

    void showTrim(double width, double height, double margin, bool print, const QString& caption, bool dark) {
        width_ = width;
        height_ = height;
        margin_ = margin;
        print_ = print;
        caption_ = caption;
        dark_ = dark;
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const DeskTheme theme = activeTheme();
        const QColor ink = theme.ink;
        const QColor muted = theme.muted;
        const QRectF bounds(12, 8, width() - 24, height() - 44);
        const double page_ratio = height_ / std::max(0.1, width_);
        QRectF page(0, 0, bounds.width(), bounds.width() * page_ratio);
        if (page.height() > bounds.height()) {
            page.setHeight(bounds.height());
            page.setWidth(bounds.height() / page_ratio);
        }
        page.moveCenter(bounds.center());
        painter.setPen(QPen(ink, 1.2));
        painter.setBrush(QColor(255, 253, 248));
        painter.drawRoundedRect(page, 2, 2);
        if (print_ && width_ > 0) {
            const double scale = page.width() / width_;
            const double inset = std::clamp(margin_, 0.0, width_ / 2.0) * scale;
            QRectF live = page.adjusted(inset, inset, -inset, -inset);
            if (live.width() > 8 && live.height() > 8) {
                painter.setPen(QPen(theme.edge, 1, Qt::DashLine));
                painter.setBrush(Qt::NoBrush);
                painter.drawRect(live);
            }
        } else {
            painter.setPen(muted);
            QFont face = font();
            face.setPointSize(11);
            painter.setFont(face);
            painter.drawText(page, Qt::AlignCenter, print_ ? QString() : QStringLiteral("Reflows"));
        }
        painter.setPen(ink);
        QFont face = font();
        face.setPointSize(11);
        painter.setFont(face);
        painter.drawText(QRect(0, height() - 30, width(), 24), Qt::AlignHCenter | Qt::AlignVCenter, caption_);
    }

private:
    double width_ = 6;
    double height_ = 9;
    double margin_ = 0.85;
    bool print_ = true;
    bool dark_ = false;
    QString caption_ = "6 × 9 in";
};

struct TrimChoice {
    const char* id;
    const char* label;
    double width;
    double height;
};

const TrimChoice kTrims[] = {
    {"5x8", "5 × 8 in   ·   Pocket", 5.0, 8.0},
    {"5.5x8.5", "5.5 × 8.5 in   ·   Digest", 5.5, 8.5},
    {"6x9", "6 × 9 in   ·   Trade", 6.0, 9.0},
    {"6.14x9.21", "6.14 × 9.21 in   ·   Royal", 6.14, 9.21},
    {"a5", "A5   ·   5.83 × 8.27 in", 5.83, 8.27},
    {"letter", "US Letter   ·   8.5 × 11 in", 8.5, 11.0},
    {"custom", "Custom", 6.0, 9.0},
};

QString export_config_path() { return QDir::homePath() + "/.config/inkwell/export"; }

QString inches(double value) {
    QString text = QString::number(value, 'f', 2);
    while (text.contains('.') && (text.endsWith('0') || text.endsWith('.'))) {
        if (text.endsWith('.')) {
            text.chop(1);
            break;
        }
        text.chop(1);
    }
    return text;
}

QPushButton* format_button(const QString& label, const QString& name, ExportFormat format, QWidget* parent) {
    auto* button = new QPushButton(label, parent);
    button->setObjectName(name);
    button->setCheckable(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setMinimumHeight(40);
    button->setProperty("format", static_cast<int>(format));
    return button;
}

}  // namespace

struct ExportSheet::Fields {
    QPushButton* back = nullptr;
    QButtonGroup* formats = nullptr;
    QLabel* format_hint = nullptr;
    QComboBox* trim = nullptr;
    QWidget* custom_row = nullptr;
    QDoubleSpinBox* width = nullptr;
    QDoubleSpinBox* height = nullptr;
    QWidget* size_block = nullptr;
    QDoubleSpinBox* margin = nullptr;
    QWidget* type_block = nullptr;
    QSpinBox* body = nullptr;
    QSpinBox* leading = nullptr;
    InkCheck* cover = nullptr;
    InkCheck* title = nullptr;
    InkCheck* copyright = nullptr;
    InkCheck* contents = nullptr;
    QWidget* chapter_host = nullptr;
    QVBoxLayout* chapter_rows = nullptr;
    QVector<InkCheck*> chapters;
    QLabel* chapter_empty = nullptr;
    QLabel* summary = nullptr;
    PageShape* shape = nullptr;
    QPushButton* go = nullptr;
    bool loading = false;
    bool has_cover = false;
    bool dark = false;
};

ExportSheet::ExportSheet(QWidget* parent) : QWidget(parent), fields_(new Fields) {
    setObjectName("exportPage");
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(28, 16, 22, 16);
    outer->setSpacing(12);

    auto* bar = new QHBoxLayout;
    fields_->back = new QPushButton("Back to the chapter", this);
    fields_->back->setObjectName("exportBack");
    fields_->back->setCursor(Qt::PointingHandCursor);
    auto* heading = new QLabel("Export", this);
    QFont heading_font = heading->font();
    heading_font.setPointSize(18);
    heading_font.setBold(true);
    heading->setFont(heading_font);
    bar->addWidget(fields_->back);
    bar->addStretch();
    bar->addWidget(heading);
    bar->addStretch();
    bar->addSpacing(fields_->back->sizeHint().width());
    outer->addLayout(bar);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* host = new QWidget(scroll);
    host->setObjectName("exportHost");
    auto* page = new QHBoxLayout(host);
    page->setContentsMargins(0, 4, 8, 8);
    page->setSpacing(28);

    auto* form = new QVBoxLayout;
    form->setSpacing(8);
    auto* format_label = new QLabel("Format", host);
    QFont section = format_label->font();
    section.setBold(true);
    section.setPointSize(13);
    format_label->setFont(section);
    form->addWidget(format_label);

    auto* format_row = new QHBoxLayout;
    format_row->setSpacing(8);
    fields_->formats = new QButtonGroup(this);
    fields_->formats->setExclusive(true);
    QPushButton* pdf = format_button("Print PDF", "exportPdf", ExportFormat::Pdf, host);
    QPushButton* epub = format_button("EPUB", "exportEpub", ExportFormat::Epub, host);
    QPushButton* word = format_button("Word", "exportDocx", ExportFormat::Docx, host);
    QPushButton* manuscript = format_button("Manuscript", "exportText", ExportFormat::Text, host);
    for (QPushButton* button : {pdf, epub, word, manuscript}) {
        fields_->formats->addButton(button);
        format_row->addWidget(button, 1);
    }
    pdf->setChecked(true);
    form->addLayout(format_row);
    fields_->format_hint = new QLabel(host);
    fields_->format_hint->setObjectName("hint");
    fields_->format_hint->setWordWrap(true);
    form->addWidget(fields_->format_hint);

    auto* size_label = new QLabel("Book size", host);
    size_label->setFont(section);
    form->addSpacing(8);
    form->addWidget(size_label);
    fields_->size_block = new QWidget(host);
    auto* size_layout = new QVBoxLayout(fields_->size_block);
    size_layout->setContentsMargins(0, 0, 0, 0);
    size_layout->setSpacing(8);
    fields_->trim = new QComboBox(fields_->size_block);
    fields_->trim->setObjectName("exportTrim");
    for (const TrimChoice& choice : kTrims) {
        fields_->trim->addItem(QString::fromUtf8(choice.label), QString::fromUtf8(choice.id));
    }
    fields_->trim->setCurrentIndex(2);
    size_layout->addWidget(fields_->trim);
    fields_->custom_row = new QWidget(fields_->size_block);
    auto* custom = new QHBoxLayout(fields_->custom_row);
    custom->setContentsMargins(0, 0, 0, 0);
    fields_->width = new QDoubleSpinBox(fields_->custom_row);
    fields_->height = new QDoubleSpinBox(fields_->custom_row);
    for (QDoubleSpinBox* box : {fields_->width, fields_->height}) {
        box->setRange(3.0, 12.0);
        box->setDecimals(2);
        box->setSingleStep(0.05);
        box->setSuffix(" in");
    }
    fields_->width->setObjectName("exportWidth");
    fields_->height->setObjectName("exportHeight");
    fields_->width->setValue(6.0);
    fields_->height->setValue(9.0);
    custom->addWidget(new QLabel("Width", fields_->custom_row));
    custom->addWidget(fields_->width, 1);
    custom->addSpacing(12);
    custom->addWidget(new QLabel("Height", fields_->custom_row));
    custom->addWidget(fields_->height, 1);
    fields_->custom_row->hide();
    size_layout->addWidget(fields_->custom_row);
    auto* margin_row = new QHBoxLayout;
    auto* margin_label = new QLabel("Margins", fields_->size_block);
    fields_->margin = new QDoubleSpinBox(fields_->size_block);
    fields_->margin->setObjectName("exportMargin");
    fields_->margin->setRange(0.40, 1.50);
    fields_->margin->setDecimals(2);
    fields_->margin->setSingleStep(0.05);
    fields_->margin->setSuffix(" in");
    fields_->margin->setValue(0.85);
    margin_row->addWidget(margin_label);
    margin_row->addWidget(fields_->margin, 1);
    margin_row->addStretch();
    size_layout->addLayout(margin_row);
    form->addWidget(fields_->size_block);

    auto* type_label = new QLabel("Body type", host);
    type_label->setFont(section);
    form->addSpacing(8);
    form->addWidget(type_label);
    fields_->type_block = new QWidget(host);
    auto* type_row = new QHBoxLayout(fields_->type_block);
    type_row->setContentsMargins(0, 0, 0, 0);
    fields_->body = new QSpinBox(fields_->type_block);
    fields_->body->setObjectName("exportBody");
    fields_->body->setRange(9, 14);
    fields_->body->setSuffix(" pt");
    fields_->body->setValue(11);
    fields_->leading = new QSpinBox(fields_->type_block);
    fields_->leading->setObjectName("exportLeading");
    fields_->leading->setRange(110, 160);
    fields_->leading->setSuffix("%");
    fields_->leading->setValue(130);
    type_row->addWidget(new QLabel("Size", fields_->type_block));
    type_row->addWidget(fields_->body, 1);
    type_row->addSpacing(12);
    type_row->addWidget(new QLabel("Leading", fields_->type_block));
    type_row->addWidget(fields_->leading, 1);
    form->addWidget(fields_->type_block);

    auto* include_label = new QLabel("Include", host);
    include_label->setFont(section);
    form->addSpacing(8);
    form->addWidget(include_label);
    auto* include_row = new QHBoxLayout;
    include_row->setSpacing(16);
    fields_->cover = new InkCheck("Cover", host);
    fields_->title = new InkCheck("Title page", host);
    fields_->copyright = new InkCheck("Copyright", host);
    fields_->contents = new InkCheck("Contents", host);
    fields_->cover->setObjectName("exportCover");
    fields_->title->setObjectName("exportTitle");
    fields_->copyright->setObjectName("exportCopyright");
    fields_->contents->setObjectName("exportContents");
    for (InkCheck* box : {fields_->cover, fields_->title, fields_->copyright, fields_->contents}) {
        box->setChecked(true);
        include_row->addWidget(box);
    }
    include_row->addStretch();
    form->addLayout(include_row);

    auto* chapter_bar = new QHBoxLayout;
    auto* chapter_label = new QLabel("Chapters", host);
    chapter_label->setFont(section);
    auto* all_button = new QPushButton("All", host);
    auto* none_button = new QPushButton("None", host);
    all_button->setObjectName("exportAll");
    none_button->setObjectName("exportNone");
    all_button->setCursor(Qt::PointingHandCursor);
    none_button->setCursor(Qt::PointingHandCursor);
    chapter_bar->addWidget(chapter_label);
    chapter_bar->addStretch();
    chapter_bar->addWidget(all_button);
    chapter_bar->addWidget(none_button);
    form->addSpacing(8);
    form->addLayout(chapter_bar);
    auto* chapter_scroll = new QScrollArea(host);
    chapter_scroll->setWidgetResizable(true);
    chapter_scroll->setFrameShape(QFrame::NoFrame);
    chapter_scroll->setMinimumHeight(120);
    chapter_scroll->setMaximumHeight(220);
    fields_->chapter_host = new QWidget(chapter_scroll);
    fields_->chapter_rows = new QVBoxLayout(fields_->chapter_host);
    fields_->chapter_rows->setContentsMargins(0, 0, 0, 0);
    fields_->chapter_rows->setSpacing(2);
    fields_->chapter_empty = new QLabel("This book has no chapters yet.", fields_->chapter_host);
    fields_->chapter_empty->setObjectName("hint");
    fields_->chapter_rows->addWidget(fields_->chapter_empty);
    fields_->chapter_rows->addStretch();
    chapter_scroll->setWidget(fields_->chapter_host);
    form->addWidget(chapter_scroll);
    form->addStretch();
    page->addLayout(form, 3);

    auto* side = new QVBoxLayout;
    side->setSpacing(12);
    fields_->shape = new PageShape(host);
    side->addWidget(fields_->shape, 0, Qt::AlignHCenter);
    fields_->summary = new QLabel(host);
    fields_->summary->setObjectName("exportSummary");
    fields_->summary->setWordWrap(true);
    fields_->summary->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    side->addWidget(fields_->summary);
    fields_->go = new QPushButton("Export PDF", host);
    fields_->go->setObjectName("exportGo");
    fields_->go->setCursor(Qt::PointingHandCursor);
    fields_->go->setMinimumHeight(40);
    side->addWidget(fields_->go);
    side->addStretch();
    page->addLayout(side, 2);
    scroll->setWidget(host);
    outer->addWidget(scroll, 1);

    fields_->loading = true;
    QFile stored(export_config_path());
    if (stored.open(QIODevice::ReadOnly)) {
        while (!stored.atEnd()) {
            const QString line = QString::fromUtf8(stored.readLine()).trimmed();
            const int split = line.indexOf('=');
            if (split <= 0) {
                continue;
            }
            const QString key = line.left(split);
            const QString value = line.mid(split + 1);
            if (key == "format") {
                for (QAbstractButton* button : fields_->formats->buttons()) {
                    if (button->property("format").toInt() == value.toInt()) {
                        button->setChecked(true);
                    }
                }
            } else if (key == "trim") {
                const int index = fields_->trim->findData(value);
                if (index >= 0) {
                    fields_->trim->setCurrentIndex(index);
                }
            } else if (key == "width") {
                fields_->width->setValue(value.toDouble());
            } else if (key == "height") {
                fields_->height->setValue(value.toDouble());
            } else if (key == "margin") {
                fields_->margin->setValue(value.toDouble());
            } else if (key == "body") {
                fields_->body->setValue(value.toInt());
            } else if (key == "leading") {
                fields_->leading->setValue(value.toInt());
            } else if (key == "cover") {
                fields_->cover->setChecked(value != "0");
            } else if (key == "title") {
                fields_->title->setChecked(value != "0");
            } else if (key == "copyright") {
                fields_->copyright->setChecked(value != "0");
            } else if (key == "contents") {
                fields_->contents->setChecked(value != "0");
            }
        }
    }
    fields_->loading = false;

    connect(fields_->back, &QPushButton::clicked, this, &ExportSheet::backRequested);
    connect(fields_->go, &QPushButton::clicked, this, &ExportSheet::exportRequested);
    connect(fields_->formats, &QButtonGroup::buttonClicked, this, [this] {
        if (!fields_->loading) {
            refresh();
            remember();
        }
    });
    const auto changed = [this] {
        if (!fields_->loading) {
            refresh();
            remember();
        }
    };
    connect(fields_->trim, qOverload<int>(&QComboBox::currentIndexChanged), this, changed);
    connect(fields_->width, qOverload<double>(&QDoubleSpinBox::valueChanged), this, changed);
    connect(fields_->height, qOverload<double>(&QDoubleSpinBox::valueChanged), this, changed);
    connect(fields_->margin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, changed);
    connect(fields_->body, qOverload<int>(&QSpinBox::valueChanged), this, changed);
    connect(fields_->leading, qOverload<int>(&QSpinBox::valueChanged), this, changed);
    for (InkCheck* box : {fields_->cover, fields_->title, fields_->copyright, fields_->contents}) {
        connect(box, &QCheckBox::toggled, this, changed);
    }
    connect(all_button, &QPushButton::clicked, this, [this] {
        for (InkCheck* box : fields_->chapters) {
            box->setChecked(true);
        }
        refresh();
    });
    connect(none_button, &QPushButton::clicked, this, [this] {
        for (InkCheck* box : fields_->chapters) {
            box->setChecked(false);
        }
        refresh();
    });
    setDark(false);
    refresh();
}

ExportSheet::~ExportSheet() { delete fields_; }

void ExportSheet::setDark(bool dark) {
    fields_->dark = dark;
    const DeskTheme theme = activeTheme().dark == dark ? activeTheme() : deskTheme(activeTheme().id, dark);
    const QColor desk = theme.desk;
    const QColor ink = theme.ink;
    const QColor field = theme.field;
    const QColor edge = theme.edge;
    const QColor muted = theme.muted;
    const QColor selected = theme.accent;
    const QColor selected_ink = theme.accent_ink;
    QPalette colors = palette();
    colors.setColor(QPalette::Window, desk);
    colors.setColor(QPalette::WindowText, ink);
    colors.setColor(QPalette::Base, field);
    colors.setColor(QPalette::Text, ink);
    colors.setColor(QPalette::Button, field);
    colors.setColor(QPalette::ButtonText, ink);
    colors.setColor(QPalette::Mid, edge);
    colors.setColor(QPalette::Highlight, selected);
    colors.setColor(QPalette::HighlightedText, selected_ink);
    setPalette(colors);
    setAutoFillBackground(true);
    setStyleSheet(QStringLiteral("QWidget#exportPage, QWidget#exportHost { background: %1; color: %2; }"
                                 "QLabel { color: %2; background: transparent; }"
                                 "QLabel#hint, QLabel#exportSummary { color: %3; }"
                                 "QComboBox, QDoubleSpinBox, QSpinBox { background: %4; color: %2; border: 1px solid %5; border-radius: %8px; padding: 4px 8px; }"
                                 "QLabel:disabled, QComboBox:disabled, QDoubleSpinBox:disabled, QSpinBox:disabled { color: %3; }"
                                 "QComboBox:disabled, QDoubleSpinBox:disabled, QSpinBox:disabled { background: %1; }"
                                 "QComboBox QAbstractItemView { background: %4; color: %2; selection-background-color: %6; selection-color: %7; }"
                                 "QScrollArea { background: transparent; }")
                      .arg(desk.name(), ink.name(), muted.name(), field.name(), edge.name(), selected.name(), selected_ink.name())
                      .arg(qMin(theme.radius, 12)));
    if (QWidget* view = fields_->trim->view()) {
        QPalette popup = view->palette();
        popup.setColor(QPalette::Base, field);
        popup.setColor(QPalette::Text, ink);
        popup.setColor(QPalette::Highlight, selected);
        popup.setColor(QPalette::HighlightedText, selected_ink);
        view->setPalette(popup);
    }
    const QString curve = QString::number(theme.radius);
    const QString quiet = QStringLiteral("QPushButton { background: transparent; color: %1; border: 1px solid %2; border-radius: %6px; padding: 8px 12px; }"
                                         "QPushButton:hover { background: %3; }"
                                         "QPushButton:checked { background: %4; color: %5; border-color: %4; }")
                              .arg(ink.name(), edge.name(), theme.hover.name(), selected.name(), selected_ink.name(), curve);
    for (QAbstractButton* button : fields_->formats->buttons()) {
        button->setStyleSheet(quiet);
    }
    fields_->back->setStyleSheet(QStringLiteral("QPushButton { background: transparent; color: %1; border: 1px solid %2; border-radius: %4px; padding: 6px 10px; }"
                                                "QPushButton:hover { background: %3; }")
                                     .arg(ink.name(), edge.name(), theme.hover.name(), curve));
    for (QPushButton* button : findChildren<QPushButton*>()) {
        if (button->objectName() == "exportAll" || button->objectName() == "exportNone") {
            button->setStyleSheet(fields_->back->styleSheet());
        }
    }
    fields_->go->setStyleSheet(QStringLiteral("QPushButton { background: %1; color: %2; border: none; border-radius: %4px; padding: 8px 14px; }"
                                              "QPushButton:hover { background: %3; }")
                                   .arg(selected.name(), selected_ink.name(), theme.accent_hover.name(), curve));
    for (QScrollArea* area : findChildren<QScrollArea*>()) {
        area->viewport()->setAutoFillBackground(true);
        QPalette viewport = colors;
        viewport.setColor(QPalette::Window, desk);
        area->viewport()->setPalette(viewport);
        if (QWidget* inner = area->widget()) {
            inner->setAutoFillBackground(true);
            inner->setPalette(colors);
        }
    }
    for (QCheckBox* box : findChildren<QCheckBox*>()) {
        box->setPalette(colors);
        box->update();
    }
    if (fields_->shape != nullptr) {
        fields_->shape->update();
    }
    refresh();
}

void ExportSheet::setChapters(const QStringList& titles, const QVector<int>& words, bool has_cover) {
    fields_->has_cover = has_cover;
    while (QLayoutItem* item = fields_->chapter_rows->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    fields_->chapters.clear();
    fields_->chapter_empty = nullptr;
    if (titles.isEmpty()) {
        fields_->chapter_empty = new QLabel("This book has no chapters yet.", fields_->chapter_host);
        fields_->chapter_empty->setObjectName("hint");
        fields_->chapter_rows->addWidget(fields_->chapter_empty);
    }
    for (int index = 0; index < titles.size(); ++index) {
        const QString title = titles.at(index).trimmed().isEmpty() ? QStringLiteral("Untitled") : titles.at(index).trimmed();
        auto* box = new InkCheck(title, fields_->chapter_host);
        box->setObjectName("exportChapter");
        box->setChecked(true);
        box->setProperty("chapter", index);
        box->setProperty("words", index < words.size() ? words.at(index) : 0);
        box->setPalette(palette());
        fields_->chapters.push_back(box);
        fields_->chapter_rows->addWidget(box);
        connect(box, &QCheckBox::toggled, this, [this] { refresh(); });
    }
    fields_->chapter_rows->addStretch();
    refresh();
}

ExportFormat ExportSheet::format() const {
    if (QAbstractButton* button = fields_->formats->checkedButton()) {
        return static_cast<ExportFormat>(button->property("format").toInt());
    }
    return ExportFormat::Pdf;
}

bool ExportSheet::printFormat() const { return format() == ExportFormat::Pdf || format() == ExportFormat::Docx; }

void ExportSheet::fill(ExportRequest& request) const {
    const QString id = fields_->trim->currentData().toString();
    double width = 6.0;
    double height = 9.0;
    for (const TrimChoice& choice : kTrims) {
        if (id == QLatin1String(choice.id) && id != "custom") {
            width = choice.width;
            height = choice.height;
        }
    }
    if (id == "custom") {
        width = fields_->width->value();
        height = fields_->height->value();
    }
    request.trim_width = width;
    request.trim_height = height;
    request.margin_inches = fields_->margin->value();
    request.body_size = fields_->body->value();
    request.body_leading = fields_->leading->value();
    request.include_cover = fields_->cover->isChecked();
    request.include_title = fields_->title->isChecked();
    request.include_copyright = fields_->copyright->isChecked();
    request.include_contents = fields_->contents->isChecked();
}

QVector<int> ExportSheet::checkedChapters() const {
    QVector<int> indexes;
    for (InkCheck* box : fields_->chapters) {
        if (box->isChecked()) {
            indexes.push_back(box->property("chapter").toInt());
        }
    }
    return indexes;
}

void ExportSheet::refresh() {
    if (fields_->trim == nullptr) {
        return;
    }
    const bool print = printFormat();
    fields_->size_block->setEnabled(print);
    fields_->type_block->setEnabled(format() != ExportFormat::Text);
    fields_->custom_row->setVisible(print && fields_->trim->currentData().toString() == "custom");
    if (format() == ExportFormat::Pdf) {
        fields_->format_hint->setText("A print PDF at this trim. Body size is for the book, not the writing desk. The typeface still comes from the writing page.");
        fields_->go->setText("Export PDF");
    } else if (format() == ExportFormat::Epub) {
        fields_->format_hint->setText("An EPUB for readers and bookstores. Body size is the starting size, and readers can change it. Trim size waits for the next print export.");
        fields_->go->setText("Export EPUB");
    } else if (format() == ExportFormat::Docx) {
        fields_->format_hint->setText("A Word document at this trim. Body size is for the book, not the writing desk. The typeface still comes from the writing page.");
        fields_->go->setText("Export Word");
    } else {
        fields_->format_hint->setText("The manuscript as a text file, with the parts you check below.");
        fields_->go->setText("Export manuscript");
    }

    double width = 6.0;
    double height = 9.0;
    QString size_name = fields_->trim->currentText();
    const QString id = fields_->trim->currentData().toString();
    for (const TrimChoice& choice : kTrims) {
        if (id == QLatin1String(choice.id) && id != "custom") {
            width = choice.width;
            height = choice.height;
            size_name = inches(choice.width) + " × " + inches(choice.height) + " in";
        }
    }
    if (id == "custom") {
        width = fields_->width->value();
        height = fields_->height->value();
        size_name = inches(width) + " × " + inches(height) + " in";
    }
    const QString caption = print ? size_name : (format() == ExportFormat::Epub ? QStringLiteral("EPUB") : QStringLiteral("Manuscript"));
    fields_->shape->showTrim(width, height, fields_->margin->value(), print, caption, fields_->dark);

    QStringList parts;
    if (fields_->cover->isChecked()) {
        parts.push_back("cover");
    }
    if (fields_->title->isChecked()) {
        parts.push_back("title page");
    }
    if (fields_->copyright->isChecked()) {
        parts.push_back("copyright");
    }
    if (fields_->contents->isChecked()) {
        parts.push_back("contents");
    }
    const int chosen = static_cast<int>(std::count_if(fields_->chapters.begin(), fields_->chapters.end(), [](const InkCheck* box) { return box->isChecked(); }));
    if (!fields_->chapters.isEmpty()) {
        if (chosen == fields_->chapters.size()) {
            parts.push_back(QString::number(chosen) + (chosen == 1 ? " chapter" : " chapters"));
        } else {
            parts.push_back(QString::number(chosen) + " of " + QString::number(fields_->chapters.size()) + " chapters");
        }
    }
    QString lead;
    if (format() == ExportFormat::Pdf) {
        lead = "Print PDF · " + size_name + " · " + inches(fields_->margin->value()) + " in margins. ";
    } else if (format() == ExportFormat::Docx) {
        lead = "Word · " + size_name + " · " + inches(fields_->margin->value()) + " in margins. ";
    } else if (format() == ExportFormat::Epub) {
        lead = "EPUB. ";
    } else {
        lead = "Manuscript. ";
    }
    if (format() != ExportFormat::Text) {
        lead += QString::number(fields_->body->value()) + " pt. ";
    }
    int words = 0;
    int chosen_chapters = 0;
    for (const InkCheck* box : fields_->chapters) {
        if (box->isChecked()) {
            words += box->property("words").toInt();
            ++chosen_chapters;
        }
    }
    QString summary = parts.isEmpty() ? lead + "Choose a part of the book to export." : lead + parts.join(", ") + ".";
    if (fields_->cover->isChecked() && !fields_->has_cover) {
        summary += " No cover is stored with this book yet.";
    }
    if (print && words > 0) {
        const double text_w = std::max(1.0, width - (2.0 * fields_->margin->value()));
        const double text_h = std::max(1.0, height - (2.0 * fields_->margin->value()));
        const double words_per_page = 250.0 * (text_w / 4.3) * (text_h / 7.3) * (11.0 / fields_->body->value()) * (11.0 / fields_->body->value()) *
                                      (130.0 / fields_->leading->value());
        const int pages = std::max(1, static_cast<int>(std::lround(words / std::max(40.0, words_per_page)))) + chosen_chapters;
        summary += " About " + QString::number(pages) + " pages.";
    }
    fields_->summary->setText(summary);
}

void ExportSheet::remember() const {
    QDir().mkpath(QFileInfo(export_config_path()).path());
    QSaveFile file(export_config_path());
    if (!file.open(QIODevice::WriteOnly)) {
        return;
    }
    QString body;
    body += "format=" + QString::number(static_cast<int>(format())) + "\n";
    body += "trim=" + fields_->trim->currentData().toString() + "\n";
    body += "width=" + QString::number(fields_->width->value(), 'f', 2) + "\n";
    body += "height=" + QString::number(fields_->height->value(), 'f', 2) + "\n";
    body += "margin=" + QString::number(fields_->margin->value(), 'f', 2) + "\n";
    body += "body=" + QString::number(fields_->body->value()) + "\n";
    body += "leading=" + QString::number(fields_->leading->value()) + "\n";
    body += QString("cover=") + (fields_->cover->isChecked() ? "1" : "0") + "\n";
    body += QString("title=") + (fields_->title->isChecked() ? "1" : "0") + "\n";
    body += QString("copyright=") + (fields_->copyright->isChecked() ? "1" : "0") + "\n";
    body += QString("contents=") + (fields_->contents->isChecked() ? "1" : "0") + "\n";
    file.write(body.toUtf8());
    file.commit();
}

}  // namespace inkwell
