#pragma once

#include <QWidget>

#include "core/note.hpp"
#include "ui/theme.hpp"

class QLabel;
class QPlainTextEdit;
class QStackedWidget;
class QTextBrowser;
class QToolButton;

namespace md {
class Highlighter;
}

/// A Markdown note's own page (5.0): the raw text with its syntax dimmed and
/// line numbers, or the rendered preview (Ctrl+E), and a formatting bar that
/// only shows while editing. The card in the list is the preview; this is
/// where it is written — a 300px card has no room for both.
///
/// Enter breaks the line here, unlike a text note: this is a document, not a
/// chat box. Ctrl+Enter (or Escape, or the back arrow) returns to the list.
class MarkdownPage : public QWidget {
    Q_OBJECT

public:
    explicit MarkdownPage(const Theme &theme, QWidget *parent = nullptr);

    /// Opens @p note in the raw editor, cursor at the end.
    void setNote(Note *note);
    Note *note() const { return m_note; }
    /// The note was deleted elsewhere (a sync): let it go without touching it.
    void forget() { m_note = nullptr; }

    void setTheme(const Theme &theme);
    void retranslate();
    void focusEditor();

signals:
    void edited();          ///< The body changed: save.
    void closeRequested();  ///< Ctrl+Enter.

private:
    void setPreview(bool on);
    /// Wraps the selection (or inserts a pair around the cursor).
    void wrap(const QString &mark);
    /// Puts @p prefix at the start of every selected line, or takes it away if
    /// all of them already have it.
    void prefixLines(const QString &prefix);
    void cycleHeading();
    void refreshPreview();

    Theme m_theme;
    Note *m_note = nullptr;
    QToolButton *m_editTab = nullptr;
    QToolButton *m_previewTab = nullptr;
    QWidget *m_formatBar = nullptr;
    QLabel *m_status = nullptr;
    QStackedWidget *m_stack = nullptr;
    QPlainTextEdit *m_editor = nullptr;
    QTextBrowser *m_preview = nullptr;
    md::Highlighter *m_highlighter = nullptr;
    bool m_loading = false;
};
