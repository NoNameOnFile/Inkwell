#pragma once

#include <QString>
#include <QVector>

namespace inkwell {

struct Leaf {
    QString id;
    QString title;
    QString body;
};

struct Book {
    QVector<Leaf> chapters;
    QVector<Leaf> notes[4];

    static Book single(const QString& title, const QString& body);
    bool write(const QString& directory, QString& error) const;
    static bool read(const QString& directory, Book& book, QString& error);
};

const char* section_key(int section);
const char* section_label(int section);
int section_index(const QString& key);

}  // namespace inkwell
