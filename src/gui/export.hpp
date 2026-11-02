#pragma once

#include "book.hpp"
#include "details.hpp"
#include "pagesettings.hpp"

#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

namespace inkwell {

enum class ExportFormat { Pdf, Epub, Docx, Text };

struct ExportRequest {
    BookDetails details;
    QVector<Leaf> chapters;
    PageSettings page;
    QString resource_root;
    QString cover_path;
    double trim_width = 6.0;
    double trim_height = 9.0;
    double margin_inches = 0.85;
    int body_size = 11;
    int body_leading = 130;
    bool include_cover = true;
    bool include_title = true;
    bool include_copyright = true;
    bool include_contents = true;
};

class ExportSheet : public QWidget {
    Q_OBJECT

public:
    explicit ExportSheet(QWidget* parent = nullptr);
    ~ExportSheet() override;
    void setDark(bool dark);
    void setChapters(const QStringList& titles, const QVector<int>& words, bool has_cover);
    ExportFormat format() const;
    void fill(ExportRequest& request) const;
    QVector<int> checkedChapters() const;

signals:
    void backRequested();
    void exportRequested();

private:
    void refresh();
    void remember() const;
    bool printFormat() const;

    struct Fields;
    Fields* fields_ = nullptr;
};

bool export_book(const ExportRequest& request, const QString& path, ExportFormat format, QString& error);

}  // namespace inkwell
