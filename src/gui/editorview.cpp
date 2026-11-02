#include "editorview.hpp"

#include "markup.hpp"
#include "theme.hpp"

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QKeyEvent>
#include <QMimeData>
#include <QResizeEvent>
#include <QStringList>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextOption>
#include <QTextFragment>
#include <QUrl>

namespace inkwell {
namespace {

constexpr int kKind = QTextFormat::UserProperty + 1;
constexpr int kFieldName = QTextFormat::UserProperty + 2;
constexpr int kImageSource = QTextFormat::UserProperty + 3;
constexpr int kImageAlt = QTextFormat::UserProperty + 4;
constexpr int kLabel = QTextFormat::UserProperty + 5;

QColor field_color(bool dark) {
    return deskTheme(activeTheme().id, dark).field_tint;
}

QString fragment_text(const QTextFragment& fragment) {
    return fragment.text();
}

QString normalize_manuscript(QString text) {
    text.replace("\r\n", "\n");
    text.replace('\r', '\n');
    QString cleaned;
    cleaned.reserve(text.size());
    for (const QChar ch : text) {
        const ushort code = ch.unicode();
        if (code == 0xFEFF || code == 0x2060 || code == 0x200B || code == 0x200C || code == 0x200D || code == 0x00AD ||
            code == 0xFFFC) {
            continue;
        }
        if (ch != '\n' && ch != '\t' && ch.isSpace()) {
            cleaned.append(QLatin1Char(' '));
            continue;
        }
        cleaned.append(ch);
    }
    return cleaned;
}

bool foreign_html(const QString& text) {
    const QString sample = text.left(6000).toLower();
    return sample.contains("<html") || sample.contains("<div") || sample.contains("<span") || sample.contains("<br") ||
           sample.contains("style=") || sample.contains("<!doctype") || sample.contains("&nbsp;");
}

}  // namespace

EditorView::EditorView(QWidget* parent) : QTextEdit(parent) {
    rememberFonts();
    setFont(serif_);
    setFrameStyle(QFrame::NoFrame);
    setLineWrapMode(WidgetWidth);
    setCursorWidth(2);
    setAcceptRichText(true);
    document()->setDocumentMargin(18);
    setPlaceholderText(prose_placeholder_);
    QTextCursor cursor = textCursor();
    cursor.setBlockFormat(proseFormat());
    setTextCursor(cursor);
    applyPaper();
    applyColumn();
    connect(document(), &QTextDocument::contentsChange, this, [this](int, int, int) {
        if (applying_ || source_mode_) {
            return;
        }
        applying_ = true;
        for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
            if (!block.blockFormat().hasProperty(kKind)) {
                QTextCursor fixer(block);
                fixer.setBlockFormat(proseFormat());
            }
        }
        applying_ = false;
    });
}

void EditorView::rememberFonts() {
    serif_.setFamilies(look_.families());
    serif_.setStyleHint(QFont::Serif);
    serif_.setPointSize(look_.font_size);
    mono_.setFamilies({"Iosevka", "Source Code Pro", "Liberation Mono", "DejaVu Sans Mono", "monospace"});
    mono_.setStyleHint(QFont::Monospace);
    mono_.setPointSize(qMax(11, look_.font_size - 4));
}

int EditorView::imageLimit() const {
    return qMax(180, look_.column_width - 120);
}

QTextBlockFormat EditorView::proseFormat() const {
    QTextBlockFormat format;
    format.setProperty(kKind, static_cast<int>(MarkupBlock::Kind::Paragraph));
    format.setAlignment(look_.justify ? Qt::AlignJustify : Qt::AlignLeft);
    format.setTextIndent(QFontMetricsF(serif_).horizontalAdvance(QStringLiteral("M")) * look_.indent_ems);
    format.setLineHeight(look_.line_percent, QTextBlockFormat::ProportionalHeight);
    format.setTopMargin(0);
    format.setBottomMargin(0);
    return format;
}

QTextBlockFormat EditorView::sceneFormat() const {
    QTextBlockFormat format;
    format.setProperty(kKind, static_cast<int>(MarkupBlock::Kind::Scene));
    format.setAlignment(Qt::AlignHCenter);
    format.setTextIndent(0);
    format.setLineHeight(140, QTextBlockFormat::ProportionalHeight);
    format.setTopMargin(16);
    format.setBottomMargin(16);
    return format;
}

