#include "mainwindow.hpp"

#include "details.hpp"
#include "drawer.hpp"
#include "export.hpp"
#include "editorview.hpp"
#include "markup.hpp"
#include "grammar.hpp"
#include "spell.hpp"
#include "text.hpp"
#include "textmap.hpp"

#include <QAbstractButton>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QEnterEvent>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QDate>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QDialogButtonBox>
#include <functional>

#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QGuiApplication>
#include <QGridLayout>
#include <QEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QInputDialog>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QLocale>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QSaveFile>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyleHints>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <utility>

namespace inkwell {
namespace {

QString kind_label(int kind) {
    if (kind == 0) {
        return "spelling";
    }
    if (kind == 1) {
        return "grammar";
    }
    if (kind == 3) {
        return "name";
    }
    return "style";
}

// Plasma's native message box deletes itself on close, then Qt hides it again and crashes.
QMessageBox::StandardButton deskMessage(QWidget* parent, QMessageBox::Icon icon, const QString& title, const QString& text,
                                        QMessageBox::StandardButtons buttons, QMessageBox::StandardButton fallback) {
    QMessageBox box(icon, title, text, buttons, parent);
    box.setOption(QMessageBox::Option::DontUseNativeDialog);
    if (fallback != QMessageBox::NoButton) {
        box.setDefaultButton(fallback);
    }
    return static_cast<QMessageBox::StandardButton>(box.exec());
}

int kind_code(IssueKind kind) {
    if (kind == IssueKind::Spelling) {
        return 0;
    }
    if (kind == IssueKind::Grammar) {
        return 1;
    }
    if (kind == IssueKind::Name) {
        return 3;
    }
    return 2;
}

struct ShelfMeasure {
    int columns = 5;
    int cover_width = 280;
};

ShelfMeasure measureShelf(int viewport) {
    const int gap = 32;
    const int card_extra = 12;
    const int edge = 8;
    const int room = qMax(260, viewport) - edge;
    int columns = 5;
    int cover = (room - gap * (columns - 1)) / columns - card_extra;
    if (cover > 348) {
        cover = 348;
        columns = qMax(1, (room + gap) / (cover + card_extra + gap));
    } else if (cover < 188) {
        const int slot = 188 + card_extra;
        columns = qMax(1, (room + gap) / (slot + gap));
        while (columns > 1 && columns * slot + (columns - 1) * gap > room) {
            --columns;
        }
        cover = qMax(150, (room - gap * (columns - 1)) / columns - card_extra);
    }
    return {columns, cover};
}

QPixmap paintedCover(const QString& title, bool dark, const QString& path, const QSize& size, qreal pixel_ratio) {
    QImage image;
    if (!path.isEmpty()) {
        image.load(path);
    }
    const QSize pixels(qRound(size.width() * pixel_ratio), qRound(size.height() * pixel_ratio));
    QImage canvas(pixels, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(dark ? QColor(46, 40, 32) : QColor(62, 52, 42));
    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setRenderHint(QPainter::Antialiasing);
    if (!image.isNull()) {
        const QImage scaled = image.scaled(pixels, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        const int x = qMax(0, (scaled.width() - pixels.width()) / 2);
        const int y = qMax(0, (scaled.height() - pixels.height()) / 2);
        painter.drawImage(0, 0, scaled, x, y, pixels.width(), pixels.height());
    } else {
        painter.setPen(QColor(243, 239, 230));
        QFont font;
        font.setFamilies({"Literata", "Liberation Serif", "serif"});
        font.setBold(true);
        font.setPixelSize(qMax(18, pixels.width() / 12));
        painter.setFont(font);
        painter.drawText(QRect(QPoint(0, 0), pixels).adjusted(18, 24, -18, -24), Qt::AlignCenter | Qt::TextWordWrap, title);
    }
    painter.end();
    canvas.setDevicePixelRatio(pixel_ratio);
    return QPixmap::fromImage(canvas);
}

class ShelfCard : public QAbstractButton {
public:
    ShelfCard(const QPixmap& cover, const QString& title, const QString& subtitle, const QString& meta, const QString& ink,
              const QString& muted, const QColor& hover, QWidget* parent = nullptr)
        : QAbstractButton(parent), hover_(hover) {
        setCursor(Qt::PointingHandCursor);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(6, 6, 6, 8);
        layout->setSpacing(4);
        auto* art = new QLabel;
        art->setPixmap(cover);
        art->setFixedSize(cover.deviceIndependentSize().toSize());
        art->setAttribute(Qt::WA_TransparentForMouseEvents);
        layout->addWidget(art, 0, Qt::AlignHCenter);
        auto add_line = [&](const QString& text, const QString& color, bool strong) {
            auto* line = new QLabel(text);
            line->setWordWrap(true);
            line->setAlignment(Qt::AlignHCenter);
            line->setAttribute(Qt::WA_TransparentForMouseEvents);
            line->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(color));
            if (strong) {
                QFont font = line->font();
                font.setBold(true);
                font.setPointSize(12);
                line->setFont(font);
            }
            layout->addWidget(line);
        };
        add_line(title, ink, true);
        if (!subtitle.isEmpty()) {
            add_line(subtitle, muted, false);
        }
        if (!meta.isEmpty()) {
            add_line(meta, muted, false);
        }
        setFixedWidth(cover.deviceIndependentSize().width() + 12);
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        Q_UNUSED(event);
        if (!underMouse() && !isDown()) {
            return;
        }
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(hover_);
        painter.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 12, 12);
    }

    void enterEvent(QEnterEvent* event) override {
        QAbstractButton::enterEvent(event);
        update();
    }

    void leaveEvent(QEvent* event) override {
        QAbstractButton::leaveEvent(event);
        update();
    }

private:
    QColor hover_;
};

QString inkwellLibrary() {
    QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (documents.isEmpty()) {
        documents = QDir::homePath() + QStringLiteral("/Documents");
    }
    const QString library = QDir(documents).filePath(QStringLiteral("Inkwell"));
    QDir().mkpath(library);
    return library;
}

QString bookFolderName(const QString& title) {
    QString name;
    for (const QChar ch : title.trimmed()) {
        if (ch == '/' || ch == '\\' || ch == ':' || ch == '*' || ch == '?' || ch == '"' || ch == '<' || ch == '>' || ch == '|' ||
            ch == '\n' || ch == '\r') {
            continue;
        }
        name += ch;
    }
    name = name.simplified();
    if (name.isEmpty() || name == QLatin1String(".") || name == QLatin1String("..")) {
        return QStringLiteral("Untitled");
    }
    return name;
}

QString defaultBookDirectory(const QString& title) {
    const QDir library(inkwellLibrary());
    const QString base = bookFolderName(title);
    QString candidate = library.filePath(base);
    for (int number = 2; QFileInfo::exists(QDir(candidate).filePath(QStringLiteral("inkwell.book"))) && number < 100; ++number) {
        candidate = library.filePath(base + QLatin1Char(' ') + QString::number(number));
    }
    return candidate;
}

class BookFolderField : public QWidget {
public:
    explicit BookFolderField(QWidget* parent = nullptr) : QWidget(parent) {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(6);
        auto* blurb = new QLabel("Saved in your Documents folder, inside Inkwell.");
        blurb->setWordWrap(true);
        place_ = new QLabel;
        place_->setWordWrap(true);
        place_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        auto* custom = new QPushButton("Custom location…");
        custom->setCursor(Qt::PointingHandCursor);
        layout->addWidget(blurb);
        layout->addWidget(place_);
        layout->addWidget(custom, 0, Qt::AlignLeft);
        connect(custom, &QPushButton::clicked, this, [this] { chooseCustom(); });
        refresh();
    }

    void setTitle(const QString& title) {
        title_ = title;
        if (custom_.isEmpty()) {
            refresh();
        }
    }

    QString folder() const {
        return custom_.isEmpty() ? defaultBookDirectory(title_) : custom_;
    }

private:
    void refresh() {
        place_->setText(QDir::toNativeSeparators(folder()));
    }

    void chooseCustom() {
        const QString start = custom_.isEmpty() ? inkwellLibrary() : QFileInfo(custom_).absolutePath();
        const QString selected = QFileDialog::getExistingDirectory(this, "Choose a folder", start);
        if (selected.isEmpty()) {
            return;
        }
        if (QFileInfo::exists(QDir(selected).filePath(QStringLiteral("inkwell.book")))) {
            deskMessage(this, QMessageBox::Information, "Save book", "That folder already holds a book.", QMessageBox::Ok,
                        QMessageBox::Ok);
            return;
        }
        custom_ = selected;
        refresh();
    }

    QString title_;
    QString custom_;
    QLabel* place_ = nullptr;
};

}  // namespace

MainWindow::~MainWindow() = default;

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    dictionary_ = std::make_unique<Dictionary>();
    std::string error;
    if (!dictionary_->load(error)) {
        dict_error_ = QString::fromStdString(error);
    }
    page_ = PageSettings::load();
    const ThemeChoice choice = loadThemeChoice();
    theme_id_ = choice.id;
    dark_ = choice.dark;
    setActiveTheme(deskTheme(theme_id_, dark_));
    buildUi();
    resize(1480, 860);
    updateTitle();
    updateCounts();
    if (!dict_error_.isEmpty()) {
        statusBar()->showMessage(dict_error_);
        detail_->setText(dict_error_);
    }
}


