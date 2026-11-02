#pragma once

#include <QString>
#include <QStringList>

namespace inkwell {

struct BookDetails {
    QString title;
    QString subtitle;
    QString author;
    QString edition = "First edition";
    QString year;
    QString isbn_epub;
    QString isbn_paperback;
    QString isbn_pdf;
    QString isbn_kindle;
    QString isbn_hardcover;
    QString publisher;
    QString extra;
    bool rights = true;
    bool fiction = false;
    bool moral = false;
    bool external = false;
    bool designations = false;

    bool writeFile(const QString& directory, QString& error) const;
    static BookDetails readFile(const QString& directory);
    QString copyrightPage() const;
    static QString contentsPage(const QStringList& chapters);
};

int book_word_count(const QString& directory);
QString cover_file(const QString& directory);

}  // namespace inkwell
