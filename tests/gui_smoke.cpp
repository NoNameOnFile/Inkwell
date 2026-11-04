#include "book.hpp"
#include "details.hpp"
#include "export.hpp"
#include "mainwindow.hpp"
#include "pagesettings.hpp"
#include "theme.hpp"
#include "textmap.hpp"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QMessageBox>
#include <QTimer>
#include <QComboBox>
#include <QFile>
#include <QImage>
#include <QProcess>
#include <QPushButton>
#include <QTemporaryDir>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

int fail(const std::string& message) {
    std::cerr << message << '\n';
    return 1;
}

}  // namespace

int main(int argc, char** argv) {
    const auto plugins = std::filesystem::temp_directory_path() / "inkwell-plugins-none";
    std::filesystem::create_directories(plugins);
    setenv("INKWELL_PLUGINS", plugins.c_str(), 1);

    QApplication app(argc, argv);

    const inkwell::DeskTheme classic = inkwell::deskTheme(inkwell::ThemeId::Classic, false);
    const inkwell::DeskTheme classic_dark = inkwell::deskTheme(inkwell::ThemeId::Classic, true);
    if (classic.desk != QColor(232, 226, 214) || classic.paper != QColor(255, 253, 248) || classic.page_ink != QColor(28, 25, 21)) {
        return fail("classic light drifted from the original desk");
    }
    if (classic_dark.desk != QColor(18, 16, 14) || classic_dark.paper != QColor(28, 25, 22) || classic.radius != 8) {
        return fail("classic dark drifted from the original desk");
    }
    if (inkwell::deskTheme(inkwell::ThemeId::Modern, false).radius != 10 ||
        inkwell::deskTheme(inkwell::ThemeId::Bubbly, false).radius != 18 ||
        inkwell::deskTheme(inkwell::ThemeId::Industrial, true).radius != 2) {
        return fail("theme shapes drifted");
    }
    if (inkwell::themePaper(false) != classic.paper || inkwell::themeInk(true) != classic_dark.page_ink) {
        return fail("page colors do not follow the classic theme");
    }

    const QString accent = QString::fromUtf8("café");
    const QString emoji = QString::fromUtf8("hi 👍");
    const inkwell::TextMap accent_map = inkwell::map_text(accent);
    const inkwell::TextMap emoji_map = inkwell::map_text(emoji);
    if (accent_map.utf8 != std::string(accent.toUtf8().constData(), static_cast<size_t>(accent.toUtf8().size()))) {
        return fail("accent map drifted from QString");
    }
    if (emoji_map.q_to_byte.size() != static_cast<size_t>(emoji.size()) + 1 || emoji_map.byte_to_q.back() != emoji.size()) {
        return fail("emoji map is the wrong length");
    }
    if (inkwell::byte_at(emoji_map, 3) != 3 || inkwell::q_at(emoji_map, 3) != 3) {
        return fail("emoji indexes do not meet at the code point");
    }

    QTemporaryDir book_dir;
    if (!book_dir.isValid()) {
        return fail("could not make a temporary book");
    }
    inkwell::Book book = inkwell::Book::single("The First Primer", "Horus kept the door.");
    book.notes[0].push_back(inkwell::Leaf{"9", "Adam Rye", "A scout from the lower floors."});
    QString book_error;
    if (!book.write(book_dir.path(), book_error) || !inkwell::Book::read(book_dir.path(), book, book_error)) {
        return fail(book_error.toStdString());
    }
    if (book.chapters.size() != 1 || book.chapters[0].title != "The First Primer" || book.chapters[0].body != "Horus kept the door.") {
        return fail("chapter roundtrip failed");
    }
    if (book.notes[0].size() != 1 || book.notes[0][0].title != "Adam Rye" || book.notes[0][0].body != "A scout from the lower floors.") {
        return fail("note roundtrip failed");
    }
    inkwell::BookDetails details;
    details.title = "Ruin Shaper";
    details.subtitle = "The Shaper's Tower";
    details.author = "David Pope";
    details.year = "2026";
    details.rights = true;
    details.fiction = true;
    QString detail_error;
    if (!details.writeFile(book_dir.path(), detail_error)) {
        return fail(detail_error.toStdString());
    }
    const inkwell::BookDetails loaded = inkwell::BookDetails::readFile(book_dir.path());
    if (loaded.title != "Ruin Shaper" || loaded.author != "David Pope" || !loaded.fiction || !loaded.rights) {
        return fail("book details did not round-trip");
    }
    if (!loaded.copyrightPage().contains("David Pope") || !loaded.copyrightPage().contains("work of fiction")) {
        return fail("copyright page missed the author or the fiction clause");
    }
    if (inkwell::BookDetails::contentsPage({"Before the Fall", "The Other Side"}) != "Contents\n\nBefore the Fall\nThe Other Side") {
        return fail("contents page was not the chapter list");
    }

    QTemporaryDir settings_dir;
    if (!settings_dir.isValid()) {
        return fail("could not make a temporary settings folder");
    }
    inkwell::PageSettings page;
    page.font_family = "Liberation Serif";
    page.font_size = 19;
    page.line_percent = 150;
    page.indent_ems = 1;
    page.column_width = 540;
    page.justify = false;
    page.custom_colors = true;
    page.paper = QColor("#203040");
    page.ink = QColor("#f0e6d2");
    const QString page_path = settings_dir.filePath("page");
    QString page_error;
    if (!page.saveFile(page_path, &page_error)) {
        return fail(page_error.toStdString());
    }
    const inkwell::PageSettings loaded_page = inkwell::PageSettings::loadFile(page_path);
    if (loaded_page.font_family != "Liberation Serif" || loaded_page.font_size != 19 || loaded_page.line_percent != 150 ||
        loaded_page.indent_ems != 1 || loaded_page.column_width != 540 || loaded_page.justify || !loaded_page.custom_colors ||
        loaded_page.paper != QColor("#203040") || loaded_page.ink != QColor("#f0e6d2")) {
        return fail("page settings did not round-trip");
    }
    inkwell::PageSettings wild = inkwell::PageSettings::loadFile(settings_dir.filePath("missing"));
    wild.font_size = 4;
    wild.column_width = 20;
    wild.line_percent = 10;
    wild.clamp();
    if (wild.font_size != 12 || wild.column_width != 420 || wild.line_percent != 120) {
        return fail("page settings were not kept in range");
    }

    inkwell::MainWindow window;
    window.resize(1500, 860);
    auto command = [&window](const char* name) { return window.findChild<QAction*>(name); };
    auto* export_action = command("bookExport");
    auto* save_action = command("fileSave");
    auto* shelf_action = command("fileShelf");
    auto* cover_action = command("bookCover");
    auto* undo_action = command("editUndo");
    auto* cut_action = command("editCut");
    if (export_action == nullptr || save_action == nullptr || shelf_action == nullptr || cover_action == nullptr || undo_action == nullptr ||
        cut_action == nullptr) {
        return fail("a book command is missing");
    }
    if (export_action->isEnabled() || save_action->isEnabled() || shelf_action->isEnabled() || cover_action->isEnabled() || undo_action->isEnabled() ||
        cut_action->isEnabled()) {
        return fail("book commands were live on the shelf");
    }
    window.setDocumentText("I recieve a apple.");
    if (!export_action->isEnabled() || !save_action->isEnabled() || !shelf_action->isEnabled() || cover_action->isEnabled() || undo_action->isEnabled() ||
        cut_action->isEnabled()) {
        return fail("book commands did not follow the open chapter");
    }
    window.showExport();
    if (!export_action->isEnabled() || undo_action->isEnabled() || cut_action->isEnabled()) {
        return fail("editing commands stayed live on the export page");
    }
    window.setDocumentText("I recieve a apple.");
    window.recheck();
    if (window.noteCount() < 2) {
        return fail("expected spelling and grammar notes, got " + std::to_string(window.noteCount()));
    }

    bool saw_grammar = false;
    int spelling = -1;
    for (int i = 0; i < window.noteCount(); ++i) {
        const QString summary = window.noteSummary(i);
        if (summary.contains("grammar")) {
            saw_grammar = true;
        }
        if (summary.contains("recieve")) {
            spelling = i;
        }
    }
    window.show();
    app.processEvents();
    QImage image(window.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    window.render(&image);
#ifdef INKWELL_PREVIEW
    const QString preview = QString::fromUtf8(INKWELL_PREVIEW);
    if (!image.save(preview)) {
        return fail("could not save the preview");
    }
    window.showDrawerPage(1);
    app.processEvents();
    QImage chat(window.size(), QImage::Format_ARGB32_Premultiplied);
    chat.fill(Qt::white);
    window.render(&chat);
    const QString chat_path = preview.endsWith(".png") ? preview.chopped(4) + "-chat.png" : preview + "-chat.png";
    if (!chat.save(chat_path)) {
        return fail("could not save the chat preview");
    }
    window.showDrawerPage(0);
    window.showLibraryPage(1);
    app.processEvents();
    QImage notes(window.size(), QImage::Format_ARGB32_Premultiplied);
    notes.fill(Qt::white);
    window.render(&notes);
    const QString notes_path = preview.endsWith(".png") ? preview.chopped(4) + "-notes.png" : preview + "-notes.png";
    if (!notes.save(notes_path)) {
        return fail("could not save the notes preview");
    }
    window.showLibraryPage(0);
    app.processEvents();
#endif

    const bool applied = spelling >= 0 && window.applyNote(spelling);
    const QString text = window.documentText();
    if (!saw_grammar || !applied || !text.contains("receive") || text.contains("recieve")) {
        return fail("apply failed: " + text.toStdString());
    }

    QTemporaryDir save_dir;
    if (!save_dir.isValid()) {
        return fail("could not make a temporary save folder");
    }
    window.setDocumentText("One\n\nTwo");
    const QString chapter = save_dir.filePath("chapter.txt");
    if (!window.saveFile(chapter)) {
        return fail("save failed");
    }
    QFile saved(chapter);
    if (!saved.open(QIODevice::ReadOnly)) {
        return fail("could not read the saved chapter");
    }
    const QString disk = QString::fromUtf8(saved.readAll());
    if (disk.contains('<') || disk != "One\n\nTwo\n") {
        return fail("plain chapter saved as " + disk.toStdString());
    }
    if (!window.openFile(chapter) || window.documentText() != "One\n\nTwo") {
        return fail("reopen failed: " + window.documentText().toStdString());
    }

    inkwell::ExportRequest request;
    request.details.title = QString::fromUtf8("Café Road");
    request.details.author = "Ada";
    request.details.year = "2026";
    request.details.isbn_epub = "978-1-4028-9462-6";
    request.details.fiction = true;
    request.page.font_size = 16;
    request.page.line_percent = 160;
    request.page.indent_ems = 2;
    request.resource_root = save_dir.path();
    QImage tower(80, 40, QImage::Format_RGB32);
    tower.fill(Qt::red);
    const QString tower_path = save_dir.filePath("tower.png");
    const QString cover_path = save_dir.filePath("cover.png");
    if (!tower.save(tower_path) || !tower.save(cover_path)) {
        return fail("could not write the export pictures");
    }
    request.cover_path = cover_path;
    inkwell::Leaf first;
    first.title = "Before the Fall";
    first.body = QString::fromUtf8("The tent held.\n\nShe <i>waited</i> in the <b>dark</b>.\n\n* * *\n\nFish & chips.\n\n<img src=\"tower.png\" alt=\"The tower\">");
    inkwell::Leaf second;
    second.title = "The Gate";
    second.body = "The gate opened.";
    request.chapters = {first, second};

    const auto archive_text = [](const QString& archive, const QString& name) {
        QProcess process;
        process.start("unzip", {"-p", archive, name});
        if (!process.waitForFinished(10000) || process.exitCode() != 0) {
            return QString();
        }
        return QString::fromUtf8(process.readAllStandardOutput());
    };

    QString error;
    const QString text_path = save_dir.filePath("book.txt");
    if (!inkwell::export_book(request, text_path, inkwell::ExportFormat::Text, error)) {
        return fail(error.toStdString());
    }
    QFile text_file(text_path);
    if (!text_file.open(QIODevice::ReadOnly)) {
        return fail("could not read the text export");
    }
    const QString manuscript = QString::fromUtf8(text_file.readAll());
    if (!manuscript.contains("All rights reserved") || !manuscript.contains("Before the Fall") || !manuscript.contains("<i>waited</i>") ||
        !manuscript.contains("The gate opened.")) {
        return fail("text export lost the manuscript");
    }

    const QString epub_path = save_dir.filePath("book.epub");
    if (!inkwell::export_book(request, epub_path, inkwell::ExportFormat::Epub, error)) {
        return fail(error.toStdString());
    }
    QFile epub_file(epub_path);
    if (!epub_file.open(QIODevice::ReadOnly)) {
        return fail("could not read the epub");
    }
    const QByteArray epub_bytes = epub_file.readAll();
    const auto read_u16 = [&epub_bytes](int at) {
        return static_cast<unsigned>(static_cast<unsigned char>(epub_bytes[at])) |
               (static_cast<unsigned>(static_cast<unsigned char>(epub_bytes[at + 1])) << 8);
    };
    const auto read_u32 = [&epub_bytes](int at) {
        return static_cast<unsigned>(static_cast<unsigned char>(epub_bytes[at])) |
               (static_cast<unsigned>(static_cast<unsigned char>(epub_bytes[at + 1])) << 8) |
               (static_cast<unsigned>(static_cast<unsigned char>(epub_bytes[at + 2])) << 16) |
               (static_cast<unsigned>(static_cast<unsigned char>(epub_bytes[at + 3])) << 24);
    };
    if (epub_bytes.size() < 40 || !epub_bytes.startsWith("PK\x03\x04") || read_u16(8) != 0 || epub_bytes.mid(30, read_u16(26)) != "mimetype") {
        return fail("epub mimetype is not the first stored entry");
    }
    const int mime_at = 30 + static_cast<int>(read_u16(26)) + static_cast<int>(read_u16(28));
    if (epub_bytes.mid(mime_at, static_cast<int>(read_u32(18))) != "application/epub+zip") {
        return fail("epub mimetype payload is wrong");
    }
    QProcess epub_test;
    epub_test.start("unzip", {"-t", epub_path});
    if (!epub_test.waitForFinished(10000) || epub_test.exitCode() != 0) {
        return fail("epub failed unzip -t");
    }
    const QString chapter_html = archive_text(epub_path, "OEBPS/chapter-1.xhtml");
    const QString opf = archive_text(epub_path, "OEBPS/content.opf");
    const QString nav = archive_text(epub_path, "OEBPS/nav.xhtml");
    if (!chapter_html.contains("<em>waited</em>") || !chapter_html.contains("<strong>dark</strong>") || !chapter_html.contains("Fish &amp; chips") ||
        !chapter_html.contains("* * *") || !chapter_html.contains("images/image") || !opf.contains("urn:isbn:9781402894626") ||
        !opf.contains("cover-image") || !nav.contains("Before the Fall") || !nav.contains("The Gate") ||
        archive_text(epub_path, "OEBPS/images/cover.png").isEmpty()) {
        return fail("epub is missing the book");
    }

    const QString docx_path = save_dir.filePath("book.docx");
    if (!inkwell::export_book(request, docx_path, inkwell::ExportFormat::Docx, error)) {
        return fail(error.toStdString());
    }
    QProcess docx_test;
    docx_test.start("unzip", {"-t", docx_path});
    if (!docx_test.waitForFinished(10000) || docx_test.exitCode() != 0) {
        return fail("docx failed unzip -t");
    }
    const QString document_xml = archive_text(docx_path, "word/document.xml");
    if (!document_xml.contains("Before the Fall") || !document_xml.contains("Fish &amp; chips") || !document_xml.contains("<w:i/>") ||
        !document_xml.contains("<w:b/>") || !document_xml.contains("waited") || !document_xml.contains("r:embed=") ||
        archive_text(docx_path, "word/media/image1.png").isEmpty()) {
        return fail("docx is missing the book");
    }

    const QString pdf_path = save_dir.filePath("book.pdf");
    if (!inkwell::export_book(request, pdf_path, inkwell::ExportFormat::Pdf, error)) {
        return fail(error.toStdString());
    }
    QFile pdf_file(pdf_path);
    if (!pdf_file.open(QIODevice::ReadOnly) || !pdf_file.read(5).startsWith("%PDF")) {
        return fail("pdf does not start with the PDF header");
    }
    QProcess pdf_text;
    pdf_text.start("pdftotext", {"-layout", pdf_path, "-"});
    if (!pdf_text.waitForFinished(10000) || pdf_text.exitCode() != 0) {
        return fail("could not read the pdf");
    }
    const QString pages = QString::fromUtf8(pdf_text.readAllStandardOutput());
    if (!pages.contains(QString::fromUtf8("Café Road")) || !pages.contains("Before the Fall") || !pages.contains("waited") ||
        !pages.contains("Fish & chips") || !pages.contains("The gate opened.") || !pages.contains("All rights reserved")) {
        return fail("pdf is missing the book: " + pages.toStdString());
    }

    inkwell::ExportRequest pocket = request;
    pocket.include_cover = false;
    pocket.include_title = false;
    pocket.include_copyright = false;
    pocket.include_contents = false;
    pocket.trim_width = 5.0;
    pocket.trim_height = 8.0;
    pocket.margin_inches = 0.5;
    pocket.chapters = {second};
    const QString pocket_pdf = save_dir.filePath("pocket.pdf");
    const QString pocket_docx = save_dir.filePath("pocket.docx");
    if (!inkwell::export_book(pocket, pocket_pdf, inkwell::ExportFormat::Pdf, error) ||
        !inkwell::export_book(pocket, pocket_docx, inkwell::ExportFormat::Docx, error)) {
        return fail(error.toStdString());
    }
    QProcess pocket_info;
    pocket_info.start("pdfinfo", {pocket_pdf});
    if (!pocket_info.waitForFinished(10000) || pocket_info.exitCode() != 0) {
        return fail("could not measure the pocket pdf");
    }
    const QString pocket_measure = QString::fromUtf8(pocket_info.readAllStandardOutput());
    QProcess pocket_text;
    pocket_text.start("pdftotext", {"-layout", pocket_pdf, "-"});
    if (!pocket_text.waitForFinished(10000) || pocket_text.exitCode() != 0) {
        return fail("could not read the pocket pdf");
    }
    const QString pocket_pages = QString::fromUtf8(pocket_text.readAllStandardOutput());
    const QString pocket_document = archive_text(pocket_docx, "word/document.xml");
    if (!pocket_measure.contains("360 x 576") || pocket_pages.contains("All rights reserved") || pocket_pages.contains("Contents") ||
        !pocket_pages.contains("The gate opened.") || !pocket_document.contains("w:w=\"7200\"") || !pocket_document.contains("w:h=\"11520\"") ||
        !pocket_document.contains("w:top=\"720\"")) {
        return fail("pocket export ignored the trim or the included parts");
    }

    QString filler;
    for (int i = 0; i < 500; ++i) {
        filler += "She waited by the door while the fire took hold in the grate.\n\n";
    }
    inkwell::Leaf packed;
    packed.title = "The Long Road";
    packed.body = filler;
    inkwell::ExportRequest sized = pocket;
    sized.chapters = {packed};
    sized.body_size = 11;
    sized.body_leading = 130;
    sized.trim_width = 6;
    sized.trim_height = 9;
    sized.margin_inches = 0.85;
    const QString trade_pdf = save_dir.filePath("trade11.pdf");
    const QString large_pdf = save_dir.filePath("trade14.pdf");
    if (!inkwell::export_book(sized, trade_pdf, inkwell::ExportFormat::Pdf, error)) {
        return fail(error.toStdString());
    }
    sized.body_size = 14;
    if (!inkwell::export_book(sized, large_pdf, inkwell::ExportFormat::Pdf, error)) {
        return fail(error.toStdString());
    }
    const auto pdf_pages = [](const QString& path) {
        QProcess info;
        info.start("pdfinfo", {path});
        if (!info.waitForFinished(10000) || info.exitCode() != 0) {
            return -1;
        }
        const QString text = QString::fromUtf8(info.readAllStandardOutput());
        const int mark = text.indexOf("Pages:");
        if (mark < 0) {
            return -1;
        }
        return text.mid(mark).section('\n', 0, 0).section(':', 1).trimmed().toInt();
    };
    const int pages_11 = pdf_pages(trade_pdf);
    const int pages_14 = pdf_pages(large_pdf);
    if (pages_11 < 12 || pages_11 > 40 || pages_14 < pages_11 + 4) {
        return fail("book type came out at " + std::to_string(pages_11) + " and " + std::to_string(pages_14) + " pages");
    }

    window.showExport();
    app.processEvents();
    auto* export_page = window.findChild<QWidget*>("exportPage");
    auto* export_trim = window.findChild<QComboBox*>("exportTrim");
    auto* export_go = window.findChild<QPushButton*>("exportGo");
    auto* export_chapter = window.findChild<QCheckBox*>("exportChapter");
    if (export_page == nullptr || export_trim == nullptr || export_go == nullptr) {
        return fail("export page widgets were not created");
    }
    if (!export_page->isVisible()) {
        return fail("export page is hidden");
    }
    if (export_trim->findData("5x8") < 0 || export_trim->findData("6x9") < 0 || export_trim->findData("a5") < 0 || export_trim->findData("custom") < 0) {
        return fail("trim list is missing a size");
    }
    if (export_chapter == nullptr || !export_chapter->isChecked()) {
        return fail("export page did not list the chapter");
    }

    QAction* about = nullptr;
    for (QAction* action : window.findChildren<QAction*>()) {
        if (action->text() == "About Inkwell") {
            about = action;
        }
    }
    if (about == nullptr) {
        return fail("about action is missing");
    }
    bool about_closed = false;
    QTimer::singleShot(0, &app, [&about_closed] {
        auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (box == nullptr || !box->testOption(QMessageBox::Option::DontUseNativeDialog)) {
            return;
        }
        about_closed = true;
        if (QAbstractButton* ok = box->button(QMessageBox::Ok)) {
            ok->click();
        }
    });
    about->trigger();
    if (!about_closed) {
        return fail("about dialog did not close");
    }

    std::cout << "ok\n";
    return 0;
}