namespace {

class ClickCatcher : public QObject {
public:
    explicit ClickCatcher(std::function<void()> callback, QObject* parent = nullptr)
        : QObject(parent), callback_(std::move(callback)) {}

    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::MouseButtonPress) {
            callback_();
            return true;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    std::function<void()> callback_;
};

QString dataDir() {
    return QDir::homePath() + "/.local/share/inkwell";
}

QString recoveryPath() {
    return dataDir() + "/recovery";
}

QString recentPath() {
    return dataDir() + "/recent";
}

bool bookHasWork(const inkwell::Book& book) {
    for (const QVector<inkwell::Leaf>& notes : book.notes) {
        for (const inkwell::Leaf& leaf : notes) {
            if (!leaf.body.trimmed().isEmpty() || !leaf.title.trimmed().isEmpty()) {
                return true;
            }
        }
    }
    if (book.chapters.size() != 1) {
        return !book.chapters.isEmpty();
    }
    const inkwell::Leaf& chapter = book.chapters.front();
    if (!chapter.body.trimmed().isEmpty()) {
        return true;
    }
    const QString title = chapter.title.trimmed();
    return !title.isEmpty() && title != "Untitled";
}

QString sessionValue(const QString& text, const QString& key) {
    const QString prefix = key + "=";
    for (const QString& line : text.split('\n')) {
        if (line.startsWith(prefix)) {
            return line.mid(prefix.size());
        }
    }
    return {};
}

QString recentLabel(const QString& path) {
    const QFileInfo info(path);
    QString name = info.fileName();
    QString parent = info.dir().dirName();
    if (name == "inkwell.book") {
        name = parent;
        parent = QFileInfo(info.dir().absolutePath()).dir().dirName();
    }
    if (parent.isEmpty()) {
        return name.isEmpty() ? path : name;
    }
    return name + "  —  " + parent;
}

class DeskCheck : public QCheckBox {
public:
    explicit DeskCheck(const QString& label, QWidget* parent) : QCheckBox(label, parent) {}

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

void storeRecentList(const QStringList& items) {
    QDir().mkpath(dataDir());
    QSaveFile file(recentPath());
    if (!file.open(QIODevice::WriteOnly)) {
        return;
    }
    if (!items.isEmpty()) {
        file.write((items.join('\n') + '\n').toUtf8());
    }
    file.commit();
}

}  // namespace

void MainWindow::buildUi() {
    editor_ = new EditorView(this);
    kicker_ = new QLabel(this);
    kicker_->setAlignment(Qt::AlignCenter);
    kicker_->hide();
    title_edit_ = new QLineEdit(this);
    title_edit_->setPlaceholderText("Untitled");
    title_edit_->setAlignment(Qt::AlignCenter);
    title_edit_->setFrame(false);
    QFont title_font = title_edit_->font();
    title_font.setPointSize(16);
    title_font.setBold(true);
    title_edit_->setFont(title_font);
    delete_piece_ = new QPushButton("Delete", this);
    delete_piece_->setCursor(Qt::PointingHandCursor);

    canvas_ = new QWidget(this);
    canvas_->setAutoFillBackground(true);
    auto* canvas_layout = new QVBoxLayout(canvas_);
    canvas_layout->setContentsMargins(28, 16, 18, 16);
    canvas_layout->setSpacing(8);
    canvas_layout->addWidget(kicker_);
    auto* title_row = new QGridLayout;
    title_row->setColumnStretch(0, 1);
    title_row->setColumnStretch(1, 3);
    title_row->setColumnStretch(2, 1);
    title_row->addWidget(title_edit_, 0, 1);
    title_row->addWidget(delete_piece_, 0, 2, Qt::AlignRight);
    canvas_layout->addLayout(title_row);
    auto* format_row = new QHBoxLayout;
    format_row->setSpacing(6);
    format_row->addStretch();
    const QStringList format_labels = {"Italic", "Bold", "Scene", "Image", "Field", "Markup"};
    QVector<QPushButton*> format_buttons;
    for (const QString& label : format_labels) {
        auto* button = new QPushButton(label);
        button->setObjectName("formatButton");
        button->setCursor(Qt::PointingHandCursor);
        if (label == "Markup") {
            button->setObjectName("markupButton");
            button->setCheckable(true);
        }
        format_row->addWidget(button);
        format_buttons.push_back(button);
    }
    format_row->addStretch();
    canvas_layout->addLayout(format_row);
    canvas_layout->addWidget(editor_, 1);
    connect(format_buttons[0], &QPushButton::clicked, this, [this] { editor_->setItalic(!editor_->currentCharFormat().fontItalic()); });
    connect(format_buttons[1], &QPushButton::clicked, this, [this] { editor_->setBold(editor_->currentCharFormat().fontWeight() <= QFont::Normal); });
    connect(format_buttons[2], &QPushButton::clicked, this, [this] { editor_->insertSceneBreak(); });
    connect(format_buttons[3], &QPushButton::clicked, this, [this] { insertPicture(); });
    connect(format_buttons[4], &QPushButton::clicked, this, [this] { insertDataField(); });
    connect(format_buttons[5], &QPushButton::toggled, this, [this](bool on) {
        editor_->setSourceMode(on);
        recheck();
    });

    notes_ = new QListWidget(this);
    notes_->setUniformItemSizes(true);
    notes_->setTextElideMode(Qt::ElideRight);
    notes_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    notes_->setFrameShape(QFrame::NoFrame);
    detail_ = new QLabel("No spelling or grammar notes.");
    detail_->setWordWrap(true);
    detail_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    detail_->setMinimumHeight(72);
    apply_button_ = new QPushButton("Apply suggestion");
    apply_button_->setEnabled(false);
    apply_button_->setCursor(Qt::PointingHandCursor);
    suggestion_blurb_ = new QLabel("Spelling, grammar, style, and possible names.");
    suggestion_blurb_->setWordWrap(true);

    auto* suggestions = new QWidget(this);
    suggestions->setObjectName("drawerPage");
    auto* suggestions_layout = new QVBoxLayout(suggestions);
    suggestions_layout->setContentsMargins(16, 18, 16, 16);
    suggestions_layout->setSpacing(10);
    auto* suggestion_heading = new QLabel("Suggestions");
    QFont heading_font = suggestion_heading->font();
    heading_font.setPointSize(16);
    heading_font.setBold(true);
    suggestion_heading->setFont(heading_font);
    suggestions_layout->addWidget(suggestion_heading);
    suggestions_layout->addWidget(suggestion_blurb_);
    suggestions_layout->addWidget(notes_, 1);
    suggestions_layout->addWidget(detail_);
    auto* note_buttons = new QHBoxLayout;
    name_button_ = new QPushButton("Add name");
    name_button_->setEnabled(false);
    name_button_->setCursor(Qt::PointingHandCursor);
    note_buttons->addWidget(name_button_);
    note_buttons->addWidget(apply_button_, 1);
    suggestions_layout->addLayout(note_buttons);

    auto* chapters = new QWidget(this);
    chapters->setObjectName("drawerPage");
    auto* chapters_layout = new QVBoxLayout(chapters);
    chapters_layout->setContentsMargins(16, 18, 12, 16);
    chapters_layout->setSpacing(10);
    auto* chapter_heading = new QLabel("Chapters");
    chapter_heading->setFont(heading_font);
    auto* chapter_header = new QHBoxLayout;
    chapter_header->addWidget(chapter_heading);
    chapter_header->addStretch();
    auto* import_button = new QPushButton("Import");
    auto* new_chapter_button = new QPushButton("+ New");
    import_button->setCursor(Qt::PointingHandCursor);
    new_chapter_button->setCursor(Qt::PointingHandCursor);
    quiet_buttons_.push_back(import_button);
    quiet_buttons_.push_back(new_chapter_button);
    chapter_header->addWidget(import_button);
    chapter_header->addWidget(new_chapter_button);
    chapters_layout->addLayout(chapter_header);
    auto* chapter_scroll = new QScrollArea(this);
    chapter_scroll->setWidgetResizable(true);
    chapter_scroll->setFrameShape(QFrame::NoFrame);
    chapter_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* chapter_host = new QWidget(chapter_scroll);
    chapter_rows_ = new QVBoxLayout(chapter_host);
    chapter_rows_->setContentsMargins(0, 0, 0, 0);
    chapter_rows_->setSpacing(4);
    chapter_scroll->setWidget(chapter_host);
    chapters_layout->addWidget(chapter_scroll, 1);

    auto* outline = new QWidget(this);
    outline->setObjectName("drawerPage");
    auto* outline_layout = new QVBoxLayout(outline);
    outline_layout->setContentsMargins(16, 18, 12, 16);
    outline_layout->setSpacing(8);
    auto* outline_header = new QHBoxLayout;
    auto* outline_heading = new QLabel("Story notes");
    outline_heading->setFont(heading_font);
    auto* new_note_button = new QPushButton("+ New");
    new_note_button->setCursor(Qt::PointingHandCursor);
    quiet_buttons_.push_back(new_note_button);
    outline_header->addWidget(outline_heading);
    outline_header->addStretch();
    outline_header->addWidget(new_note_button);
    auto* outline_blurb = new QLabel("An outline for characters, plot, and the world. These notes stay off the page.");
    outline_blurb->setWordWrap(true);
    outline_blurb->setObjectName("hint");
    auto* outline_scroll = new QScrollArea(this);
    outline_scroll->setWidgetResizable(true);
    outline_scroll->setFrameShape(QFrame::NoFrame);
    outline_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* outline_host = new QWidget(outline_scroll);
    outline_rows_ = new QVBoxLayout(outline_host);
    outline_rows_->setContentsMargins(0, 0, 0, 0);
    outline_rows_->setSpacing(12);
    outline_scroll->setWidget(outline_host);
    outline_layout->addLayout(outline_header);
    outline_layout->addWidget(outline_blurb);
    outline_layout->addWidget(outline_scroll, 1);

    auto* book_page = new QWidget(this);
    book_page->setObjectName("drawerPage");
    auto* book_scroll = new QScrollArea(this);
    book_scroll->setWidgetResizable(true);
    book_scroll->setFrameShape(QFrame::NoFrame);
    book_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* book_host = new QWidget(book_scroll);
    auto* book_form = new QVBoxLayout(book_host);
    book_form->setContentsMargins(16, 18, 12, 16);
    book_form->setSpacing(8);
    auto* book_heading = new QLabel("Book");
    book_heading->setFont(heading_font);
    auto* book_blurb = new QLabel("Title, copyright, and contents travel with the book when you export it.");
    book_blurb->setWordWrap(true);
    book_blurb->setObjectName("hint");
    book_form->addWidget(book_heading);
    book_form->addWidget(book_blurb);
    const auto add_field = [&](const QString& label, QLineEdit*& slot, const QString& placeholder) {
        auto* name = new QLabel(label);
        name->setObjectName("hint");
        slot = new QLineEdit(book_host);
        slot->setPlaceholderText(placeholder);
        book_form->addWidget(name);
        book_form->addWidget(slot);
        connect(slot, &QLineEdit::textEdited, this, [this] { detailsEdited(); });
    };
    add_field("Title", detail_title_, "Book title");
    add_field("Subtitle", detail_subtitle_, "Optional");
    add_field("Author", detail_author_, "Name on the copyright page");
    add_field("Edition", detail_edition_, "First edition");
    add_field("Year", detail_year_, "2026");
    add_field("Publisher", detail_publisher_, "Optional");
    add_field("EPUB ISBN", detail_epub_, {});
    add_field("Paperback ISBN", detail_paper_, {});
    add_field("PDF ISBN", detail_pdf_, {});
    add_field("Kindle ISBN", detail_kindle_, {});
    add_field("Hardcover ISBN", detail_hard_, {});
    auto* extra_label = new QLabel("Additional clause");
    extra_label->setObjectName("hint");
    detail_extra_ = new QPlainTextEdit(book_host);
    detail_extra_->setPlaceholderText("A sentence of your own, if you need one");
    detail_extra_->setFixedHeight(72);
    book_form->addWidget(extra_label);
    book_form->addWidget(detail_extra_);
    connect(detail_extra_, &QPlainTextEdit::textChanged, this, [this] { detailsEdited(); });
    const auto add_check = [&](const QString& label, QCheckBox*& slot) {
        slot = new DeskCheck(label, book_host);
        book_form->addWidget(slot);
        connect(slot, &QCheckBox::toggled, this, [this](bool) { detailsEdited(); });
    };
    add_check("All rights reserved", detail_rights_);
    add_check("This book is fiction", detail_fiction_);
    add_check("Assert moral rights", detail_moral_);
    add_check("External links", detail_external_);
    add_check("Trademark designations", detail_marks_);
    auto* matter_row = new QHBoxLayout;
    auto* copyright_button = new QPushButton("Copyright");
    auto* contents_button = new QPushButton("Contents");
    copyright_button->setCursor(Qt::PointingHandCursor);
    contents_button->setCursor(Qt::PointingHandCursor);
    quiet_buttons_.push_back(copyright_button);
    quiet_buttons_.push_back(contents_button);
    matter_row->addWidget(copyright_button);
    matter_row->addWidget(contents_button);
    book_form->addLayout(matter_row);
    cover_button_ = new QPushButton("Choose cover");
    auto* export_button = new QPushButton("Export book");
    export_button->setObjectName("primaryButton");
    cover_button_->setCursor(Qt::PointingHandCursor);
    export_button->setCursor(Qt::PointingHandCursor);
    quiet_buttons_.push_back(cover_button_);
    book_form->addWidget(cover_button_);
    book_form->addWidget(export_button);
    book_form->addStretch();
    book_scroll->setWidget(book_host);

    library_ = new SideDrawer(DrawerSide::Left, 300, this);
    library_->addPage("Chapters", chapters);
    library_->addPage("Notes", outline);
    library_->addPage("Book", book_scroll);
    library_->openPage(0);

    drawer_ = new SideDrawer(DrawerSide::Right, 320, this);
    drawer_->addPage("Suggestions", suggestions);
    drawer_->openPage(0);

    matter_ = new QWidget(this);
    auto* matter_layout = new QVBoxLayout(matter_);
    matter_layout->setContentsMargins(28, 16, 18, 16);
    matter_layout->setSpacing(8);
    auto* matter_bar = new QHBoxLayout;
    matter_heading_ = new QLabel("Copyright");
    QFont matter_font = matter_heading_->font();
    matter_font.setPointSize(16);
    matter_font.setBold(true);
    matter_heading_->setFont(matter_font);
    matter_heading_->setAlignment(Qt::AlignCenter);
    auto* back_chapter = new QPushButton("Back to the chapter");
    back_chapter->setCursor(Qt::PointingHandCursor);
    quiet_buttons_.push_back(back_chapter);
    matter_bar->addWidget(back_chapter);
    matter_bar->addStretch();
    matter_layout->addLayout(matter_bar);
    matter_layout->addWidget(matter_heading_);
    matter_view_ = new EditorView(matter_);
    matter_view_->setReadOnly(true);
    matter_layout->addWidget(matter_view_, 1);
    center_stack_ = new QStackedWidget(this);
    export_sheet_ = new ExportSheet(this);
    center_stack_->addWidget(canvas_);
    center_stack_->addWidget(matter_);
    center_stack_->addWidget(export_sheet_);

    shelf_ = new QWidget(this);
    auto* shelf_layout = new QVBoxLayout(shelf_);
    shelf_layout->setContentsMargins(36, 28, 36, 28);
    shelf_layout->setSpacing(16);
    auto* shelf_bar = new QHBoxLayout;
    auto* shelf_heading = new QLabel("Shelf");
    QFont shelf_font = shelf_heading->font();
    shelf_font.setPointSize(22);
    shelf_font.setBold(true);
    shelf_heading->setFont(shelf_font);
    auto* new_book_button = new QPushButton("New book");
    auto* open_book_button = new QPushButton("Open book");
    new_book_button->setObjectName("primaryButton");
    new_book_button->setCursor(Qt::PointingHandCursor);
    open_book_button->setCursor(Qt::PointingHandCursor);
    shelf_bar->addWidget(shelf_heading);
    shelf_bar->addStretch();
    shelf_bar->addWidget(open_book_button);
    shelf_bar->addWidget(new_book_button);
    shelf_layout->addLayout(shelf_bar);
    shelf_scroll_ = new QScrollArea(shelf_);
    shelf_scroll_->setWidgetResizable(true);
    shelf_scroll_->setFrameShape(QFrame::NoFrame);
    shelf_scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* shelf_host = new QWidget(shelf_scroll_);
    shelf_cards_ = new QGridLayout(shelf_host);
    shelf_cards_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    shelf_cards_->setContentsMargins(4, 4, 4, 12);
    shelf_cards_->setHorizontalSpacing(32);
    shelf_cards_->setVerticalSpacing(36);
    shelf_scroll_->setWidget(shelf_host);
    shelf_layout->addWidget(shelf_scroll_, 1);

    desk_ = new QWidget(this);
    auto* desk_layout = new QHBoxLayout(desk_);
    desk_layout->setContentsMargins(0, 0, 0, 0);
    desk_layout->setSpacing(0);
    desk_layout->addWidget(library_);
    desk_layout->addWidget(center_stack_, 1);
    desk_layout->addWidget(drawer_);
    root_stack_ = new QStackedWidget(this);
    root_stack_->addWidget(shelf_);
    root_stack_->addWidget(desk_);
    setCentralWidget(root_stack_);
    connect(new_book_button, &QPushButton::clicked, this, [this] { newBook(); });
    connect(open_book_button, &QPushButton::clicked, this, [this] {
        if (!confirmDiscard()) {
            return;
        }
        const QString selected = QFileDialog::getExistingDirectory(this, "Open Book");
        if (!selected.isEmpty()) {
            openFile(selected);
        }
    });
    connect(back_chapter, &QPushButton::clicked, this, [this] { showWriting(); });
    connect(copyright_button, &QPushButton::clicked, this, [this] { showCopyright(); });
    connect(contents_button, &QPushButton::clicked, this, [this] { showContents(); });
    connect(cover_button_, &QPushButton::clicked, this, [this] { chooseCover(); });
    connect(export_button, &QPushButton::clicked, this, [this] { showExport(); });
    connect(export_sheet_, &ExportSheet::backRequested, this, [this] { showWriting(); });
    connect(export_sheet_, &ExportSheet::exportRequested, this, [this] { exportBook(); });
    connect(import_button, &QPushButton::clicked, this, [this] { importChapter(); });
    connect(new_chapter_button, &QPushButton::clicked, this, [this] { addChapter(); });
    connect(new_note_button, &QPushButton::clicked, this, [this] { addStoryNote(3); });
    connect(delete_piece_, &QPushButton::clicked, this, [this] { deleteActivePiece(); });
    connect(title_edit_, &QLineEdit::textChanged, this, [this] {
        if (loading_) {
            return;
        }
        if (Leaf* leaf = activeLeaf()) {
            leaf->title = title_edit_->text();
            book_dirty_ = true;
            editor_->document()->setModified(true);
            scheduleAutosave();
            if (active_title_label_ != nullptr) {
                const QString shown = title_edit_->text().trimmed().isEmpty() ? "Untitled" : title_edit_->text();
                active_title_label_->setText(shown);
            }
            updateTitle();
        }
    });

    debounce_ = new QTimer(this);
    debounce_->setSingleShot(true);
    debounce_->setInterval(350);
    connect(debounce_, &QTimer::timeout, this, &MainWindow::recheck);
    autosave_ = new QTimer(this);
    autosave_->setSingleShot(true);
    autosave_->setInterval(2000);
    connect(autosave_, &QTimer::timeout, this, &MainWindow::autosave);
    connect(editor_, &QTextEdit::textChanged, this, [this] {
        if (!loading_) {
            debounce_->start();
            book_dirty_ = true;
            scheduleAutosave();
            if (Leaf* leaf = activeLeaf()) {
                leaf->body = editor_->markup();
            }
        }
        updateCounts();
        updateTitle();
    });
    connect(editor_->document(), &QTextDocument::modificationChanged, this, [this](bool) { updateTitle(); });
    connect(editor_->document(), &QTextDocument::undoAvailable, this, [this](bool) { updateCommands(); });
    connect(editor_->document(), &QTextDocument::redoAvailable, this, [this](bool) { updateCommands(); });
    connect(editor_, &QTextEdit::copyAvailable, this, [this](bool) { updateCommands(); });
    connect(editor_, &QTextEdit::cursorPositionChanged, this, [this] { updateCommands(); });
    if (QClipboard* board = QGuiApplication::clipboard()) {
        connect(board, &QClipboard::dataChanged, this, [this] { updateCommands(); });
    }
    connect(notes_, &QListWidget::currentRowChanged, this, &MainWindow::showNote);
    connect(notes_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) { jumpToNote(notes_->row(item)); });
    connect(notes_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) { applyNote(notes_->row(item)); });
    connect(apply_button_, &QPushButton::clicked, this, [this] { applyNote(notes_->currentRow()); });
    connect(name_button_, &QPushButton::clicked, this, &MainWindow::addNameFromNote);

    auto* file_menu = menuBar()->addMenu("File");
    auto* edit_menu = menuBar()->addMenu("Edit");

    auto add_action = [&](QMenu* menu, const QString& text, QKeySequence shortcut, auto slot) {
        auto* action = menu->addAction(text);
        action->setShortcut(shortcut);
        connect(action, &QAction::triggered, this, [slot](bool) { slot(); });
        return action;
    };

    shelf_action_ = add_action(file_menu, "Shelf", QKeySequence(), [this] { returnToShelf(); });
    shelf_action_->setObjectName("fileShelf");
    add_action(file_menu, "New", QKeySequence::New, [this] {
        if (!confirmDiscard()) {
            return;
        }
        book_ = Book::single("Untitled", {});
        leaf_seq_ = 2;
        book_dir_.clear();
        path_.clear();
        on_note_ = false;
        active_chapter_ = 0;
        book_dirty_ = false;
        dirty_at_ = 0;
        autosave_->stop();
        clearRecovery();
        details_ = BookDetails{};
        details_.year = QString::number(QDate::currentDate().year());
        pushDetails();
        loadActive();
        showDesk();
    });
    add_action(file_menu, "Open…", QKeySequence::Open, [this] {
        if (!confirmDiscard()) {
            return;
        }
        const QString selected = QFileDialog::getOpenFileName(this, "Open", QFileInfo(path_).absolutePath(),
                                                               "Text (*.txt *.md);;All files (*)");
        if (!selected.isEmpty()) {
            openFile(selected);
        }
    });
    add_action(file_menu, "Open Book…", QKeySequence(), [this] {
        if (!confirmDiscard()) {
            return;
        }
        const QString selected = QFileDialog::getExistingDirectory(this, "Open Book", QFileInfo(path_).absolutePath());
        if (!selected.isEmpty()) {
            openFile(selected);
        }
    });
    recent_menu_ = file_menu->addMenu("Open Recent");
    refreshRecentMenu();
    save_action_ = add_action(file_menu, "Save", QKeySequence::Save, [this] { saveCurrent(); });
    save_action_->setObjectName("fileSave");
    save_as_action_ = add_action(file_menu, "Save As…", QKeySequence::SaveAs, [this] {
        if (!singleFileBook() || !book_dir_.isEmpty()) {
            const QString start = book_dir_.isEmpty() ? inkwellLibrary() : book_dir_;
            const QString selected = QFileDialog::getExistingDirectory(this, "Save Book", start);
            if (!selected.isEmpty() && saveBook(selected)) {
                rememberRecent(book_dir_);
            }
            return;
        }
        const QString selected = QFileDialog::getSaveFileName(this, "Save As", path_.isEmpty() ? "untitled.txt" : path_,
                                                               "Text (*.txt *.md);;All files (*)");
        if (!selected.isEmpty() && saveFile(selected)) {
            rememberRecent(path_);
        }
    });
    save_as_action_->setObjectName("fileSaveAs");
    file_menu->addSeparator();
    add_action(file_menu, "Quit", QKeySequence::Quit, [this] { close(); });

    undo_action_ = add_action(edit_menu, "Undo", QKeySequence::Undo, [this] { editor_->undo(); });
    redo_action_ = add_action(edit_menu, "Redo", QKeySequence(Qt::CTRL | Qt::Key_Y), [this] { editor_->redo(); });
    edit_menu->addSeparator();
    cut_action_ = add_action(edit_menu, "Cut", QKeySequence::Cut, [this] { editor_->cut(); });
    copy_action_ = add_action(edit_menu, "Copy", QKeySequence::Copy, [this] { editor_->copy(); });
    paste_action_ = add_action(edit_menu, "Paste", QKeySequence::Paste, [this] { editor_->paste(); });
    edit_menu->addSeparator();
    learn_action_ = add_action(edit_menu, "Learn Word", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_L), [this] { learnWord(true); });
    name_action_ = add_action(edit_menu, "Add Name…", QKeySequence(), [this] { addName(); });
    ignore_action_ = add_action(edit_menu, "Ignore Word", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I), [this] { learnWord(false); });
    undo_action_->setObjectName("editUndo");
    redo_action_->setObjectName("editRedo");
    cut_action_->setObjectName("editCut");
    copy_action_->setObjectName("editCopy");
    paste_action_->setObjectName("editPaste");

    auto* book_menu = menuBar()->addMenu("Book");
    details_action_ = add_action(book_menu, "Details", QKeySequence(), [this] {
        showDesk();
        library_->openPage(2);
    });
    copyright_action_ = add_action(book_menu, "Copyright", QKeySequence(), [this] { showCopyright(); });
    contents_action_ = add_action(book_menu, "Contents", QKeySequence(), [this] { showContents(); });
    cover_action_ = add_action(book_menu, "Choose Cover…", QKeySequence(), [this] { chooseCover(); });
    book_menu->addSeparator();
    export_action_ = add_action(book_menu, "Export Book…", QKeySequence(), [this] { showExport(); });
    details_action_->setObjectName("bookDetails");
    cover_action_->setObjectName("bookCover");
    export_action_->setObjectName("bookExport");

    edit_menu->addSeparator();
    next_note_action_ = add_action(edit_menu, "Next Note", QKeySequence(Qt::Key_F2), [this] {
        drawer_->openPage(0);
        stepNote(1);
    });
    prev_note_action_ = add_action(edit_menu, "Previous Note", QKeySequence(Qt::Key_F3), [this] {
        drawer_->openPage(0);
        stepNote(-1);
    });
    apply_action_ = add_action(edit_menu, "Apply Suggestion", QKeySequence(Qt::Key_F4), [this] {
        drawer_->openPage(0);
        if (debounce_->isActive()) {
            recheck();
        }
        const int selected = notes_->currentRow() >= 0 ? notes_->currentRow() : noteAtCursor();
        applyNote(selected);
    });

    auto* view_menu = menuBar()->addMenu("View");
    auto* theme_menu = view_menu->addMenu("Theme");
    auto* theme_actions = new QActionGroup(this);
    theme_actions->setExclusive(true);
    const ThemeId theme_ids[] = {ThemeId::Classic, ThemeId::Modern, ThemeId::Bubbly, ThemeId::Industrial};
    const char* theme_names[] = {"Classic", "Modern", "Bubbly", "Industrial"};
    for (int i = 0; i < 4; ++i) {
        theme_actions_[i] = theme_menu->addAction(theme_names[i]);
        theme_actions_[i]->setCheckable(true);
        theme_actions->addAction(theme_actions_[i]);
        const ThemeId id = theme_ids[i];
        connect(theme_actions_[i], &QAction::triggered, this, [this, id] { setTheme(id); });
    }
    view_menu->addSeparator();
    light_action_ = view_menu->addAction("Light");
    dark_action_ = view_menu->addAction("Dark");
    light_action_->setCheckable(true);
    dark_action_->setCheckable(true);
    connect(light_action_, &QAction::triggered, this, [this] { setDarkMode(false); });
    connect(dark_action_, &QAction::triggered, this, [this] { setDarkMode(true); });
    view_menu->addSeparator();
    add_action(view_menu, "Page…", QKeySequence(Qt::CTRL | Qt::Key_Comma), [this] { editPage(); });
    auto* theme_switch = new QWidget(menuBar());
    auto* theme_row = new QHBoxLayout(theme_switch);
    theme_row->setContentsMargins(0, 0, 12, 0);
    theme_row->setSpacing(0);
    light_switch_ = new QPushButton("Light", theme_switch);
    dark_switch_ = new QPushButton("Dark", theme_switch);
    auto* theme_group = new QButtonGroup(this);
    theme_group->setExclusive(true);
    for (QPushButton* button : {light_switch_, dark_switch_}) {
        button->setObjectName("themeSwitch");
        button->setCursor(Qt::PointingHandCursor);
        button->setCheckable(true);
        button->setFocusPolicy(Qt::NoFocus);
        theme_row->addWidget(button);
    }
    theme_group->addButton(light_switch_, 0);
    theme_group->addButton(dark_switch_, 1);
    connect(theme_group, &QButtonGroup::idClicked, this, [this](int id) { setDarkMode(id == 1); });
    menuBar()->setCornerWidget(theme_switch, Qt::TopRightCorner);

    auto* help_menu = menuBar()->addMenu("Help");
    help_menu->addAction("About Inkwell", this, [this] {
        const QString source = dictionary_->ready() ? QString::fromStdString(dictionary_->source()) : dict_error_;
        QMessageBox about(QMessageBox::Information, "Inkwell",
                          "Inkwell 0.1.0\n\nA writing desk with spelling and grammar.\n\n" + source, QMessageBox::Ok, this);
        about.setOption(QMessageBox::Option::DontUseNativeDialog);
        about.setTextInteractionFlags(Qt::TextBrowserInteraction);
        about.exec();
    });

    counts_ = new QLabel(this);
    statusBar()->addPermanentWidget(counts_);
    QIcon icon = QIcon::fromTheme(QStringLiteral("org.inkwell.Inkwell"));
    if (icon.isNull()) {
        icon = QIcon(QStringLiteral(":/inkwell/org.inkwell.Inkwell.png"));
    }
    setWindowIcon(icon);
    setWindowTitle("Inkwell");
    applyTheme();
    book_ = Book::single("Untitled", {});
    leaf_seq_ = 2;
    details_.year = QString::number(QDate::currentDate().year());
    pushDetails();
    loadActive();
    showShelf();
}

