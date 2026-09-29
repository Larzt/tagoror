#include "ui/notecard.hpp"
#include "core/lang.hpp"
#include "ui/elidedlabel.hpp"
#include "ui/imagethumb.hpp"
#include "ui/keynav.hpp"
#include "ui/popup.hpp"
#include "audio/recorder.hpp"
#include "ui/theme.hpp"
#include "audio/wave.hpp"
#include "ui/waveform.hpp"

#include <QAbstractTextDocumentLayout>
#include <QClipboard>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QAudioOutput>
#include <QCheckBox>
#include <QContextMenuEvent>
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QStyle>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMediaPlayer>
#include <QMouseEvent>
#include <QProgressBar>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QPainter>
#include <QVBoxLayout>

namespace {

/// Text to prefill the date field with, in the format parseDue() reads.
/// Prefilling it with @c due (the chip label, "Tue 25 Aug 09:00") turned
/// "open, tweak the time, accept" into unparseable text: the reminder lost its
/// instant and vanished from the planner while the chip still showed a date.
QString dueFieldText(const Note *n) {
    return n->dueAtMs > 0 ? n->dueAt().toString("dd/MM HH:mm") : n->due;
}

/// Parses dd/MM HH:mm with an optional year after the month. Built piece by
/// piece rather than with QDateTime::fromString(), which anchors a missing
/// year at 1900 (not a leap year), so 29 February could never be typed. The
/// missing year is the current one.
QDateTime parseDue(const QString &text) {
    static const QRegularExpression re(
        R"(^\s*(\d{1,2})\s*/\s*(\d{1,2})(?:\s*/\s*(\d{4}))?[\s,]+(\d{1,2})\s*:\s*(\d{2})\s*$)");
    const QRegularExpressionMatch m = re.match(text);
    if (!m.hasMatch()) return QDateTime();

    const int year = m.captured(3).isEmpty() ? QDate::currentDate().year()
                                             : m.captured(3).toInt();
    const QDate date(year, m.captured(2).toInt(), m.captured(1).toInt());
    const QTime time(m.captured(4).toInt(), m.captured(5).toInt());
    return date.isValid() && time.isValid() ? QDateTime(date, time) : QDateTime();
}

/// A body editor that grows with its content: no scrollbars of its own, the
/// height follows the document between a minimum and a maximum.
void autoGrow(QTextEdit *e, const QString &placeholder) {
    // No native cut/paste menu: right click is the note's menu. Shortcuts still
    // work.
    e->setContextMenuPolicy(Qt::NoContextMenu);
    e->setPlaceholderText(placeholder);
    // Tab leaves the editor instead of typing a tab, or keyboard navigation got
    // stuck in the first text note.
    e->setTabChangesFocus(true);
    e->setAcceptRichText(false);
    e->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    e->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    e->setFixedHeight(46);
    // A fixed height is not enough: a QTextEdit's policy is still Expanding, which
    // the card's layout propagates upwards; in a tall window the spare height
    // ended up as a gap under the type label (see *Fixed height* in CLAUDE.md).
    e->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    QObject::connect(e->document()->documentLayout(),
                     &QAbstractTextDocumentLayout::documentSizeChanged, e,
                     [e](const QSizeF &s) {
                         e->setFixedHeight(qBound(46, int(s.height()) + 14, 170));
                     });
}

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

/// A note body with Markdown: rendered at rest, source while focused. One
/// editor, not two widgets taking turns, so height, focus and tab order never
/// change owner. @c m_source is the truth: the rendered document is never
/// written to the note.
class MarkdownEdit : public QTextEdit {
public:
    std::function<void(const QString &)> edited;   ///< The source text changed.

    MarkdownEdit(const QString &source, const QColor &accent) : m_source(source) {
        // Links in the accent. The stylesheet does not touch this role, so the
        // widget's palette decides.
        QPalette pal = palette();
        pal.setColor(QPalette::Link, accent);
        setPalette(pal);
        setAcceptRichText(false);
        viewport()->setMouseTracking(true);

        connect(this, &QTextEdit::textChanged, this, [this] {
            if (m_internal || !m_editing) return;
            m_source = toPlainText();
            if (edited) edited(m_source);
        });
        render();
    }

    const QString &source() const { return m_source; }
    bool editing() const { return m_editing; }

    void beginEdit() {
        if (m_editing) return;
        m_editing = true;
        m_internal = true;
        setPlainText(m_source);
        m_internal = false;
        moveCursor(QTextCursor::End);
    }

    void endEdit() {
        if (!m_editing) return;
        m_editing = false;
        render();
    }

protected:
    // Headings are sized from the editor's font, which the stylesheet sets when
    // polishing (after the constructor) and again when the text size changes.
    void changeEvent(QEvent *e) override {
        QTextEdit::changeEvent(e);
        if (e->type() == QEvent::FontChange && !m_editing) render();
    }

    void focusInEvent(QFocusEvent *e) override {
        QTextEdit::focusInEvent(e);
        // With the mouse, wait for the release: the click may be on a link, which
        // should open instead of editing.
        if (e->reason() == Qt::MouseFocusReason && !m_editing) m_clickPending = true;
        else beginEdit();
    }

    void focusOutEvent(QFocusEvent *e) override {
        QTextEdit::focusOutEvent(e);
        m_clickPending = false;
        // Switching window or opening a menu is not finishing the edit.
        if (e->reason() == Qt::ActiveWindowFocusReason || e->reason() == Qt::PopupFocusReason)
            return;
        endEdit();
    }

    void keyPressEvent(QKeyEvent *e) override {
        if (e->key() == Qt::Key_Escape && m_editing) {
            clearFocus();   // focusOut renders
            return;
        }
        if (!m_editing) beginEdit();   // focus arrived neither by mouse nor by Tab
        QTextEdit::keyPressEvent(e);
    }

    void mouseMoveEvent(QMouseEvent *e) override {
        if (!m_editing)
            viewport()->setCursor(anchorAt(e->position().toPoint()).isEmpty()
                                      ? Qt::IBeamCursor : Qt::PointingHandCursor);
        QTextEdit::mouseMoveEvent(e);
    }

    void mouseReleaseEvent(QMouseEvent *e) override {
        if (m_clickPending && e->button() == Qt::LeftButton) {
            m_clickPending = false;
            const QPoint at = e->position().toPoint();
            const QString href = anchorAt(at);
            if (!href.isEmpty()) {
                QDesktopServices::openUrl(QUrl(href));
                clearFocus();
                return;
            }
            // The click position in the rendered text is not the same in the source, but
            // it is close: better than sending the cursor to the end.
            beginEdit();
            setTextCursor(cursorForPosition(at));
            viewport()->setCursor(Qt::IBeamCursor);
            return;
        }
        QTextEdit::mouseReleaseEvent(e);
    }

private:
    void render() {
        m_internal = true;
        if (m_source.trimmed().isEmpty()) {
            clear();   // so the placeholder shows
        } else {
            setMarkdown(hardBreaks(m_source));
            tidy();
        }
        m_internal = false;
    }

    /// Reshapes what Qt's importer produces for a document into something for a
    /// 300px card: a "#" came out at twice the text size, lists indented 40px and
    /// links used the application palette's blue instead of the accent.
    void tidy() {
        QTextDocument *doc = document();
        doc->setIndentWidth(14);
        const int base = QFontInfo(font()).pixelSize();
        const QColor accent = palette().color(QPalette::Link);
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

    QString m_source;
    bool m_editing = false;
    bool m_internal = false;       ///< The editor's own changes, not the user's.
    bool m_clickPending = false;
};

/// The date chip is clickable: changing a reminder should not require the
/// context menu.
class ClickableLabel : public QLabel {
public:
    ClickableLabel(const QString &text, std::function<void(const QPoint &)> onClick)
        : QLabel(text), m_click(std::move(onClick)) {
        setCursor(Qt::PointingHandCursor);
    }

