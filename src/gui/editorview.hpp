#pragma once

#include "pagesettings.hpp"

#include <QTextEdit>
#include <QVector>

class QMimeData;

namespace inkwell {

struct Underline {
    int start = 0;
    int end = 0;
    int kind = 0;
};

class EditorView : public QTextEdit {
    Q_OBJECT

public:
    explicit EditorView(QWidget* parent = nullptr);

    void setUnderlines(const QVector<Underline>& underlines);
    void setMarkup(const QString& markup);
    void setMatterText(const QString& text);
    QString markup() const;
    QString proseText() const;
    void setResourceRoot(const QString& directory);
    void setSourceMode(bool source);
    bool isSourceMode() const { return source_mode_; }
    void setPlaceholderText(const QString& text);
    void setItalic(bool italic);
    void setBold(bool bold);
    void setDark(bool dark);
    void setPageLook(const PageSettings& look, bool restyle);
    void insertSceneBreak();
    void insertImage(const QString& source);
    void insertField(const QString& name, const QString& value);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void insertFromMimeData(const QMimeData* source) override;

private:
    void applyPaper();
    void refreshSelections();
    void applyColumn();
    void rememberFonts();
    void restyleDocument();
    int imageLimit() const;
    QTextBlockFormat proseFormat() const;
    QTextBlockFormat sceneFormat() const;
    QTextBlockFormat fieldFormat(const QString& name) const;
    QString resolvePath(const QString& source) const;

    QVector<Underline> underlines_;
    QString resource_root_;
    QString prose_placeholder_ = "Start writing…";
    QFont serif_;
    QFont mono_;
    PageSettings look_;
    bool source_mode_ = false;
    bool plain_matter_ = false;
    bool applying_ = false;
    bool dark_ = false;
};

}  // namespace inkwell
