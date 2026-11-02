#include "export.hpp"

#include "markup.hpp"

#include <QAbstractTextDocumentLayout>
#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontMetricsF>
#include <QImage>
#include <QPdfWriter>
#include <QPageLayout>
#include <QPageSize>
#include <QSaveFile>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextImageFormat>
#include <QUrl>

#include <zlib.h>

#include <algorithm>
#include <vector>

namespace inkwell {
namespace {

struct BoundBook {
    double width = 6.0;
    double height = 9.0;
    double margin = 0.85;
};

BoundBook bound_book(const ExportRequest& request) {
    BoundBook book;
    book.width = std::clamp(request.trim_width, 3.0, 12.0);
    book.height = std::clamp(request.trim_height, 3.0, 12.0);
    book.margin = std::clamp(request.margin_inches, 0.4, 1.5);
    const double room = std::min(book.width, book.height);
    while (room - (book.margin * 2.0) < 2.0 && book.margin > 0.4) {
        book.margin = std::max(0.4, book.margin - 0.05);
    }
    return book;
}

struct Piece {
    enum class Kind { Title, Subtitle, Byline, Heading, Prose, Scene, Blank, Image };
    enum class Section { Front, Copyright, Contents, Chapter };

    Kind kind = Kind::Prose;
    Section section = Section::Front;
    QString text;
    std::vector<MarkupRun> runs;
    QString image_path;
    QString image_alt;
    bool indent = false;
    bool page_break = false;
};

QString xml_escape(const QString& text) {
    QString out;
    out.reserve(text.size());
    for (const QChar ch : text) {
        switch (ch.unicode()) {
            case '&':
                out += "&amp;";
                break;
            case '<':
                out += "&lt;";
                break;
            case '>':
                out += "&gt;";
                break;
            case '"':
                out += "&quot;";
                break;
            case '\'':
                out += "&apos;";
                break;
            default:
                if (ch.unicode() < 0x20 && ch != u'\t' && ch != u'\n' && ch != u'\r') {
                    break;
                }
                out += ch;
                break;
        }
    }
    return out;
}

QString face_name(const PageSettings& page) {
    const QString face = page.resolvedFamily().trimmed();
    return face.isEmpty() ? QStringLiteral("Times New Roman") : face;
}

QString media_type_for(const QString& path) {
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == "jpg" || ext == "jpeg") {
        return "image/jpeg";
    }
    if (ext == "gif") {
        return "image/gif";
    }
    if (ext == "webp") {
        return "image/webp";
    }
    return "image/png";
}

QString image_extension(const QString& path) {
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == "jpg" || ext == "jpeg" || ext == "gif" || ext == "webp" || ext == "png") {
        return ext == "jpeg" ? QStringLiteral("jpg") : ext;
    }
    return "png";
}

QString resolve_image(const QString& root, const QString& src) {
    if (src.isEmpty()) {
        return {};
    }
    const QFileInfo direct(src);
    if (direct.isAbsolute() && direct.exists()) {
        return direct.absoluteFilePath();
    }
    if (!root.isEmpty()) {
        const QString joined = QDir(root).filePath(src);
        if (QFileInfo::exists(joined)) {
            return QFileInfo(joined).absoluteFilePath();
        }
    }
    if (direct.exists()) {
        return direct.absoluteFilePath();
    }
    return {};
}