QTextBlockFormat EditorView::fieldFormat(const QString& name) const {
    QTextBlockFormat format;
    format.setProperty(kKind, static_cast<int>(MarkupBlock::Kind::Field));
    format.setProperty(kFieldName, name);
    format.setAlignment(Qt::AlignLeft);
    format.setTextIndent(0);
    format.setLineHeight(140, QTextBlockFormat::ProportionalHeight);
    format.setTopMargin(12);
    format.setBottomMargin(12);
    format.setBackground(field_color(dark_));
    return format;
}

QString EditorView::resolvePath(const QString& source) const {
    if (source.isEmpty() || QFileInfo(source).isAbsolute() || resource_root_.isEmpty()) {
        return source;
    }
    return QDir(resource_root_).filePath(source);
}

void EditorView::setResourceRoot(const QString& directory) {
    resource_root_ = directory;
}

void EditorView::resizeEvent(QResizeEvent* event) {
    QTextEdit::resizeEvent(event);
    applyColumn();
}

void EditorView::changeEvent(QEvent* event) {
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange) {
        applyPaper();
    }
    QTextEdit::changeEvent(event);
}

void EditorView::insertFromMimeData(const QMimeData* source) {
    if (source == nullptr) {
        return;
    }
    // Ctrl+V, the Edit menu, and a drop all arrive here.
    if (!source->hasText() && !source->hasHtml()) {
        QTextEdit::insertFromMimeData(source);
        return;
    }
    QString text = source->hasText() ? source->text() : QString();
    if (text.trimmed().isEmpty() || foreign_html(text)) {
        if (source->hasHtml()) {
            QTextDocument page;
            page.setHtml(source->html());
            text = page.toPlainText();
        }
    }
    text = normalize_manuscript(text);
    if (source_mode_) {
        insertPlainText(text);
        return;
    }
    QTextCursor cursor = textCursor();
    const bool covers_all = cursor.hasSelection() && cursor.selectionStart() == 0 &&
                            cursor.selectionEnd() >= document()->characterCount() - 1;
    const bool blank = document()->characterCount() <= 1;
    if (blank || covers_all) {
        setMarkup(text);
        moveCursor(QTextCursor::End);
        applyColumn();
        return;
    }
    if (source->hasHtml()) {
        cursor.beginEditBlock();
        if (cursor.hasSelection()) {
            cursor.removeSelectedText();
        }
        cursor.insertText(text);
        cursor.endEditBlock();
        const int position = cursor.position();
        setMarkup(markup());
        QTextCursor restored = textCursor();
        restored.setPosition(qMin(position, qMax(0, document()->characterCount() - 1)));
        setTextCursor(restored);
        applyColumn();
        return;
    }
    applying_ = true;
    cursor.beginEditBlock();
    if (cursor.hasSelection()) {
        cursor.removeSelectedText();
    }
    const QStringList lines = text.split('\n');
    for (int i = 0; i < lines.size(); ++i) {
        if (i > 0) {
            cursor.insertBlock(proseFormat());
        }
        cursor.insertText(lines.at(i));
    }
    cursor.endEditBlock();
    applying_ = false;
    setTextCursor(cursor);
    applyColumn();
}

void EditorView::keyPressEvent(QKeyEvent* event) {
    const bool enter = event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter;
    if (!source_mode_ && enter && (event->modifiers() & Qt::ShiftModifier) == 0) {
        const int kind = textCursor().block().blockFormat().property(kKind).toInt();
        if (kind != static_cast<int>(MarkupBlock::Kind::Paragraph)) {
            QTextCursor cursor = textCursor();
            cursor.insertBlock(proseFormat());
            setTextCursor(cursor);
            return;
        }
    }
    QTextEdit::keyPressEvent(event);
}