bool MainWindow::openFile(const QString& path) {
    const QFileInfo info(path);
    if (info.isDir() || info.fileName() == "inkwell.book") {
        return loadBook(info.isDir() ? info.absoluteFilePath() : info.absolutePath());
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        statusBar()->showMessage("Could not open " + path);
        return false;
    }
    QString text = QString::fromUtf8(file.readAll());
    text.replace("\r\n", "\n");
    text.replace('\r', '\n');
    if (text.endsWith('\n')) {
        text.chop(1);
    }
    book_ = Book::single(info.completeBaseName(), text);
    leaf_seq_ = 2;
    book_dir_.clear();
    path_ = path;
    on_note_ = false;
    active_chapter_ = 0;
    book_dirty_ = false;
    dirty_at_ = 0;
    autosave_->stop();
    details_ = BookDetails{};
    details_.title = info.completeBaseName();
    details_.edition = "First edition";
    details_.year = QString::number(QDate::currentDate().year());
    details_.rights = true;
    pushDetails();
    loadActive();
    editor_->moveCursor(QTextCursor::Start);
    clearRecovery();
    rememberRecent(path_);
    showDesk();
    statusBar()->showMessage("Opened " + info.fileName());
    return true;
}

bool MainWindow::saveFile(const QString& path, bool quiet) {
    commitLeaf();
    QString body = editor_->markup();
    if (!body.endsWith('\n')) {
        body += '\n';
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(body.toUtf8()) < 0 || !file.commit()) {
        statusBar()->showMessage("Could not save " + path);
        dirty_at_ = QDateTime::currentMSecsSinceEpoch();
        return false;
    }
    path_ = path;
    finishSave(quiet, QStringLiteral("Saved"));
    return true;
}