QByteArray read_bytes(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

bool empty_paragraph(const MarkupBlock& block) {
    if (block.kind != MarkupBlock::Kind::Paragraph) {
        return false;
    }
    for (const MarkupRun& run : block.runs) {
        if (!run.text.empty()) {
            return false;
        }
    }
    return true;
}

std::vector<MarkupBlock> book_blocks(const std::vector<MarkupBlock>& blocks) {
    std::vector<MarkupBlock> out;
    size_t index = 0;
    while (index < blocks.size()) {
        if (!empty_paragraph(blocks[index])) {
            out.push_back(blocks[index]);
            ++index;
            continue;
        }
        size_t end = index;
        while (end < blocks.size() && empty_paragraph(blocks[end])) {
            ++end;
        }
        const size_t extra = end - index;
        for (size_t kept = 1; kept < extra; ++kept) {
            out.push_back(blocks[index]);
        }
        index = end;
    }
    return out;
}

Piece plain_piece(Piece::Kind kind, const QString& text, Piece::Section section, bool page_break = false) {
    Piece piece;
    piece.kind = kind;
    piece.section = section;
    piece.text = text;
    piece.page_break = page_break;
    return piece;
}

void append_body(std::vector<Piece>& out, const QString& body, const QString& root, int indent_ems) {
    const std::vector<MarkupBlock> blocks = book_blocks(parse_markup(body.toStdString()));
    bool indent_next = false;
    for (const MarkupBlock& block : blocks) {
        if (empty_paragraph(block)) {
            out.push_back(plain_piece(Piece::Kind::Blank, {}, Piece::Section::Chapter));
            continue;
        }
        if (block.kind == MarkupBlock::Kind::Scene) {
            out.push_back(plain_piece(Piece::Kind::Scene, "* * *", Piece::Section::Chapter));
            indent_next = false;
            continue;
        }
        if (block.kind == MarkupBlock::Kind::Image) {
            Piece image;
            image.kind = Piece::Kind::Image;
            image.section = Piece::Section::Chapter;
            image.image_alt = QString::fromStdString(block.name);
            image.image_path = resolve_image(root, QString::fromStdString(block.src));
            if (image.image_path.isEmpty()) {
                const QString missing = image.image_alt.isEmpty() ? QString::fromStdString(block.src) : image.image_alt;
                if (!missing.isEmpty()) {
                    out.push_back(plain_piece(Piece::Kind::Prose, missing, Piece::Section::Chapter));
                    out.back().indent = indent_next && indent_ems > 0;
                    out.back().runs.push_back(MarkupRun{missing.toStdString(), false, false});
                }
            } else {
                out.push_back(image);
            }
            indent_next = true;
            continue;
        }
        Piece prose;
        prose.kind = Piece::Kind::Prose;
        prose.section = Piece::Section::Chapter;
        prose.indent = indent_next && indent_ems > 0;
        if (block.kind == MarkupBlock::Kind::Field && !block.name.empty()) {
            prose.runs.push_back(MarkupRun{block.name + ": ", false, true});
            prose.indent = false;
        }
        prose.runs.insert(prose.runs.end(), block.runs.begin(), block.runs.end());
        out.push_back(std::move(prose));
        indent_next = true;
    }
}

std::vector<Piece> compose(const ExportRequest& request, const PageSettings& page) {
    std::vector<Piece> pieces;
    const bool cover = request.include_cover && !request.cover_path.isEmpty() && QFileInfo::exists(request.cover_path);
    if (cover) {
        Piece image;
        image.kind = Piece::Kind::Image;
        image.section = Piece::Section::Front;
        image.image_path = request.cover_path;
        image.image_alt = request.details.title;
        pieces.push_back(image);
    }
    const QString title = request.details.title.trimmed();
    const QString subtitle = request.details.subtitle.trimmed();
    const QString author = request.details.author.trimmed();
    if (request.include_title && !title.isEmpty()) {
        pieces.push_back(plain_piece(Piece::Kind::Title, title, Piece::Section::Front));
    }
    if (request.include_title && !subtitle.isEmpty()) {
        pieces.push_back(plain_piece(Piece::Kind::Subtitle, subtitle, Piece::Section::Front));
    }
    if (request.include_title && !author.isEmpty()) {
        pieces.push_back(plain_piece(Piece::Kind::Byline, author, Piece::Section::Front));
    }
    const bool title_page = !pieces.empty();

    const QString copyright = request.details.copyrightPage();
    bool copyright_started = false;
    if (request.include_copyright && !copyright.trimmed().isEmpty()) {
    for (const QString& line : copyright.split('\n')) {
        Piece piece = plain_piece(Piece::Kind::Prose, line.trimmed(), Piece::Section::Copyright);
        if (!copyright_started && title_page) {
            piece.page_break = true;
            copyright_started = true;
        }
        copyright_started = true;
        if (!piece.text.isEmpty()) {
            piece.runs.push_back(MarkupRun{piece.text.toStdString(), false, false});
        } else {
            piece.kind = Piece::Kind::Blank;
        }
        pieces.push_back(std::move(piece));
    }
    }

    if (request.include_contents) {
        pieces.push_back(plain_piece(Piece::Kind::Heading, "Contents", Piece::Section::Contents, true));
        for (const Leaf& leaf : request.chapters) {
            const QString heading = leaf.title.trimmed().isEmpty() ? QStringLiteral("Untitled") : leaf.title.trimmed();
            Piece listed = plain_piece(Piece::Kind::Prose, heading, Piece::Section::Contents);
            listed.runs.push_back(MarkupRun{heading.toStdString(), false, false});
            pieces.push_back(std::move(listed));
        }
    }

    for (const Leaf& leaf : request.chapters) {
        const QString heading = leaf.title.trimmed().isEmpty() ? QStringLiteral("Untitled") : leaf.title.trimmed();
        pieces.push_back(plain_piece(Piece::Kind::Heading, heading, Piece::Section::Chapter, true));
        append_body(pieces, leaf.body, request.resource_root, page.indent_ems);
    }
    return pieces;
}

void put_u16(QByteArray& out, quint16 value) {
    out.push_back(static_cast<char>(value & 0xff));
    out.push_back(static_cast<char>((value >> 8) & 0xff));
}

void put_u32(QByteArray& out, quint32 value) {
    out.push_back(static_cast<char>(value & 0xff));
    out.push_back(static_cast<char>((value >> 8) & 0xff));
    out.push_back(static_cast<char>((value >> 16) & 0xff));
    out.push_back(static_cast<char>((value >> 24) & 0xff));
}

QByteArray deflate_raw(const QByteArray& input, QString& error) {
    z_stream stream{};
    if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        error = "Could not compress the book.";
        return {};
    }
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.constData()));
    stream.avail_in = static_cast<uInt>(input.size());
    QByteArray output(static_cast<qsizetype>(deflateBound(&stream, static_cast<uLong>(input.size()))), '\0');
    stream.next_out = reinterpret_cast<Bytef*>(output.data());
    stream.avail_out = static_cast<uInt>(output.size());
    const int result = deflate(&stream, Z_FINISH);
    const auto produced = static_cast<qsizetype>(stream.total_out);
    deflateEnd(&stream);
    if (result != Z_STREAM_END) {
        error = "Could not compress the book.";
        return {};
    }
    output.truncate(produced);
    return output;
}