void EditorView::applyColumn() {
    if (source_mode_) {
        setViewportMargins(32, 8, 32, 12);
    } else {
        const int column = qMin(look_.column_width, qMax(320, width() - 80));
        const int side = qMax(24, (width() - column) / 2);
        setViewportMargins(side, 10, qMax(24, width() - side - column), 16);
    }
    // QTextEdit wraps during resizeEvent, before these margins shrink the page.
    QTextOption option = document()->defaultTextOption();
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    document()->setDefaultTextOption(option);
    if (viewport()->width() > 0) {
        document()->setTextWidth(viewport()->width());
    }
}

void EditorView::setDark(bool dark) {
    dark_ = dark;
    applyPaper();
}

void EditorView::setPageLook(const PageSettings& look, bool restyle) {
    const bool modified = document()->isModified();
    const int position = textCursor().position();
    const bool pristine = document()->availableUndoSteps() == 0 && document()->availableRedoSteps() == 0;
    look_ = look;
    look_.clamp();
    rememberFonts();
    if (source_mode_) {
        setFont(mono_);
        document()->setDefaultFont(mono_);
    } else {
        setFont(serif_);
        document()->setDefaultFont(serif_);
    }
    applyColumn();
    applyPaper();
    if (restyle) {
        restyleDocument();
        if (pristine) {
            document()->clearUndoRedoStacks();
        }
        const int limit = qMax(0, document()->characterCount() - 1);
        QTextCursor cursor = textCursor();
        cursor.setPosition(qBound(0, position, limit));
        setTextCursor(cursor);
    }
    document()->setModified(modified);
}

void EditorView::restyleDocument() {
    struct Piece {
        int position = 0;
        int length = 0;
        QTextCharFormat format;
    };
    QVector<Piece> pieces;
    QVector<QPair<int, QTextBlockFormat>> paragraphs;
    for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
        const int kind = block.blockFormat().hasProperty(kKind) ? block.blockFormat().property(kKind).toInt()
                                                                 : static_cast<int>(MarkupBlock::Kind::Paragraph);
        if (!source_mode_ && !plain_matter_ && kind == static_cast<int>(MarkupBlock::Kind::Paragraph)) {
            QTextBlockFormat format = block.blockFormat();
            format.setAlignment(look_.justify ? Qt::AlignJustify : Qt::AlignLeft);
            format.setTextIndent(QFontMetricsF(serif_).horizontalAdvance(QStringLiteral("M")) * look_.indent_ems);
            format.setLineHeight(look_.line_percent, QTextBlockFormat::ProportionalHeight);
            paragraphs.push_back({block.position(), format});
        }
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (fragment.length() <= 0) {
                continue;
            }
            QTextCharFormat format = fragment.charFormat();
            if (format.isImageFormat()) {
                QTextImageFormat image = format.toImageFormat();
                if (image.width() <= imageLimit()) {
                    continue;
                }
                image.setWidth(imageLimit());
                pieces.push_back({fragment.position(), fragment.length(), image});
                continue;
            }
            QFont font = source_mode_ ? mono_ : serif_;
            if (format.property(kLabel).toBool()) {
                font.setWeight(QFont::DemiBold);
                font.setCapitalization(QFont::SmallCaps);
            } else {
                font.setItalic(format.fontItalic());
                font.setWeight(format.fontWeight() > QFont::Normal ? QFont::Bold : QFont::Normal);
            }
            format.setFont(font);
            pieces.push_back({fragment.position(), fragment.length(), format});
        }
    }
    applying_ = true;
    QTextCursor cursor(document());
    cursor.beginEditBlock();
    for (const auto& paragraph : paragraphs) {
        cursor.setPosition(paragraph.first);
        cursor.setBlockFormat(paragraph.second);
    }
    for (const Piece& piece : pieces) {
        cursor.setPosition(piece.position);
        cursor.setPosition(piece.position + piece.length, QTextCursor::KeepAnchor);
        cursor.setCharFormat(piece.format);
    }
    cursor.endEditBlock();
    applying_ = false;
    refreshSelections();
}

