#include "details.hpp"

#include "book.hpp"
#include "markup.hpp"
#include "text.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTextStream>

namespace inkwell {
namespace {

QString clean_line(QString text) {
    text.replace('\r', ' ');
    text.replace('\n', ' ');
    return text.trimmed();
}

void write_line(QTextStream& out, const QString& key, const QString& value) {
    out << key << '=' << clean_line(value) << '\n';
}

void write_flag(QTextStream& out, const QString& key, bool value) {
    out << key << '=' << (value ? '1' : '0') << '\n';
}

void add_paragraph(QStringList& lines, const QString& text) {
    if (!text.trimmed().isEmpty()) {
        if (!lines.isEmpty()) {
            lines.push_back({});
        }
        lines.push_back(text.trimmed());
    }
}

}  // namespace

bool BookDetails::writeFile(const QString& directory, QString& error) const {
    QDir dir(directory);
    if (!dir.exists() && !dir.mkpath(".")) {
        error = "Could not create the book folder";
        return false;
    }
    QSaveFile file(dir.filePath("details"));
    if (!file.open(QIODevice::WriteOnly)) {
        error = "Could not write the book details";
        return false;
    }
    QTextStream out(&file);
    out << "inkwell-details 1\n";
    write_line(out, "title", title);
    write_line(out, "subtitle", subtitle);
    write_line(out, "author", author);
    write_line(out, "edition", edition);
    write_line(out, "year", year);
    write_line(out, "isbn_epub", isbn_epub);
    write_line(out, "isbn_paperback", isbn_paperback);
    write_line(out, "isbn_pdf", isbn_pdf);
    write_line(out, "isbn_kindle", isbn_kindle);
    write_line(out, "isbn_hardcover", isbn_hardcover);
    write_line(out, "publisher", publisher);
    write_line(out, "extra", extra);
    write_flag(out, "rights", rights);
    write_flag(out, "fiction", fiction);
    write_flag(out, "moral", moral);
    write_flag(out, "external", external);
    write_flag(out, "designations", designations);
    out.flush();
    if (out.status() != QTextStream::Ok || !file.commit()) {
        error = "Could not finish the book details";
        return false;
    }
    return true;
}

BookDetails BookDetails::readFile(const QString& directory) {
    BookDetails details;
    QFile file(QDir(directory).filePath("details"));
    if (!file.open(QIODevice::ReadOnly)) {
        return details;
    }
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith("inkwell-details")) {
            continue;
        }
        const int split = line.indexOf('=');
        if (split <= 0) {
            continue;
        }
        const QString key = line.left(split);
        const QString value = line.mid(split + 1);
        if (key == "title") {
            details.title = value;
        } else if (key == "subtitle") {
            details.subtitle = value;
        } else if (key == "author") {
            details.author = value;
        } else if (key == "edition") {
            details.edition = value;
        } else if (key == "year") {
            details.year = value;
        } else if (key == "isbn_epub") {
            details.isbn_epub = value;
        } else if (key == "isbn_paperback") {
            details.isbn_paperback = value;
        } else if (key == "isbn_pdf") {
            details.isbn_pdf = value;
        } else if (key == "isbn_kindle") {
            details.isbn_kindle = value;
        } else if (key == "isbn_hardcover") {
            details.isbn_hardcover = value;
        } else if (key == "publisher") {
            details.publisher = value;
        } else if (key == "extra") {
            details.extra = value;
        } else if (key == "rights") {
            details.rights = value != "0";
        } else if (key == "fiction") {
            details.fiction = value == "1";
        } else if (key == "moral") {
            details.moral = value == "1";
        } else if (key == "external") {
            details.external = value == "1";
        } else if (key == "designations") {
            details.designations = value == "1";
        }
    }
    return details;
}

QString BookDetails::copyrightPage() const {
    QStringList lines;
    add_paragraph(lines, title);
    add_paragraph(lines, subtitle);
    add_paragraph(lines, author);
    QString edition_line = edition.trimmed();
    if (!year.trimmed().isEmpty()) {
        edition_line = edition_line.isEmpty() ? year.trimmed() : edition_line + ", " + year.trimmed();
    }
    add_paragraph(lines, edition_line);
    const auto isbn = [&lines](const QString& label, const QString& value) {
        if (!value.trimmed().isEmpty()) {
            add_paragraph(lines, label + " " + value.trimmed());
        }
    };
    isbn("EPUB", isbn_epub);
    isbn("Paperback", isbn_paperback);
    isbn("PDF", isbn_pdf);
    isbn("Kindle", isbn_kindle);
    isbn("Hardcover", isbn_hardcover);
    add_paragraph(lines, publisher);
    if (rights) {
        add_paragraph(lines, "All rights reserved. No part of this book may be reproduced, stored, or transmitted in any form "
                             "without written permission from the publisher.");
    }
    if (fiction) {
        add_paragraph(lines, "This is a work of fiction. Names, characters, places, and incidents are the product of the author's "
                             "imagination or are used fictitiously. Any resemblance to actual persons or events is coincidental.");
    }
    if (moral && !author.trimmed().isEmpty()) {
        add_paragraph(lines, author.trimmed() + " asserts the moral right to be identified as the author of this work.");
    } else if (moral) {
        add_paragraph(lines, "The author asserts the moral right to be identified as the author of this work.");
    }
    if (external) {
        add_paragraph(lines, "The author has no responsibility for the persistence or accuracy of links to other sites, and does not "
                             "endorse their content.");
    }
    if (designations) {
        add_paragraph(lines, "Product names used in this book are trademarks of their owners. They are used without permission and "
                             "without endorsement.");
    }
    add_paragraph(lines, extra);
    return lines.join("\n");
}

QString BookDetails::contentsPage(const QStringList& chapters) {
    QStringList lines;
    lines.push_back("Contents");
    lines.push_back({});
    for (const QString& chapter : chapters) {
        const QString title = chapter.trimmed().isEmpty() ? QStringLiteral("Untitled") : chapter.trimmed();
        lines.push_back(title);
    }
    return lines.join("\n");
}

int book_word_count(const QString& directory) {
    Book book;
    QString error;
    if (!Book::read(directory, book, error)) {
        return 0;
    }
    int total = 0;
    for (const Leaf& leaf : book.chapters) {
        const std::string plain = markup_plain(leaf.body.toStdString());
        total += static_cast<int>(scan_words(plain).size());
    }
    return total;
}

QString cover_file(const QString& directory) {
    const QDir dir(directory);
    for (const char* name : {"cover.png", "cover.jpg", "cover.jpeg", "cover.webp"}) {
        const QString path = dir.filePath(name);
        if (QFileInfo::exists(path)) {
            return path;
        }
    }
    return {};
}

}  // namespace inkwell
