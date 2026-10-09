#include "ui/mdpage.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPlainTextEdit>
#include <QShortcut>
#include <QStackedWidget>
#include <QStyle>
#include <QTextBlock>
#include <QTextBrowser>
#include <QToolButton>
#include <QVBoxLayout>

#include "core/lang.hpp"
#include "ui/markdown.hpp"

namespace {

/// The raw editor: QPlainTextEdit with a gutter of line numbers, the classic
/// Qt pattern (the gutter is a child painted from the editor's blocks).
class CodeEditor : public QPlainTextEdit {
public:
    std::function<void()> submit;   ///< Ctrl+Enter.

    CodeEditor() {
        m_gutter = new Gutter(this);
        connect(this, &QPlainTextEdit::blockCountChanged, this, [this] { updateMargins(); });
        connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect &r, int dy) {
            if (dy) m_gutter->scroll(0, dy);
            else m_gutter->update(0, r.y(), m_gutter->width(), r.height());
        });
        updateMargins();
    }

    int gutterWidth() const {
        const int digits = QString::number(qMax(1, blockCount())).size();
        return 14 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * qMax(2, digits);
    }

protected:
    void resizeEvent(QResizeEvent *e) override {
        QPlainTextEdit::resizeEvent(e);
        const QRect cr = contentsRect();
        m_gutter->setGeometry(cr.left(), cr.top(), gutterWidth(), cr.height());
    }

    void keyPressEvent(QKeyEvent *e) override {
        // Ctrl+Enter is "done" here; Enter alone is a new line, as in any document.
        if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) &&
            (e->modifiers() & Qt::ControlModifier)) {
            if (submit) submit();
            return;
        }
        QPlainTextEdit::keyPressEvent(e);
    }

private:
    class Gutter : public QWidget {
    public:
        explicit Gutter(CodeEditor *e) : QWidget(e), m_editor(e) {}

    protected:
        void paintEvent(QPaintEvent *ev) override {
            QPainter p(this);
            QFont f = m_editor->font();
            f.setFamily("IBM Plex Mono");
            f.setStyleHint(QFont::Monospace);
            f.setPixelSize(qMax(9, f.pixelSize() - 2));
            p.setFont(f);
            p.setPen(QColor("#636872"));
            QTextBlock block = m_editor->firstVisibleBlock();
            int number = block.blockNumber() + 1;
            qreal top = m_editor->blockBoundingGeometry(block).translated(m_editor->contentOffset()).top();
            while (block.isValid() && top <= ev->rect().bottom()) {
                const qreal h = m_editor->blockBoundingRect(block).height();
                if (block.isVisible())
                    p.drawText(QRectF(0, top, width() - 8, m_editor->fontMetrics().height()),
                               Qt::AlignRight | Qt::AlignVCenter, QString::number(number));
                top += h;
                block = block.next();
                ++number;
            }
        }

    private:
        CodeEditor *m_editor;
    };

    void updateMargins() { setViewportMargins(gutterWidth(), 0, 0, 0); }

    Gutter *m_gutter;
};

QToolButton *formatButton(const QString &text, const QString &tip) {
    auto *b = new QToolButton;
    b->setObjectName("mdFormat");
    b->setText(text);
    b->setToolTip(tip);
    b->setFixedSize(26, 24);
    b->setCursor(Qt::PointingHandCursor);
    b->setFocusPolicy(Qt::TabFocus);
    return b;
}

}  // namespace