bool write_zip(const QString& path, const QVector<QPair<QString, QByteArray>>& files, const QString& stored_name, QString& error) {
    struct Entry {
        QByteArray name;
        quint32 crc = 0;
        quint32 size = 0;
        quint32 compressed = 0;
        quint16 method = 0;
        quint32 offset = 0;
        QByteArray payload;
    };
    if (files.isEmpty()) {
        error = "The book had nothing to pack.";
        return false;
    }
    const QDateTime now = QDateTime::currentDateTime();
    const quint16 dos_time = static_cast<quint16>((now.time().hour() << 11) | (now.time().minute() << 5) | (now.time().second() / 2));
    const quint16 dos_date = static_cast<quint16>(((now.date().year() - 1980) << 9) | (now.date().month() << 5) | now.date().day());

    std::vector<Entry> entries;
    entries.reserve(static_cast<size_t>(files.size()));
    for (const auto& file : files) {
        if (file.second.size() > 0xffffffff || file.first.toUtf8().size() > 0xffff) {
            error = "A file in the book is too large to pack.";
            return false;
        }
        Entry entry;
        entry.name = file.first.toUtf8();
        entry.size = static_cast<quint32>(file.second.size());
        entry.crc = file.second.isEmpty()
                        ? 0
                        : static_cast<quint32>(crc32(0L, reinterpret_cast<const Bytef*>(file.second.constData()), static_cast<uInt>(file.second.size())));
        const bool store = file.first == stored_name;
        if (store || file.second.isEmpty()) {
            entry.method = 0;
            entry.payload = file.second;
        } else {
            entry.payload = deflate_raw(file.second, error);
            if (entry.payload.isNull() && !error.isEmpty()) {
                return false;
            }
            entry.method = 8;
        }
        entry.compressed = static_cast<quint32>(entry.payload.size());
        entries.push_back(std::move(entry));
    }

    QByteArray archive;
    for (Entry& entry : entries) {
        entry.offset = static_cast<quint32>(archive.size());
        put_u32(archive, 0x04034b50);
        put_u16(archive, 20);
        put_u16(archive, 0x0800);
        put_u16(archive, entry.method);
        put_u16(archive, dos_time);
        put_u16(archive, dos_date);
        put_u32(archive, entry.crc);
        put_u32(archive, entry.compressed);
        put_u32(archive, entry.size);
        put_u16(archive, static_cast<quint16>(entry.name.size()));
        put_u16(archive, 0);
        archive.append(entry.name);
        archive.append(entry.payload);
    }
    const quint32 directory = static_cast<quint32>(archive.size());
    for (const Entry& entry : entries) {
        put_u32(archive, 0x02014b50);
        put_u16(archive, 20);
        put_u16(archive, 20);
        put_u16(archive, 0x0800);
        put_u16(archive, entry.method);
        put_u16(archive, dos_time);
        put_u16(archive, dos_date);
        put_u32(archive, entry.crc);
        put_u32(archive, entry.compressed);
        put_u32(archive, entry.size);
        put_u16(archive, static_cast<quint16>(entry.name.size()));
        put_u16(archive, 0);
        put_u16(archive, 0);
        put_u16(archive, 0);
        put_u16(archive, 0);
        put_u32(archive, 0);
        put_u32(archive, entry.offset);
        archive.append(entry.name);
    }
    const quint32 directory_size = static_cast<quint32>(archive.size()) - directory;
    put_u32(archive, 0x06054b50);
    put_u16(archive, 0);
    put_u16(archive, 0);
    put_u16(archive, static_cast<quint16>(entries.size()));
    put_u16(archive, static_cast<quint16>(entries.size()));
    put_u32(archive, directory_size);
    put_u32(archive, directory);
    put_u16(archive, 0);

    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly) || output.write(archive) != archive.size() || !output.commit()) {
        error = "Could not write " + path;
        return false;
    }
    return true;
}

QString html_runs(const std::vector<MarkupRun>& runs) {
    QString out;
    for (const MarkupRun& run : runs) {
        if (run.text.empty()) {
            continue;
        }
        QString piece = xml_escape(QString::fromStdString(run.text));
        if (run.italic) {
            piece = "<em>" + piece + "</em>";
        }
        if (run.bold) {
            piece = "<strong>" + piece + "</strong>";
        }
        out += piece;
    }
    return out;
}

QString xhtml_page(const QString& title, const QString& body) {
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
           "<!DOCTYPE html>\n"
           "<html xmlns=\"http://www.w3.org/1999/xhtml\" xmlns:epub=\"http://www.idpf.org/2007/ops\" xml:lang=\"en\">\n"
           "<head>\n<title>" +
           xml_escape(title) +
           "</title>\n"
           "<link rel=\"stylesheet\" type=\"text/css\" href=\"style.css\"/>\n"
           "</head>\n<body>\n" +
           body + "\n</body>\n</html>\n";
}

QString css_for(const PageSettings& page) {
    const QString align = page.justify ? "justify" : "left";
    return "body { font-family: \"" + xml_escape(face_name(page)) + "\", Palatino, \"Palatino Linotype\", serif; font-size: " +
           QString::number(page.font_size) + "pt; line-height: " + QString::number(page.line_percent / 100.0, 'f', 2) +
           "; }\n"
           "p { margin: 0 0 0.2em 0; text-align: " +
           align + "; text-indent: " + QString::number(page.indent_ems) +
           "em; }\n"
           "h1, h2, p.scene, p.plain, p.blank, p.title, p.subtitle, p.byline { text-indent: 0; }\n"
           "h1 { text-align: center; font-size: 1.5em; margin: 0 0 1em 0; }\n"
           "h2 { text-align: center; font-size: 1.2em; margin: 1.4em 0 0.6em 0; }\n"
           "p.scene { text-align: center; margin: 1em 0; }\n"
           "p.plain, p.blank { text-align: left; line-height: 1.2; }\n"
           "p.title { text-align: center; font-size: 2em; font-weight: bold; margin: 1.4em 0 0.4em 0; }\n"
           "p.subtitle { text-align: center; font-style: italic; margin: 0.3em 0; }\n"
           "p.byline { text-align: center; margin: 1em 0; }\n"
           "img { max-width: 100%; height: auto; }\n"
           "p.cover { text-align: center; }\n";
}