void EditorView::applyPaper() {
    const bool dark = dark_;
    const QColor paper = look_.paperFor(dark);
    const QColor ink = look_.inkFor(dark);
    QPalette colors = palette();
    colors.setColor(QPalette::Base, paper);
    colors.setColor(QPalette::Text, ink);
    colors.setColor(QPalette::Highlight, ink);
    colors.setColor(QPalette::HighlightedText, paper);
    colors.setColor(QPalette::PlaceholderText, QColor((paper.red() + ink.red()) / 2, (paper.green() + ink.green()) / 2,
                                                      (paper.blue() + ink.blue()) / 2));
    setPalette(colors);
    if (!source_mode_) {
        applying_ = true;
        for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
            if (block.blockFormat().property(kKind).toInt() == static_cast<int>(MarkupBlock::Kind::Field)) {
                QTextBlockFormat format = block.blockFormat();
                format.setBackground(field_color(dark));
                QTextCursor cursor(block);
                cursor.setBlockFormat(format);
            }
        }
        applying_ = false;
    }
    refreshSelections();
}

void EditorView::setUnderlines(const QVector<Underline>& underlines) {
    underlines_ = underlines;
    refreshSelections();
}

void EditorView::refreshSelections() {
    if (source_mode_) {
        setExtraSelections({});
        return;
    }
    const bool dark = dark_;
    const int limit = qMax(0, document()->characterCount() - 1);
    QList<QTextEdit::ExtraSelection> selections;
    for (const Underline& mark : underlines_) {
        if (mark.end <= mark.start) {
            continue;
        }
        QTextEdit::ExtraSelection selection;
        selection.cursor = textCursor();
        selection.cursor.setPosition(qBound(0, mark.start, limit));
        selection.cursor.setPosition(qBound(0, mark.end, limit), QTextCursor::KeepAnchor);
        QColor color(180, 35, 24);
        if (mark.kind == 1) {
            color = dark ? QColor(230, 196, 122) : QColor(138, 90, 0);
        } else if (mark.kind == 2) {
            color = dark ? QColor(142, 182, 224) : QColor(29, 78, 137);
        } else if (mark.kind == 3) {
            color = dark ? QColor(168, 196, 154) : QColor(46, 110, 68);
        } else if (dark) {
            color = QColor(255, 138, 128);
        }
        selection.format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
        selection.format.setUnderlineColor(color);
        selections.push_back(selection);
    }
    setExtraSelections(selections);
}

void EditorView::setMarkup(const QString& markup) {
    plain_matter_ = false;
    const std::vector<MarkupBlock> blocks = parse_markup(normalize_manuscript(markup).toStdString());
    applying_ = true;
    document()->setUndoRedoEnabled(false);
    QTextCursor cursor(document());
    cursor.beginEditBlock();
    cursor.select(QTextCursor::Document);
    cursor.removeSelectedText();
    if (blocks.empty()) {
        cursor.setBlockFormat(source_mode_ ? QTextBlockFormat{} : proseFormat());
    }
    bool first = true;
    for (const MarkupBlock& block : blocks) {
        QTextBlockFormat format = proseFormat();
        if (block.kind == MarkupBlock::Kind::Scene) {
            format = sceneFormat();
        } else if (block.kind == MarkupBlock::Kind::Field) {
            format = fieldFormat(QString::fromStdString(block.name));
        } else if (block.kind == MarkupBlock::Kind::Image) {
            format = proseFormat();
            format.setProperty(kKind, static_cast<int>(MarkupBlock::Kind::Image));
            format.setProperty(kImageSource, QString::fromStdString(block.src));
            format.setProperty(kImageAlt, QString::fromStdString(block.name));
            format.setTextIndent(0);
            format.setAlignment(Qt::AlignHCenter);
            format.setTopMargin(12);
            format.setBottomMargin(12);
        }
        if (first) {
            cursor.setBlockFormat(format);
            first = false;
        } else {
            cursor.insertBlock(format);
        }
        if (block.kind == MarkupBlock::Kind::Scene) {
            cursor.insertText(QStringLiteral("* * *"));
        } else if (block.kind == MarkupBlock::Kind::Image) {
            const QString source = QString::fromStdString(block.src);
            QTextImageFormat image;
            image.setName(source);
            const QImage picture(resolvePath(source));
            if (!picture.isNull()) {
                document()->addResource(QTextDocument::ImageResource, QUrl(source), picture);
                if (picture.width() > imageLimit()) {
                    image.setWidth(imageLimit());
                }
            }
            cursor.insertImage(image);
        } else {
            if (block.kind == MarkupBlock::Kind::Field) {
                QTextCharFormat label;
                label.setProperty(kLabel, true);
                label.setFont(serif_);
                label.setFontWeight(QFont::DemiBold);
                label.setFontCapitalization(QFont::SmallCaps);
                label.setForeground(dark_ ? QColor(214, 176, 106) : QColor(122, 78, 18));
                cursor.insertText(QString::fromStdString(block.name) + QStringLiteral("  "), label);
            }
            for (const MarkupRun& run : block.runs) {
                QTextCharFormat chars;
                chars.setFont(serif_);
                chars.setFontItalic(run.italic);
                chars.setFontWeight(run.bold ? QFont::Bold : QFont::Normal);
                cursor.insertText(normalize_manuscript(QString::fromStdString(run.text)), chars);
            }
        }
    }
    cursor.endEditBlock();
    document()->setUndoRedoEnabled(true);
    document()->clearUndoRedoStacks();
    applying_ = false;
    moveCursor(QTextCursor::Start);
}

