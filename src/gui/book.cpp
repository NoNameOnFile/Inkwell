#include "book.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTextStream>

namespace inkwell {
namespace {

QString clean_title(QString title) {
    title.replace('\t', ' ');
    title.replace('\n', ' ');
    title.replace('\r', ' ');
    title = title.trimmed();
    return title.isEmpty() ? QString("Untitled") : title;
}

QString read_body(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QString text = QString::fromUtf8(file.readAll());
    text.replace("\r\n", "\n");
    text.replace('\r', '\n');
    if (text.endsWith('\n')) {
        text.chop(1);
    }
    return text;
}

bool write_body(const QString& path, const QString& body, QString& error) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        error = "Could not write " + path;
        return false;
    }
    QString text = body;
    if (!text.endsWith('\n')) {
        text += '\n';
    }
    if (file.write(text.toUtf8()) < 0 || !file.commit()) {
        error = "Could not finish " + path;
        return false;
    }
    return true;
}

bool safe_id(const QString& id) {
    if (id.isEmpty()) {
        return false;
    }
    for (const QChar ch : id) {
        const bool ok = ch.isLetterOrNumber() || ch == QLatin1Char('-') || ch == QLatin1Char('_');
        if (!ok) {
            return false;
        }
    }
    return true;
}

}  // namespace

const char* section_key(int section) {
    switch (section) {
        case 0:
            return "character";
        case 1:
            return "plot";
        case 2:
            return "world";
        default:
            return "general";
    }
}

const char* section_label(int section) {
    switch (section) {
        case 0:
            return "Character";
        case 1:
            return "Plot";
        case 2:
            return "Worldbuilding";
        default:
            return "General";
    }
}

int section_index(const QString& key) {
    const QString name = key.trimmed().toLower();
    if (name == "character") {
        return 0;
    }
    if (name == "plot") {
        return 1;
    }
    if (name == "world" || name == "worldbuilding") {
        return 2;
    }
    if (name == "general") {
        return 3;
    }
    return -1;
}

Book Book::single(const QString& title, const QString& body) {
    Book book;
    book.chapters.push_back(Leaf{"1", clean_title(title), body});
    return book;
}

bool Book::write(const QString& directory, QString& error) const {
    QDir dir(directory);
    if (!dir.mkpath("chapters") || !dir.mkpath("notes")) {
        error = "Could not create the book folder";
        return false;
    }
    QSaveFile manifest(dir.filePath("inkwell.book"));
    if (!manifest.open(QIODevice::WriteOnly)) {
        error = "Could not write " + manifest.fileName();
        return false;
    }
    QTextStream out(&manifest);
    out << "inkwell-book 1\n";
    for (const Leaf& leaf : chapters) {
        if (!safe_id(leaf.id) || !write_body(dir.filePath("chapters/" + leaf.id + ".txt"), leaf.body, error)) {
            return false;
        }
        out << "chapter\t" << leaf.id << '\t' << clean_title(leaf.title) << '\n';
    }
    for (int section = 0; section < 4; ++section) {
        for (const Leaf& leaf : notes[section]) {
            if (!safe_id(leaf.id) || !write_body(dir.filePath("notes/" + leaf.id + ".txt"), leaf.body, error)) {
                return false;
            }
            out << "note\t" << section_key(section) << '\t' << leaf.id << '\t' << clean_title(leaf.title) << '\n';
        }
    }
    out.flush();
    if (out.status() != QTextStream::Ok || !manifest.commit()) {
        error = "Could not finish " + manifest.fileName();
        return false;
    }
    return true;
}

bool Book::read(const QString& directory, Book& book, QString& error) {
    QFile manifest(QDir(directory).filePath("inkwell.book"));
    if (!manifest.open(QIODevice::ReadOnly)) {
        error = "Could not open " + manifest.fileName();
        return false;
    }
    Book loaded;
    QTextStream in(&manifest);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.trimmed().isEmpty() || line.startsWith("inkwell-book")) {
            continue;
        }
        const QStringList parts = line.split('\t');
        if (parts.size() < 3) {
            continue;
        }
        if (parts[0] == "chapter") {
            if (!safe_id(parts[1])) {
                continue;
            }
            Leaf leaf;
            leaf.id = parts[1];
            leaf.title = parts.mid(2).join(' ');
            leaf.body = read_body(QDir(directory).filePath("chapters/" + leaf.id + ".txt"));
            loaded.chapters.push_back(leaf);
            continue;
        }
        if (parts[0] == "note" && parts.size() >= 4) {
            const int section = section_index(parts[1]);
            if (section < 0 || !safe_id(parts[2])) {
                continue;
            }
            Leaf leaf;
            leaf.id = parts[2];
            leaf.title = parts.mid(3).join(' ');
            leaf.body = read_body(QDir(directory).filePath("notes/" + leaf.id + ".txt"));
            loaded.notes[section].push_back(leaf);
        }
    }
    if (loaded.chapters.isEmpty()) {
        error = "The book has no chapters";
        return false;
    }
    book = loaded;
    return true;
}

}  // namespace inkwell