QString identifier_for(const BookDetails& details) {
    const QString isbn = details.isbn_epub.trimmed();
    QString digits;
    for (const QChar ch : isbn) {
        if (ch.isDigit() || ch == u'X' || ch == u'x') {
            digits += ch;
        }
    }
    if (!digits.isEmpty()) {
        return "urn:isbn:" + digits;
    }
    const QByteArray hash = QCryptographicHash::hash((details.title + "\n" + details.author).toUtf8(), QCryptographicHash::Sha1).toHex();
    return "urn:sha1:" + QString::fromLatin1(hash);
}

bool export_epub(const ExportRequest& request, const PageSettings& page, const std::vector<Piece>& pieces, const QString& path, QString& error) {
    QVector<QPair<QString, QByteArray>> files;
    files.push_back({"mimetype", QByteArray("application/epub+zip")});
    files.push_back({"META-INF/container.xml",
                     QByteArray("<?xml version=\"1.0\"?>\n"
                                "<container version=\"1.0\" xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\">\n"
                                "  <rootfiles>\n"
                                "    <rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>\n"
                                "  </rootfiles>\n"
                                "</container>\n")});
    files.push_back({"OEBPS/style.css", css_for(page).toUtf8()});

    QString manifest;
    QString spine;
    QString chapter_spine;
    int image_count = 0;
    QString chapter_body;
    int chapter_index = 0;
    bool in_front = true;
    QString front;
    auto flush_front = [&](const QString& file, const QString& id, const QString& title) {
        if (front.isEmpty()) {
            return;
        }
        files.push_back({"OEBPS/" + file, xhtml_page(title, front).toUtf8()});
        manifest += "    <item id=\"" + id + "\" href=\"" + file + "\" media-type=\"application/xhtml+xml\"/>\n";
        spine += "    <itemref idref=\"" + id + "\"/>\n";
        front.clear();
    };

    auto add_image = [&](const QString& source, bool cover) {
        const QByteArray bytes = read_bytes(source);
        if (bytes.isEmpty()) {
            return QString();
        }
        ++image_count;
        const QString name = (cover ? QStringLiteral("images/cover.") : QStringLiteral("images/image")) +
                             (cover ? image_extension(source) : QString::number(image_count) + "." + image_extension(source));
        const QString id = cover ? QStringLiteral("cover-image") : "image-" + QString::number(image_count);
        files.push_back({"OEBPS/" + name, bytes});
        manifest += "    <item id=\"" + id + "\" href=\"" + name + "\" media-type=\"" + media_type_for(source) + "\"" +
                    (cover ? " properties=\"cover-image\"" : "") + "/>\n";
        return name;
    };

    Piece::Section active = Piece::Section::Front;
    const QString title_label = request.details.title.trimmed().isEmpty() ? QStringLiteral("Title") : request.details.title.trimmed();
    for (const Piece& piece : pieces) {
        if (piece.section == Piece::Section::Copyright && active == Piece::Section::Front) {
            flush_front("title.xhtml", "title", title_label);
            active = Piece::Section::Copyright;
        }
        if (piece.section == Piece::Section::Contents) {
            if (active == Piece::Section::Front) {
                flush_front("title.xhtml", "title", title_label);
            } else {
                flush_front("copyright.xhtml", "copyright", "Copyright");
            }
            in_front = false;
            active = Piece::Section::Contents;
            continue;
        }
        if (piece.section == Piece::Section::Chapter && piece.kind == Piece::Kind::Heading) {
            if (chapter_index > 0 && !chapter_body.isEmpty()) {
                const QString file = "chapter-" + QString::number(chapter_index) + ".xhtml";
                const QString title = request.chapters.at(chapter_index - 1).title.trimmed().isEmpty()
                                          ? QStringLiteral("Untitled")
                                          : request.chapters.at(chapter_index - 1).title.trimmed();
                files.push_back({"OEBPS/" + file, xhtml_page(title, chapter_body).toUtf8()});
                manifest += "    <item id=\"chapter-" + QString::number(chapter_index) + "\" href=\"" + file +
                            "\" media-type=\"application/xhtml+xml\"/>\n";
                chapter_spine += "    <itemref idref=\"chapter-" + QString::number(chapter_index) + "\"/>\n";
            }
            ++chapter_index;
            chapter_body = "<h1>" + xml_escape(piece.text) + "</h1>\n";
            continue;
        }
        QString html;
        switch (piece.kind) {
            case Piece::Kind::Title:
                html = "<p class=\"title\">" + xml_escape(piece.text) + "</p>";
                break;
            case Piece::Kind::Subtitle:
                html = "<p class=\"subtitle\">" + xml_escape(piece.text) + "</p>";
                break;
            case Piece::Kind::Byline:
                html = "<p class=\"byline\">" + xml_escape(piece.text) + "</p>";
                break;
            case Piece::Kind::Heading:
                html = "<h2>" + xml_escape(piece.text) + "</h2>";
                break;
            case Piece::Kind::Scene:
                html = "<p class=\"scene\">* * *</p>";
                break;
            case Piece::Kind::Blank:
                html = "<p class=\"blank\">&#160;</p>";
                break;
            case Piece::Kind::Image: {
                const bool cover = in_front && image_count == 0 && piece.image_path == request.cover_path;
                const QString name = add_image(piece.image_path, cover);
                if (name.isEmpty()) {
                    if (!piece.image_alt.isEmpty()) {
                        html = "<p class=\"plain\">" + xml_escape(piece.image_alt) + "</p>";
                    }
                } else {
                    html = "<p class=\"" + QString(cover ? "cover" : "plain") + "\"><img src=\"" + name + "\" alt=\"" +
                           xml_escape(piece.image_alt) + "\"/></p>";
                }
                break;
            }
            case Piece::Kind::Prose: {
                const QString body = html_runs(piece.runs);
                if (body.isEmpty()) {
                    html = "<p class=\"blank\">&#160;</p>";
                } else {
                    html = "<p" + QString(piece.indent ? "" : " class=\"plain\"") + ">" + body + "</p>";
                }
                break;
            }
        }
        if (html.isEmpty()) {
            continue;
        }
        if (in_front) {
            front += html + "\n";
        } else if (chapter_index > 0) {
            chapter_body += html + "\n";
        }
    }
    if (chapter_index > 0) {
        const QString file = "chapter-" + QString::number(chapter_index) + ".xhtml";
        const QString title = request.chapters.at(chapter_index - 1).title.trimmed().isEmpty()
                                  ? QStringLiteral("Untitled")
                                  : request.chapters.at(chapter_index - 1).title.trimmed();
        files.push_back({"OEBPS/" + file, xhtml_page(title, chapter_body).toUtf8()});
        manifest += "    <item id=\"chapter-" + QString::number(chapter_index) + "\" href=\"" + file +
                    "\" media-type=\"application/xhtml+xml\"/>\n";
        chapter_spine += "    <itemref idref=\"chapter-" + QString::number(chapter_index) + "\"/>\n";
    }
    QString clean_nav = "  <h1>Contents</h1>\n  <ol>\n";
    if (manifest.contains("id=\"copyright\"")) {
        clean_nav += "    <li><a href=\"copyright.xhtml\">Copyright</a></li>\n";
    }
    for (int index = 1; index <= chapter_index; ++index) {
        const QString title = request.chapters.at(index - 1).title.trimmed().isEmpty() ? QStringLiteral("Untitled")
                                                                                        : request.chapters.at(index - 1).title.trimmed();
        clean_nav += "    <li><a href=\"chapter-" + QString::number(index) + ".xhtml\">" + xml_escape(title) + "</a></li>\n";
    }
    clean_nav += "  </ol>\n";
    files.push_back({"OEBPS/nav.xhtml", xhtml_page("Contents", "<nav epub:type=\"toc\" id=\"toc\">\n" + clean_nav + "</nav>\n").toUtf8()});
    manifest += "    <item id=\"nav\" href=\"nav.xhtml\" media-type=\"application/xhtml+xml\" properties=\"nav\"/>\n";
    manifest += "    <item id=\"css\" href=\"style.css\" media-type=\"text/css\"/>\n";
    spine += "    <itemref idref=\"nav\"/>\n";
    spine += chapter_spine;

    const QString book_title = request.details.title.trimmed().isEmpty() ? QStringLiteral("Untitled") : request.details.title.trimmed();
    const QString modified = QDateTime::currentDateTimeUtc().toString("yyyy-MM-ddThh:mm:ssZ");
    QString opf = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                  "<package xmlns=\"http://www.idpf.org/2007/opf\" version=\"3.0\" unique-identifier=\"bookid\" xml:lang=\"en\">\n"
                  "  <metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\">\n"
                  "    <dc:identifier id=\"bookid\">" +
                  xml_escape(identifier_for(request.details)) + "</dc:identifier>\n    <dc:title>" + xml_escape(book_title) +
                  "</dc:title>\n    <dc:language>en</dc:language>\n";
    if (!request.details.author.trimmed().isEmpty()) {
        opf += "    <dc:creator>" + xml_escape(request.details.author.trimmed()) + "</dc:creator>\n";
    }
    if (!request.details.year.trimmed().isEmpty()) {
        opf += "    <dc:date>" + xml_escape(request.details.year.trimmed()) + "</dc:date>\n";
    }
    opf += "    <meta property=\"dcterms:modified\">" + modified + "</meta>\n";
    if (manifest.contains("cover-image")) {
        opf += "    <meta name=\"cover\" content=\"cover-image\"/>\n";
    }
    opf += "  </metadata>\n  <manifest>\n" + manifest + "  </manifest>\n  <spine>\n" + spine + "  </spine>\n</package>\n";
    files.push_back({"OEBPS/content.opf", opf.toUtf8()});
    return write_zip(path, files, "mimetype", error);
}

