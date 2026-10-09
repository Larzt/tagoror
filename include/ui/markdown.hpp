#pragma once

#include <QColor>
#include <QString>
#include <QSyntaxHighlighter>

class QTextDocument;
class QTextEdit;

/// @file
/// Markdown shared by the card preview, the reminder details and the
/// Markdown note's own page: Qt's importer, adjusted for a narrow panel.
namespace md {

/// Keeps single line breaks as breaks (see the definition).
QString hardBreaks(const QString &source);

/// Reshapes what Qt's importer produces for a document into something for a
/// 300px card. @p base is the editor's font size in pixels.
void tidy(QTextDocument *doc, int base, const QColor &accent);

/// Renders @p source into @p e: hard breaks, the importer, then tidy() with
/// the editor's own font and its palette's link colour as the accent.
void render(QTextEdit *e, const QString &source);

/// The raw editor's colouring: the syntax is dimmed so the text leads
/// (markers in grey, inline code and fences in the warm "code" colour,
/// headings bold), as in the 5.0 design. Line by line, plus fences.
class Highlighter : public QSyntaxHighlighter {
public:
    Highlighter(QTextDocument *doc, const QColor &accent);

protected:
    void highlightBlock(const QString &text) override;

private:
    QColor m_accent;
};

}  // namespace md