    // With the keyboard the menu opens at the label itself, where a click would
    // have landed.
    void makeKeyboardReachable() {
        keynav::activatable(this, [this] {
            if (m_click) m_click(mapToGlobal(QPoint(0, height())));
        });
    }

protected:
    void mouseReleaseEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton && rect().contains(e->position().toPoint()) && m_click)
            m_click(e->globalPosition().toPoint());
    }

private:
    std::function<void(const QPoint &)> m_click;
};

/// Strike-through of a done item, on the label since the text left the
/// QCheckBox.
void strikeOut(QLabel *label, bool on) {
    QFont f = label->font();
    f.setStrikeOut(on);
    label->setFont(f);
}

/// A link row: left click opens, right click shows its options. The menu is
/// handled here so the whole card's menu does not answer instead.
class LinkRow : public QWidget {
public:
    explicit LinkRow(QWidget *parent = nullptr) : QWidget(parent) {
        setObjectName("linkRow");
        setAttribute(Qt::WA_StyledBackground, true);
        setAttribute(Qt::WA_Hover, true);
        setCursor(Qt::PointingHandCursor);
        keynav::activatable(this, [this] { if (activate) activate(); });
    }

    std::function<void()> activate;
    std::function<void(const QPoint &)> menu;

protected:
    void mouseReleaseEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton && rect().contains(e->position().toPoint()) && activate)
            activate();
    }

    void contextMenuEvent(QContextMenuEvent *e) override {
        if (!menu) return;
        menu(e->globalPos());
        e->accept();
    }
};

/// An address without a scheme is what people type; it is completed so
/// QDesktopServices knows what to do with it.
QString normalizedUrl(const QString &raw) {
    const QString text = raw.trimmed();
    if (text.isEmpty()) return text;
    return QUrl(text).scheme().isEmpty() ? "https://" + text : text;
}

/// What a link shows when unnamed: without scheme or trailing slash, which is
/// noise in a narrow card.
QString prettyUrl(const QString &url) {
    QString text = url;
    for (const QString &prefix : {"https://", "http://"})
        if (text.startsWith(prefix, Qt::CaseInsensitive)) text = text.mid(prefix.size());
    if (text.startsWith("www.", Qt::CaseInsensitive)) text = text.mid(4);
    if (text.endsWith("/")) text.chop(1);
    return text;
}

QString linkText(const Link &link) {
    return link.label.isEmpty() ? prettyUrl(link.url) : link.label;
}

QString formatMs(qint64 ms) {
    const qint64 total = ms / 1000;
    return QString("%1:%2").arg(total / 60).arg(total % 60, 2, 10, QChar('0'));
}

QString typeLabel(Note::Type t) {
    switch (t) {
        case Note::Check:    return L("Lista");
        case Note::Reminder: return L("Recordatorio");
        case Note::Voice:    return L("Nota de voz");
        default:             return L("Nota");
    }
}

/// Grip to reorder a card or an item. Unlike DragBar it never asks the
/// compositor for anything: the movement stays inside the list, so it only
/// reports positions. The drag starts past startDragDistance, so a clumsy
/// click reorders nothing.
class DragHandle : public QToolButton {
public:
    std::function<void()> start;
    std::function<void(const QPoint &)> moved;
    std::function<void()> finished;

protected:
    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() != Qt::LeftButton) {
            QToolButton::mousePressEvent(e);
            return;
        }
        m_press = e->globalPosition().toPoint();
        m_active = false;
        e->accept();     // Qt grabs the mouse: the moves keep arriving here
    }

    void mouseMoveEvent(QMouseEvent *e) override {
        if (!(e->buttons() & Qt::LeftButton)) return;
        const QPoint at = e->globalPosition().toPoint();
        if (!m_active &&
            (at - m_press).manhattanLength() < QApplication::startDragDistance())
            return;
        if (!m_active) {
            m_active = true;
            setDown(false);       // otherwise it stays pressed on release
            setCursor(Qt::ClosedHandCursor);   // the hand closes while grabbing
            if (start) start();
        }
        if (moved) moved(at);
    }

    void mouseReleaseEvent(QMouseEvent *e) override {
        if (!m_active) {
            QToolButton::mouseReleaseEvent(e);
            return;
        }
        m_active = false;
        setCursor(Qt::OpenHandCursor);
        if (finished) finished();
        e->accept();
    }

private:
    QPoint m_press;
    bool m_active = false;
};

QToolButton *roundButton(const QString &kind, const QColor &color, const QString &tip) {
    auto *b = new QToolButton;
    b->setIcon(paintIcon(kind, color));
    b->setIconSize(QSize(16, 16));
    b->setFixedSize(28, 28);
    b->setCursor(Qt::PointingHandCursor);
    b->setToolTip(tip);
    return b;
}

} // namespace

NoteCard::NoteCard(Note *note, const Theme &theme, QWidget *parent)
    : QFrame(parent), m_note(note), m_theme(theme) {
    setObjectName("card");
    build();
}

void NoteCard::build() {
    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(11, 9, 11, 10);
    l->setSpacing(7);

    buildTitleRow(l);

    switch (m_note->type) {
        case Note::Check:    buildCheck(l);    break;
        case Note::Reminder: buildReminder(l); refreshDue(); break;
        case Note::Voice:    buildVoice(l);    break;
        default:             buildText(l);     break;
    }

    buildImages(l);

    m_linksBox = new QWidget;
    m_linksLayout = new QVBoxLayout(m_linksBox);
    m_linksLayout->setContentsMargins(0, 0, 0, 0);
    m_linksLayout->setSpacing(1);

    // At the end of the card but above the type label when there is one: the
    // footer is read last. For reminders and checklists the label is not a direct
    // child of this layout, so the links go last there.
    const int at = m_meta ? l->indexOf(m_meta) : -1;
    if (at >= 0) l->insertWidget(at, m_linksBox);
    else l->addWidget(m_linksBox);
    refreshLinks();
}

/// Editable title with the reorder grip on its right. The grip is dimmed and
/// lights up only on hover, so it does not compete with the title.
void NoteCard::buildTitleRow(QVBoxLayout *l) {
    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(4);

    m_title = new QLineEdit(m_note->title);
    m_title->setContextMenuPolicy(Qt::NoContextMenu);
    m_title->setObjectName("cardTitleEdit");
    m_title->setPlaceholderText(L("Sin título"));
    connect(m_title, &QLineEdit::textChanged, this, [this](const QString &t) {
        m_note->title = t;
        emit dirty();
    });
    row->addWidget(m_title, 1);

    auto *handle = new DragHandle;
    handle->setObjectName("dragHandle");
    handle->setIcon(paintIcon("grip", QColor(Theme::muted()), 13));
    handle->setIconSize(QSize(13, 13));
    handle->setFixedSize(20, 20);
    // An open hand, not the resize arrow: SizeVerCursor promised to change the
    // card's height.
    handle->setCursor(Qt::OpenHandCursor);
    handle->setToolTip(L("Arrastra para reordenar"));
    // Out of the tab order: with the keyboard, reordering is Move up / Move down
    // in the card menu.
    handle->setFocusPolicy(Qt::NoFocus);
    handle->start = [this] { emit dragStarted(); };
    handle->moved = [this](const QPoint &at) { emit dragMoved(at); };
    handle->finished = [this] { emit dragFinished(); };
    row->addWidget(handle, 0, Qt::AlignVCenter);

    l->addLayout(row);
}