QString docx_runs(const std::vector<MarkupRun>& runs, const QString& face, int half_points, bool bold, bool italic) {
    auto props = [&](bool run_bold, bool run_italic) {
        QString style = "<w:rPr><w:rFonts w:ascii=\"" + xml_escape(face) + "\" w:hAnsi=\"" + xml_escape(face) + "\" w:cs=\"" + xml_escape(face) + "\"/>";
        if (run_bold) {
            style += "<w:b/>";
        }
        if (run_italic) {
            style += "<w:i/>";
        }
        style += "<w:sz w:val=\"" + QString::number(half_points) + "\"/><w:szCs w:val=\"" + QString::number(half_points) + "\"/></w:rPr>";
        return style;
    };
    if (runs.empty()) {
        return "<w:r>" + props(bold, italic) + "<w:t></w:t></w:r>";
    }
    QString out;
    for (const MarkupRun& run : runs) {
        const QString text = QString::fromStdString(run.text);
        if (text.isEmpty()) {
            continue;
        }
        const bool preserve = text.front().isSpace() || text.back().isSpace() || text.contains("  ");
        out += "<w:r>" + props(bold || run.bold, italic || run.italic) + (preserve ? "<w:t xml:space=\"preserve\">" : "<w:t>") + xml_escape(text) +
               "</w:t></w:r>";
    }
    return out;
}

QString docx_paragraph(const QString& runs, bool page_break, bool center, bool indent, bool justify, int line, int first_twips, int before, int after) {
    QString paragraph = "<w:p><w:pPr>";
    if (page_break) {
        paragraph += "<w:pageBreakBefore/>";
    }
    paragraph += "<w:spacing w:before=\"" + QString::number(before) + "\" w:after=\"" + QString::number(after) + "\" w:line=\"" + QString::number(line) +
                 "\" w:lineRule=\"auto\"/>";
    paragraph += "<w:ind w:firstLine=\"" + QString::number(indent ? first_twips : 0) + "\"/>";
    paragraph += "<w:jc w:val=\"" + QString(center ? "center" : (justify ? "both" : "left")) + "\"/>";
    paragraph += "</w:pPr>" + runs + "</w:p>";
    return paragraph;
}

