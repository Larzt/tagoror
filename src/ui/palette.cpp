#include "ui/palette.hpp"

#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

#include "core/lang.hpp"

namespace {

constexpr int kRow = 32;
constexpr int kHead = 26;
constexpr int kMargin = 8;     ///< Around the dialog, inside the panel.
constexpr int kTopGap = 40;    ///< Below the panel's top: the header still shows.

QFont monoFont(const QFont &base, qreal px) {
    QFont f = base;
    f.setFamily("IBM Plex Mono");
    f.setStyleHint(QFont::Monospace);
    f.setPixelSize(qRound(px));
    return f;
}

}  // namespace

namespace palette {

/// The grouped rows, painted: a heading per group, then its commands. One
/// selected row, moved by the arrows or the pointer; the field keeps the
/// keyboard focus throughout, as in any palette.
class CommandList : public QWidget {
public:
    std::function<void(int)> picked;   ///< Index into shown().

    explicit CommandList(const Theme *theme) : m_theme(theme) {
        setMouseTracking(true);
        setAttribute(Qt::WA_StyledBackground, false);
    }

    void setCommands(const QList<CommandPalette::Command> &cmds) {
        m_cmds = cmds;
        m_selected = cmds.isEmpty() ? -1 : 0;
        m_rows.clear();
        int y = 4;
        QString group;
        for (int i = 0; i < cmds.size(); ++i) {
            if (cmds.at(i).group != group) {
                group = cmds.at(i).group;
                m_rows.append({-1, y, group});
                y += kHead;
            }
            m_rows.append({i, y, QString()});
            y += kRow;
        }
        setFixedHeight(cmds.isEmpty() ? 64 : y + 4);
        update();
    }

    int selected() const { return m_selected; }
    int count() const { return int(m_cmds.size()); }

    void select(int i) {
        if (m_cmds.isEmpty()) return;
        m_selected = qBound(0, i, int(m_cmds.size()) - 1);
        update();
    }

    /// The selected row's rectangle, to scroll it into view.
    QRect selectedRect() const {
        for (const Row &r : m_rows)
            if (r.index == m_selected) return QRect(0, r.y, width(), kRow);
        return {};
    }

protected:
    void mouseMoveEvent(QMouseEvent *e) override {
        const int i = indexAt(e->position().toPoint().y());
        setCursor(i >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        if (i >= 0 && i != m_selected) select(i);
    }
    void mouseReleaseEvent(QMouseEvent *e) override {
        const int i = indexAt(e->position().toPoint().y());
        if (i >= 0 && picked) picked(i);
    }

    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        if (m_cmds.isEmpty()) {
            QFont f = font();
            f.setPixelSize(12);
            p.setFont(f);
            p.setPen(QColor(Theme::muted()));
            p.drawText(rect(), Qt::AlignCenter, L("Nada coincide"));
            return;
        }
        for (const Row &r : m_rows) {
            if (r.index < 0) {
                QFont mono = monoFont(font(), 10);
                mono.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
                p.setFont(mono);
                p.setPen(QColor("#636872"));
                p.drawText(QRect(14, r.y, width() - 28, kHead - 4), Qt::AlignLeft | Qt::AlignBottom,
                           r.group.toUpper());
                continue;
            }
            const CommandPalette::Command &c = m_cmds.at(r.index);
            const QRectF row(6, r.y, width() - 12, kRow);
            const bool sel = r.index == m_selected;
            if (sel) {
                QColor tint = m_theme->accent;
                tint.setAlphaF(0.16);
                p.setPen(Qt::NoPen);
                p.setBrush(tint);
                p.drawRoundedRect(row, 7, 7);
            }
            paintIcon(c.icon, sel ? m_theme->accent : QColor(Theme::muted()), 16)
                .paint(&p, QRect(int(row.left()) + 8, r.y + (kRow - 16) / 2, 16, 16));

            int hintW = 0;
            if (!c.hint.isEmpty()) {
                const QFont mono = monoFont(font(), 10);
                p.setFont(mono);
                p.setPen(QColor("#636872"));
                hintW = QFontMetrics(mono).horizontalAdvance(c.hint) + 12;
                p.drawText(QRectF(row).adjusted(0, 0, -8, 0), Qt::AlignVCenter | Qt::AlignRight, c.hint);
            }
            QFont f = font();
            f.setPixelSize(13);
            p.setFont(f);
            p.setPen(QColor(Theme::fg()));
            const QRect text(int(row.left()) + 34, r.y, int(row.width()) - 42 - hintW, kRow);
            p.drawText(text, Qt::AlignVCenter | Qt::AlignLeft,
                       QFontMetrics(f).elidedText(c.label, Qt::ElideRight, text.width()));
        }
    }

private:
    struct Row {
        int index;      ///< -1 for a heading.
        int y;
        QString group;
    };

    int indexAt(int y) const {
        for (const Row &r : m_rows)
            if (r.index >= 0 && y >= r.y && y < r.y + kRow) return r.index;
        return -1;
    }

    const Theme *m_theme;
    QList<CommandPalette::Command> m_cmds;
    QList<Row> m_rows;
    int m_selected = -1;
};

}  // namespace palette