void EditorView::setMatterText(const QString& text) {
    setMarkup(text);
    plain_matter_ = true;
    applying_ = true;
    for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
        QTextBlockFormat format = block.blockFormat();
        format.setAlignment(Qt::AlignLeft);
        format.setTextIndent(0);
        format.setLineHeight(150, QTextBlockFormat::ProportionalHeight);
        QTextCursor cursor(block);
        cursor.setBlockFormat(format);
    }
    applying_ = false;
    moveCursor(QTextCursor::Start);
}

QString EditorView::markup() const {
    if (source_mode_) {
        return toPlainText();
    }
    std::vector<MarkupBlock> blocks;
    for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
        const int kind = block.blockFormat().hasProperty(kKind) ? block.blockFormat().property(kKind).toInt()
                                                                 : static_cast<int>(MarkupBlock::Kind::Paragraph);
        const bool only_empty = document()->blockCount() == 1 && block.text().isEmpty();
        if (only_empty) {
            break;
        }
        MarkupBlock piece;
        piece.kind = static_cast<MarkupBlock::Kind>(kind);
        if (piece.kind == MarkupBlock::Kind::Scene) {
            blocks.push_back(std::move(piece));
            continue;
        }
        if (piece.kind == MarkupBlock::Kind::Image) {
            piece.src = block.blockFormat().property(kImageSource).toString().toStdString();
            piece.name = block.blockFormat().property(kImageAlt).toString().toStdString();
            if (piece.src.empty()) {
                for (auto it = block.begin(); !it.atEnd(); ++it) {
                    if (it.fragment().charFormat().isImageFormat()) {
                        piece.src = it.fragment().charFormat().toImageFormat().name().toStdString();
                        break;
                    }
                }
            }
            blocks.push_back(std::move(piece));
            continue;
        }
        if (piece.kind == MarkupBlock::Kind::Field) {
            piece.name = block.blockFormat().property(kFieldName).toString().toStdString();
        }
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (fragment.charFormat().isImageFormat() || fragment.charFormat().property(kLabel).toBool()) {
                continue;
            }
            MarkupRun run;
            run.text = fragment_text(fragment).toStdString();
            run.italic = fragment.charFormat().fontItalic();
            run.bold = fragment.charFormat().fontWeight() > QFont::Normal;
            if (run.text.empty()) {
                continue;
            }
            if (!piece.runs.empty() && piece.runs.back().italic == run.italic && piece.runs.back().bold == run.bold) {
                piece.runs.back().text += run.text;
            } else {
                piece.runs.push_back(std::move(run));
            }
        }
        blocks.push_back(std::move(piece));
    }
    return QString::fromStdString(emit_markup(blocks));
}

QString EditorView::proseText() const {
    if (source_mode_) {
        return QString::fromStdString(markup_plain(toPlainText().toStdString()));
    }
    return toPlainText();
}

void EditorView::setPlaceholderText(const QString& text) {
    prose_placeholder_ = text;
    if (!source_mode_) {
        QTextEdit::setPlaceholderText(text);
    }
}