QString docx_drawing(const QString& relation, int width_emu, int height_emu, int picture_id) {
    const QString cx = QString::number(width_emu);
    const QString cy = QString::number(height_emu);
    const QString id = QString::number(picture_id);
    return "<w:r><w:drawing><wp:inline distT=\"0\" distB=\"0\" distL=\"0\" distR=\"0\">"
           "<wp:extent cx=\"" +
           cx + "\" cy=\"" + cy +
           "\"/>"
           "<wp:effectExtent l=\"0\" t=\"0\" r=\"0\" b=\"0\"/>"
           "<wp:docPr id=\"" +
           id + "\" name=\"Picture" + id +
           "\"/>"
           "<wp:cNvGraphicFramePr><a:graphicFrameLocks xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" noChangeAspect=\"1\"/></wp:cNvGraphicFramePr>"
           "<a:graphic xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\">"
           "<a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/picture\">"
           "<pic:pic xmlns:pic=\"http://schemas.openxmlformats.org/drawingml/2006/picture\">"
           "<pic:nvPicPr><pic:cNvPr id=\"" +
           id + "\" name=\"Picture" + id +
           "\"/><pic:cNvPicPr/></pic:nvPicPr>"
           "<pic:blipFill><a:blip r:embed=\"" +
           relation +
           "\"/><a:stretch><a:fillRect/></a:stretch></pic:blipFill>"
           "<pic:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"" +
           cx + "\" cy=\"" + cy +
           "\"/></a:xfrm>"
           "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></pic:spPr>"
           "</pic:pic></a:graphicData></a:graphic></wp:inline></w:drawing></w:r>";
}

bool export_docx(const ExportRequest& request, const PageSettings& page, const std::vector<Piece>& pieces, const QString& path, QString& error) {
    const QString face = face_name(page);
    const int body_half = page.font_size * 2;
    const int line = 240 * page.line_percent / 100;
    const int first_twips = page.indent_ems * page.font_size * 20;
    const BoundBook bound = bound_book(request);
    const int page_twips_w = static_cast<int>(bound.width * 1440.0 + 0.5);
    const int page_twips_h = static_cast<int>(bound.height * 1440.0 + 0.5);
    const int margin_twips = static_cast<int>(bound.margin * 1440.0 + 0.5);
    const qreal max_emu = (bound.width - (bound.margin * 2.0)) * 914400.0;

    QString body;
    QString relationships =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">\n"
        "  <Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>\n";
    QVector<QPair<QString, QByteArray>> files;
    int picture = 0;
    int relation = 1;
    bool opened = false;

    for (const Piece& piece : pieces) {
        const bool page_break = piece.page_break && opened;
        opened = true;
        if (piece.kind == Piece::Kind::Image) {
            const QByteArray bytes = read_bytes(piece.image_path);
            QImage image;
            image.loadFromData(bytes);
            if (bytes.isEmpty() || image.isNull()) {
                if (!piece.image_alt.isEmpty()) {
                    body += docx_paragraph(docx_runs({MarkupRun{piece.image_alt.toStdString(), false, false}}, face, body_half, false, false), page_break,
                                           true, false, false, line, first_twips, 120, 120);
                }
                continue;
            }
            ++picture;
            ++relation;
            const QString relation_id = "rId" + QString::number(relation);
            const QString media = "media/image" + QString::number(picture) + "." + image_extension(piece.image_path);
            qreal width = image.width() * (914400.0 / 96.0);
            qreal height = image.height() * (914400.0 / 96.0);
            if (width > max_emu && width > 0) {
                height *= max_emu / width;
                width = max_emu;
            }
            files.push_back({"word/" + media, bytes});
            relationships += "  <Relationship Id=\"" + relation_id +
                             "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/image\" Target=\"" + media + "\"/>\n";
            body += docx_paragraph(docx_drawing(relation_id, static_cast<int>(width), static_cast<int>(height), picture), page_break, true, false, false,
                                   line, first_twips, 120, 120);
            continue;
        }
        const bool center = piece.kind == Piece::Kind::Title || piece.kind == Piece::Kind::Subtitle || piece.kind == Piece::Kind::Byline ||
                            piece.kind == Piece::Kind::Heading || piece.kind == Piece::Kind::Scene;
        int half = body_half;
        bool bold = false;
        bool italic = piece.kind == Piece::Kind::Subtitle;
        int before = 0;
        int after = 0;
        if (piece.kind == Piece::Kind::Title) {
            half = (page.font_size + 14) * 2;
            bold = true;
            before = 360;
            after = 160;
        } else if (piece.kind == Piece::Kind::Heading) {
            half = (page.font_size + 6) * 2;
            bold = true;
            before = 200;
            after = 240;
        } else if (piece.kind == Piece::Kind::Scene) {
            before = 200;
            after = 200;
        }
        std::vector<MarkupRun> runs = piece.runs;
        if (runs.empty() && !piece.text.isEmpty()) {
            runs.push_back(MarkupRun{piece.text.toStdString(), false, false});
        }
        if (piece.kind == Piece::Kind::Blank) {
            runs.clear();
            runs.push_back(MarkupRun{" ", false, false});
        }
        const bool manuscript = piece.section == Piece::Section::Chapter && piece.kind != Piece::Kind::Heading;
        const bool justify = page.justify && manuscript && piece.kind == Piece::Kind::Prose;
        body += docx_paragraph(docx_runs(runs, face, half, bold, italic), page_break, center, piece.indent, justify, manuscript ? line : 240, first_twips,
                               before, after);
    }

    body += "<w:sectPr><w:pgSz w:w=\"" + QString::number(page_twips_w) + "\" w:h=\"" + QString::number(page_twips_h) + "\"/><w:pgMar w:top=\"" +
            QString::number(margin_twips) + "\" w:right=\"" + QString::number(margin_twips) + "\" w:bottom=\"" + QString::number(margin_twips) +
            "\" w:left=\"" + QString::number(margin_twips) + "\" w:header=\"720\" w:footer=\"720\" w:gutter=\"0\"/></w:sectPr>";
    const QString document =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" "
        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
        "xmlns:wp=\"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing\">"
        "<w:body>" +
        body + "</w:body></w:document>";
    relationships += "</Relationships>\n";

    const QString styles =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<w:styles xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
        "<w:style w:type=\"paragraph\" w:default=\"1\" w:styleId=\"Normal\"><w:name w:val=\"Normal\"/>"
        "<w:rPr><w:rFonts w:ascii=\"" +
        xml_escape(face) + "\" w:hAnsi=\"" + xml_escape(face) + "\"/><w:sz w:val=\"" + QString::number(body_half) + "\"/></w:rPr>"
        "<w:pPr><w:spacing w:line=\"" +
        QString::number(line) + "\" w:lineRule=\"auto\"/></w:pPr></w:style></w:styles>";

    const QString book_title = request.details.title.trimmed().isEmpty() ? QStringLiteral("Untitled") : request.details.title.trimmed();
    const QString created = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    const QString core = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                         "<cp:coreProperties xmlns:cp=\"http://schemas.openxmlformats.org/package/2006/metadata/core-properties\" "
                         "xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:dcterms=\"http://purl.org/dc/terms/\" "
                         "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">"
                         "<dc:title>" +
                         xml_escape(book_title) + "</dc:title><dc:creator>" + xml_escape(request.details.author.trimmed()) +
                         "</dc:creator><cp:lastModifiedBy>Inkwell</cp:lastModifiedBy>"
                         "<dcterms:created xsi:type=\"dcterms:W3CDTF\">" +
                         created + "</dcterms:created><dcterms:modified xsi:type=\"dcterms:W3CDTF\">" + created +
                         "</dcterms:modified></cp:coreProperties>";

    const QString content_types =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Default Extension=\"png\" ContentType=\"image/png\"/>"
        "<Default Extension=\"jpg\" ContentType=\"image/jpeg\"/>"
        "<Default Extension=\"jpeg\" ContentType=\"image/jpeg\"/>"
        "<Default Extension=\"gif\" ContentType=\"image/gif\"/>"
        "<Default Extension=\"webp\" ContentType=\"image/webp\"/>"
        "<Override PartName=\"/word/document.xml\" "
        "ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
        "<Override PartName=\"/word/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml\"/>"
        "<Override PartName=\"/docProps/core.xml\" ContentType=\"application/vnd.openxmlformats-package.core-properties+xml\"/>"
        "</Types>";

    const QString root_rels =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" "
        "Target=\"word/document.xml\"/>"
        "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties\" "
        "Target=\"docProps/core.xml\"/>"
        "</Relationships>";

    files.push_back({"[Content_Types].xml", content_types.toUtf8()});
    files.push_back({"_rels/.rels", root_rels.toUtf8()});
    files.push_back({"word/document.xml", document.toUtf8()});
    files.push_back({"word/styles.xml", styles.toUtf8()});
    files.push_back({"word/_rels/document.xml.rels", relationships.toUtf8()});
    files.push_back({"docProps/core.xml", core.toUtf8()});
    return write_zip(path, files, {}, error);
}