void NoteCard::buildText(QVBoxLayout *l) {
    auto *body = new MarkdownEdit(m_note->body, m_theme.accent);
    autoGrow(body, L("Escribe… admite Markdown"));
    body->edited = [this](const QString &text) {
        m_note->body = text;
        emit dirty();
    };
    l->addWidget(body);

    m_meta = new QLabel(L("TEXTO"));
    m_meta->setObjectName("meta");
    l->addWidget(m_meta);
}

void NoteCard::buildReminder(QVBoxLayout *l) {
    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(7);

    // Clock/bell icon next to the date: the state is visible at a glance.
    m_dueIcon = new QLabel;
    m_dueIcon->setFixedSize(14, 14);
    row->addWidget(m_dueIcon, 0, Qt::AlignVCenter);

    m_chip = new ClickableLabel(m_note->dueLabel().isEmpty() ? L("Sin fecha") : m_note->dueLabel(),
                                [this](const QPoint &p) { openDuePopup(p); });
    m_chip->setObjectName("chip");
    m_chip->setToolTip(L("Clic para cambiar la fecha"));
    static_cast<ClickableLabel *>(m_chip)->makeKeyboardReachable();
    row->addWidget(m_chip);

    // A repeat icon rather than a chip with text ("Every week"): with the chip a
    // weekly overdue reminder asked for 296px of minimum width against the 284
    // the list gets at minimum size, and one card too wide clips every card (see
    // *Card widths*). The type label on the right spells it out when there is
    // nothing more urgent to say.
    m_repeatChip = new ClickableLabel(QString(), [this](const QPoint &p) { openDuePopup(p); });
    m_repeatChip->setFixedSize(12, 12);
    row->addWidget(m_repeatChip, 0, Qt::AlignVCenter);
    refreshRepeat();

    // Only shown while the alarm rings: it is how to silence it.
    m_dueBtn = roundButton("stop", QColor("#ff7a6b"), L("Detener aviso"));
    m_dueBtn->setObjectName("dueBtn");
    m_dueBtn->hide();
    connect(m_dueBtn, &QToolButton::clicked, this, [this] {
        emit dismissRequested(m_note);
    });
    row->addWidget(m_dueBtn);
    row->addStretch();

    m_meta = new QLabel(L("RECORDATORIO"));
    m_meta->setObjectName("meta");
    row->addWidget(m_meta);
    l->addLayout(row);

    // Without details the card is just its date: an empty editor took 46px of
    // nothing on every reminder.
    m_detailSlot = new QWidget;
    m_detailLayout = new QVBoxLayout(m_detailSlot);
    m_detailLayout->setContentsMargins(0, 0, 0, 0);
    m_detailLayout->setSpacing(0);
    l->addWidget(m_detailSlot);

    if (m_note->body.isEmpty()) showDetailsGhost();
    else showDetailsEditor(false);
}