void EditorView::setSourceMode(bool source) {
    if (source == source_mode_) {
        return;
    }
    if (source) {
        const QString text = markup();
        source_mode_ = true;
        setAcceptRichText(false);
        setFont(mono_);
        applying_ = true;
        setPlainText(text);
        applying_ = false;
        QTextEdit::setPlaceholderText("HTML markup…");
    } else {
        const QString text = toPlainText();
        source_mode_ = false;
        setAcceptRichText(true);
        setFont(serif_);
        setMarkup(text);
        QTextEdit::setPlaceholderText(prose_placeholder_);
    }
    applyColumn();
    applyPaper();
}

void EditorView::setItalic(bool italic) {
    QTextCharFormat format;
    format.setFontItalic(italic);
    QTextCursor cursor = textCursor();
    if (cursor.hasSelection()) {
        cursor.mergeCharFormat(format);
    }
    mergeCurrentCharFormat(format);
}

void EditorView::setBold(bool bold) {
    QTextCharFormat format;
    format.setFontWeight(bold ? QFont::Bold : QFont::Normal);
    QTextCursor cursor = textCursor();
    if (cursor.hasSelection()) {
        cursor.mergeCharFormat(format);
    }
    mergeCurrentCharFormat(format);
}

void EditorView::insertSceneBreak() {
    if (source_mode_) {
        insertPlainText("\n<hr>\n");
        return;
    }
    QTextCursor cursor = textCursor();
    cursor.beginEditBlock();
    if (cursor.block().text().isEmpty()) {
        cursor.setBlockFormat(sceneFormat());
    } else {
        cursor.movePosition(QTextCursor::EndOfBlock);
        cursor.insertBlock(sceneFormat());
    }
    cursor.insertText(QStringLiteral("* * *"));
    cursor.insertBlock(proseFormat());
    cursor.endEditBlock();
    setTextCursor(cursor);
}

void EditorView::insertImage(const QString& source) {
    if (source.isEmpty()) {
        return;
    }
    if (source_mode_) {
        insertPlainText("\n<img src=\"" + source + "\">\n");
        return;
    }
    QTextCursor cursor = textCursor();
    cursor.beginEditBlock();
    QTextBlockFormat format = proseFormat();
    format.setProperty(kKind, static_cast<int>(MarkupBlock::Kind::Image));
    format.setProperty(kImageSource, source);
    format.setTextIndent(0);
    format.setAlignment(Qt::AlignHCenter);
    format.setTopMargin(12);
    format.setBottomMargin(12);
    if (cursor.block().text().isEmpty()) {
        cursor.setBlockFormat(format);
    } else {
        cursor.movePosition(QTextCursor::EndOfBlock);
        cursor.insertBlock(format);
    }
    QTextImageFormat image;
    image.setName(source);
    const QImage picture(resolvePath(source));
    if (!picture.isNull()) {
        document()->addResource(QTextDocument::ImageResource, QUrl(source), picture);
        if (picture.width() > imageLimit()) {
            image.setWidth(imageLimit());
        }
    }
    cursor.insertImage(image);
    cursor.insertBlock(proseFormat());
    cursor.endEditBlock();
    setTextCursor(cursor);
}

void EditorView::insertField(const QString& name, const QString& value) {
    const QString field = name.trimmed().isEmpty() ? QStringLiteral("note") : name.trimmed();
    if (source_mode_) {
        insertPlainText("\n<field name=\"" + field + "\">" + value + "</field>\n");
        return;
    }
    QTextCursor cursor = textCursor();
    cursor.beginEditBlock();
    if (cursor.block().text().isEmpty()) {
        cursor.setBlockFormat(fieldFormat(field));
    } else {
        cursor.movePosition(QTextCursor::EndOfBlock);
        cursor.insertBlock(fieldFormat(field));
    }
    QTextCharFormat label;
    label.setProperty(kLabel, true);
    label.setFont(serif_);
    label.setFontWeight(QFont::DemiBold);
    label.setFontCapitalization(QFont::SmallCaps);
    label.setForeground(dark_ ? QColor(214, 176, 106) : QColor(122, 78, 18));
    cursor.insertText(field + QStringLiteral("  "), label);
    QTextCharFormat body;
    body.setFont(serif_);
    cursor.insertText(value, body);
    cursor.insertBlock(proseFormat());
    cursor.endEditBlock();
    setTextCursor(cursor);
}

}  // namespace inkwell