bool export_pdf(const ExportRequest& request, const PageSettings& page, const std::vector<Piece>& pieces, const QString& path, QString& error) {
    QByteArray bytes;
    {
        QBuffer buffer(&bytes);
        if (!buffer.open(QIODevice::WriteOnly)) {
            error = "Could not write " + path;
            return false;
        }
        QPdfWriter writer(&buffer);
        writer.setTitle(request.details.title);
        writer.setCreator("Inkwell");
        if (!request.details.author.trimmed().isEmpty()) {
            writer.setAuthor(request.details.author.trimmed());
        }
        const BoundBook bound = bound_book(request);
        writer.setResolution(72);
        writer.setPageSize(QPageSize(QSizeF(bound.width, bound.height), QPageSize::Inch));
        writer.setPageMargins(QMarginsF(bound.margin, bound.margin, bound.margin, bound.margin), QPageLayout::Inch);

        QFont body(face_name(page));
        body.setPointSize(page.font_size);
        body.setStyleHint(QFont::Serif);
        const qreal em = QFontMetricsF(body, &writer).horizontalAdvance(QStringLiteral("M"));

        QTextDocument document;
        document.documentLayout()->setPaintDevice(&writer);
        document.setDefaultFont(body);
        document.setDocumentMargin(0);
        const QSizeF paint(writer.width() > 0 ? writer.width() : (bound.width - bound.margin * 2.0) * 72.0,
                           writer.height() > 0 ? writer.height() : (bound.height - bound.margin * 2.0) * 72.0);
        document.setPageSize(paint);
        QTextOption option;
        option.setWrapMode(QTextOption::WordWrap);
        document.setDefaultTextOption(option);

        QTextCursor cursor(&document);
        bool started = false;
        auto place = [&](const QTextBlockFormat& format, const QTextCharFormat& chars) {
            if (!started) {
                cursor.setBlockFormat(format);
                cursor.setCharFormat(chars);
                started = true;
                return;
            }
            cursor.insertBlock(format, chars);
        };
        auto format_for = [&](bool center, bool justify, bool indent, bool page_break, qreal before, qreal after, int leading) {
            QTextBlockFormat format;
            format.setLineHeight(leading, QTextBlockFormat::ProportionalHeight);
            format.setAlignment(center ? Qt::AlignHCenter : (justify ? Qt::AlignJustify : Qt::AlignLeft));
            format.setTextIndent(indent ? em * page.indent_ems : 0);
            format.setTopMargin(before);
            format.setBottomMargin(after);
            if (page_break && started) {
                format.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
            }
            return format;
        };

        for (const Piece& piece : pieces) {
            if (piece.kind == Piece::Kind::Image) {
                QImage image(piece.image_path);
                QTextCharFormat chars;
                chars.setFont(body);
                chars.setForeground(Qt::black);
                place(format_for(true, false, false, piece.page_break, 8, 8, 120), chars);
                if (image.isNull()) {
                    if (!piece.image_alt.isEmpty()) {
                        cursor.insertText(piece.image_alt);
                    }
                    continue;
                }
                const QUrl url = QUrl::fromLocalFile(piece.image_path);
                document.addResource(QTextDocument::ImageResource, url, image);
                QTextImageFormat picture;
                picture.setName(url.toString());
                qreal width = image.width();
                qreal height = image.height();
                const qreal limit = paint.width();
                if (width > limit && width > 0) {
                    height *= limit / width;
                    width = limit;
                }
                picture.setWidth(width);
                picture.setHeight(height);
                cursor.insertImage(picture);
                continue;
            }
            QFont face = body;
            bool center = false;
            qreal before = 0;
            qreal after = 0;
            if (piece.kind == Piece::Kind::Title) {
                face.setPointSize(page.font_size + 14);
                face.setBold(true);
                center = true;
                before = 18;
                after = 8;
            } else if (piece.kind == Piece::Kind::Subtitle) {
                face.setItalic(true);
                center = true;
                after = 6;
            } else if (piece.kind == Piece::Kind::Byline) {
                center = true;
                before = 10;
            } else if (piece.kind == Piece::Kind::Heading) {
                face.setPointSize(page.font_size + 6);
                face.setBold(true);
                center = true;
                before = 8;
                after = 12;
            } else if (piece.kind == Piece::Kind::Scene) {
                center = true;
                before = 10;
                after = 10;
            }
            QTextCharFormat chars;
            chars.setFont(face);
            chars.setForeground(Qt::black);
            const bool manuscript = piece.section == Piece::Section::Chapter && piece.kind != Piece::Kind::Heading;
            const bool justify = page.justify && manuscript && piece.kind == Piece::Kind::Prose;
            place(format_for(center, justify, piece.indent, piece.page_break, before, after, manuscript ? page.line_percent : 120), chars);
            if (piece.kind == Piece::Kind::Blank) {
                cursor.insertText(QStringLiteral(" "));
                continue;
            }
            if (piece.kind == Piece::Kind::Scene) {
                cursor.insertText(QStringLiteral("* * *"));
                continue;
            }
            if (!piece.runs.empty() && piece.kind == Piece::Kind::Prose) {
                for (const MarkupRun& run : piece.runs) {
                    QTextCharFormat run_chars = chars;
                    run_chars.setFontItalic(run.italic);
                    run_chars.setFontWeight(run.bold ? QFont::Bold : QFont::Normal);
                    cursor.insertText(QString::fromStdString(run.text), run_chars);
                }
                continue;
            }
            if (!piece.text.isEmpty()) {
                cursor.insertText(piece.text);
            }
        }
        document.print(&writer);
    }
    if (!bytes.startsWith("%PDF")) {
        error = "Could not write " + path;
        return false;
    }
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) {
        error = "Could not write " + path;
        return false;
    }
    return true;
}