void MainWindow::setDocumentText(const QString& text) {
    book_ = Book::single("Untitled", text);
    leaf_seq_ = 2;
    book_dir_.clear();
    path_.clear();
    on_note_ = false;
    active_chapter_ = 0;
    book_dirty_ = false;
    dirty_at_ = 0;
    if (autosave_ != nullptr) {
        autosave_->stop();
    }
    loadActive();
    showDesk();
}

QString MainWindow::documentText() const {
    return editor_->proseText();
}

int MainWindow::noteCount() const {
    return items_.size();
}

QString MainWindow::noteSummary(int index) const {
    if (index < 0 || index >= items_.size()) {
        return {};
    }
    const Note& note = items_[index];
    QString summary = QString::number(note.line) + " " + kind_label(note.kind) + " " + note.message;
    if (!note.replacements.isEmpty()) {
        summary += " → " + note.replacements.front();
    }
    return summary;
}

bool MainWindow::applyNote(int index) {
    if (debounce_->isActive()) {
        recheck();
    }
    if (index < 0 || index >= items_.size()) {
        statusBar()->showMessage("No note");
        return false;
    }
    const Note note = items_[index];
    if (note.kind == 3) {
        const QString word = editor_->toPlainText().mid(note.start, note.end - note.start).trimmed();
        return keepName(word);
    }
    if (note.replacements.isEmpty()) {
        statusBar()->showMessage("No suggestion for that note");
        return false;
    }
    QTextCursor cursor(editor_->document());
    cursor.beginEditBlock();
    cursor.setPosition(note.start);
    cursor.setPosition(note.end, QTextCursor::KeepAnchor);
    cursor.insertText(note.replacements.front());
    cursor.endEditBlock();
    recheck();
    statusBar()->showMessage("Applied");
    return true;
}

void MainWindow::recheck() {
    debounce_->stop();
    if (editor_->isSourceMode()) {
        editor_->setUnderlines({});
        notes_->clear();
        items_.clear();
        if (suggestion_blurb_ != nullptr) {
            suggestion_blurb_->setText("Suggestions return when you leave markup.");
        }
        showNote(-1);
        updateCounts();
        return;
    }
    const TextMap mapped = map_text(editor_->toPlainText());
    std::vector<Issue> issues = analyze_document(mapped.utf8, *dictionary_);
    for (Issue& issue : issues) {
        if (issue.kind == IssueKind::Spelling && issue.end >= issue.begin && issue.end <= mapped.utf8.size()) {
            issue.replacements = dictionary_->suggest(mapped.utf8.substr(issue.begin, issue.end - issue.begin), 5, false);
        }
    }

    notes_->blockSignals(true);
    notes_->clear();
    items_.clear();
    QVector<Underline> marks;
    for (const Issue& issue : issues) {
        Note note;
        note.start = q_at(mapped, issue.begin);
        note.end = q_at(mapped, issue.end);
        note.kind = kind_code(issue.kind);
        int column = 1;
        locate_offset(mapped.utf8, issue.begin, note.line, column);
        const std::string quoted = snippet(mapped.utf8, issue.begin, issue.end, 48);
        note.message = QString::fromUtf8(quoted.data(), static_cast<qsizetype>(quoted.size()));
        if (!issue.message.empty()) {
            note.message += " — " + QString::fromStdString(issue.message);
        }
        for (const std::string& replacement : issue.replacements) {
            note.replacements.push_back(QString::fromStdString(replacement));
        }
        items_.push_back(note);
        marks.push_back(Underline{note.start, note.end, note.kind});
        const QString excerpt = QString::fromUtf8(quoted.data(), static_cast<qsizetype>(quoted.size()));
        QString label = kind_label(note.kind) + "   " + excerpt;
        if (!note.replacements.isEmpty()) {
            label += "  →  " + note.replacements.front();
        }
        notes_->addItem(label);
    }
    editor_->setUnderlines(marks);
    notes_->blockSignals(false);

    int selected = noteAtCursor();
    if (selected < 0 && !items_.isEmpty()) {
        selected = 0;
    }
    if (selected >= 0) {
        notes_->setCurrentRow(selected);
    }
    showNote(notes_->currentRow());
    updateCounts();
    if (suggestion_blurb_ != nullptr) {
        if (!dict_error_.isEmpty() && items_.isEmpty()) {
            suggestion_blurb_->setText(dict_error_);
        } else if (items_.isEmpty()) {
            suggestion_blurb_->setText("Nothing to mark on this page.");
        } else if (items_.size() == 1) {
            suggestion_blurb_->setText("1 note on this page.");
        } else {
            suggestion_blurb_->setText(QString::number(items_.size()) + " notes on this page.");
        }
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (book_dirty_ || (editor_ != nullptr && editor_->document()->isModified())) {
        autosave();
    }
    if (!confirmDiscard()) {
        event->ignore();
        return;
    }
    clearRecovery();
    event->accept();
}

void MainWindow::updateTitle() {
    if (root_stack_ != nullptr && shelf_ != nullptr && root_stack_->currentWidget() == shelf_) {
        setWindowTitle("Inkwell");
        return;
    }
    QString name = title_edit_ != nullptr ? title_edit_->text().trimmed() : QString();
    if (name.isEmpty()) {
        name = "Untitled";
    }
    const QString star = editor_ != nullptr && editor_->document()->isModified() ? "*" : "";
    setWindowTitle(star + name + " — Inkwell");
}

void MainWindow::updateCounts() {
    if (counts_ == nullptr || editor_ == nullptr) {
        return;
    }
    const TextMap mapped = map_text(editor_->proseText());
    const int words = static_cast<int>(scan_words(mapped.utf8).size());
    QString reading = "0 min";
    if (words > 0 && words < 200) {
        reading = "<1 min";
    } else if (words >= 200) {
        reading = QString::number((words + 199) / 200) + " min";
    }
    int spelling = 0;
    int grammar = 0;
    int style = 0;
    int names = 0;
    for (const Note& note : items_) {
        if (note.kind == 0) {
            ++spelling;
        } else if (note.kind == 1) {
            ++grammar;
        } else if (note.kind == 3) {
            ++names;
        } else {
            ++style;
        }
    }
    QString text = QString::number(words) + " words · " + reading;
    if (spelling > 0) {
        text += " · " + QString::number(spelling) + " spelling";
    }
    if (grammar > 0) {
        text += " · " + QString::number(grammar) + " grammar";
    }
    if (style > 0) {
        text += " · " + QString::number(style) + " style";
    }
    if (names > 0) {
        text += " · " + QString::number(names) + (names == 1 ? " name" : " names");
    }
    counts_->setText(text);
    if (chapter_words_ != nullptr && !on_note_) {
        chapter_words_->setText(words == 0 ? "Draft" : QString::number(words) + (words == 1 ? " word" : " words"));
    }
}

void MainWindow::showNote(int index) {
    if (apply_button_ != nullptr) {
        apply_button_->setText("Apply suggestion");
    }
    if (name_button_ != nullptr) {
        name_button_->setEnabled(false);
    }
    if (index < 0 || index >= items_.size()) {
        detail_->setText(items_.isEmpty() ? (dict_error_.isEmpty() ? "No spelling or grammar notes." : dict_error_)
                                           : QString("No note selected."));
        apply_button_->setEnabled(false);
        updateCommands();
        return;
    }
    const Note& note = items_[index];
    const QString word = editor_->toPlainText().mid(note.start, note.end - note.start);
    const bool name_like = note.kind == 3 || (note.kind == 0 && !word.isEmpty() && word.at(0).isUpper());
    if (name_button_ != nullptr) {
        name_button_->setEnabled(name_like);
    }
    QString text = kind_label(note.kind) + "\n" + note.message;
    if (note.kind == 3) {
        text += "\nIf this is a name, add it to the dictionary.";
        apply_button_->setText("Add name");
        apply_button_->setEnabled(true);
    } else if (note.replacements.isEmpty()) {
        text += "\nNo suggestion.";
        apply_button_->setEnabled(false);
    } else {
        text += "\nSuggestion: " + note.replacements.join(", ");
        if (name_like) {
            text += "\nIf this is a name, add it instead of replacing it.";
        }
        apply_button_->setEnabled(true);
    }
    detail_->setText(text);
    updateCommands();
}

void MainWindow::jumpToNote(int index) {
    if (index < 0 || index >= items_.size()) {
        return;
    }
    notes_->setCurrentRow(index);
    QTextCursor cursor = editor_->textCursor();
    cursor.setPosition(items_[index].start);
    cursor.setPosition(items_[index].end, QTextCursor::KeepAnchor);
    editor_->setTextCursor(cursor);
    editor_->ensureCursorVisible();
}

void MainWindow::stepNote(int direction) {
    if (items_.isEmpty()) {
        statusBar()->showMessage("No notes");
        return;
    }
    if (debounce_->isActive()) {
        recheck();
    }
    int index = notes_->currentRow();
    if (index < 0) {
        index = noteAtCursor();
    }
    if (index < 0) {
        index = 0;
    } else {
        index = (index + direction + items_.size()) % items_.size();
    }
    jumpToNote(index);
}

void MainWindow::learnWord(bool persist) {
    const TextMap mapped = map_text(editor_->toPlainText());
    const auto word = word_touching(mapped.utf8, byte_at(mapped, editor_->textCursor().position()));
    if (!word) {
        statusBar()->showMessage("Place the cursor on a word");
        return;
    }
    const std::string surface = mapped.utf8.substr(word->begin, word->end - word->begin);
    if (persist) {
        std::string error;
        if (!dictionary_->learn(surface, error)) {
            statusBar()->showMessage(QString::fromStdString(error));
            return;
        }
        statusBar()->showMessage("Learned " + QString::fromStdString(surface));
    } else {
        dictionary_->ignore(surface);
        statusBar()->showMessage("Ignored " + QString::fromStdString(surface) + " for this session");
    }
    recheck();
}

bool MainWindow::keepName(const QString& name) {
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        statusBar()->showMessage("Type a name");
        return false;
    }
    const QStringList parts = trimmed.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        std::string error;
        if (!dictionary_->learn(part.toStdString(), error)) {
            statusBar()->showMessage(QString::fromStdString(error));
            return false;
        }
    }
    statusBar()->showMessage("Added " + trimmed);
    recheck();
    return true;
}

void MainWindow::addName() {
    QString seed;
    const TextMap mapped = map_text(editor_->toPlainText());
    const auto word = word_touching(mapped.utf8, byte_at(mapped, editor_->textCursor().position()));
    if (word) {
        seed = QString::fromStdString(mapped.utf8.substr(word->begin, word->end - word->begin));
    }
    bool ok = false;
    const QString name = QInputDialog::getText(this, "Add name", "Name", QLineEdit::Normal, seed, &ok).trimmed();
    if (!ok) {
        return;
    }
    keepName(name);
}

void MainWindow::addNameFromNote() {
    const int index = notes_->currentRow();
    if (index < 0 || index >= items_.size()) {
        addName();
        return;
    }
    const Note& note = items_[index];
    const QString word = editor_->toPlainText().mid(note.start, note.end - note.start).trimmed();
    if (word.isEmpty()) {
        addName();
        return;
    }
    keepName(word);
}