MarkdownPage::MarkdownPage(const Theme &theme, QWidget *parent)
    : QWidget(parent), m_theme(theme) {
    setObjectName("mdPage");
    auto *col = new QVBoxLayout(this);
    col->setContentsMargins(8, 4, 8, 8);
    col->setSpacing(6);

    auto *bar = new QHBoxLayout;
    bar->setSpacing(6);

    auto *track = new QFrame;
    track->setObjectName("segTrack");
    auto *tl = new QHBoxLayout(track);
    tl->setContentsMargins(2, 2, 2, 2);
    tl->setSpacing(2);
    m_editTab = new QToolButton;
    m_previewTab = new QToolButton;
    for (QToolButton *b : {m_editTab, m_previewTab}) {
        b->setObjectName("segOption");
        b->setCursor(Qt::PointingHandCursor);
        b->setFocusPolicy(Qt::TabFocus);
        tl->addWidget(b);
    }
    connect(m_editTab, &QToolButton::clicked, this, [this] { setPreview(false); });
    connect(m_previewTab, &QToolButton::clicked, this, [this] { setPreview(true); });
    bar->addWidget(track);
    bar->addStretch();

    // Only while editing: in the preview there is nothing to format.
    m_formatBar = new QWidget;
    auto *fl = new QHBoxLayout(m_formatBar);
    fl->setContentsMargins(0, 0, 0, 0);
    fl->setSpacing(2);
    auto *bold = formatButton("B", L("Negrita · Ctrl+B"));
    QFont bf = bold->font();
    bf.setBold(true);
    bold->setFont(bf);
    auto *italic = formatButton("I", L("Cursiva · Ctrl+I"));
    QFont itf = italic->font();
    itf.setItalic(true);
    italic->setFont(itf);
    auto *heading = formatButton("#", L("Título"));
    auto *bullet = formatButton(QStringLiteral("•"), L("Lista"));
    auto *code = formatButton("`", L("Código"));
    connect(bold, &QToolButton::clicked, this, [this] { wrap("**"); });
    connect(italic, &QToolButton::clicked, this, [this] { wrap("*"); });
    connect(heading, &QToolButton::clicked, this, [this] { cycleHeading(); });
    connect(bullet, &QToolButton::clicked, this, [this] { prefixLines("- "); });
    connect(code, &QToolButton::clicked, this, [this] { wrap("`"); });
    for (QToolButton *b : {bold, italic, heading, bullet, code}) fl->addWidget(b);
    bar->addWidget(m_formatBar);

    m_status = new QLabel;
    m_status->setObjectName("mdStatus");
    bar->addWidget(m_status);
    col->addLayout(bar);

    m_stack = new QStackedWidget;
    auto *editor = new CodeEditor;
    editor->submit = [this] { emit closeRequested(); };
    m_editor = editor;
    m_editor->setObjectName("mdEditor");
    m_editor->setContextMenuPolicy(Qt::NoContextMenu);
    m_editor->setTabChangesFocus(true);
    m_editor->setPlaceholderText(L("Escribe en Markdown: # título, **negrita**, - lista…"));
    m_highlighter = new md::Highlighter(m_editor->document(), m_theme.accent);
    connect(m_editor, &QPlainTextEdit::textChanged, this, [this] {
        if (m_loading || !m_note) return;
        m_note->body = m_editor->toPlainText();
        m_status->setText(L("Guardado"));
        emit edited();
    });
    m_stack->addWidget(m_editor);

    m_preview = new QTextBrowser;
    m_preview->setObjectName("mdPreview");
    m_preview->setOpenExternalLinks(true);
    m_preview->setContextMenuPolicy(Qt::NoContextMenu);
    QPalette pal = m_preview->palette();
    pal.setColor(QPalette::Link, m_theme.accent);
    m_preview->setPalette(pal);
    m_stack->addWidget(m_preview);
    col->addWidget(m_stack, 1);

    // Ctrl+E switches between the two; Ctrl+B/I format while editing.
    auto *toggle = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_E), this);
    toggle->setContext(Qt::WidgetWithChildrenShortcut);
    connect(toggle, &QShortcut::activated, this, [this] { setPreview(m_stack->currentWidget() != m_preview); });
    auto *boldKey = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_B), m_editor);
    boldKey->setContext(Qt::WidgetShortcut);
    connect(boldKey, &QShortcut::activated, this, [this] { wrap("**"); });
    auto *italicKey = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_I), m_editor);
    italicKey->setContext(Qt::WidgetShortcut);
    connect(italicKey, &QShortcut::activated, this, [this] { wrap("*"); });

    retranslate();
    setPreview(false);
}

void MarkdownPage::setNote(Note *note) {
    m_note = note;
    m_loading = true;
    m_editor->setPlainText(note ? note->body : QString());
    m_loading = false;
    m_editor->moveCursor(QTextCursor::End);
    m_status->clear();
    setPreview(false);
}

void MarkdownPage::focusEditor() {
    if (m_stack->currentWidget() == m_editor) m_editor->setFocus(Qt::OtherFocusReason);
}