CommandPalette::CommandPalette(const Theme *theme, QWidget *host)
    : QWidget(host), m_theme(theme) {
    setObjectName("palette");
    hide();

    m_dialog = new QWidget(this);
    m_dialog->setObjectName("paletteDialog");
    m_dialog->setAttribute(Qt::WA_StyledBackground, true);
    auto *col = new QVBoxLayout(m_dialog);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);

    auto *top = new QWidget;
    top->setObjectName("paletteTop");
    top->setAttribute(Qt::WA_StyledBackground, true);
    top->setFixedHeight(44);
    auto *tl = new QHBoxLayout(top);
    tl->setContentsMargins(12, 0, 10, 0);
    tl->setSpacing(9);
    auto *icon = new QLabel;
    icon->setPixmap(paintIcon("search", QColor(Theme::muted()), 16).pixmap(16, 16));
    tl->addWidget(icon);
    m_field = new QLineEdit;
    m_field->setObjectName("paletteField");
    m_field->setContextMenuPolicy(Qt::NoContextMenu);
    m_field->setMinimumWidth(24);
    m_field->installEventFilter(this);
    connect(m_field, &QLineEdit::textChanged, this, [this] { filter(); });
    tl->addWidget(m_field, 1);
    auto *esc = new QLabel("Esc");
    esc->setObjectName("paletteKey");
    esc->setFixedHeight(18);
    esc->setAlignment(Qt::AlignCenter);
    tl->addWidget(esc, 0, Qt::AlignVCenter);
    col->addWidget(top);

    m_list = new palette::CommandList(m_theme);
    m_list->picked = [this](int i) {
        m_list->select(i);
        runSelected();
    };
    m_scroll = new QScrollArea;
    m_scroll->setObjectName("paletteScroll");
    m_scroll->setWidget(m_list);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setFocusPolicy(Qt::NoFocus);
    m_scroll->viewport()->setObjectName("paletteViewport");
    m_list->setAutoFillBackground(false);
    col->addWidget(m_scroll, 1);

    auto *foot = new QLabel(QString());
    foot->setObjectName("paletteFoot");
    foot->setFixedHeight(28);
    foot->setContentsMargins(14, 0, 14, 0);
    col->addWidget(foot);

    host->installEventFilter(this);
}

QString CommandPalette::fold(const QString &text) {
    QString out;
    for (const QChar c : text.normalized(QString::NormalizationForm_D))
        if (c.category() != QChar::Mark_NonSpacing) out += c.toLower();
    return out;
}

void CommandPalette::open(const QList<Command> &commands) {
    m_all = commands;
    // The footer is re-read here so it follows the language.
    if (auto *foot = m_dialog->findChild<QLabel *>("paletteFoot"))
        foot->setText(L("↑↓ moverse   ↵ abrir   Esc cerrar"));
    m_field->setPlaceholderText(L("Página, acción o área…"));
    m_back = QApplication::focusWidget();
    m_field->clear();
    filter();
    show();
    raise();
    m_field->setFocus(Qt::ShortcutFocusReason);
}

void CommandPalette::close() {
    if (!isVisible()) return;
    hide();
    m_field->clear();
    if (m_back) m_back->setFocus(Qt::OtherFocusReason);
}

void CommandPalette::filter() {
    // Every word typed must appear in the label or its group: "nueva voz" finds
    // "Nueva nota de voz", "areas trabajo" the area.
    const QStringList words = fold(m_field->text()).split(' ', Qt::SkipEmptyParts);
    QList<Command> shown;
    for (const Command &c : m_all) {
        const QString hay = fold(c.label + ' ' + c.group);
        bool ok = true;
        for (const QString &w : words) ok = ok && hay.contains(w);
        if (ok) shown << c;
    }
    m_list->setCommands(shown);
    m_shown = shown;
    m_scroll->verticalScrollBar()->setValue(0);
    place();   // the dialog is as tall as what it lists, up to the panel
}

void CommandPalette::runSelected() {
    const int i = m_list->selected();
    if (i < 0 || i >= m_shown.size()) return;
    const std::function<void()> run = m_shown.at(i).run;
    close();
    // After closing: the action may open a menu or rebuild what is under it.
    if (run) QTimer::singleShot(0, parentWidget(), run);
}

void CommandPalette::place() {
    QWidget *host = parentWidget();
    setGeometry(host->rect());
    // Beside the planner the panel is 900px wide: a palette that wide reads like a
    // page, not like a box to type into.
    const int w = qMin(width() - 2 * kMargin, 560);
    const int maxH = height() - kTopGap - kMargin;
    const int want = 44 + m_list->height() + 28 + 2;
    m_dialog->setGeometry((width() - w) / 2, kTopGap, w, qBound(140, want, qMax(140, maxH)));
}

void CommandPalette::paintEvent(QPaintEvent *) {
    // The panel shows through, dimmed: where you were is still where you are.
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(9, 11, 14, 158));
    p.drawRoundedRect(QRectF(rect()), 14, 14);
}

void CommandPalette::mousePressEvent(QMouseEvent *e) {
    // A click on the dimmed panel, outside the dialog, dismisses.
    if (!m_dialog->geometry().contains(e->position().toPoint())) close();
}

bool CommandPalette::eventFilter(QObject *watched, QEvent *event) {
    if (watched == parentWidget() && event->type() == QEvent::Resize && isVisible()) place();
    if (watched == m_field && event->type() == QEvent::KeyPress) {
        auto *k = static_cast<QKeyEvent *>(event);
        const auto follow = [this] {
            const QRect r = m_list->selectedRect();
            m_scroll->ensureVisible(r.center().x(), r.center().y(), 0, r.height());
        };
        switch (k->key()) {
            case Qt::Key_Down:
                m_list->select(m_list->selected() + 1);
                follow();
                return true;
            case Qt::Key_Up:
                m_list->select(m_list->selected() - 1);
                follow();
                return true;
            case Qt::Key_PageDown:
                m_list->select(m_list->selected() + 6);
                follow();
                return true;
            case Qt::Key_PageUp:
                m_list->select(m_list->selected() - 6);
                follow();
                return true;
            case Qt::Key_Return:
            case Qt::Key_Enter:
                runSelected();
                return true;
            case Qt::Key_Escape:
                close();
                return true;
            default:
                break;
        }
    }
    return QWidget::eventFilter(watched, event);
}
