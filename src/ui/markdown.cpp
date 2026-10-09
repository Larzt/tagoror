#include "ui/markdown.hpp"

#include <QFontInfo>
#include <QRegularExpression>
#include <QStringList>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>

#include "ui/theme.hpp"

namespace md {

/// In CommonMark a single line break does not split a paragraph ("milk\nbread"
/// reads "milk bread"). In a quick note every line is a line, as existing
/// notes were written. Marks them as hard breaks (two trailing spaces) unless
/// the next line starts a block of its own, and never inside a code fence.
QString hardBreaks(const QString &source) {
    static const QRegularExpression blockStart(
        R"(^\s*([-*+]\s|\d+[.)]\s|#{1,6}\s|>|```|~~~))");
    const QStringList lines = source.split('\n');
    QStringList out;
    bool fenced = false;
    for (int i = 0; i < lines.size(); ++i) {
        const QString &line = lines.at(i);
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith("```") || trimmed.startsWith("~~~")) fenced = !fenced;
        const bool hasNext = i + 1 < lines.size();
        const bool breakHere = !fenced && hasNext && !trimmed.isEmpty() &&
                               !lines.at(i + 1).trimmed().isEmpty() &&
                               !blockStart.match(lines.at(i + 1)).hasMatch();
        out << (breakHere ? line + "  " : line);
    }
    return out.join('\n');
}

void tidy(QTextDocument *doc, int base, const QColor &accent) {
    doc->setIndentWidth(14);
    QColor tint = accent;
    tint.setAlphaF(0.16);

    QTextCursor c(doc);
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        QTextBlockFormat f = b.blockFormat();
        const int level = f.headingLevel();
        // 12px between lines made a shopping list twice as tall as plain text.
        f.setTopMargin(level > 0 && b != doc->begin() ? 6 : 0);
        f.setBottomMargin(level > 0 ? 2 : 1);
        // A quote was indented 40px on each side: half a line in a narrow card.
        const bool quote = f.hasProperty(QTextFormat::BlockQuoteLevel);
        if (quote) {
            f.setLeftMargin(10);
            f.setRightMargin(0);
        }
        c.setPosition(b.position());
        c.setBlockFormat(f);

        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment frag = it.fragment();
            QTextCharFormat cf = frag.charFormat();
            bool touched = false;
            if (level > 0) {
                // The importer's relative size must be cleared, not set to 0: while the
                // property exists it wins over the pixel size and the heading stays body-sized.
                cf.clearProperty(QTextFormat::FontSizeAdjustment);
                cf.clearProperty(QTextFormat::FontPointSize);
                cf.setProperty(QTextFormat::FontPixelSize,
                               qRound(base * (level == 1 ? 1.3 : level == 2 ? 1.15 : 1.05)));
                cf.setFontWeight(QFont::Bold);
                touched = true;
            }
            if (quote) {
                cf.setFontItalic(true);
                cf.setForeground(QColor(Theme::muted()));
                touched = true;
            }
            if (cf.isAnchor()) {
                cf.setForeground(accent);
                touched = true;
            }
            if (cf.fontFixedPitch()) {
                cf.setForeground(accent);
                cf.setBackground(tint);
                touched = true;
            }
            if (!touched) continue;
            c.setPosition(frag.position());
            c.setPosition(frag.position() + frag.length(), QTextCursor::KeepAnchor);
            c.setCharFormat(cf);
        }
    }
}


void render(QTextEdit *e, const QString &source) {
    e->setMarkdown(hardBreaks(source));
    tidy(e->document(), QFontInfo(e->font()).pixelSize(), e->palette().color(QPalette::Link));
}

Highlighter::Highlighter(QTextDocument *doc, const QColor &accent)
    : QSyntaxHighlighter(doc), m_accent(accent) {}

void Highlighter::highlightBlock(const QString &text) {
    const QColor dim("#636872");
    const QColor code("#f2b757");

    // Inside a fence (state 1) everything is code; the fence lines themselves too.
    const QString trimmed = text.trimmed();
    const bool fenceLine = trimmed.startsWith("```") || trimmed.startsWith("~~~");
    const bool inFence = previousBlockState() == 1;
    if (inFence || fenceLine) {
        QTextCharFormat f;
        f.setForeground(code);
        setFormat(0, int(text.size()), f);
        setCurrentBlockState(fenceLine ? (inFence ? 0 : 1) : 1);
        return;
    }
    setCurrentBlockState(0);

    QTextCharFormat marker;
    marker.setForeground(dim);

    static const QRegularExpression heading(R"(^(#{1,6})\s)");
    if (const auto m = heading.match(text); m.hasMatch()) {
        QTextCharFormat bold;
        bold.setFontWeight(QFont::Bold);
        setFormat(0, int(text.size()), bold);
        setFormat(0, int(m.capturedLength(1)), marker);
        return;
    }
    static const QRegularExpression listOrQuote(R"(^\s*([-*+]|\d+[.)]|>)\s)");
    if (const auto m = listOrQuote.match(text); m.hasMatch())
        setFormat(int(m.capturedStart(1)), int(m.capturedLength(1)), marker);

    // Emphasis: the markers dim, the text keeps its colour and gets the weight.
    static const QRegularExpression strong(R"((\*\*|__)(?=\S)(.+?)(?<=\S)\1)");
    for (auto it = strong.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        QTextCharFormat f;
        f.setFontWeight(QFont::Bold);
        setFormat(int(m.capturedStart(2)), int(m.capturedLength(2)), f);
        setFormat(int(m.capturedStart()), 2, marker);
        setFormat(int(m.capturedEnd()) - 2, 2, marker);
    }
    static const QRegularExpression em(R"((?<![*\w])([*_])(?=\S)([^*_\n]+?)(?<=\S)\1(?![*\w]))");
    for (auto it = em.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        QTextCharFormat f;
        f.setFontItalic(true);
        setFormat(int(m.capturedStart(2)), int(m.capturedLength(2)), f);
        setFormat(int(m.capturedStart()), 1, marker);
        setFormat(int(m.capturedEnd()) - 1, 1, marker);
    }
    static const QRegularExpression inlineCode(R"(`[^`]+`)");
    for (auto it = inlineCode.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        QTextCharFormat f;
        f.setForeground(code);
        setFormat(int(m.capturedStart()), int(m.capturedLength()), f);
    }
    static const QRegularExpression link(R"(\[([^\]]+)\]\(([^)]+)\))");
    for (auto it = link.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        QTextCharFormat label;
        label.setForeground(m_accent);
        setFormat(int(m.capturedStart()), int(m.capturedLength()), marker);
        setFormat(int(m.capturedStart(1)), int(m.capturedLength(1)), label);
    }
}

}  // namespace md