void MarkdownPage::setTheme(const Theme &theme) {
    m_theme = theme;
    QPalette pal = m_preview->palette();
    pal.setColor(QPalette::Link, theme.accent);
    m_preview->setPalette(pal);
    delete m_highlighter;
    m_highlighter = new md::Highlighter(m_editor->document(), theme.accent);
    if (m_stack->currentWidget() == m_preview) refreshPreview();
}

void MarkdownPage::retranslate() {
    m_editTab->setText(L("Editar"));
    m_previewTab->setText(L("Vista previa"));
    m_editTab->setToolTip(L("Texto en crudo · Ctrl+E"));
    m_previewTab->setToolTip(L("Cómo se verá · Ctrl+E"));
    m_editor->setPlaceholderText(L("Escribe en Markdown: # título, **negrita**, - lista…"));
    if (!m_status->text().isEmpty()) m_status->setText(L("Guardado"));
}

void MarkdownPage::setPreview(bool on) {
    if (on) refreshPreview();
    m_stack->setCurrentWidget(on ? static_cast<QWidget *>(m_preview) : m_editor);
    m_formatBar->setVisible(!on);
    m_editTab->setProperty("chosen", !on);
    m_previewTab->setProperty("chosen", on);
    for (QToolButton *b : {m_editTab, m_previewTab}) {
        b->style()->unpolish(b);
        b->style()->polish(b);
    }
    if (on) m_preview->setFocus(Qt::OtherFocusReason);
    else if (isVisible()) m_editor->setFocus(Qt::OtherFocusReason);
}

void MarkdownPage::refreshPreview() {
    const QString source = m_note ? m_note->body : QString();
    if (source.trimmed().isEmpty()) {
        m_preview->setHtml(QString("<p style='color:%1'>%2</p>")
                               .arg(Theme::muted(), L("Todavía no hay nada que ver.").toHtmlEscaped()));
        return;
    }
    md::render(m_preview, source);
}

void MarkdownPage::wrap(const QString &mark) {
    if (m_stack->currentWidget() != m_editor) return;
    QTextCursor c = m_editor->textCursor();
    const QString sel = c.selectedText();
    c.beginEditBlock();
    if (sel.isEmpty()) {
        c.insertText(mark + mark);
        c.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor, int(mark.size()));
    } else if (sel.startsWith(mark) && sel.endsWith(mark) && sel.size() >= 2 * mark.size()) {
        // Already wrapped: the same button takes it off.
        c.insertText(sel.mid(mark.size(), sel.size() - 2 * mark.size()));
    } else {
        c.insertText(mark + sel + mark);
    }
    c.endEditBlock();
    m_editor->setTextCursor(c);
    m_editor->setFocus(Qt::OtherFocusReason);
}

void MarkdownPage::prefixLines(const QString &prefix) {
    if (m_stack->currentWidget() != m_editor) return;
    QTextCursor c = m_editor->textCursor();
    QTextBlock first = m_editor->document()->findBlock(c.selectionStart());
    const QTextBlock last = m_editor->document()->findBlock(c.selectionEnd());
    bool all = true;
    for (QTextBlock b = first; b.isValid(); b = b.next()) {
        all = all && b.text().startsWith(prefix);
        if (b == last) break;
    }
    c.beginEditBlock();
    for (QTextBlock b = first; b.isValid(); b = b.next()) {
        QTextCursor at(b);
        if (all) {
            at.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, int(prefix.size()));
            at.removeSelectedText();
        } else if (!b.text().startsWith(prefix)) {
            at.insertText(prefix);
        }
        if (b == last) break;
    }
    c.endEditBlock();
    m_editor->setFocus(Qt::OtherFocusReason);
}

/// # → ## → ### → nothing, on the line under the cursor.
void MarkdownPage::cycleHeading() {
    if (m_stack->currentWidget() != m_editor) return;
    QTextCursor c = m_editor->textCursor();
    const QTextBlock b = c.block();
    const QString text = b.text();
    int level = 0;
    while (level < text.size() && text.at(level) == '#') ++level;
    const bool marked = level > 0 && level < text.size() && text.at(level) == ' ';
    QTextCursor at(b);
    at.beginEditBlock();
    if (marked) {
        at.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, level + 1);
        at.removeSelectedText();
        if (level < 3) at.insertText(QString(level + 1, '#') + ' ');
    } else {
        at.insertText("# ");
    }
    at.endEditBlock();
    m_editor->setFocus(Qt::OtherFocusReason);
}