bool export_text(const ExportRequest& request, const QString& path, QString& error) {
    QString pages;
    if (request.include_copyright) {
        const QString copyright = request.details.copyrightPage();
        if (!copyright.trimmed().isEmpty()) {
            pages += copyright + "\n\n";
        }
    }
    QStringList titles;
    for (const Leaf& leaf : request.chapters) {
        titles.push_back(leaf.title);
    }
    if (request.include_contents) {
        pages += BookDetails::contentsPage(titles);
        pages += "\n\n";
    }
    for (const Leaf& leaf : request.chapters) {
        const QString heading = leaf.title.trimmed().isEmpty() ? QStringLiteral("Untitled") : leaf.title.trimmed();
        pages += heading + "\n\n" + leaf.body;
        if (!pages.endsWith('\n')) {
            pages += '\n';
        }
        pages += '\n';
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(pages.toUtf8()) < 0 || !file.commit()) {
        error = "Could not write " + path;
        return false;
    }
    return true;
}

}  // namespace

bool export_book(const ExportRequest& request, const QString& path, ExportFormat format, QString& error) {
    if (path.isEmpty()) {
        error = "Choose a file for the book.";
        return false;
    }
    PageSettings page = request.page;
    page.clamp();
    if (format != ExportFormat::Text) {
        page.font_size = std::clamp(request.body_size, 9, 14);
        page.line_percent = std::clamp(request.body_leading, 110, 160);
    }
    if (format == ExportFormat::Text) {
        return export_text(request, path, error);
    }
    const std::vector<Piece> pieces = compose(request, page);
    switch (format) {
        case ExportFormat::Pdf:
            return export_pdf(request, page, pieces, path, error);
        case ExportFormat::Epub:
            return export_epub(request, page, pieces, path, error);
        case ExportFormat::Docx:
            return export_docx(request, page, pieces, path, error);
        case ExportFormat::Text:
            break;
    }
    return export_text(request, path, error);
}

}  // namespace inkwell