bool MainWindow::confirmDiscard() {
    if (editor_ == nullptr || !editor_->document()->isModified()) {
        return true;
    }
    const auto choice = deskMessage(this, QMessageBox::Question, "Unsaved changes", "Save changes before closing this document?",
                                    QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (choice == QMessageBox::Cancel) {
        return false;
    }
    if (choice == QMessageBox::Discard) {
        return true;
    }
    saveCurrent();
    return editor_ != nullptr && !editor_->document()->isModified();
}

void MainWindow::showDrawerPage(int index) {
    if (index < 0) {
        drawer_->setOpen(false);
        return;
    }
    drawer_->openPage(index);
}

void MainWindow::showLibraryPage(int index) {
    if (library_ == nullptr) {
        return;
    }
    if (index < 0) {
        library_->setOpen(false);
        return;
    }
    library_->openPage(index);
}

void MainWindow::commitLeaf() {
    Leaf* leaf = activeLeaf();
    if (leaf == nullptr || loading_) {
        return;
    }
    leaf->body = editor_->markup();
    leaf->title = title_edit_->text();
}

Leaf* MainWindow::activeLeaf() {
    if (on_note_) {
        if (active_section_ < 0 || active_section_ > 3 || active_note_ < 0 || active_note_ >= book_.notes[active_section_].size()) {
            return nullptr;
        }
        return &book_.notes[active_section_][active_note_];
    }
    if (active_chapter_ < 0 || active_chapter_ >= book_.chapters.size()) {
        return nullptr;
    }
    return &book_.chapters[active_chapter_];
}

void MainWindow::loadActive() {
    showWriting();
    const Leaf* leaf = activeLeaf();
    loading_ = true;
    if (QPushButton* markup = findChild<QPushButton*>("markupButton")) {
        markup->blockSignals(true);
        markup->setChecked(false);
        markup->blockSignals(false);
    }
    editor_->setSourceMode(false);
    editor_->setResourceRoot(book_dir_);
    editor_->setMarkup(leaf != nullptr ? leaf->body : QString());
    title_edit_->setText(leaf != nullptr ? leaf->title : QString());
    loading_ = false;
    editor_->document()->setModified(book_dirty_);
    const bool note = on_note_ && leaf != nullptr;
    kicker_->setVisible(note);
    kicker_->setText(note ? QString(section_label(active_section_)).toUpper() : QString());
    editor_->setPlaceholderText(note ? "Write the note…" : "Start writing…");
    delete_piece_->setEnabled(note || book_.chapters.size() > 1);
    editor_->moveCursor(QTextCursor::Start);
    refreshLibrary();
    updateTitle();
    recheck();
    updateCommands();
}

void MainWindow::showChapter(int index) {
    if (index < 0 || index >= book_.chapters.size()) {
        return;
    }
    if (!on_note_ && index == active_chapter_) {
        library_->openPage(0);
        return;
    }
    commitLeaf();
    on_note_ = false;
    active_chapter_ = index;
    library_->openPage(0);
    loadActive();
}

void MainWindow::showStoryNote(int section, int index) {
    if (section < 0 || section > 3 || index < 0 || index >= book_.notes[section].size()) {
        return;
    }
    if (on_note_ && section == active_section_ && index == active_note_) {
        library_->openPage(1);
        return;
    }
    commitLeaf();
    on_note_ = true;
    active_section_ = section;
    active_note_ = index;
    library_->openPage(1);
    loadActive();
}

void MainWindow::clearLayout(QLayout* layout) {
    if (layout == nullptr) {
        return;
    }
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        if (QLayout* child = item->layout()) {
            clearLayout(child);
        }
        delete item;
    }
}

void MainWindow::refreshLibrary() {
    active_title_label_ = nullptr;
    chapter_words_ = nullptr;
    if (chapter_rows_ == nullptr || outline_rows_ == nullptr) {
        return;
    }
    clearLayout(chapter_rows_);
    clearLayout(outline_rows_);
    const DeskTheme theme = activeTheme();
    const QColor ink = theme.ink;
    const QColor muted = theme.muted;
    const QColor selected_bg = theme.accent;
    const QColor selected_fg = theme.accent_ink;

    auto add_row = [&](QLayout* layout, const QString& title, const QString& meta, bool selected, const std::function<void()>& pick,
                       bool track) {
        auto* row = new QWidget;
        row->setAutoFillBackground(selected);
        row->setCursor(Qt::PointingHandCursor);
        row->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        if (selected) {
            row->setStyleSheet(QStringLiteral("background: %1; border-radius: %2px;").arg(selected_bg.name()).arg(theme.radius));
        }
        auto* box = new QVBoxLayout(row);
        box->setContentsMargins(10, 8, 10, 8);
        box->setSpacing(2);
        auto* name = new QLabel(title.trimmed().isEmpty() ? "Untitled" : title);
        QFont name_font = name->font();
        name_font.setBold(true);
        name->setFont(name_font);
        QPalette row_palette = row->palette();
        row_palette.setColor(QPalette::Window, selected_bg);
        row->setPalette(row_palette);
        const QString shown = (selected ? selected_fg : ink).name();
        name->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(shown));
        box->addWidget(name);
        QLabel* detail = nullptr;
        if (!meta.isEmpty()) {
            detail = new QLabel(meta);
            detail->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg((selected ? selected_fg : muted).name()));
            box->addWidget(detail);
        }
        auto* catcher = new ClickCatcher(pick, row);
        row->installEventFilter(catcher);
        name->installEventFilter(catcher);
        if (detail != nullptr) {
            detail->installEventFilter(catcher);
        }
        layout->addWidget(row);
        if (track) {
            active_title_label_ = name;
            chapter_words_ = detail;
        }
    };

    for (int i = 0; i < book_.chapters.size(); ++i) {
        const Leaf& leaf = book_.chapters[i];
        const QString body = !on_note_ && i == active_chapter_ ? editor_->proseText()
                                                               : QString::fromStdString(markup_plain(leaf.body.toStdString()));
        const int words = static_cast<int>(scan_words(body.toUtf8().toStdString()).size());
        const QString meta = words == 0 ? "Draft" : QString::number(words) + (words == 1 ? " word" : " words");
        add_row(chapter_rows_, leaf.title, meta, !on_note_ && i == active_chapter_, [this, i] { showChapter(i); },
                !on_note_ && i == active_chapter_);
    }
    chapter_rows_->addStretch();

    for (int section = 0; section < 4; ++section) {
        auto* block = new QWidget;
        auto* block_layout = new QVBoxLayout(block);
        block_layout->setContentsMargins(0, 0, 0, 0);
        block_layout->setSpacing(4);
        auto* header = new QHBoxLayout;
        auto* label = new QLabel(QString(section_label(section)).toUpper());
        label->setObjectName("hint");
        auto* add = new QPushButton("+");
        add->setFixedSize(26, 26);
        add->setCursor(Qt::PointingHandCursor);
        const int add_radius = theme.radius >= 12 ? 13 : theme.radius;
        add->setStyleSheet(QStringLiteral(
                                "QPushButton { background: transparent; color: %1; border: 1px solid %2; border-radius: %3px; }"
                                "QPushButton:hover { background: %4; }")
                                .arg(ink.name(), theme.edge.name())
                                .arg(add_radius)
                                .arg(theme.hover.name()));
        connect(add, &QPushButton::clicked, this, [this, section] { addStoryNote(section); });
        header->addWidget(label);
        header->addStretch();
        header->addWidget(add);
        block_layout->addLayout(header);
        if (book_.notes[section].isEmpty()) {
            auto* empty = new QLabel("None yet");
            empty->setObjectName("hint");
            block_layout->addWidget(empty);
        }
        for (int i = 0; i < book_.notes[section].size(); ++i) {
            const bool selected = on_note_ && active_section_ == section && active_note_ == i;
            add_row(block_layout, book_.notes[section][i].title, {}, selected, [this, section, i] { showStoryNote(section, i); },
                    selected);
        }
        outline_rows_->addWidget(block);
    }
    outline_rows_->addStretch();
}

void MainWindow::addChapter() {
    commitLeaf();
    book_.chapters.push_back(Leaf{freshId(), "Untitled Chapter", {}});
    on_note_ = false;
    active_chapter_ = book_.chapters.size() - 1;
    book_dirty_ = true;
    library_->openPage(0);
    loadActive();
    scheduleAutosave();
    title_edit_->setFocus();
    title_edit_->selectAll();
}

void MainWindow::importChapter() {
    const QString selected = QFileDialog::getOpenFileName(this, "Import Chapter", QFileInfo(path_).absolutePath(),
                                                           "Text (*.txt *.md);;All files (*)");
    if (selected.isEmpty()) {
        return;
    }
    QFile file(selected);
    if (!file.open(QIODevice::ReadOnly)) {
        statusBar()->showMessage("Could not import " + selected);
        return;
    }
    QString text = QString::fromUtf8(file.readAll());
    text.replace("\r\n", "\n");
    text.replace('\r', '\n');
    if (text.endsWith('\n')) {
        text.chop(1);
    }
    commitLeaf();
    book_.chapters.push_back(Leaf{freshId(), QFileInfo(selected).completeBaseName(), text});
    on_note_ = false;
    active_chapter_ = book_.chapters.size() - 1;
    book_dirty_ = true;
    library_->openPage(0);
    loadActive();
    scheduleAutosave();
    statusBar()->showMessage("Imported " + QFileInfo(selected).fileName());
}

void MainWindow::addStoryNote(int section) {
    if (section < 0 || section > 3) {
        return;
    }
    commitLeaf();
    const QString title = QString("New ") + QString(section_label(section)).toLower();
    book_.notes[section].push_back(Leaf{freshId(), title, {}});
    on_note_ = true;
    active_section_ = section;
    active_note_ = book_.notes[section].size() - 1;
    book_dirty_ = true;
    library_->openPage(1);
    loadActive();
    scheduleAutosave();
    title_edit_->setFocus();
    title_edit_->selectAll();
}

void MainWindow::deleteActivePiece() {
    const bool note = on_note_;
    if (!note && book_.chapters.size() <= 1) {
        return;
    }
    const auto answer = deskMessage(this, QMessageBox::Question, "Delete", note ? "Delete this note?" : "Delete this chapter?",
                                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }
    if (note) {
        book_.notes[active_section_].removeAt(active_note_);
        on_note_ = false;
        active_note_ = -1;
    } else {
        book_.chapters.removeAt(active_chapter_);
        if (active_chapter_ >= book_.chapters.size()) {
            active_chapter_ = book_.chapters.size() - 1;
        }
    }
    book_dirty_ = true;
    loadActive();
    scheduleAutosave();
}

bool MainWindow::saveBook(const QString& directory, bool quiet) {
    commitLeaf();
    QString error;
    if (!book_.write(directory, error)) {
        statusBar()->showMessage(error);
        dirty_at_ = QDateTime::currentMSecsSinceEpoch();
        return false;
    }
    book_dir_ = directory;
    path_ = directory;
    QString detail_error;
    if (details_.title.trimmed().isEmpty()) {
        details_.title = QFileInfo(directory).fileName();
        pushDetails();
    }
    if (!details_.writeFile(directory, detail_error)) {
        statusBar()->showMessage(detail_error);
    }
    finishSave(quiet, QStringLiteral("Saved the book"));
    return true;
}

bool MainWindow::loadBook(const QString& directory) {
    Book loaded;
    QString error;
    if (!Book::read(directory, loaded, error)) {
        statusBar()->showMessage(error);
        return false;
    }
    book_ = std::move(loaded);
    int highest = 1;
    const auto scan = [&highest](const QVector<Leaf>& leaves) {
        for (const Leaf& leaf : leaves) {
            bool ok = false;
            const int id = leaf.id.toInt(&ok);
            if (ok) {
                highest = qMax(highest, id);
            }
        }
    };
    scan(book_.chapters);
    for (const QVector<Leaf>& notes : book_.notes) {
        scan(notes);
    }
    leaf_seq_ = highest + 1;
    book_dir_ = directory;
    path_ = directory;
    on_note_ = false;
    active_chapter_ = 0;
    book_dirty_ = false;
    dirty_at_ = 0;
    autosave_->stop();
    details_ = BookDetails::readFile(directory);
    if (details_.title.trimmed().isEmpty()) {
        details_.title = QFileInfo(directory).fileName();
    }
    if (details_.year.trimmed().isEmpty()) {
        details_.year = QString::number(QDate::currentDate().year());
    }
    pushDetails();
    loadActive();
    clearRecovery();
    rememberRecent(book_dir_);
    showDesk();
    statusBar()->showMessage("Opened " + QFileInfo(directory).fileName());
    return true;
}

bool MainWindow::singleFileBook() const {
    if (book_.chapters.size() != 1 || on_note_) {
        return false;
    }
    for (const QVector<Leaf>& notes : book_.notes) {
        if (!notes.isEmpty()) {
            return false;
        }
    }
    return true;
}

bool MainWindow::hasAutosaveTarget() const {
    if (!book_dir_.isEmpty()) {
        return true;
    }
    return singleFileBook() && !path_.isEmpty();
}