/// The single dimmed line shown when there are no details. Emptying the
/// editor does not come back here while typing (the cursor would be yanked
/// mid-sentence); it does on leaving, which is how an editor opened by
/// mistake gets closed.
void NoteCard::showDetailsGhost() {
    if (!m_detailLayout) return;
    m_detailBody = nullptr;
    while (QLayoutItem *it = m_detailLayout->takeAt(0)) {
        if (QWidget *w = it->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete it;
    }

    auto *ghost = new ClickableLabel(L("+ Añadir detalles"),
                                     [this](const QPoint &) { showDetailsEditor(true); });
    ghost->setObjectName("addDetails");
    ghost->setContextMenuPolicy(Qt::NoContextMenu);
    ghost->makeKeyboardReachable();
    ghost->setToolTip(L("Escribir detalles del recordatorio"));
    m_detailLayout->addWidget(ghost);
}

void NoteCard::showDetailsEditor(bool focus) {
    if (!m_detailLayout) return;
    while (QLayoutItem *it = m_detailLayout->takeAt(0)) {
        if (QWidget *w = it->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete it;
    }

    auto *body = new MarkdownEdit(m_note->body, m_theme.accent);
    autoGrow(body, L("Detalles…"));
    body->edited = [this](const QString &text) {
        m_note->body = text;
        emit dirty();
    };
    body->installEventFilter(this);
    m_detailBody = body;
    m_detailLayout->addWidget(body);
    if (focus) body->setFocus(Qt::OtherFocusReason);
}

/// An empty details editor closes itself on focus-out (or Escape) and the
/// "+ Add details" line returns. The close is deferred: an event of the
/// editor itself is still being dispatched and showDetailsGhost() destroys it.
bool NoteCard::eventFilter(QObject *watched, QEvent *event) {
    // Alt+Up / Alt+Down on an item's checkbox moves it.
    if (event->type() == QEvent::KeyPress && qobject_cast<QCheckBox *>(watched) &&
        watched->property("itemIndex").isValid()) {
        auto *k = static_cast<QKeyEvent *>(event);
        if (k->modifiers() == Qt::AltModifier &&
            (k->key() == Qt::Key_Up || k->key() == Qt::Key_Down)) {
            moveItem(watched->property("itemIndex").toInt(),
                     k->key() == Qt::Key_Up ? -1 : 1);
            return true;
        }
    }
    // Escape leaves the item as it was. Losing focus then fires editingFinished,
    // but by then no row is being edited and the commit does nothing.
    if (watched == m_itemEdit && m_itemEdit && event->type() == QEvent::KeyPress &&
        static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
        cancelItemEdit();
        m_itemEdit->clearFocus();
        return true;
    }

    if (watched == m_detailBody && m_detailBody) {
        const bool leaving = event->type() == QEvent::FocusOut;
        const bool escape = event->type() == QEvent::KeyPress &&
                            static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape;
        // The note's body, not toPlainText(): rendered, the document is not the text
        // that gets saved.
        if ((leaving || escape) && m_note->body.trimmed().isEmpty()) {
            if (escape) m_detailBody->clearFocus();
            QTimer::singleShot(0, this, [this] {
                if (m_detailBody && !m_detailBody->hasFocus() &&
                    m_note->body.trimmed().isEmpty())
                    showDetailsGhost();
            });
        }
    }
    return QWidget::eventFilter(watched, event);
}

void NoteCard::buildCheck(QVBoxLayout *l) {
    m_itemsLayout = new QVBoxLayout;
    m_itemsLayout->setContentsMargins(0, 0, 0, 0);
    m_itemsLayout->setSpacing(3);
    l->addLayout(m_itemsLayout);
    rebuildItems();

    // Add row: a dotted checkbox (the item does not exist yet) and a borderless
    // field, so it reads as one more item rather than a form.
    auto *addRow = new QWidget;
    auto *al = new QHBoxLayout(addRow);
    al->setContentsMargins(0, 0, 0, 0);
    al->setSpacing(8);

    auto *ghost = new QLabel;
    ghost->setFixedSize(13, 13);
    ghost->setPixmap(paintIcon("checkdots", QColor(Theme::muted()), 13).pixmap(13, 13));
    al->addWidget(ghost, 0, Qt::AlignVCenter);

    m_newItem = new QLineEdit;
    m_newItem->setContextMenuPolicy(Qt::NoContextMenu);
    m_newItem->setObjectName("newItemEdit");
    m_newItem->setPlaceholderText(L("Añadir elemento…"));
    connect(m_newItem, &QLineEdit::returnPressed, this, [this] {
        const QString text = m_newItem->text().trimmed();
        if (text.isEmpty()) return;
        m_note->items.append(CheckItem{text, false});
        m_newItem->clear();
        rebuildItems();
        refreshProgress();
        emit dirty();
    });
    al->addWidget(m_newItem, 1);
    l->addWidget(addRow);

    auto *foot = new QHBoxLayout;
    foot->setContentsMargins(0, 0, 0, 0);
    foot->setSpacing(8);

    m_bar = new QProgressBar;
    m_bar->setTextVisible(false);
    // The accent arrives through the constructor, so it is applied inline: the
    // global sheet is not regenerated for it without rebuilding the cards.
    m_bar->setStyleSheet(QString("QProgressBar::chunk { background:%1; border-radius:2px; }")
                             .arg(m_theme.accent.name()));
    foot->addWidget(m_bar, 1);

    m_progress = new QLabel;
    m_progress->setObjectName("meta");
    foot->addWidget(m_progress);
    l->addLayout(foot);

    // A row of its own, not beside the counter: the button carries text and a row
    // of fixed widths narrows the whole list. Only shown when the list is done.
    auto *doneRow = new QHBoxLayout;
    doneRow->setContentsMargins(0, 0, 0, 0);
    doneRow->addStretch();

    m_clearBtn = new QToolButton;
    m_clearBtn->setObjectName("listDone");
    m_clearBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_clearBtn->setIcon(paintIcon("trash", QColor("#ff7a6b"), 12));
    m_clearBtn->setIconSize(QSize(12, 12));
    m_clearBtn->setCursor(Qt::PointingHandCursor);
    m_clearBtn->setText(L("Eliminar lista"));
    m_clearBtn->setToolTip(L("Ya está todo hecho: quitar esta nota"));
    m_clearBtn->hide();
    connect(m_clearBtn, &QToolButton::clicked, this, [this] {
        emit deleteRequested(m_note);
    });
    doneRow->addWidget(m_clearBtn);
    l->addLayout(doneRow);

    refreshProgress();
}

void NoteCard::addCheckRow(QVBoxLayout *l, int index) {
    const CheckItem &item = m_note->items.at(index);

    auto *row = new QWidget;
    // Its index in items, to read the row order on drop: during a drag rows move
    // and the index captured by the lambdas no longer matches their position.
    row->setProperty("itemIndex", index);
    auto *rl = new QHBoxLayout(row);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(6);

    // The text lives in a separate label, not in the QCheckBox: with the text
    // inside, the checkbox's size hint is the whole phrase, which became the
    // card's minimum width and pushed long items off the right edge.
    auto *box = new QCheckBox;
    box->setContextMenuPolicy(Qt::NoContextMenu);
    // Tab focus only: with click focus, ticking with the mouse left the keyboard
    // ring on the checkbox.
    box->setFocusPolicy(Qt::TabFocus);
    box->setChecked(item.done);
    box->setCursor(Qt::PointingHandCursor);
    box->setProperty("itemIndex", index);
    box->installEventFilter(this);   // Alt+Up / Alt+Down move it; see eventFilter()
    rl->addWidget(box, 0, Qt::AlignTop);

    // While editing, the row shows a QLineEdit instead of the label. A QLineEdit
    // does not demand the width of its text, so renaming cannot blow up the
    // list's width.
    QLabel *text = nullptr;
    if (index == m_editingItem) {
        auto *edit = new QLineEdit(item.text);
        edit->setObjectName("checkTextEdit");
        edit->setContextMenuPolicy(Qt::NoContextMenu);
        edit->installEventFilter(this);   // Escape cancels; see eventFilter()
        m_itemEdit = edit;
        connect(edit, &QLineEdit::editingFinished, this, [this, index, edit] {
            // editingFinished fires on Enter and again on focus-out; the second call finds
            // nothing to do.
            if (m_editingItem != index) return;
            commitItemEdit(index, edit->text());
        });
        rl->addWidget(edit, 1);
        // Deferred: rebuildItems() is the caller, and focus requested mid-rebuild is
        // lost once the rows are laid out.
        QTimer::singleShot(0, edit, [edit] {
            edit->setFocus();
            edit->selectAll();
        });
    } else {
        text = new ClickableLabel(item.text, [box](const QPoint &) { box->toggle(); });
        text->setObjectName("checkText");
        text->setContextMenuPolicy(Qt::NoContextMenu);
        text->setWordWrap(true);
        // Word wrap breaks at spaces, so a single long word (a password, a file name)
        // demanded its full width as minimum: measured 335px against the list's 284,
        // which clipped EVERY card. With a bounded minimum the word is cut at its own
        // row's edge; the full text stays in the tooltip and the rename field.
        text->setMinimumWidth(24);
        text->setToolTip(item.text);
        strikeOut(text, item.done);
        rl->addWidget(text, 1);
    }

    connect(box, &QCheckBox::toggled, this, [this, text, index](bool on) {
        if (index >= m_note->items.size()) return;
        m_note->items[index].done = on;
        if (text) strikeOut(text, on);
        refreshProgress();
        emit dirty();
    });

    // Renaming has its own button rather than a click on the text: that gesture
    // already ticks the item.
    auto *rename = new QToolButton;
    rename->setIcon(paintIcon("pencil", QColor(Theme::muted()), 12));
    rename->setIconSize(QSize(12, 12));
    rename->setFixedSize(18, 18);
    rename->setCursor(Qt::PointingHandCursor);
    rename->setToolTip(L("Renombrar elemento"));
    rename->setVisible(index != m_editingItem);
    connect(rename, &QToolButton::clicked, this, [this, index] { beginItemEdit(index); });
    rl->addWidget(rename, 0, Qt::AlignTop);

    auto *del = new QToolButton;
    del->setIcon(paintIcon("minus", QColor(Theme::muted()), 12));
    del->setIconSize(QSize(12, 12));
    del->setFixedSize(18, 18);
    del->setCursor(Qt::PointingHandCursor);
    del->setToolTip(L("Quitar elemento"));
    connect(del, &QToolButton::clicked, this, [this, index] {
        if (index >= m_note->items.size()) return;
        m_note->items.removeAt(index);
        rebuildItems();
        refreshProgress();
        emit dirty();
    });
    rl->addWidget(del, 0, Qt::AlignTop);   // level with the first line

    // The same grip as the card's, at the end of the row as that one is at the
    // end of the title. On the left it misaligned the checkbox with the add row.
    auto *handle = new DragHandle;
    handle->setObjectName("dragHandle");
    handle->setIcon(paintIcon("grip", QColor(Theme::muted()), 11));
    handle->setIconSize(QSize(11, 11));
    handle->setFixedSize(14, 18);
    handle->setCursor(Qt::OpenHandCursor);
    handle->setToolTip(L("Arrastra para reordenar"));
    // With the keyboard, Alt+Up / Alt+Down from the checkbox.
    handle->setFocusPolicy(Qt::NoFocus);
    handle->start = [this] { m_itemsMoved = false; };
    handle->moved = [this, row](const QPoint &at) { dragItemTo(row, at); };
    handle->finished = [this] { endItemDrag(); };
    rl->addWidget(handle, 0, Qt::AlignTop);

    l->addWidget(row);
}

/// Like Panel::dragCardTo(), inside the card: the row moves as soon as it
/// crosses another's centre. items is untouched until the drop; meanwhile the
/// layout order rules.
void NoteCard::dragItemTo(QWidget *row, const QPoint &globalPos) {
    if (!row || !m_itemsLayout) return;

    // A long list may leave the visible area: scroll near the edges, as when
    // dragging cards.
    for (QWidget *w = parentWidget(); w; w = w->parentWidget()) {
        if (auto *area = qobject_cast<QScrollArea *>(w)) {
            const int y = area->viewport()->mapFromGlobal(globalPos).y();
            const int edge = 26;
            QScrollBar *bar = area->verticalScrollBar();
            if (y < edge) bar->setValue(bar->value() - 12);
            else if (y > area->viewport()->height() - edge) bar->setValue(bar->value() + 12);
            break;
        }
    }

    QList<QWidget *> rows;
    for (int i = 0; i < m_itemsLayout->count(); ++i)
        if (QWidget *w = m_itemsLayout->itemAt(i)->widget()) rows.append(w);

    const int from = int(rows.indexOf(row));
    if (from < 0) return;

    const int y = row->parentWidget()->mapFromGlobal(globalPos).y();
    int to = from;
    for (int i = 0; i < rows.size(); ++i) {
        if (i == from) continue;
        const int center = rows.at(i)->geometry().center().y();
        if (i < from && y < center) { to = i; break; }
        if (i > from && y > center) to = i;
    }
    if (to == from) return;

    m_itemsLayout->removeWidget(row);
    m_itemsLayout->insertWidget(to, row);
    m_itemsMoved = true;
}

/// On drop the row order is written to items and the rows are rebuilt, since
/// their lambdas captured the old indices.
void NoteCard::endItemDrag() {
    if (!m_itemsMoved || !m_itemsLayout) return;
    m_itemsMoved = false;

    QList<CheckItem> order;
    int editing = -1;
    for (int i = 0; i < m_itemsLayout->count(); ++i) {
        QWidget *w = m_itemsLayout->itemAt(i)->widget();
        if (!w) continue;
        const int was = w->property("itemIndex").toInt();
        if (was < 0 || was >= m_note->items.size()) return;   // stale rows: do not trust them
        if (was == m_editingItem) editing = int(order.size());
        order.append(m_note->items.at(was));
    }
    if (order.size() != m_note->items.size()) return;

    m_note->items = order;
    m_editingItem = editing;
    // Deferred: the caller is the grip of one of the rows being destroyed.
    QTimer::singleShot(0, this, [this] { rebuildItems(); });
    emit dirty();
}

/// One step up or down from the keyboard. The focus returns to the item's
/// checkbox at its new place, so it can keep moving.
void NoteCard::moveItem(int index, int steps) {
    const int to = index + steps;
    if (index < 0 || index >= m_note->items.size() || to < 0 || to >= m_note->items.size())
        return;
    m_note->items.move(index, to);
    if (m_editingItem == index) m_editingItem = to;
    else if (m_editingItem == to) m_editingItem = index;
    emit dirty();
    QTimer::singleShot(0, this, [this, to] {
        rebuildItems();
        QLayoutItem *it = m_itemsLayout->itemAt(to);
        if (QWidget *row = it ? it->widget() : nullptr)
            if (auto *box = row->findChild<QCheckBox *>())
                box->setFocus(Qt::TabFocusReason);   // with a ring: it arrived by keyboard
    });
}

/// The edit field closes itself on focus-out, so recording the row and
/// rebuilding is enough.
void NoteCard::beginItemEdit(int index) {
    if (index < 0 || index >= m_note->items.size()) return;
    m_editingItem = index;
    rebuildItems();
}

/// An empty text does not delete the item (the minus button does): clearing
/// the field by accident must not cost the line.
void NoteCard::commitItemEdit(int index, const QString &text) {
    m_editingItem = -1;
    if (index < 0 || index >= m_note->items.size()) {
        rebuildItems();
        return;
    }

    const QString clean = text.trimmed();
    const bool changed = !clean.isEmpty() && clean != m_note->items.at(index).text;
    if (changed) m_note->items[index].text = clean;

    // Deferred: the caller is the field itself, from its editingFinished, and
    // rebuildItems() destroys it mid-signal.
    QTimer::singleShot(0, this, [this] { rebuildItems(); });
    if (changed) emit dirty();
}

void NoteCard::cancelItemEdit() {
    if (m_editingItem < 0) return;
    m_editingItem = -1;
    QTimer::singleShot(0, this, [this] { rebuildItems(); });
}

void NoteCard::rebuildItems() {
    m_itemEdit = nullptr;   // whatever was there dies in this same sweep
    // Rows capture their index, so they are rebuilt after a structural change.
    // Hidden before deleting: until deleteLater() runs they stay painted and
    // keep accepting events.
    while (QLayoutItem *it = m_itemsLayout->takeAt(0)) {
        if (QWidget *w = it->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete it;
    }
    for (int i = 0; i < m_note->items.size(); ++i)
        addCheckRow(m_itemsLayout, i);
}

void NoteCard::refreshProgress() {
    if (!m_bar) return;
    const int total = m_note->items.size();
    const int done = m_note->doneCount();

    m_bar->setRange(0, total > 0 ? total : 1);
    m_bar->setValue(done);
    if (m_progress) m_progress->setText(QString("%1/%2").arg(done).arg(total));
    // An empty list is not finished, it is unstarted.
    if (m_clearBtn) m_clearBtn->setVisible(total > 0 && done == total);
}

void NoteCard::openDuePopup(const QPoint &globalPos) {
    auto *menu = new Popup(m_theme, this);
    menu->addHeader(L("Recordar"));

    const QDateTime now = QDateTime::currentDateTime();
    QDateTime todaySix = QDateTime(now.date(), QTime(18, 0));
    if (todaySix <= now) todaySix = todaySix.addDays(1);

    const QList<QPair<QString, QDateTime>> presets = {
        {L("En 5 minutos"), now.addSecs(300)},
        {L("En 1 hora"), now.addSecs(3600)},
        {L("A las 18:00"), todaySix},
        {L("Mañana 09:00"), QDateTime(now.date().addDays(1), QTime(9, 0))},
    };

    // Presets store the real instant: that is what makes it ring.
    for (const auto &[label, when] : presets) {
        menu->addItem("clock", label, Lang::locale().toString(when, "ddd d MMM HH:mm"),
                      [this, when] {
            applyDue(when.toMSecsSinceEpoch(),
                     Lang::locale().toString(when, "ddd d MMM HH:mm"));
        });
    }

    menu->addSeparator();
    menu->addHeader(L("A mano · dd/MM HH:mm"));
    menu->addEditor(L("p. ej. 24/12 20:30"), dueFieldText(m_note), [this](const QString &value) {
        // Text that parses as a date also rings; otherwise it stays a free label.
        // When it parses, the standard label is stored rather than what was typed, so
        // the chip reads as usual and the field reopens in its format.
        const QDateTime parsed = parseDue(value);
        applyDue(parsed.isValid() ? parsed.toMSecsSinceEpoch() : 0,
                 parsed.isValid() ? Lang::locale().toString(parsed, "ddd d MMM HH:mm") : value);
    });

    // The repetition lives here: it is part of "when does this ring", and means
    // nothing with a free-text date.
    if (m_note->isScheduled()) {
        menu->addSeparator();
        menu->addHeader(L("Repetir"));
        const QList<QPair<Note::Repeat, QPair<QString, QString>>> modes = {
            {Note::Once,   {L("No repetir"), L("Suena una vez")}},
            {Note::Weekly, {L("Cada semana"), Lang::locale().toString(m_note->dueAt(), "dddd")}},
            {Note::Yearly, {L("Cada año"), Lang::locale().toString(m_note->dueAt(), "d MMMM")}},
        };
        for (const auto &[mode, text] : modes)
            menu->addItem(m_note->repeat == mode ? "check" : "repeat", text.first, text.second,
                          [this, mode = mode] { setRepeat(mode); });
    }

    if (!m_note->due.isEmpty()) {
        menu->addSeparator();
        menu->addItem("minus", L("Quitar fecha"), QString(), [this] {
            applyDue(0, QString());
        });
    }
    menu->showAt(globalPos);
}

/// The repeat icon and the type label say the same thing two ways; the label
/// is written by refreshDue(), which also knows whether it is ringing.
void NoteCard::refreshRepeat() {
    if (!m_repeatChip) return;
    m_repeatChip->setPixmap(paintIcon("repeat", m_theme.accent, 12).pixmap(12, 12));
    m_repeatChip->setToolTip(m_note->repeats()
                                 ? L("%1 · clic para cambiarlo").arg(m_note->repeatLabel())
                                 : L("Clic para cambiar la repetición"));
    m_repeatChip->setVisible(m_note->repeats());
}

/// Moving a reminder's date also acknowledges it: postponing is not ignoring,
/// so it stops ringing. Otherwise the note carried @c ringing to its new date
/// and the tone, the red dock and the planner all kept ringing for a day not
/// yet come. @c fired is cleared because the instant is new.
void NoteCard::applyDue(qint64 whenMs, const QString &label) {
    const bool wasRinging = m_note->ringing;

    m_note->dueAtMs = whenMs;
    m_note->due = label;
    m_note->fired = false;
    m_note->ringing = false;

    if (m_chip) m_chip->setText(m_note->dueLabel().isEmpty() ? L("Sin fecha") : m_note->dueLabel());
    refreshRepeat();
    refreshDue();

    // First report that it no longer rings: the panel checks every note's state,
    // and this one is already up to date.
    if (wasRinging) emit rescheduled(m_note);
    emit dirty();
}

void NoteCard::setRepeat(Note::Repeat repeat) {
    m_note->repeat = repeat;
    // A repeating reminder cannot stay marked as fired or its next turn would not
    // ring; it moves to its next date when the current one has passed. Stopping
    // the repetition keeps @c fired, or an alarm already given would ring again.
    if (m_note->repeats()) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        applyDue(m_note->dueAtMs <= now ? m_note->nextOccurrenceAfter(now) : m_note->dueAtMs,
                 m_note->due);
        return;
    }
    if (m_chip) m_chip->setText(m_note->dueLabel().isEmpty() ? L("Sin fecha") : m_note->dueLabel());
    refreshRepeat();
    refreshDue();
    emit dirty();
}

void NoteCard::focusTitle() {
    if (!m_title) return;
    m_title->setFocus();
    m_title->selectAll();
}

/// Foldable header with the thumbnail strip below. Any note type can carry
/// images, like links, so this is built in build() and not in a type branch.
void NoteCard::buildImages(QVBoxLayout *l) {
    m_imagesBox = new QWidget;
    auto *col = new QVBoxLayout(m_imagesBox);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(5);

    // LinkRow is already a clickable row with its own menu and styled background;
    // only the object name the theme paints it by changes.
    auto *header = new LinkRow;
    header->setObjectName("imgHeader");
    header->activate = [this] { toggleImages(); };
    header->menu = [this](const QPoint &at) { openImageMenu(-1, at); };

    auto *hl = new QHBoxLayout(header);
    hl->setContentsMargins(4, 2, 2, 2);
    hl->setSpacing(6);

    auto *icon = new QLabel;
    icon->setFixedSize(13, 13);
    icon->setPixmap(paintIcon("image", QColor(Theme::muted()), 13).pixmap(13, 13));
    hl->addWidget(icon, 0, Qt::AlignVCenter);

    m_imagesTitle = new QLabel;
    m_imagesTitle->setObjectName("imgTitle");
    hl->addWidget(m_imagesTitle, 1);

    m_imagesToggle = new QToolButton;
    m_imagesToggle->setIconSize(QSize(12, 12));
    m_imagesToggle->setFixedSize(18, 18);
    m_imagesToggle->setCursor(Qt::PointingHandCursor);
    connect(m_imagesToggle, &QToolButton::clicked, this, &NoteCard::toggleImages);
    hl->addWidget(m_imagesToggle, 0, Qt::AlignVCenter);
    col->addWidget(header);

    m_imagesStrip = new QWidget;
    m_imagesLayout = new QVBoxLayout(m_imagesStrip);
    m_imagesLayout->setContentsMargins(0, 0, 0, 0);
    m_imagesLayout->setSpacing(4);
    col->addWidget(m_imagesStrip);

    const int at = m_meta ? l->indexOf(m_meta) : -1;
    if (at >= 0) l->insertWidget(at, m_imagesBox);
    else l->addWidget(m_imagesBox);
    refreshImages();
}

void NoteCard::refreshImages() {
    if (!m_imagesLayout) return;

    // Hide before deleting: out of the layout they stay painted until
    // deleteLater() runs, and a thumbnail is very visible.
    while (QLayoutItem *it = m_imagesLayout->takeAt(0)) {
        if (QWidget *w = it->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete it;
    }

    const int count = int(m_note->images.size());
    m_imagesBox->setVisible(count > 0);
    if (count == 0) return;

    const bool hidden = m_note->imagesHidden;
    m_imagesTitle->setText(count == 1 ? L("1 IMAGEN") : L("%1 IMÁGENES").arg(count));
    m_imagesToggle->setIcon(paintIcon(hidden ? "chevronRight" : "chevronDown",
                                      QColor(Theme::muted()), 12));
    m_imagesToggle->setToolTip(hidden ? L("Mostrar imágenes") : L("Ocultar imágenes"));

    // Folded, thumbnails are not even built: a closed card has no reason to load
    // screenshots nobody sees.
    m_imagesStrip->setVisible(!hidden);
    if (hidden) return;

    for (int i = 0; i < count; ++i) {
        auto *thumb = new ImageThumb(Note::imagePath(m_note->images.at(i)));
        thumb->activate = [this, i] {
            if (i < m_note->images.size())
                QDesktopServices::openUrl(
                    QUrl::fromLocalFile(Note::imagePath(m_note->images.at(i))));
        };
        thumb->menu = [this, i](const QPoint &at) { openImageMenu(i, at); };
        // The thumbnail has its own size: left-aligned, not centred in a gap it no
        // longer fills.
        m_imagesLayout->addWidget(thumb, 0, Qt::AlignLeft);
    }
}

void NoteCard::toggleImages() {
    if (m_note->images.isEmpty()) return;
    m_note->imagesHidden = !m_note->imagesHidden;
    refreshImages();
    emit dirty();
}

/// Images are copied into the data folder, not linked: the note keeps its
/// picture if the original moves, and attachments travel with the notes.
void NoteCard::addImages() {
    const QStringList picked = QFileDialog::getOpenFileNames(
        this, L("Elegir imágenes"), QDir::homePath(),
        L("Imágenes") + " (*.png *.jpg *.jpeg *.gif *.bmp *.webp)");
    if (picked.isEmpty()) return;

    bool added = false;
    for (const QString &source : picked) {
        const QFileInfo info(source);
        const QString suffix = info.suffix().isEmpty() ? "png" : info.suffix().toLower();
        // The name carries the note and a timestamp: two sources with the same name
        // cannot overwrite each other.
        const QString name = QString("%1-%2.%3")
                                 .arg(m_note->id)
                                 .arg(QDateTime::currentMSecsSinceEpoch() +
                                      m_note->images.size())
                                 .arg(suffix);
        if (!QFile::copy(source, Note::imagePath(name))) continue;
        m_note->images.append(name);
        added = true;
    }
    if (!added) return;

    m_note->imagesHidden = false;   // just added: show it
    refreshImages();
    emit dirty();
}

void NoteCard::openImageMenu(int index, const QPoint &globalPos) {
    auto *menu = new Popup(m_theme, this);
    menu->addHeader(L("Imágenes"));
    menu->addItem("plus", L("Añadir imagen…"), L("Se copia junto a la nota"),
                  [this] { addImages(); });

    if (!m_note->images.isEmpty())
        menu->addItem(m_note->imagesHidden ? "chevronDown" : "chevronRight",
                      m_note->imagesHidden ? L("Mostrar imágenes") : L("Ocultar imágenes"),
                      QString(), [this] { toggleImages(); });

    if (index >= 0 && index < m_note->images.size()) {
        const QString name = m_note->images.at(index);
        menu->addSeparator();
        menu->addItem("image", L("Abrir"), QString(), [name] {
            QDesktopServices::openUrl(QUrl::fromLocalFile(Note::imagePath(name)));
        });
        menu->addItem("trash", L("Quitar imagen"), QString(), [this, index] {
            if (index >= m_note->images.size()) return;
            // The file is our copy: once removed from the note nobody refers to it.
            QFile::remove(Note::imagePath(m_note->images.at(index)));
            m_note->images.removeAt(index);
            refreshImages();
            emit dirty();
        });
    }
    menu->showAt(globalPos);
}

void NoteCard::refreshLinks() {
    if (!m_linksLayout) return;

    // Hide before deleting: out of the layout a row stays painted, and alive,
    // until deleteLater() runs.
    while (QLayoutItem *it = m_linksLayout->takeAt(0)) {
        if (QWidget *w = it->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete it;
    }

    for (int i = 0; i < m_note->links.size(); ++i) {
        const Link &link = m_note->links.at(i);

        auto *row = new LinkRow;
        row->activate = [this, i] { openLink(i); };
        row->menu = [this, i](const QPoint &at) { openLinkMenu(i, at); };

        auto *rl = new QHBoxLayout(row);
        rl->setContentsMargins(4, 3, 4, 3);
        rl->setSpacing(6);

        auto *icon = new QLabel;
        icon->setFixedSize(13, 13);
        icon->setPixmap(paintIcon("link", m_theme.accent, 13).pixmap(13, 13));
        rl->addWidget(icon, 0, Qt::AlignVCenter);

        auto *text = new ElidedLabel(linkText(link), m_theme.accent);
        text->setObjectName("linkText");
        text->setUnderlineOnHover(true);
        text->setContextMenuPolicy(Qt::NoContextMenu);
        text->setToolTip(link.url);
        rl->addWidget(text, 1);

        m_linksLayout->addWidget(row);
    }
    m_linksBox->setVisible(!m_note->links.isEmpty());
}

void NoteCard::openLink(int index) {
    if (index < 0 || index >= m_note->links.size()) return;
    QDesktopServices::openUrl(QUrl(m_note->links.at(index).url));
}

void NoteCard::openLinkEditor(int index, const QPoint &globalPos) {
    const bool isNew = index < 0 || index >= m_note->links.size();
    const Link current = isNew ? Link{} : m_note->links.at(index);

    auto *menu = new Popup(m_theme, this);
    menu->addHeader(isNew ? L("Nuevo enlace") : L("Editar enlace"));
    menu->addFields({L("https://ejemplo.com"), L("Nombre (opcional)")},
                    {current.url, current.label},
                    [this, index, isNew](const QStringList &values) {
                        const QString url = normalizedUrl(values.value(0));
                        if (url.isEmpty()) return;          // no address, no link

                        Link link{url, values.value(1)};
                        if (isNew) m_note->links.append(link);
                        else if (index < m_note->links.size()) m_note->links[index] = link;

                        refreshLinks();
                        emit dirty();
                    });
    menu->showAt(globalPos);
}

void NoteCard::openLinkMenu(int index, const QPoint &globalPos) {
    if (index < 0 || index >= m_note->links.size()) return;
    const Link link = m_note->links.at(index);

    auto *menu = new Popup(m_theme, this);
    menu->addHeader(L("Enlace"));
    menu->addItem("link", L("Abrir"), prettyUrl(link.url), [this, index] { openLink(index); });
    menu->addItem("copy", L("Copiar dirección"), QString(),
                  [link] { QGuiApplication::clipboard()->setText(link.url); });
    menu->addItem("pencil", L("Editar…"), link.label.isEmpty() ? L("Sin nombre") : link.label,
                  [this, index, globalPos] { openLinkEditor(index, globalPos); });
    menu->addSeparator();
    menu->addItem("trash", L("Quitar enlace"), QString(), [this, index] {
        if (index >= m_note->links.size()) return;
        m_note->links.removeAt(index);
        refreshLinks();
        emit dirty();
    });
    menu->showAt(globalPos);
}

void NoteCard::refreshDue() {
    if (m_note->type != Note::Reminder) return;

    const bool ringing = m_note->ringing;
    const bool overdue = m_note->dueAtMs > 0 &&
                         QDateTime::currentMSecsSinceEpoch() >= m_note->dueAtMs;

    if (m_dueIcon)
        m_dueIcon->setPixmap(paintIcon(ringing || overdue ? "bell" : "clock",
                                       QColor(ringing || overdue ? "#ff7a6b" : Theme::muted()),
                                       14)
                                 .pixmap(14, 14));
    if (m_dueBtn) m_dueBtn->setVisible(ringing);
    if (m_chip) {
        // Re-read from the note: silencing a repeating reminder moves it to its next
        // turn, and the chip must show the new date without rebuilding the card.
        const QString label = m_note->dueLabel();
        m_chip->setText(label.isEmpty() ? L("Sin fecha") : label);
        m_chip->setProperty("state", ringing ? "ringing" : (overdue ? "overdue" : ""));
        // A dynamic property does not repaint by itself: repolish.
        m_chip->style()->unpolish(m_chip);
        m_chip->style()->polish(m_chip);
    }
    if (m_meta) {
        // At rest the label says how often it repeats, more useful than "REMINDER"
        // next to a clock icon.
        const QString idle = m_note->repeats() ? m_note->repeatLabel().toUpper()
                                               : L("RECORDATORIO");
        m_meta->setText(ringing ? L("¡AHORA!") : (overdue ? L("VENCIDO") : idle));
    }
    refreshRepeat();
}

void NoteCard::buildVoice(QVBoxLayout *l) {
    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(7);

    m_recBtn = roundButton("record", QColor("#ff7a6b"), L("Grabar"));
    m_recBtn->setObjectName("recBtn");
    connect(m_recBtn, &QToolButton::clicked, this, &NoteCard::toggleRecord);
    row->addWidget(m_recBtn);

    m_playBtn = roundButton("play", QColor(Theme::fg()), L("Reproducir"));
    m_playBtn->setObjectName("playBtn");
    connect(m_playBtn, &QToolButton::clicked, this, &NoteCard::togglePlay);
    row->addWidget(m_playBtn);

    m_wave = new Waveform;
    m_wave->setAccent(m_theme.accent);
    connect(m_wave, &Waveform::seeked, this, [this](qreal f) {
        if (m_player && m_player->duration() > 0)
            m_player->setPosition(qint64(f * m_player->duration()));
    });
    row->addWidget(m_wave, 1);

    m_progress = new QLabel;
    m_progress->setObjectName("meta");
    row->addWidget(m_progress);
    l->addLayout(row);

    m_meta = new QLabel;
    m_meta->setObjectName("meta");
    l->addWidget(m_meta);

    m_wave->setPeaks(m_note->peaks);
    refreshVoice();

    // Peaks already stored; if missing (older note) the file is scanned once and
    // the result saved. Deferred because dirty() is not connected yet during the
    // constructor, and so building the list does not read files from disk.
    if (m_note->peaks.isEmpty() && !m_note->audio.isEmpty()) {
        QTimer::singleShot(0, this, [this] {
            const WaveScan scan = scanWave(m_note->audioPath());
            if (!scan.ok) return;
            m_note->peaks = scan.peaks;
            m_note->level = int(scan.loudest * 100);
            m_wave->setPeaks(m_note->peaks);
            refreshVoice();
            emit dirty();
        });
    }
}

void NoteCard::ensureAudio() {
    if (m_recorder) return;

    m_recorder = new VoiceRecorder(this);
    connect(m_recorder, &VoiceRecorder::durationChanged, this, [this](qint64 ms) {
        m_note->durationMs = ms;
        if (m_wave) m_wave->setPeaks(m_recorder->peaks());
        refreshVoice();
    });
    connect(m_recorder, &VoiceRecorder::finished, this, [this](bool ok) {
        if (ok) {
            m_note->audio = m_note->id + ".wav";
            m_note->peaks = m_recorder->peaks();
            m_note->level = int(m_recorder->loudest() * 100);
            if (m_wave) {
                m_wave->setLive(false);
                m_wave->setPeaks(m_note->peaks);
            }
            // Re-recording overwrites the same file: unless the source is released, the
            // player keeps serving the previous take.
            if (m_player) m_player->setSource(QUrl());
            emit dirty();
        }
        refreshVoice();
    });

    m_player = new QMediaPlayer(this);
    m_audioOut = new QAudioOutput(this);
    m_player->setAudioOutput(m_audioOut);

    connect(m_player, &QMediaPlayer::positionChanged, this, [this](qint64 pos) {
        const qint64 total = m_player->duration();
        if (m_wave && total > 0) m_wave->setProgress(qreal(pos) / total);
        if (m_progress) m_progress->setText(formatMs(pos));
    });
    connect(m_player, &QMediaPlayer::playbackStateChanged, this,
            [this](QMediaPlayer::PlaybackState state) {
                if (state == QMediaPlayer::StoppedState && m_wave) m_wave->setProgress(0);
                refreshVoice();
            });
}

void NoteCard::toggleRecord() {
    ensureAudio();

    if (m_recorder->isRecording()) {
        m_recorder->stop();
        return;
    }

    if (m_player) m_player->stop();
    m_note->durationMs = 0;
    m_note->peaks.clear();
    if (m_wave) {
        m_wave->setLive(true);
        m_wave->setPeaks({});
    }
    // One file per note: recording again replaces the previous take.
    m_recorder->start(audioDir() + "/" + m_note->id + ".wav");
    refreshVoice();
}

void NoteCard::togglePlay() {
    if (m_note->audio.isEmpty()) return;
    ensureAudio();

    if (m_player->playbackState() == QMediaPlayer::PlayingState) {
        m_player->pause();
        refreshVoice();
        return;
    }
    if (m_player->source().isEmpty())
        m_player->setSource(QUrl::fromLocalFile(m_note->audioPath()));
    m_player->play();
    refreshVoice();
}

void NoteCard::refreshVoice() {
    const bool recording = m_recorder && m_recorder->isRecording();
    const bool playing = m_player && m_player->playbackState() == QMediaPlayer::PlayingState;
    const bool hasAudio = !m_note->audio.isEmpty();

    if (m_recBtn) {
        m_recBtn->setIcon(paintIcon(recording ? "stop" : "record", QColor("#ff7a6b")));
        m_recBtn->setToolTip(recording ? L("Detener") : (hasAudio ? L("Regrabar") : L("Grabar")));
    }
    if (m_playBtn) {
        m_playBtn->setIcon(paintIcon(playing ? "pause" : "play",
                                     QColor(hasAudio && !recording ? Theme::fg() : Theme::muted())));
        m_playBtn->setEnabled(hasAudio && !recording);
    }
    if (m_progress) m_progress->setText(formatMs(m_note->durationMs));

    if (!m_meta) return;
    if (recording) {
        m_meta->setText(L("GRABANDO…"));
    } else if (!hasAudio) {
        const QString err = m_recorder ? m_recorder->errorText() : QString();
        m_meta->setText(err.isEmpty() ? L("VOZ · SIN GRABAR") : err.toUpper());
    } else if (m_note->isSilentTake()) {
        // The take exists but has no audible signal; without saying so the user just
        // sees a note that "does not play".
        m_meta->setText(L("VOZ · SIN SEÑAL, REVISA EL MICRÓFONO"));
    } else {
        m_meta->setText(L("VOZ"));
    }
}

void NoteCard::contextMenuEvent(QContextMenuEvent *e) {
    auto *menu = new Popup(m_theme, this);
    menu->addHeader(typeLabel(m_note->type));

    if (m_note->type == Note::Check && m_newItem) {
        menu->addItem("plus", L("Añadir elemento"), L("Enter para confirmar"),
                      [this] { m_newItem->setFocus(); });
        menu->addSeparator();
    }
    if (m_note->type == Note::Reminder) {
        const QPoint at = e->globalPos();
        if (m_note->ringing)
            menu->addItem("stop", L("Detener aviso"), L("Silencia la alarma"),
                          [this] { emit dismissRequested(m_note); });
        menu->addItem("clock", L("Cambiar fecha"),
                      m_note->dueLabel().isEmpty() ? L("Sin fecha") : m_note->dueLabel(),
                      [this, at] { openDuePopup(at); });
        menu->addItem("repeat", L("Repetir"),
                      m_note->repeats() ? m_note->repeatLabel() : L("Suena una vez"),
                      [this, at] { openDuePopup(at); });
        if (m_note->body.isEmpty())
            menu->addItem("text", L("Añadir detalles"), L("Escribe bajo la fecha"),
                          [this] { showDetailsEditor(true); });
        menu->addSeparator();
    }
    if (m_note->type == Note::Voice) {
        menu->addItem("record", m_note->audio.isEmpty() ? L("Grabar") : L("Regrabar"),
                      L("Sustituye la toma actual"), [this] { toggleRecord(); });
        menu->addSeparator();
    }

    const QPoint at = e->globalPos();
    menu->addItem("link", L("Añadir enlace…"),
                  m_note->links.isEmpty()
                      ? L("Se abre en el navegador")
                      : L("%1 ya adjuntos").arg(m_note->links.size()),
                  [this, at] { openLinkEditor(-1, at); });
    menu->addItem("image", L("Añadir imagen…"),
                  m_note->images.isEmpty()
                      ? L("Se copia junto a la nota")
                      : L("%1 ya adjuntas").arg(m_note->images.size()),
                  [this] { addImages(); });
    if (!m_note->images.isEmpty())
        menu->addItem(m_note->imagesHidden ? "chevronDown" : "chevronRight",
                      m_note->imagesHidden ? L("Mostrar imágenes") : L("Ocultar imágenes"),
                      QString(), [this] { toggleImages(); });

    // Reordering from here too: a step at a time reaches far in a long list
    // without fighting the scroll.
    menu->addSeparator();
    menu->addHeader(L("Orden"));
    menu->addItem("chevronUp", L("Subir"), QString(),
                  [this] { emit moveRequested(m_note, -1); });
    menu->addItem("chevronDown", L("Bajar"), QString(),
                  [this] { emit moveRequested(m_note, 1); });
    menu->addItem("chevronRight", L("Mover a…"), L("Otra área de trabajo"),
                  [this, at] { emit areaMenuRequested(m_note, at); });

    menu->addSeparator();
    menu->addItem("trash", L("Eliminar nota"), QString(),
                  [this] { emit deleteRequested(m_note); });
    menu->showAt(at);
}