void MainWindow::scheduleAutosave() {
    if (loading_ || autosave_ == nullptr) {
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (dirty_at_ == 0) {
        dirty_at_ = now;
    }
    autosave_->start(now - dirty_at_ >= 20000 ? 0 : 2000);
}

void MainWindow::autosave() {
    if (loading_) {
        return;
    }
    if (!book_dirty_ && (editor_ == nullptr || !editor_->document()->isModified())) {
        dirty_at_ = 0;
        return;
    }
    if (!writeRecovery()) {
        dirty_at_ = QDateTime::currentMSecsSinceEpoch();
    } else if (!hasAutosaveTarget()) {
        dirty_at_ = 0;
    }
    if (!hasAutosaveTarget()) {
        return;
    }
    if (!book_dir_.isEmpty()) {
        saveBook(book_dir_, true);
        return;
    }
    saveFile(path_, true);
}

void MainWindow::finishSave(bool quiet, const QString& message) {
    book_dirty_ = false;
    dirty_at_ = 0;
    if (autosave_ != nullptr) {
        autosave_->stop();
    }
    if (editor_ != nullptr) {
        editor_->document()->setModified(false);
    }
    updateTitle();
    updateCommands();
    statusBar()->showMessage(quiet ? QStringLiteral("Autosaved") : message, quiet ? 2500 : 0);
}

bool MainWindow::writeRecovery() {
    commitLeaf();
    const QString staging = dataDir() + "/recovery.tmp";
    QDir(staging).removeRecursively();
    if (!bookHasWork(book_)) {
        clearRecovery();
        return true;
    }
    QString error;
    if (!book_.write(staging, error)) {
        QDir(staging).removeRecursively();
        return false;
    }
    QFile session(staging + "/session");
    if (!session.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QDir(staging).removeRecursively();
        return false;
    }
    const QString origin = book_dir_.isEmpty() ? path_ : QString();
    const QString text = "origin=" + origin + "\nbook=" + book_dir_ + "\nchapter=" + QString::number(active_chapter_) +
                         "\non_note=" + QString::number(on_note_ ? 1 : 0) + "\nsection=" + QString::number(active_section_) +
                         "\nnote=" + QString::number(active_note_) + "\n";
    if (session.write(text.toUtf8()) < 0) {
        session.close();
        QDir(staging).removeRecursively();
        return false;
    }
    session.close();
    QDir().mkpath(dataDir());
    QDir(recoveryPath()).removeRecursively();
    if (!QDir(dataDir()).rename("recovery.tmp", "recovery")) {
        return false;
    }
    return true;
}

void MainWindow::clearRecovery() {
    QDir(recoveryPath()).removeRecursively();
    QDir(dataDir() + "/recovery.tmp").removeRecursively();
}

QStringList MainWindow::recentPaths() const {
    QFile file(recentPath());
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QStringList items;
    for (const QString& line : QString::fromUtf8(file.readAll()).split('\n')) {
        const QString path = line.trimmed();
        if (!path.isEmpty() && !items.contains(path)) {
            items.push_back(path);
        }
    }
    return items;
}

void MainWindow::rememberRecent(const QString& path) {
    const QString absolute = QFileInfo(path).absoluteFilePath();
    if (absolute.isEmpty()) {
        return;
    }
    QStringList items = recentPaths();
    items.removeAll(absolute);
    items.prepend(absolute);
    while (items.size() > 12) {
        items.removeLast();
    }
    storeRecentList(items);
    refreshRecentMenu();
}

void MainWindow::refreshRecentMenu() {
    if (recent_menu_ == nullptr) {
        return;
    }
    recent_menu_->clear();
    const QStringList items = recentPaths();
    if (items.isEmpty()) {
        QAction* empty = recent_menu_->addAction("No recent files");
        empty->setEnabled(false);
        return;
    }
    for (const QString& path : items) {
        QAction* action = recent_menu_->addAction(recentLabel(path));
        action->setToolTip(path);
        action->setEnabled(QFileInfo::exists(path));
        connect(action, &QAction::triggered, this, [this, path](bool) {
            if (!QFileInfo::exists(path)) {
                statusBar()->showMessage("Could not open " + path);
                QStringList kept = recentPaths();
                kept.removeAll(QFileInfo(path).absoluteFilePath());
                storeRecentList(kept);
                refreshRecentMenu();
                return;
            }
            if (!confirmDiscard()) {
                return;
            }
            if (!openFile(path)) {
                QStringList kept = recentPaths();
                kept.removeAll(QFileInfo(path).absoluteFilePath());
                storeRecentList(kept);
                refreshRecentMenu();
            }
        });
    }
    recent_menu_->addSeparator();
    connect(recent_menu_->addAction("Clear recent"), &QAction::triggered, this, [this](bool) {
        QFile::remove(recentPath());
        refreshRecentMenu();
    });
}

bool MainWindow::offerRecovery() {
    const QString directory = recoveryPath();
    if (!QFileInfo::exists(directory + "/inkwell.book")) {
        return false;
    }
    Book recovered;
    QString error;
    if (!Book::read(directory, recovered, error) || !bookHasWork(recovered)) {
        clearRecovery();
        return false;
    }
    QString session_text;
    QFile session(directory + "/session");
    if (session.open(QIODevice::ReadOnly)) {
        session_text = QString::fromUtf8(session.readAll());
    }
    const QString origin = sessionValue(session_text, "origin");
    const QString saved_book = sessionValue(session_text, "book");
    QString detail;
    if (!recovered.chapters.isEmpty() && !recovered.chapters.front().title.trimmed().isEmpty()) {
        detail = recovered.chapters.front().title.trimmed();
    }
    const QString where = !origin.isEmpty() ? origin : saved_book;
    if (!where.isEmpty()) {
        detail = detail.isEmpty() ? where : detail + "\n" + where;
    }
    QMessageBox box(this);
    box.setOption(QMessageBox::Option::DontUseNativeDialog);
    box.setWindowTitle("Recover writing");
    box.setText("Inkwell closed before this writing was saved. Recover it?");
    if (!detail.isEmpty()) {
        box.setInformativeText(detail);
    }
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    if (QAbstractButton* recover = box.button(QMessageBox::Yes)) {
        recover->setText("Recover");
    }
    if (QAbstractButton* discard = box.button(QMessageBox::No)) {
        discard->setText("Discard");
    }
    box.setDefaultButton(QMessageBox::Yes);
    if (box.exec() != QMessageBox::Yes) {
        clearRecovery();
        return false;
    }
    book_ = std::move(recovered);
    book_dir_ = saved_book;
    path_ = book_dir_.isEmpty() ? origin : book_dir_;
    on_note_ = sessionValue(session_text, "on_note") == "1";
    bool ok = false;
    const int chapter = sessionValue(session_text, "chapter").toInt(&ok);
    active_chapter_ = ok ? chapter : 0;
    if (active_chapter_ < 0 || active_chapter_ >= book_.chapters.size()) {
        active_chapter_ = 0;
        on_note_ = false;
    }
    const int section = sessionValue(session_text, "section").toInt(&ok);
    active_section_ = ok ? section : 0;
    if (active_section_ < 0 || active_section_ > 3) {
        active_section_ = 0;
        on_note_ = false;
    }
    const int note = sessionValue(session_text, "note").toInt(&ok);
    active_note_ = ok ? note : -1;
    if (on_note_ && (active_note_ < 0 || active_note_ >= book_.notes[active_section_].size())) {
        on_note_ = false;
    }
    int highest = 1;
    const auto scan = [&highest](const QVector<Leaf>& leaves) {
        for (const Leaf& leaf : leaves) {
            bool numeric = false;
            const int id = leaf.id.toInt(&numeric);
            if (numeric) {
                highest = qMax(highest, id);
            }
        }
    };
    scan(book_.chapters);
    for (const QVector<Leaf>& notes : book_.notes) {
        scan(notes);
    }
    leaf_seq_ = highest + 1;
    book_dirty_ = true;
    dirty_at_ = 0;
    if (autosave_ != nullptr) {
        autosave_->stop();
    }
    loadActive();
    showDesk();
    if (!path_.isEmpty()) {
        rememberRecent(path_);
    }
    statusBar()->showMessage("Recovered writing that had not been saved.");
    return true;
}

void MainWindow::updateCommands() {
    const bool book = root_stack_ != nullptr && desk_ != nullptr && root_stack_->currentWidget() == desk_;
    const bool writing = book && center_stack_ != nullptr && canvas_ != nullptr && center_stack_->currentWidget() == canvas_;
    const bool saved = book && !book_dir_.isEmpty();
    const auto enable = [](QAction* action, bool on) {
        if (action != nullptr) {
            action->setEnabled(on);
        }
    };
    enable(shelf_action_, book);
    enable(save_action_, book);
    enable(save_as_action_, book);
    enable(details_action_, book);
    enable(copyright_action_, book);
    enable(contents_action_, book);
    enable(export_action_, book);
    enable(cover_action_, saved);
    if (cover_button_ != nullptr) {
        cover_button_->setEnabled(saved);
    }
    const bool undo = writing && editor_ != nullptr && editor_->document()->isUndoAvailable();
    const bool redo = writing && editor_ != nullptr && editor_->document()->isRedoAvailable();
    const bool selection = writing && editor_ != nullptr && editor_->textCursor().hasSelection();
    bool on_word = false;
    if (writing && editor_ != nullptr) {
        const TextMap mapped = map_text(editor_->toPlainText());
        on_word = static_cast<bool>(word_touching(mapped.utf8, byte_at(mapped, editor_->textCursor().position())));
    }
    bool can_paste = false;
    if (writing) {
        const QMimeData* mime = QGuiApplication::clipboard() != nullptr ? QGuiApplication::clipboard()->mimeData() : nullptr;
        can_paste = mime != nullptr && mime->hasText() && !mime->text().isEmpty();
    }
    enable(undo_action_, undo);
    enable(redo_action_, redo);
    enable(cut_action_, selection);
    enable(copy_action_, selection);
    enable(paste_action_, can_paste);
    enable(learn_action_, on_word);
    enable(ignore_action_, on_word);
    enable(name_action_, writing);
    enable(next_note_action_, writing && !items_.isEmpty());
    enable(prev_note_action_, writing && !items_.isEmpty());
    enable(apply_action_, writing && apply_button_ != nullptr && apply_button_->isEnabled());
}

void MainWindow::showWriting() {
    if (center_stack_ != nullptr && canvas_ != nullptr) {
        center_stack_->setCurrentWidget(canvas_);
    }
}

void MainWindow::showDesk() {
    showWriting();
    if (root_stack_ != nullptr && desk_ != nullptr) {
        root_stack_->setCurrentWidget(desk_);
    }
    updateTitle();
    updateCommands();
}

void MainWindow::showShelf() {
    refreshShelf();
    if (root_stack_ != nullptr && shelf_ != nullptr) {
        root_stack_->setCurrentWidget(shelf_);
    }
    setWindowTitle("Inkwell");
    updateCommands();
}

void MainWindow::closeOpenBook() {
    if (autosave_ != nullptr) {
        autosave_->stop();
    }
    book_ = Book::single("Untitled", {});
    leaf_seq_ = 2;
    book_dir_.clear();
    path_.clear();
    on_note_ = false;
    active_chapter_ = 0;
    book_dirty_ = false;
    dirty_at_ = 0;
    details_ = BookDetails{};
    details_.year = QString::number(QDate::currentDate().year());
    pushDetails();
    clearRecovery();
    loadActive();
}

void MainWindow::returnToShelf() {
    if (root_stack_ != nullptr && shelf_ != nullptr && root_stack_->currentWidget() == shelf_) {
        showShelf();
        return;
    }
    commitLeaf();
    const bool dirty = book_dirty_ || (editor_ != nullptr && editor_->document()->isModified());
    const bool open = dirty || bookHasWork(book_) || !book_dir_.isEmpty() || !path_.isEmpty();
    if (open) {
        QMessageBox box(this);
        box.setOption(QMessageBox::Option::DontUseNativeDialog);
        box.setIcon(QMessageBox::Question);
        box.setWindowTitle("Close book");
        box.setText("Save this book before returning to the shelf?");
        box.setStandardButtons(QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        box.setDefaultButton(QMessageBox::Save);
        if (QAbstractButton* discard = box.button(QMessageBox::Discard)) {
            discard->setText("Don't save");
        }
        const int choice = box.exec();
        if (choice == QMessageBox::Cancel || choice == QMessageBox::NoButton) {
            return;
        }
        if (choice == QMessageBox::Save) {
            saveCurrent();
            if (book_dirty_ || (editor_ != nullptr && editor_->document()->isModified())) {
                return;
            }
        }
    }
    closeOpenBook();
    showShelf();
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    if (refreshing_shelf_ || shelf_scroll_ == nullptr || root_stack_ == nullptr || root_stack_->currentWidget() != shelf_) {
        return;
    }
    const ShelfMeasure measure = measureShelf(shelfViewport());
    if (measure.columns != shelf_columns_ || qAbs(measure.cover_width - shelf_cover_width_) >= 16) {
        refreshShelf();
    }
}

int MainWindow::shelfViewport() const {
    const int scroll_width = shelf_scroll_ != nullptr ? shelf_scroll_->viewport()->width() : 0;
    return qMax(scroll_width, qMax(0, width() - 72));
}

void MainWindow::refreshShelf() {
    if (shelf_cards_ == nullptr || refreshing_shelf_) {
        return;
    }
    refreshing_shelf_ = true;
    clearLayout(shelf_cards_);
    const DeskTheme theme = activeTheme();
    const bool dark = theme.dark;
    const QString ink = theme.ink.name();
    const QString muted = theme.muted.name();
    const QColor hover = theme.hover;
    const int viewport = shelfViewport();
    const ShelfMeasure measure = measureShelf(viewport);
    shelf_columns_ = measure.columns;
    shelf_cover_width_ = measure.cover_width;
    for (int column = 0; column < 8; ++column) {
        shelf_cards_->setColumnStretch(column, 0);
    }
    shelf_cards_->setColumnStretch(measure.columns, 1);
    const qreal pixel_ratio = shelf_scroll_ != nullptr ? shelf_scroll_->devicePixelRatioF() : devicePixelRatioF();
    QStringList paths = recentPaths();
    if (!book_dir_.isEmpty() && !paths.contains(book_dir_)) {
        paths.prepend(book_dir_);
    }
    int shown = 0;
    for (const QString& path : paths) {
        const QFileInfo info(path);
        if (!info.exists()) {
            continue;
        }
        const bool folder = info.isDir() || info.fileName() == "inkwell.book";
        const QString directory = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
        QString title = folder ? QFileInfo(directory).fileName() : info.completeBaseName();
        QString subtitle;
        int words = 0;
        QString cover;
        if (folder && QFileInfo::exists(QDir(directory).filePath("inkwell.book"))) {
            const BookDetails stored = BookDetails::readFile(directory);
            if (!stored.title.trimmed().isEmpty()) {
                title = stored.title.trimmed();
            }
            subtitle = stored.subtitle.trimmed();
            words = book_word_count(directory);
            cover = cover_file(directory);
        }
        const QSize cover_size(measure.cover_width, measure.cover_width * 3 / 2);
        const QString meta = words > 0 ? QLocale().toString(words) + QStringLiteral(" words")
                                       : (folder ? QString() : QStringLiteral("Manuscript"));
        auto* card = new ShelfCard(paintedCover(title, dark, cover, cover_size, pixel_ratio), title, subtitle, meta, ink, muted, hover);
        const QString open_path = folder && QFileInfo::exists(QDir(directory).filePath("inkwell.book")) ? directory : path;
        connect(card, &QAbstractButton::clicked, this, [this, open_path] {
            if (!confirmDiscard()) {
                return;
            }
            openFile(open_path);
        });
        shelf_cards_->addWidget(card, shown / measure.columns, shown % measure.columns, Qt::AlignTop);
        ++shown;
    }
    if (shown == 0) {
        auto* empty = new QLabel("Books you create or open will stand here.");
        empty->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(muted));
        shelf_cards_->addWidget(empty, 0, 0);
    }
    refreshing_shelf_ = false;
}

void MainWindow::newBook() {
    if (!confirmDiscard()) {
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle("New book");
    dialog.setMinimumWidth(460);
    auto* form = new QFormLayout(&dialog);
    auto* title = new QLineEdit(&dialog);
    auto* subtitle = new QLineEdit(&dialog);
    title->setPlaceholderText("Book title");
    subtitle->setPlaceholderText("Optional");
    form->addRow("Title", title);
    form->addRow("Subtitle", subtitle);
    auto* place = new BookFolderField(&dialog);
    place->setTitle(title->text());
    connect(title, &QLineEdit::textChanged, &dialog, [place](const QString& text) { place->setTitle(text); });
    form->addRow(place);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText("Create");
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const QString book_title = title->text().trimmed().isEmpty() ? QStringLiteral("Untitled") : title->text().trimmed();
    const QString folder = place->folder();
    if (QFileInfo::exists(QDir(folder).filePath("inkwell.book"))) {
        statusBar()->showMessage("That folder already holds a book.");
        return;
    }
    book_ = Book::single("Chapter 1", {});
    leaf_seq_ = 2;
    book_dir_.clear();
    path_.clear();
    on_note_ = false;
    active_chapter_ = 0;
    book_dirty_ = true;
    details_ = BookDetails{};
    details_.title = book_title;
    details_.subtitle = subtitle->text().trimmed();
    details_.edition = "First edition";
    details_.year = QString::number(QDate::currentDate().year());
    details_.rights = true;
    pushDetails();
    if (!saveBook(folder)) {
        return;
    }
    rememberRecent(book_dir_);
    loadActive();
    showDesk();
    library_->openPage(2);
    statusBar()->showMessage("Created " + book_title);
}

void MainWindow::showCopyright() {
    pullDetails();
    showDesk();
    if (matter_heading_ != nullptr) {
        matter_heading_->setText("Copyright");
    }
    if (matter_view_ != nullptr) {
        matter_view_->setDark(dark_);
        matter_view_->setMatterText(details_.copyrightPage());
    }
    if (center_stack_ != nullptr && matter_ != nullptr) {
        center_stack_->setCurrentWidget(matter_);
    }
    updateCommands();
}

void MainWindow::showContents() {
    pullDetails();
    showDesk();
    if (matter_heading_ != nullptr) {
        matter_heading_->setText("Contents");
    }
    if (matter_view_ != nullptr) {
        matter_view_->setDark(dark_);
        matter_view_->setMatterText(BookDetails::contentsPage(chapterTitles()));
    }
    if (center_stack_ != nullptr && matter_ != nullptr) {
        center_stack_->setCurrentWidget(matter_);
    }
    updateCommands();
}

void MainWindow::chooseCover() {
    if (book_dir_.isEmpty()) {
        statusBar()->showMessage("Save the book before choosing a cover.");
        return;
    }
    const QString selected = QFileDialog::getOpenFileName(this, "Cover", book_dir_, "Images (*.png *.jpg *.jpeg *.webp)");
    if (selected.isEmpty()) {
        return;
    }
    const QImage image(selected);
    if (image.isNull()) {
        statusBar()->showMessage("Could not read that image.");
        return;
    }
    const QDir folder(book_dir_);
    for (const char* name : {"cover.png", "cover.jpg", "cover.jpeg", "cover.webp"}) {
        QFile::remove(folder.filePath(QString::fromLatin1(name)));
    }
    if (!image.save(folder.filePath("cover.png"))) {
        statusBar()->showMessage("Could not save the cover.");
        return;
    }
    statusBar()->showMessage("Saved the cover");
}

void MainWindow::showExport() {
    commitLeaf();
    pullDetails();
    if (root_stack_ != nullptr && desk_ != nullptr) {
        root_stack_->setCurrentWidget(desk_);
    }
    if (export_sheet_ != nullptr) {
        QStringList titles;
        QVector<int> words;
        for (const Leaf& leaf : book_.chapters) {
            titles.push_back(leaf.title);
            words.push_back(static_cast<int>(scan_words(markup_plain(leaf.body.toStdString())).size()));
        }
        export_sheet_->setChapters(titles, words, !book_dir_.isEmpty() && !cover_file(book_dir_).isEmpty());
        export_sheet_->setDark(dark_);
        if (center_stack_ != nullptr) {
            center_stack_->setCurrentWidget(export_sheet_);
        }
    }
    updateTitle();
    updateCommands();
}

void MainWindow::exportBook() {
    if (export_sheet_ == nullptr) {
        return;
    }
    commitLeaf();
    pullDetails();
    const ExportFormat format = export_sheet_->format();
    ExportRequest request;
    export_sheet_->fill(request);
    for (int index : export_sheet_->checkedChapters()) {
        if (index >= 0 && index < book_.chapters.size()) {
            request.chapters.push_back(book_.chapters.at(index));
        }
    }
    const bool anything = !request.chapters.isEmpty() || request.include_cover || request.include_title || request.include_copyright ||
                          request.include_contents;
    if (!anything) {
        statusBar()->showMessage("Choose a part of the book to export.");
        return;
    }
    request.details = details_;
    request.page = page_;
    request.resource_root = book_dir_;
    if (request.include_cover && !book_dir_.isEmpty()) {
        request.cover_path = cover_file(book_dir_);
    }
    const QString base = details_.title.trimmed().isEmpty() ? QStringLiteral("Manuscript") : details_.title.trimmed();
    QString extension = ".pdf";
    QString filter = "PDF (*.pdf)";
    QString kind = "PDF";
    if (format == ExportFormat::Epub) {
        extension = ".epub";
        filter = "EPUB (*.epub)";
        kind = "EPUB";
    } else if (format == ExportFormat::Docx) {
        extension = ".docx";
        filter = "Word (*.docx)";
        kind = "Word document";
    } else if (format == ExportFormat::Text) {
        extension = ".txt";
        filter = "Text (*.txt)";
        kind = "text";
    }
    QString path = QFileDialog::getSaveFileName(this, "Export Book", base + extension, filter);
    if (path.isEmpty()) {
        return;
    }
    if (!path.endsWith(extension, Qt::CaseInsensitive)) {
        path += extension;
    }
    QString error;
    if (!export_book(request, path, format, error)) {
        statusBar()->showMessage(error.isEmpty() ? "Could not export " + path : error);
        return;
    }
    statusBar()->showMessage("Exported the " + kind);
}

QStringList MainWindow::chapterTitles() const {
    QStringList titles;
    for (const Leaf& leaf : book_.chapters) {
        titles.push_back(leaf.title);
    }
    return titles;
}

void MainWindow::pullDetails() {
    if (detail_title_ == nullptr) {
        return;
    }
    details_.title = detail_title_->text();
    details_.subtitle = detail_subtitle_->text();
    details_.author = detail_author_->text();
    details_.edition = detail_edition_->text();
    details_.year = detail_year_->text();
    details_.publisher = detail_publisher_->text();
    details_.isbn_epub = detail_epub_->text();
    details_.isbn_paperback = detail_paper_->text();
    details_.isbn_pdf = detail_pdf_->text();
    details_.isbn_kindle = detail_kindle_->text();
    details_.isbn_hardcover = detail_hard_->text();
    details_.extra = detail_extra_->toPlainText();
    details_.rights = detail_rights_->isChecked();
    details_.fiction = detail_fiction_->isChecked();
    details_.moral = detail_moral_->isChecked();
    details_.external = detail_external_->isChecked();
    details_.designations = detail_marks_->isChecked();
}

void MainWindow::pushDetails() {
    if (detail_title_ == nullptr) {
        return;
    }
    filling_details_ = true;
    detail_title_->setText(details_.title);
    detail_subtitle_->setText(details_.subtitle);
    detail_author_->setText(details_.author);
    detail_edition_->setText(details_.edition);
    detail_year_->setText(details_.year);
    detail_publisher_->setText(details_.publisher);
    detail_epub_->setText(details_.isbn_epub);
    detail_paper_->setText(details_.isbn_paperback);
    detail_pdf_->setText(details_.isbn_pdf);
    detail_kindle_->setText(details_.isbn_kindle);
    detail_hard_->setText(details_.isbn_hardcover);
    detail_extra_->setPlainText(details_.extra);
    detail_rights_->setChecked(details_.rights);
    detail_fiction_->setChecked(details_.fiction);
    detail_moral_->setChecked(details_.moral);
    detail_external_->setChecked(details_.external);
    detail_marks_->setChecked(details_.designations);
    filling_details_ = false;
}

void MainWindow::detailsEdited() {
    if (filling_details_ || loading_) {
        return;
    }
    pullDetails();
    if (book_dir_.isEmpty()) {
        return;
    }
    QString error;
    if (!details_.writeFile(book_dir_, error)) {
        statusBar()->showMessage(error);
    }
}

void MainWindow::saveCurrent() {
    commitLeaf();
    if (!book_dir_.isEmpty()) {
        if (saveBook(book_dir_)) {
            rememberRecent(book_dir_);
        }
        return;
    }
    if (!singleFileBook()) {
        QString title = details_.title.trimmed();
        if (title.isEmpty() && !book_.chapters.isEmpty()) {
            title = book_.chapters.first().title.trimmed();
        }
        QDialog dialog(this);
        dialog.setWindowTitle("Save book");
        dialog.setMinimumWidth(460);
        auto* layout = new QVBoxLayout(&dialog);
        auto* place = new BookFolderField(&dialog);
        place->setTitle(title);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Save, &dialog);
        layout->addWidget(place);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }
        const QString folder = place->folder();
        if (QFileInfo::exists(QDir(folder).filePath(QStringLiteral("inkwell.book")))) {
            statusBar()->showMessage("That folder already holds a book.");
            return;
        }
        if (saveBook(folder)) {
            rememberRecent(book_dir_);
        }
        return;
    }
    if (path_.isEmpty()) {
        const QString selected = QFileDialog::getSaveFileName(this, "Save", "untitled.txt", "Text (*.txt *.md);;All files (*)");
        if (!selected.isEmpty() && saveFile(selected)) {
            rememberRecent(path_);
        }
        return;
    }
    if (saveFile(path_)) {
        rememberRecent(path_);
    }
}

QString MainWindow::freshId() {
    return QString::number(leaf_seq_++);
}

void MainWindow::insertPicture() {
    const QString selected = QFileDialog::getOpenFileName(this, "Image", book_dir_, "Images (*.png *.jpg *.jpeg *.gif *.webp)");
    if (selected.isEmpty()) {
        return;
    }
    QString stored = selected;
    if (!book_dir_.isEmpty()) {
        QDir folder(book_dir_);
        folder.mkpath("images");
        const QString name = QFileInfo(selected).fileName();
        const QString destination = folder.filePath("images/" + name);
        if (QFileInfo(destination).absoluteFilePath() != QFileInfo(selected).absoluteFilePath()) {
            if (QFile::exists(destination)) {
                QFile::remove(destination);
            }
            QFile::copy(selected, destination);
        }
        stored = QStringLiteral("images/") + name;
    }
    editor_->setResourceRoot(book_dir_);
    editor_->insertImage(stored);
}

void MainWindow::insertDataField() {
    bool accepted = false;
    const QString name = QInputDialog::getText(this, "Field", "Field name", QLineEdit::Normal, "pov", &accepted);
    if (!accepted || name.trimmed().isEmpty()) {
        return;
    }
    const QString value = QInputDialog::getText(this, "Field", "Value", QLineEdit::Normal, {}, &accepted);
    if (!accepted) {
        return;
    }
    editor_->insertField(name.trimmed(), value);
}

void MainWindow::setDarkMode(bool dark) {
    dark_ = dark;
    saveThemeChoice({theme_id_, dark_});
    applyTheme();
}

void MainWindow::setTheme(ThemeId id) {
    theme_id_ = id;
    saveThemeChoice({theme_id_, dark_});
    applyTheme();
}

void MainWindow::applyTheme() {
    applying_theme_ = true;
    const DeskTheme theme = deskTheme(theme_id_, dark_);
    setActiveTheme(theme);
    QFont chrome = QApplication::font();
    if (!theme.chrome_family.isEmpty()) {
        chrome.setFamily(theme.chrome_family);
    }
    setFont(chrome);
    QGuiApplication::styleHints()->setColorScheme(dark_ ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
    const QColor desk = theme.desk;
    const QColor panel = theme.panel;
    const QColor ink = theme.ink;
    QPalette colors = palette();
    colors.setColor(QPalette::Window, desk);
    colors.setColor(QPalette::WindowText, ink);
    colors.setColor(QPalette::Base, panel);
    colors.setColor(QPalette::AlternateBase, panel);
    colors.setColor(QPalette::Text, ink);
    colors.setColor(QPalette::Button, panel);
    colors.setColor(QPalette::ButtonText, ink);
    colors.setColor(QPalette::Mid, theme.edge);
    colors.setColor(QPalette::Highlight, theme.accent);
    colors.setColor(QPalette::HighlightedText, theme.accent_ink);
    colors.setColor(QPalette::PlaceholderText, theme.muted);
    setPalette(colors);
    if (QWidget* desk_widget = centralWidget()) {
        desk_widget->setAutoFillBackground(true);
        desk_widget->setPalette(colors);
    }
    menuBar()->setAutoFillBackground(true);
    menuBar()->setPalette(colors);
    statusBar()->setAutoFillBackground(true);
    statusBar()->setPalette(colors);
    if (editor_ != nullptr) {
        editor_->setDark(dark_);
    }
    if (matter_view_ != nullptr) {
        matter_view_->setDark(dark_);
    }
    if (export_sheet_ != nullptr) {
        export_sheet_->setDark(dark_);
    }
    applyPageLook(true);
    polishChrome();
    for (int i = 0; i < 4; ++i) {
        if (theme_actions_[i] == nullptr) {
            continue;
        }
        theme_actions_[i]->blockSignals(true);
        theme_actions_[i]->setChecked(static_cast<int>(theme_id_) == i);
        theme_actions_[i]->blockSignals(false);
    }
    if (light_action_ != nullptr && dark_action_ != nullptr) {
        light_action_->blockSignals(true);
        dark_action_->blockSignals(true);
        light_action_->setChecked(!dark_);
        dark_action_->setChecked(dark_);
        light_action_->blockSignals(false);
        dark_action_->blockSignals(false);
    }
    if (light_switch_ != nullptr && dark_switch_ != nullptr) {
        light_switch_->blockSignals(true);
        dark_switch_->blockSignals(true);
        light_switch_->setChecked(!dark_);
        dark_switch_->setChecked(dark_);
        light_switch_->blockSignals(false);
        dark_switch_->blockSignals(false);
    }
    applying_theme_ = false;
}

void MainWindow::applyPageLook(bool restyle) {
    const bool was = loading_;
    loading_ = true;
    if (editor_ != nullptr) {
        editor_->setPageLook(page_, restyle);
    }
    if (matter_view_ != nullptr) {
        matter_view_->setPageLook(page_, restyle);
    }
    loading_ = was;
}

void MainWindow::editPage() {
    const PageSettings previous = page_;
    const bool accepted = editPageSettings(this, page_, dark_, [this](const PageSettings& next, bool restyle) {
        page_ = next;
        applyPageLook(restyle);
    });
    if (!accepted) {
        page_ = previous;
    }
}

void MainWindow::changeEvent(QEvent* event) {
    if (!applying_theme_ && (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange)) {
        applyTheme();
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::polishChrome() {
    const DeskTheme theme = activeTheme();
    const bool dark = theme.dark;
    const QColor desk = theme.desk;
    const QColor panel = theme.panel;
    const QColor ink = theme.ink;
    const QColor muted = theme.muted;
    const int field_radius = qMin(theme.radius, 12);
    const int item_radius = qMin(theme.radius, 10);
    if (canvas_ != nullptr) {
        QPalette colors = canvas_->palette();
        colors.setColor(QPalette::Window, desk);
        canvas_->setPalette(colors);
    }
    menuBar()->setStyleSheet(QStringLiteral("QMenuBar { background: %1; color: %2; }"
                                            "QMenuBar::item:selected { background: %3; }"
                                            "QMenu { background: %4; color: %2; border: 1px solid %5; padding: 4px; }"
                                            "QMenu::item { padding: 6px 18px; border-radius: %6px; }"
                                            "QMenu::item:selected { background: %7; color: %8; }"
                                            "QMenu::item:disabled { color: %9; }")
                                  .arg(desk.name(), ink.name(), theme.menu_hover.name(), panel.name(), theme.edge.name())
                                  .arg(item_radius)
                                  .arg(theme.accent.name(), theme.accent_ink.name(), theme.muted.name()));
    statusBar()->setStyleSheet(QStringLiteral("QStatusBar { background: %1; color: %2; }").arg(desk.name(), muted.name()));
    if (counts_ != nullptr) {
        counts_->setStyleSheet(QStringLiteral("color: %1;").arg(muted.name()));
    }
    if (light_switch_ != nullptr && dark_switch_ != nullptr) {
        const QString filled = theme.accent.name();
        const QString filled_ink = theme.accent_ink.name();
        const QString border = theme.edge.name();
        const auto paint = [&](QPushButton* button, bool on, const QString& radius) {
            button->setStyleSheet(QStringLiteral("QPushButton { background: %1; color: %2; border: 1px solid %3; "
                                                  "border-radius: 0; %4 padding: 3px 10px; }")
                                       .arg(on ? filled : QStringLiteral("transparent"), on ? filled_ink : ink.name(), border, radius));
        };
        const QString curve = QString::number(theme.radius);
        paint(light_switch_, !dark, QStringLiteral("border-top-left-radius: %1px; border-bottom-left-radius: %1px;").arg(curve));
        paint(dark_switch_, dark, QStringLiteral("border-top-right-radius: %1px; border-bottom-right-radius: %1px;").arg(curve));
    }
    if (title_edit_ != nullptr) {
        title_edit_->setStyleSheet(QStringLiteral("QLineEdit { background: transparent; border: none; color: %1; }").arg(ink.name()));
    }
    if (kicker_ != nullptr) {
        kicker_->setStyleSheet(QStringLiteral("color: %1;").arg(muted.name()));
    }
    if (library_ != nullptr) {
        library_->setStyleSheet(QStringLiteral(
                                     "QWidget#drawerPage { background: %1; }"
                                     "QLabel#hint { color: %2; }"
                                     "QLineEdit, QPlainTextEdit { background: %3; color: %4; border: 1px solid %5; "
                                     "border-radius: %8px; padding: 4px 8px; selection-background-color: %6; selection-color: %7; }")
                                     .arg(panel.name(), muted.name(), theme.field.name(), ink.name(), theme.edge.name(), theme.accent.name(),
                                          theme.accent_ink.name())
                                     .arg(field_radius));
        const QColor box = theme.field;
        const QColor edge = theme.edge;
        const QColor mark = theme.accent_ink;
        for (QCheckBox* check : {detail_rights_, detail_fiction_, detail_moral_, detail_external_, detail_marks_}) {
            if (check == nullptr) {
                continue;
            }
            QPalette colors = check->palette();
            colors.setColor(QPalette::WindowText, ink);
            colors.setColor(QPalette::Base, box);
            colors.setColor(QPalette::Mid, edge);
            colors.setColor(QPalette::HighlightedText, mark);
            check->setPalette(colors);
        }
    }
    if (library_ != nullptr) {
        library_->polish(theme);
        const auto areas = library_->findChildren<QScrollArea*>();
        for (QScrollArea* area : areas) {
            area->viewport()->setAutoFillBackground(true);
            QPalette viewport = area->viewport()->palette();
            viewport.setColor(QPalette::Window, panel);
            viewport.setColor(QPalette::Base, panel);
            area->viewport()->setPalette(viewport);
            if (QWidget* host = area->widget()) {
                host->setAutoFillBackground(true);
                host->setPalette(viewport);
            }
        }
    }
    if (drawer_ != nullptr) {
        drawer_->setStyleSheet(QStringLiteral(
            "QWidget#drawerPage { background: %1; }"
            "QLabel { color: %2; }"
            "QLabel#hint { color: %3; }")
                                    .arg(panel.name(), ink.name(), muted.name()));
        drawer_->polish(theme);
    }
    const QString radius = QString::number(theme.radius);
    const QString primary = QStringLiteral(
                                "QPushButton { background: %1; color: %2; border: none; border-radius: %6px; padding: 8px 14px; }"
                                "QPushButton:disabled { background: %3; color: %4; }"
                                "QPushButton:hover { background: %5; }")
                                .arg(theme.accent.name(), theme.accent_ink.name(), theme.disabled.name(), muted.name(), theme.accent_hover.name(),
                                     radius);
    const QString quiet = QStringLiteral(
                              "QPushButton { background: transparent; color: %1; border: 1px solid %2; "
                              "border-radius: %5px; padding: 6px 10px; text-align: left; }"
                              "QPushButton:disabled { color: %3; }"
                              "QPushButton:hover { background: %4; }")
                              .arg(ink.name(), theme.edge.name(), muted.name(), theme.hover.name(), radius);
    for (QPushButton* button : findChildren<QPushButton*>()) {
        if (button->objectName() == "primaryButton") {
            button->setStyleSheet(primary);
        }
    }
    if (apply_button_ != nullptr) {
        apply_button_->setStyleSheet(primary);
    }
    for (QPushButton* button : quiet_buttons_) {
        button->setStyleSheet(quiet);
    }
    if (delete_piece_ != nullptr) {
        delete_piece_->setStyleSheet(quiet);
    }
    const QString format_style = quiet + QStringLiteral("QPushButton:checked { background: %1; color: %2; }")
                                             .arg(theme.accent.name(), theme.accent_ink.name());
    for (QPushButton* button : findChildren<QPushButton*>()) {
        if (button->objectName() == "formatButton" || button->objectName() == "markupButton") {
            button->setStyleSheet(format_style);
        }
    }
    if (notes_ != nullptr) {
        notes_->setStyleSheet(QStringLiteral(
                                   "QListWidget { background: %1; color: %2; border: none; }"
                                   "QListWidget::item { padding: 8px 4px; border-radius: %5px; }"
                                   "QListWidget::item:selected { background: %3; color: %4; }")
                                   .arg(panel.name(), ink.name(), theme.accent.name(), theme.accent_ink.name())
                                   .arg(item_radius));
        notes_->viewport()->setAutoFillBackground(true);
        QPalette list_palette = notes_->viewport()->palette();
        list_palette.setColor(QPalette::Base, panel);
        list_palette.setColor(QPalette::Text, ink);
        notes_->viewport()->setPalette(list_palette);
    }
    if (chapter_rows_ != nullptr && !book_.chapters.isEmpty()) {
        refreshLibrary();
    }
    if (shelf_ != nullptr) {
        shelf_->setAutoFillBackground(true);
        QPalette shelf_palette = shelf_->palette();
        shelf_palette.setColor(QPalette::Window, desk);
        shelf_->setPalette(shelf_palette);
    }
    if (shelf_scroll_ != nullptr) {
        shelf_scroll_->viewport()->setAutoFillBackground(true);
        QPalette shelf_palette = shelf_scroll_->viewport()->palette();
        shelf_palette.setColor(QPalette::Window, desk);
        shelf_palette.setColor(QPalette::Base, desk);
        shelf_scroll_->viewport()->setPalette(shelf_palette);
        if (QWidget* host = shelf_scroll_->widget()) {
            host->setAutoFillBackground(true);
            host->setPalette(shelf_palette);
        }
    }
    refreshShelf();
}

int MainWindow::noteAtCursor() const {
    const int caret = editor_->textCursor().position();
    for (int i = 0; i < items_.size(); ++i) {
        if (caret >= items_[i].start && caret <= items_[i].end) {
            return i;
        }
    }
    for (int i = 0; i < items_.size(); ++i) {
        if (items_[i].start >= caret) {
            return i;
        }
    }
    return items_.isEmpty() ? -1 : items_.size() - 1;
}

}  // namespace inkwell
