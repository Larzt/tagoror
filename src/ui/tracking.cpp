#include "ui/tracking.hpp"

#include <algorithm>

#include <QAbstractListModel>
#include <QCompleter>
#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHash>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStringListModel>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>
#include <functional>

#include "core/lang.hpp"
#include "ui/elidedlabel.hpp"
#include "ui/keynav.hpp"
#include "ui/popup.hpp"

namespace {

const QColor kDim("#636872");
/// Sessions without a project: the muted grey, which reads as "no colour".
const QColor kNoProject("#8b909a");

QColor withAlpha(QColor c, qreal alpha) {
    c.setAlphaF(alpha);
    return c;
}

QFont mono(const QFont &base, qreal px, QFont::Weight weight = QFont::Normal) {
    QFont f = base;
    f.setFamily("IBM Plex Mono");
    f.setStyleHint(QFont::Monospace);
    f.setPixelSize(qRound(px));
    f.setWeight(weight);
    return f;
}

QFont sans(const QFont &base, qreal px, QFont::Weight weight = QFont::Normal) {
    QFont f = base;
    f.setPixelSize(qRound(px));
    f.setWeight(weight);
    return f;
}

QString hhmm(qint64 ms) { return QDateTime::fromMSecsSinceEpoch(ms).toString("HH:mm"); }

/// "HOY · VIE 30 OCT", "AYER · JUE 29 OCT", "MIÉ 28 OCT". Short names come with a
/// full stop in some locales ("vie."), which a heading does not need.
QString dayHeading(const QDate &d) {
    const QDate today = QDate::currentDate();
    QString date = Lang::locale().toString(d, d.year() == today.year() ? "ddd d MMM" : "ddd d MMM yyyy");
    date.remove('.');
    date = date.toUpper();
    if (d == today) return L("HOY") + " · " + date;
    if (d == today.addDays(-1)) return L("AYER") + " · " + date;
    return date;
}

/// "26 oct – 1 nov", or the month's name.
QString periodName(const QDate &from, const QDate &to, bool month) {
    if (month) {
        QString m = Lang::locale().toString(from, "MMMM yyyy");
        return m.left(1).toUpper() + m.mid(1);
    }
    QString a = Lang::locale().toString(from, from.month() == to.month() ? "d" : "d MMM");
    QString b = Lang::locale().toString(to, "d MMM");
    a.remove('.');
    b.remove('.');
    return a + " – " + b;
}

QToolButton *iconButton(const QString &kind, const QString &tip, int size = 28) {
    auto *b = new QToolButton;
    b->setIcon(paintIcon(kind, QColor(Theme::muted())));
    b->setIconSize(QSize(16, 16));
    b->setFixedSize(size, size);
    b->setCursor(Qt::PointingHandCursor);
    b->setFocusPolicy(Qt::TabFocus);
    b->setToolTip(tip);
    return b;
}

QLabel *section(const QString &text) {
    auto *l = new QLabel(text.toUpper());
    l->setObjectName("pageSection");
    return l;
}

void repolish(QWidget *w) {
    w->style()->unpolish(w);
    w->style()->polish(w);
    w->update();
}

}  // namespace

namespace track {

// ---------------------------------------------------------------------------
// The stopwatch

/// The card on top. Idle it is an invitation: the field, the project and a
/// 0:00:00 that is already there. Running it takes the project's colour, with
/// the clock counting up, pause and stop. Built once: the field survives every
/// refresh, so a sync or a tick never takes half a word away.
class ClockCard : public QFrame {
public:
    std::function<void(const QString &)> submit;   ///< Enter or ▶ with text.
    std::function<void()> pause;
    std::function<void()> stop;
    std::function<void(QWidget *)> pickProject;
    std::function<void(const QString &)> typed;

    explicit ClockCard(const Theme *theme) : m_theme(theme) {
        setObjectName("trackClock");
        auto *col = new QVBoxLayout(this);
        col->setContentsMargins(12, 10, 10, 10);
        col->setSpacing(6);

        m_top = new QHBoxLayout;
        m_top->setSpacing(8);
        m_field = new QLineEdit;
        m_field->setObjectName("trackField");
        m_field->setMinimumWidth(40);
        m_task = new ElidedLabel(QString(), QColor(Theme::fg()));
        m_task->setObjectName("trackTask");
        m_task->hide();
        m_top->addWidget(m_field, 1);
        m_top->addWidget(m_task, 1);
        col->addLayout(m_top);

        m_bottom = new QHBoxLayout;
        m_bottom->setSpacing(8);
        m_pick = new QToolButton;
        m_pick->setObjectName("trackChip");
        m_pick->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        m_pick->setCursor(Qt::PointingHandCursor);
        m_pick->setFocusPolicy(Qt::TabFocus);
        m_pick->setIconSize(QSize(12, 12));
        m_pick->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        m_pick->setMinimumWidth(40);
        m_bottom->addWidget(m_pick, 0);
        m_bottom->addStretch(1);
        m_clock = new QLabel(Track::clock(0));
        m_clock->setObjectName("trackClockTime");
        m_pause = new QToolButton;
        m_pause->setObjectName("trackGhost");
        m_pause->setFixedSize(34, 34);
        m_pause->setIconSize(QSize(14, 14));
        m_pause->setCursor(Qt::PointingHandCursor);
        m_pause->setFocusPolicy(Qt::TabFocus);
        m_main = new QToolButton;
        m_main->setObjectName("trackGo");
        m_main->setFixedSize(34, 34);
        m_main->setIconSize(QSize(14, 14));
        m_main->setCursor(Qt::PointingHandCursor);
        m_main->setFocusPolicy(Qt::TabFocus);
        placeControls();
        col->addLayout(m_bottom);

        m_sub = new ElidedLabel(QString(), QColor(Theme::muted()));
        m_sub->setObjectName("timerSub");
        col->addWidget(m_sub);

        connect(m_field, &QLineEdit::returnPressed, this, [this] { go(); });
        connect(m_field, &QLineEdit::textEdited, this, [this](const QString &t) {
            if (typed) typed(t);
        });
        connect(m_main, &QToolButton::clicked, this, [this] {
            if (m_running) {
                if (stop) stop();
            } else {
                go();
            }
        });
        connect(m_pause, &QToolButton::clicked, this, [this] { if (pause) pause(); });
        connect(m_pick, &QToolButton::clicked, this, [this] { if (pickProject) pickProject(m_pick); });
        apply();
        showPick(QColor(), L("Sin proyecto"));
    }

    QLineEdit *field() const { return m_field; }

    void retranslate() {
        m_field->setPlaceholderText(L("¿En qué estás trabajando?"));
        m_main->setToolTip(m_running ? L("Detener y guardar") : L("Empezar · Intro"));
        m_pause->setToolTip(m_paused ? L("Reanudar · Ctrl+⇧+Espacio")
                                     : L("Pausar · Ctrl+⇧+Espacio"));
        m_pick->setToolTip(L("Proyecto · opcional"));
    }

    /// Compact (on Resumen): one row, the clock and its buttons beside the task.
    void setCompact(bool on) {
        if (on == m_compact) return;
        m_compact = on;
        placeControls();
        apply();
        m_sub->setVisible(!on && m_running);
    }

    /// The project the next session will start with, while idle.
    void setPending(const Project *p) {
        m_pending = p ? p->name : QString();
        m_pendingColor = p ? p->tint() : QColor();
        if (!m_running) showPick(p ? p->tint() : QColor(), p ? p->name : L("Sin proyecto"));
    }

    /// Everything from scratch: state, colours, texts.
    void present(const Session *s, const Project *p, qint64 todayMs) {
        const bool running = s != nullptr;
        const bool paused = s && s->paused();
        const QColor tint = s ? (p ? p->tint() : m_theme->accent) : QColor();
        const bool changed = running != m_running || paused != m_paused || tint != m_tint ||
                             (s ? s->id : QString()) != m_id;
        m_running = running;
        m_paused = paused;
        m_tint = tint;
        m_id = s ? s->id : QString();
        if (changed) apply();
        if (s) {
            m_task->setText(s->task.isEmpty() ? L("Sin tarea") : s->task);
            m_task->setToolTip(s->task);
            m_task->update();
            showPick(p ? p->tint() : QColor(), p ? p->name : L("Sin proyecto"));
        } else {
            showPick(m_pendingColor, m_pending.isEmpty() ? L("Sin proyecto") : m_pending);
        }
        numbers(s, todayMs);
    }

    /// The accent or the font changed: colours again, whatever the state.
    void restyle() { apply(); }

private:
    /// Only the numbers, every second.
    void numbers(const Session *s, qint64 todayMs) {
        m_clock->setText(Track::clock(s ? s->durationMs() : 0));
        if (!s) {
            m_sub->hide();
            return;
        }
        QString sub = L("desde %1 · hoy %2").arg(hhmm(s->startMs), Track::words(todayMs));
        if (s->paused()) sub = L("en pausa") + " · " + sub;
        m_sub->setText(sub);
        m_sub->setVisible(!m_compact);
        m_sub->update();
    }

    /// The clock and its buttons: on the second row, or beside the task when
    /// compact. Moving a widget between two layouts of one parent keeps it.
    void placeControls() {
        for (QWidget *w : {static_cast<QWidget *>(m_clock), static_cast<QWidget *>(m_pause),
                           static_cast<QWidget *>(m_main)}) {
            m_top->removeWidget(w);
            m_bottom->removeWidget(w);
        }
        QHBoxLayout *into = m_compact ? m_top : m_bottom;
        into->addWidget(m_clock, 0, Qt::AlignVCenter);
        into->addWidget(m_pause, 0, Qt::AlignVCenter);
        into->addWidget(m_main, 0, Qt::AlignVCenter);
    }

    void showPick(const QColor &color, const QString &name) {
        m_pick->setIcon(color.isValid() ? paintIcon("glyph-dot", color, 12)
                                        : paintIcon("minus", QColor(Theme::muted()), 12));
        const QFontMetrics fm(m_pick->font());
        m_pick->setText(fm.elidedText(name, Qt::ElideRight, 150) + QStringLiteral("  ▾"));
    }

    void go() {
        const QString text = m_field->text().trimmed();
        if (text.isEmpty()) {
            m_field->setFocus(Qt::OtherFocusReason);
            return;
        }
        if (submit) submit(text);
        m_field->clear();
    }

    /// Colours and what shows, after a change of state.
    void apply() {
        m_field->setVisible(!m_running);
        m_task->setVisible(m_running);
        m_pause->setVisible(m_running);
        // The second row is the project and the clock; compact and idle on
        // Resumen, it is only the project picker, which is not worth a row.
        m_pick->setVisible(!m_compact);

        const QColor accent = m_theme->accent;
        if (m_running) {
            const QColor c = m_tint;
            const qreal a = m_paused ? 0.05 : 0.11;
            setStyleSheet(QString("QFrame#trackClock { background: rgba(%1,%2,%3,%4);"
                                  " border: 1px solid rgba(%1,%2,%3,%5); border-radius: 12px; }"
                                  "QToolButton#trackGo { background: %6; border: none; border-radius: 17px; }"
                                  "QToolButton#trackGo:hover { background: %7; }")
                              .arg(c.red()).arg(c.green()).arg(c.blue())
                              .arg(a, 0, 'f', 3).arg(m_paused ? 0.25 : 0.5, 0, 'f', 3)
                              .arg(c.name(), c.lighter(112).name()));
            m_main->setIcon(paintIcon("stop", QColor("#0d1014"), 14));
            m_pause->setIcon(paintIcon(m_paused ? "play" : "pause", QColor(Theme::fg()), 14));
            m_clock->setProperty("state", m_paused ? "paused" : "running");
        } else {
            setStyleSheet(QString("QToolButton#trackGo { background: %1; border: none; border-radius: 17px; }"
                                  "QToolButton#trackGo:hover { background: %2; }")
                              .arg(accent.name(), accent.lighter(112).name()));
            m_main->setIcon(paintIcon("play", QColor("#0d1014"), 14));
            m_clock->setProperty("state", "idle");
        }
        m_clock->setProperty("compact", m_compact);
        repolish(m_clock);
        retranslate();
    }

    const Theme *m_theme;
    QHBoxLayout *m_top = nullptr;
    QHBoxLayout *m_bottom = nullptr;
    QLineEdit *m_field = nullptr;
    ElidedLabel *m_task = nullptr;
    ElidedLabel *m_sub = nullptr;
    QToolButton *m_pick = nullptr;
    QLabel *m_clock = nullptr;
    QToolButton *m_pause = nullptr;
    QToolButton *m_main = nullptr;
    QString m_pending;
    QColor m_pendingColor;
    bool m_running = false;
    bool m_paused = false;
    bool m_compact = false;
    QColor m_tint;
    QString m_id;
};

// ---------------------------------------------------------------------------
// Registro

/// Rows of the log: a heading per day, today's line, and the sessions.
class LogModel : public QAbstractListModel {
public:
    struct Row {
        enum Kind { Day, Line, Item };
        Kind kind = Item;
        QDate day;
        Session *session = nullptr;
        QList<Session *> group;   ///< Day and Line: the day's sessions.
    };

    using QAbstractListModel::QAbstractListModel;

    void setRows(const QList<Row> &rows) {
        beginResetModel();
        m_rows = rows;
        endResetModel();
    }
    const Row &row(int i) const { return m_rows.at(i); }
    int indexOf(const Session *s) const {
        for (int i = 0; i < m_rows.size(); ++i)
            if (m_rows.at(i).session == s) return i;
        return -1;
    }

    int rowCount(const QModelIndex &parent = {}) const override {
        return parent.isValid() ? 0 : int(m_rows.size());
    }
    QVariant data(const QModelIndex &index, int role) const override {
        if (!index.isValid() || role != Qt::AccessibleTextRole) return {};
        const Row &r = m_rows.at(index.row());
        return r.session ? r.session->task : dayHeading(r.day);
    }
    Qt::ItemFlags flags(const QModelIndex &index) const override {
        if (!index.isValid()) return Qt::NoItemFlags;
        return m_rows.at(index.row()).kind == Row::Item ? Qt::ItemIsEnabled | Qt::ItemIsSelectable
                                                        : Qt::ItemIsEnabled;
    }

private:
    QList<Row> m_rows;
};

constexpr int kDayH = 32;
constexpr int kLineH = 44;
constexpr int kItemH = 48;
constexpr int kAction = 26;   ///< ▶ and ⋯ on a hovered row.

/// Where ▶ and ⋯ sit on a session's row.
QRect playRect(const QRect &row) {
    return QRect(row.right() - 2 * kAction - 8, row.center().y() - kAction / 2, kAction, kAction);
}
QRect moreRect(const QRect &row) {
    return QRect(row.right() - kAction - 4, row.center().y() - kAction / 2, kAction, kAction);
}

class LogDelegate : public QStyledItemDelegate {
public:
    LogDelegate(const Theme *theme, std::function<const Project *(const QString &)> project,
                QObject *parent)
        : QStyledItemDelegate(parent), m_theme(theme), m_project(std::move(project)) {}

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &index) const override {
        const auto *model = static_cast<const LogModel *>(index.model());
        switch (model->row(index.row()).kind) {
            case LogModel::Row::Day: return {100, kDayH};
            case LogModel::Row::Line: return {100, kLineH};
            default: return {100, kItemH};
        }
    }

    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &index) const override {
        const auto *model = static_cast<const LogModel *>(index.model());
        const LogModel::Row &r = model->row(index.row());
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (r.kind == LogModel::Row::Day) paintDay(p, opt, r, now);
        else if (r.kind == LogModel::Row::Line) paintLine(p, opt, r, now);
        else paintItem(p, opt, r, now);
        p->restore();
    }

private:
    QColor tintOf(const Session *s) const {
        const Project *pr = m_project(s->project);
        return pr ? pr->tint() : kNoProject;
    }

    void paintDay(QPainter *p, const QStyleOptionViewItem &opt, const LogModel::Row &r,
                  qint64 now) const {
        const QRect box = opt.rect.adjusted(4, 10, -6, -4);
        qint64 total = 0;
        for (const Session *s : r.group) total += s->durationMs(now);
        QFont f = mono(opt.font, 10);
        f.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
        p->setFont(f);
        p->setPen(kDim);
        p->drawText(box, Qt::AlignLeft | Qt::AlignVCenter, dayHeading(r.day));
        p->setFont(mono(opt.font, 10.5));
        p->setPen(QColor(Theme::muted()));
        p->drawText(box, Qt::AlignRight | Qt::AlignVCenter, Track::words(total));
    }

    /// The day from 8 to 20 h (wider if the sessions are): the sessions in their
    /// colours, the gaps nobody logged hatched, and a mark at the current time.
    void paintLine(QPainter *p, const QStyleOptionViewItem &opt, const LogModel::Row &r,
                   qint64 now) const {
        const QDateTime midnight(r.day, QTime(0, 0));
        const qint64 day0 = midnight.toMSecsSinceEpoch();
        int h0 = 8, h1 = 20;
        for (const Session *s : r.group) {
            h0 = qMin(h0, int((s->startMs - day0) / 3600000));
            h1 = qMax(h1, int((s->endOr(now) - day0 + 3599999) / 3600000));
        }
        h0 = qMax(0, h0);
        h1 = qMin(24, qMax(h1, h0 + 1));

        const QRect box = opt.rect.adjusted(6, 4, -8, -6);
        const QRectF track(box.left(), box.top() + 18, box.width(), 10);
        const auto xAt = [&](qint64 ms) {
            const qreal hours = (ms - day0) / 3600000.0;
            return track.left() + track.width() * qBound(0.0, (hours - h0) / (h1 - h0), 1.0);
        };

        p->setFont(mono(opt.font, 9.5));
        p->setPen(kDim);
        const int step = qMax(1, (h1 - h0 + 3) / 4);
        for (int h = h0; h <= h1; h += step) {
            const qreal x = track.left() + track.width() * (h - h0) / qreal(h1 - h0);
            const QRectF label(x - 20, box.top(), 40, 14);
            const Qt::Alignment align = h == h0 ? Qt::AlignLeft : h + step > h1 ? Qt::AlignRight
                                                                               : Qt::AlignHCenter;
            p->drawText(align == Qt::AlignLeft ? label.translated(20, 0)
                        : align == Qt::AlignRight ? label.translated(-20, 0) : label,
                        align | Qt::AlignVCenter, QString::number(h));
        }

        QPainterPath clip;
        clip.addRoundedRect(track, 5, 5);
        p->setClipPath(clip);
        p->fillRect(track, QColor(255, 255, 255, 10));
        // Up to now, what was not logged is a gap; after now it is just the day.
        const qreal nowX = r.day == QDate::currentDate() ? xAt(now) : track.right();
        QBrush hatch(QColor(255, 255, 255, 34), Qt::BDiagPattern);
        p->fillRect(QRectF(track.left(), track.top(), nowX - track.left(), track.height()), hatch);
        for (const Session *s : r.group) {
            const qreal x0 = xAt(s->startMs), x1 = xAt(s->endOr(now));
            const QColor c = tintOf(s);
            p->fillRect(QRectF(x0, track.top(), qMax(2.0, x1 - x0), track.height()),
                        s->paused() ? withAlpha(c, 0.45) : c);
        }
        p->setClipping(false);
        if (r.day == QDate::currentDate()) {
            p->setPen(QPen(QColor(Theme::fg()), 1.4));
            p->drawLine(QPointF(nowX, track.top() - 3), QPointF(nowX, track.bottom() + 3));
        }
    }

    void paintItem(QPainter *p, const QStyleOptionViewItem &opt, const LogModel::Row &r,
                   qint64 now) const {
        const Session *s = r.session;
        const QRect rowRect = opt.rect.adjusted(0, 1, -2, -1);
        const bool hover = opt.state & QStyle::State_MouseOver;
        const bool selected = opt.state & QStyle::State_Selected;
        if (selected) {
            p->setPen(QPen(withAlpha(m_theme->accent, 0.38), 1));
            p->setBrush(withAlpha(m_theme->accent, 0.10));
            p->drawRoundedRect(QRectF(rowRect).adjusted(0.5, 0.5, -0.5, -0.5), 9, 9);
        } else if (hover) {
            p->setPen(Qt::NoPen);
            p->setBrush(QColor(255, 255, 255, 18));
            p->drawRoundedRect(QRectF(rowRect), 9, 9);
        }

        const QColor tint = tintOf(s);
        const Project *pr = m_project(s->project);
        const int left = rowRect.left() + 12;
        const int rightW = 96;
        const int textW = rowRect.width() - 12 - rightW - 8;

        QFont title = sans(opt.font, m_theme->fs(12), QFont::DemiBold);
        p->setFont(title);
        p->setPen(QColor(Theme::fg()));
        p->drawText(QRect(left, rowRect.top() + 7, textW, 18), Qt::AlignLeft | Qt::AlignVCenter,
                    QFontMetrics(title).elidedText(s->task.isEmpty() ? L("Sin tarea") : s->task,
                                                   Qt::ElideRight, textW));
        p->setPen(Qt::NoPen);
        p->setBrush(tint);
        p->drawEllipse(QPointF(left + 3, rowRect.top() + 33), 3, 3);
        const QFont small = sans(opt.font, 10.5);
        p->setFont(small);
        p->setPen(QColor(Theme::muted()));
        p->drawText(QRect(left + 11, rowRect.top() + 26, textW - 11, 15), Qt::AlignLeft | Qt::AlignVCenter,
                    QFontMetrics(small).elidedText(pr ? pr->name : L("Sin proyecto"), Qt::ElideRight,
                                                   textW - 11));

        if (hover && !s->running()) {
            // On hover the length makes room for ▶ (again now) and ⋯.
            for (const auto &[rect, kind] : {std::pair{playRect(rowRect), QString("play")},
                                             std::pair{moreRect(rowRect), QString("more")}}) {
                p->setPen(QPen(QColor(255, 255, 255, 30), 1));
                p->setBrush(QColor(255, 255, 255, 10));
                p->drawEllipse(QRectF(rect).adjusted(1.5, 1.5, -1.5, -1.5));
                paintIcon(kind, QColor(Theme::fg()), 14).paint(p, rect.adjusted(6, 6, -6, -6));
            }
            return;
        }
        const QRect right(rowRect.right() - rightW - 8, rowRect.top(), rightW, rowRect.height());
        p->setFont(mono(opt.font, m_theme->fs(12.5), QFont::Medium));
        p->setPen(s->running() ? (s->paused() ? QColor(Theme::muted()) : tint) : QColor(Theme::fg()));
        p->drawText(QRect(right.left(), rowRect.top() + 7, right.width(), 18),
                    Qt::AlignRight | Qt::AlignVCenter,
                    s->running() ? Track::clock(s->durationMs(now)) : Track::hm(s->durationMs(now)));
        p->setFont(mono(opt.font, 10));
        p->setPen(QColor(Theme::muted()));
        const QString end = s->running() ? (s->paused() ? L("pausa") : L("ahora")) : hhmm(s->endMs);
        p->drawText(QRect(right.left(), rowRect.top() + 26, right.width(), 15),
                    Qt::AlignRight | Qt::AlignVCenter, hhmm(s->startMs) + "–" + end);
    }

    const Theme *m_theme;
    std::function<const Project *(const QString &)> m_project;
};

/// The log as a list: double click or Enter edits, ▶ relaunches, ⋯ or a right
/// click opens the menu, Supr deletes (with "Deshacer" in the notices) and
/// Ctrl+D duplicates.
class LogView : public QListView {
public:
    std::function<void(Session *)> edit;
    std::function<void(Session *)> relaunch;
    std::function<void(Session *, const QPoint &)> menu;
    std::function<void(Session *)> remove;
    std::function<void(Session *)> duplicate;

    explicit LogView(LogModel *model) : m_model(model) {
        setModel(model);
        setObjectName("trackList");
        setFrameShape(QFrame::NoFrame);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        setSelectionMode(QAbstractItemView::SingleSelection);
        setEditTriggers(QAbstractItemView::NoEditTriggers);
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
        viewport()->setAttribute(Qt::WA_Hover);
        viewport()->setAutoFillBackground(false);
        setContextMenuPolicy(Qt::CustomContextMenu);
        connect(this, &QWidget::customContextMenuRequested, this, [this](const QPoint &at) {
            if (Session *s = sessionAt(at); s && menu) menu(s, viewport()->mapToGlobal(at));
        });
        connect(this, &QAbstractItemView::doubleClicked, this, [this](const QModelIndex &i) {
            if (Session *s = sessionAt(i); s && edit) edit(s);
        });
    }

    Session *current() const { return sessionAt(currentIndex()); }

protected:
    void mouseReleaseEvent(QMouseEvent *e) override {
        const QPoint at = e->position().toPoint();
        const QModelIndex i = indexAt(at);
        if (Session *s = sessionAt(i); s && e->button() == Qt::LeftButton && !s->running()) {
            const QRect row = visualRect(i).adjusted(0, 1, -2, -1);
            if (playRect(row).contains(at)) {
                if (relaunch) relaunch(s);
                return;
            }
            if (moreRect(row).contains(at)) {
                if (menu) menu(s, viewport()->mapToGlobal(moreRect(row).bottomLeft()));
                return;
            }
        }
        QListView::mouseReleaseEvent(e);
    }

    void keyPressEvent(QKeyEvent *e) override {
        Session *s = current();
        if (s && (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter)) {
            if (edit) edit(s);
            return;
        }
        if (s && e->key() == Qt::Key_Delete) {
            if (remove) remove(s);
            return;
        }
        if (s && e->key() == Qt::Key_D && (e->modifiers() & Qt::ControlModifier)) {
            if (duplicate) duplicate(s);
            return;
        }
        if (s && e->key() == Qt::Key_Menu) {
            if (menu) menu(s, viewport()->mapToGlobal(visualRect(currentIndex()).center()));
            return;
        }
        QListView::keyPressEvent(e);
    }

private:
    Session *sessionAt(const QModelIndex &i) const {
        return i.isValid() ? m_model->row(i.row()).session : nullptr;
    }
    Session *sessionAt(const QPoint &at) const { return sessionAt(indexAt(at)); }

    LogModel *m_model;
};

// ---------------------------------------------------------------------------
// Proyectos

class ProjectModel : public QAbstractListModel {
public:
    struct Row {
        Project *project = nullptr;   ///< Null: the "Archivados" heading.
        qint64 monthMs = 0;
    };
    using QAbstractListModel::QAbstractListModel;

    void setRows(const QList<Row> &rows) {
        beginResetModel();
        m_rows = rows;
        endResetModel();
    }
    const Row &row(int i) const { return m_rows.at(i); }
    int rowCount(const QModelIndex &parent = {}) const override {
        return parent.isValid() ? 0 : int(m_rows.size());
    }
    QVariant data(const QModelIndex &index, int role) const override {
        if (!index.isValid() || role != Qt::AccessibleTextRole) return {};
        const Row &r = m_rows.at(index.row());
        return r.project ? r.project->name : L("Archivados");
    }
    Qt::ItemFlags flags(const QModelIndex &index) const override {
        if (!index.isValid()) return Qt::NoItemFlags;
        return m_rows.at(index.row()).project ? Qt::ItemIsEnabled | Qt::ItemIsSelectable
                                              : Qt::ItemIsEnabled;
    }

private:
    QList<Row> m_rows;
};

class ProjectDelegate : public QStyledItemDelegate {
public:
    ProjectDelegate(const Theme *theme, QObject *parent) : QStyledItemDelegate(parent), m_theme(theme) {}

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &index) const override {
        const auto *model = static_cast<const ProjectModel *>(index.model());
        return {100, model->row(index.row()).project ? 52 : kDayH};
    }

    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &index) const override {
        const auto *model = static_cast<const ProjectModel *>(index.model());
        const ProjectModel::Row &r = model->row(index.row());
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        if (!r.project) {
            QFont f = mono(opt.font, 10);
            f.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
            p->setFont(f);
            p->setPen(kDim);
            p->drawText(opt.rect.adjusted(4, 10, -6, -4), Qt::AlignLeft | Qt::AlignVCenter,
                        L("Archivados").toUpper());
            p->restore();
            return;
        }
        const Project *pr = r.project;
        const QRect rowRect = opt.rect.adjusted(0, 2, -2, -2);
        const bool hover = opt.state & QStyle::State_MouseOver;
        const bool selected = opt.state & QStyle::State_Selected;
        p->setPen(QPen(selected ? withAlpha(m_theme->accent, 0.38) : QColor(255, 255, 255, 23), 1));
        p->setBrush(hover ? QColor(255, 255, 255, 18) : QColor(255, 255, 255, 8));
        p->drawRoundedRect(QRectF(rowRect).adjusted(0.5, 0.5, -0.5, -0.5), 10, 10);
        if (pr->archived) p->setOpacity(0.5);

        p->setPen(Qt::NoPen);
        p->setBrush(pr->tint());
        p->drawEllipse(QPointF(rowRect.left() + 17, rowRect.center().y()), 5, 5);

        const int left = rowRect.left() + 32;
        const QString hours = L("%1 h en %2").arg(Track::hm(r.monthMs),
                                                  Lang::locale().toString(QDate::currentDate(), "MMM")
                                                      .remove('.'));
        const QFont monoF = mono(opt.font, 10.5);
        const int rightW = QFontMetrics(monoF).horizontalAdvance(hours) + 4;
        const int textW = rowRect.width() - 32 - rightW - 14;

        const QFont title = sans(opt.font, m_theme->fs(12.5), QFont::DemiBold);
        p->setFont(title);
        p->setPen(QColor(Theme::fg()));
        p->drawText(QRect(left, rowRect.top() + 8, textW, 18), Qt::AlignLeft | Qt::AlignVCenter,
                    QFontMetrics(title).elidedText(pr->name, Qt::ElideRight, textW));
        QStringList sub;
        sub << (pr->client.isEmpty() ? L("Sin cliente") : pr->client);
        sub << (pr->hasRate() ? Track::rateLabel(pr->rate) : L("sin tarifa"));
        const QFont small = sans(opt.font, 10.5);
        p->setFont(small);
        p->setPen(QColor(Theme::muted()));
        p->drawText(QRect(left, rowRect.top() + 27, textW, 15), Qt::AlignLeft | Qt::AlignVCenter,
                    QFontMetrics(small).elidedText(sub.join(" · "), Qt::ElideRight, textW));
        p->setFont(monoF);
        p->drawText(QRect(rowRect.right() - rightW - 12, rowRect.top(), rightW, rowRect.height()),
                    Qt::AlignRight | Qt::AlignVCenter, hours);
        p->restore();
    }

private:
    const Theme *m_theme;
};

class ProjectView : public QListView {
public:
    std::function<void(Project *)> edit;

    explicit ProjectView(ProjectModel *model) : m_model(model) {
        setModel(model);
        setObjectName("trackList");
        setFrameShape(QFrame::NoFrame);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        setSelectionMode(QAbstractItemView::SingleSelection);
        setEditTriggers(QAbstractItemView::NoEditTriggers);
        setMouseTracking(true);
        setSpacing(0);
        viewport()->setAttribute(Qt::WA_Hover);
        viewport()->setAutoFillBackground(false);
        viewport()->setCursor(Qt::PointingHandCursor);
    }

protected:
    void mouseReleaseEvent(QMouseEvent *e) override {
        QListView::mouseReleaseEvent(e);
        const QModelIndex i = indexAt(e->position().toPoint());
        if (e->button() == Qt::LeftButton && i.isValid())
            if (Project *p = m_model->row(i.row()).project; p && edit) edit(p);
    }
    void keyPressEvent(QKeyEvent *e) override {
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
            const QModelIndex i = currentIndex();
            if (i.isValid())
                if (Project *p = m_model->row(i.row()).project; p && edit) edit(p);
            return;
        }
        QListView::keyPressEvent(e);
    }

private:
    ProjectModel *m_model;
};

// ---------------------------------------------------------------------------
// Resumen

/// Stacked bars, one per day, each project in its colour, over a dashed 8 h
/// guide. Painted: a chart made of widgets would be a hundred of them in a
/// month.
class DayBars : public QWidget {
public:
    struct Day {
        QString label;
        QDate date;
        QList<QPair<QColor, qint64>> parts;
        qint64 total = 0;
    };

    explicit DayBars(const Theme *theme) : m_theme(theme) {
        setMinimumHeight(140);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setMouseTracking(true);
    }

    void setDays(const QList<Day> &days) {
        m_days = days;
        update();
    }

protected:
    bool event(QEvent *e) override {
        if (e->type() == QEvent::ToolTip) {
            const auto *help = static_cast<QHelpEvent *>(e);
            const int i = columnAt(help->pos().x());
            if (i >= 0) {
                const Day &d = m_days.at(i);
                QString date = Lang::locale().toString(d.date, "ddd d MMM");
                QToolTip::showText(help->globalPos(), date.remove('.') + " · " + Track::words(d.total), this);
            } else {
                QToolTip::hideText();
            }
            return true;
        }
        return QWidget::event(e);
    }

    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        if (m_days.isEmpty()) return;
        const QRectF plot = plotRect();
        qint64 top = 8 * 3600000LL;
        for (const Day &d : m_days) top = qMax(top, d.total);
        const auto yAt = [&](qint64 ms) { return plot.bottom() - plot.height() * ms / qreal(top); };

        // The 8 h guide: a working day, for scale.
        const qreal guide = yAt(8 * 3600000LL);
        p.setPen(QPen(QColor(255, 255, 255, 40), 1, Qt::DashLine));
        p.drawLine(QPointF(plot.left(), guide), QPointF(plot.right(), guide));
        p.setFont(mono(font(), 9.5));
        p.setPen(kDim);
        p.drawText(QRectF(plot.right() + 4, guide - 7, 30, 14), Qt::AlignLeft | Qt::AlignVCenter,
                   L("%1 h").arg(8));

        const qreal colW = plot.width() / m_days.size();
        const qreal barW = qMin<qreal>(colW * 0.62, 26);
        const QDate today = QDate::currentDate();
        const bool sparse = m_days.size() > 10;   // a month: label every few days
        for (int i = 0; i < m_days.size(); ++i) {
            const Day &d = m_days.at(i);
            const qreal cx = plot.left() + colW * (i + 0.5);
            const QRectF slot(cx - barW / 2, plot.top(), barW, plot.height());
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 255, 255, 9));
            p.drawRoundedRect(slot, qMin(4.0, barW / 3), qMin(4.0, barW / 3));
            qreal y = plot.bottom();
            QPainterPath clip;
            clip.addRoundedRect(slot, qMin(4.0, barW / 3), qMin(4.0, barW / 3));
            p.save();
            p.setClipPath(clip);
            for (const auto &[color, ms] : d.parts) {
                const qreal h = plot.height() * ms / qreal(top);
                p.fillRect(QRectF(slot.left(), y - h, barW, h), color);
                y -= h;
                if (y > plot.top()) p.fillRect(QRectF(slot.left(), y - 0.5, barW, 1), QColor(19, 22, 27, 160));
            }
            p.restore();

            // A month labels every fifth day and today, leaving room around today's.
            if (sparse && d.date != today &&
                ((d.date.day() != 1 && d.date.day() % 5 != 0) || qAbs(d.date.daysTo(today)) < 2))
                continue;
            p.setFont(mono(font(), 9.5, d.date == today ? QFont::Bold : QFont::Normal));
            p.setPen(d.date == today ? QColor(Theme::fg()) : kDim);
            p.drawText(QRectF(cx - 14, plot.bottom() + 4, 28, 14), Qt::AlignHCenter | Qt::AlignTop,
                       d.label);
        }
    }

private:
    QRectF plotRect() const { return QRectF(rect()).adjusted(2, 6, -34, -20); }
    int columnAt(int x) const {
        const QRectF plot = plotRect();
        if (m_days.isEmpty() || x < plot.left() || x >= plot.right()) return -1;
        return qBound(0, int((x - plot.left()) / (plot.width() / m_days.size())), int(m_days.size()) - 1);
    }

    const Theme *m_theme;
    QList<Day> m_days;
};

/// Per project: its share as a bar, hours and, if it has a rate, the amount.
class Breakdown : public QWidget {
public:
    struct Row {
        QColor color;
        QString name;
        QString sub;
        QString hours;
        QString amount;
        qreal share = 0;
    };

    explicit Breakdown(const Theme *theme) : m_theme(theme) {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    void setRows(const QList<Row> &rows) {
        m_rows = rows;
        setFixedHeight(rows.isEmpty() ? 44 : int(rows.size()) * kRow);
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        if (m_rows.isEmpty()) {
            p.setFont(sans(font(), 11.5));
            p.setPen(QColor(Theme::muted()));
            p.drawText(rect(), Qt::AlignCenter, L("Nada registrado en este periodo"));
            return;
        }
        for (int i = 0; i < m_rows.size(); ++i) {
            const Row &r = m_rows.at(i);
            const QRect row(0, i * kRow, width(), kRow);
            p.setPen(Qt::NoPen);
            p.setBrush(r.color);
            p.drawEllipse(QPointF(row.left() + 8, row.top() + 15), 4, 4);

            const QFont monoF = mono(font(), 11.5, QFont::Medium);
            const QFontMetrics mfm(monoF);
            const int rightW = qMax(mfm.horizontalAdvance(r.hours), mfm.horizontalAdvance(r.amount)) + 4;
            const int textW = row.width() - 20 - rightW - 8;
            const QFont title = sans(font(), m_theme->fs(12), QFont::DemiBold);
            p.setFont(title);
            p.setPen(QColor(Theme::fg()));
            p.drawText(QRect(row.left() + 20, row.top() + 6, textW, 18), Qt::AlignLeft | Qt::AlignVCenter,
                       QFontMetrics(title).elidedText(r.name, Qt::ElideRight, textW));
            const QFont small = sans(font(), 10.5);
            p.setFont(small);
            p.setPen(QColor(Theme::muted()));
            p.drawText(QRect(row.left() + 20, row.top() + 24, textW, 14), Qt::AlignLeft | Qt::AlignVCenter,
                       QFontMetrics(small).elidedText(r.sub, Qt::ElideRight, textW));
            p.setFont(monoF);
            p.setPen(QColor(Theme::fg()));
            p.drawText(QRect(row.right() - rightW, row.top() + 6, rightW, 18),
                       Qt::AlignRight | Qt::AlignVCenter, r.hours);
            p.setFont(mono(font(), 10.5));
            p.setPen(QColor(Theme::muted()));
            p.drawText(QRect(row.right() - rightW, row.top() + 24, rightW, 14),
                       Qt::AlignRight | Qt::AlignVCenter, r.amount);

            const QRectF track(row.left() + 20, row.top() + 42, row.width() - 20, 3);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 255, 255, 14));
            p.drawRoundedRect(track, 1.5, 1.5);
            p.setBrush(r.color);
            p.drawRoundedRect(QRectF(track.left(), track.top(), track.width() * r.share, 3), 1.5, 1.5);
        }
    }

private:
    static constexpr int kRow = 52;
    const Theme *m_theme;
    QList<Row> m_rows;
};

}  // namespace track

using namespace track;

// ---------------------------------------------------------------------------

TrackingView::TrackingView(const Theme &theme, QWidget *parent) : QWidget(parent), m_theme(theme) {
    setObjectName("tracking");

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(buildTabs());

    auto *body = new QWidget;
    auto *col = new QVBoxLayout(body);
    col->setContentsMargins(9, 8, 9, 8);
    col->setSpacing(8);

    m_clock = new ClockCard(&m_theme);
    m_clock->submit = [this](const QString &task) {
        const QString project = m_pendingPicked ? m_pending : lastProjectFor(task);
        emit startRequested(task, this->project(project) ? project : QString());
        m_pendingPicked = false;
    };
    m_clock->pause = [this] { emit pauseToggled(); };
    m_clock->stop = [this] { emit stopRequested(); };
    m_clock->pickProject = [this](QWidget *anchor) {
        const bool running = m_sessions && std::any_of(m_sessions->begin(), m_sessions->end(),
                                                       [](const Session *s) { return s->running(); });
        openProjectPicker(anchor, running);
    };
    // The project remembers the task: typing one already logged preselects the
    // project it went with last, unless one was picked by hand.
    m_clock->typed = [this](const QString &text) {
        if (m_pendingPicked) return;
        m_pending = lastProjectFor(text.trimmed());
        m_clock->setPending(project(m_pending));
    };
    m_completer = new QCompleter(new QStringListModel(this), this);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setCompletionMode(QCompleter::InlineCompletion);
    m_clock->field()->setCompleter(m_completer);
    col->addWidget(m_clock);

    m_recents = new QWidget;
    auto *rcol = new QVBoxLayout(m_recents);
    rcol->setContentsMargins(0, 0, 0, 0);
    rcol->setSpacing(4);
    rcol->addWidget(section(QString()));
    auto *chips = new QWidget;
    m_recentGrid = new QGridLayout(chips);
    m_recentGrid->setContentsMargins(0, 0, 0, 0);
    m_recentGrid->setSpacing(6);
    rcol->addWidget(chips);
    col->addWidget(m_recents);

    // Registro and Resumen in one row: one or the other while narrow, both
    // side by side when wide.
    m_split = new QWidget;
    auto *split = new QHBoxLayout(m_split);
    split->setContentsMargins(0, 0, 0, 0);
    split->setSpacing(14);

    m_logStack = new QStackedWidget;
    m_logStack->addWidget(buildEmpty());
    m_logModel = new LogModel(this);
    m_logView = new LogView(m_logModel);
    m_logView->setItemDelegate(new LogDelegate(
        &m_theme, [this](const QString &id) { return project(id); }, m_logView));
    m_logView->edit = [this](Session *s) { emit sessionEditRequested(s); };
    m_logView->relaunch = [this](Session *s) { emit relaunchRequested(s); };
    m_logView->menu = [this](Session *s, const QPoint &at) { openSessionMenu(s, at); };
    m_logView->remove = [this](Session *s) { emit sessionDeleteRequested(s); };
    m_logView->duplicate = [this](Session *s) { emit sessionDuplicateRequested(s); };
    m_logStack->addWidget(m_logView);
    split->addWidget(m_logStack, 1);

    m_summary = new QScrollArea;
    m_summary->setWidget(buildSummary());
    m_summary->setWidgetResizable(true);
    m_summary->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_summary->viewport()->setAutoFillBackground(false);
    m_summary->viewport()->setObjectName("scrollViewport");
    split->addWidget(m_summary, 1);
    col->addWidget(m_split, 1);

    m_projectsPane = buildProjects();
    col->addWidget(m_projectsPane, 1);

    root->addWidget(body, 1);
    applyLayout();
}

QWidget *TrackingView::buildTabs() {
    auto *row = new QFrame;
    row->setObjectName("toolsRow");
    row->setFixedHeight(36);
    auto *l = new QHBoxLayout(row);
    l->setContentsMargins(9, 0, 9, 0);
    l->setSpacing(4);
    for (Tab t : {Log, Summary, Projects}) {
        auto *b = new QToolButton;
        b->setObjectName("setTab");
        b->setCursor(Qt::PointingHandCursor);
        b->setFocusPolicy(Qt::TabFocus);
        connect(b, &QToolButton::clicked, this, [this, t] { setTab(t); });
        l->addWidget(b);
        m_tabButtons << b;
    }
    l->addStretch();
    return row;
}

/// 01: the stopwatch above is already the invitation; this only points at it.
/// Starting needs no project; creating one is the second option.
QWidget *TrackingView::buildEmpty() {
    auto *box = new QWidget;
    auto *col = new QVBoxLayout(box);
    col->setContentsMargins(10, 18, 10, 10);
    col->setSpacing(8);

    auto *icon = new QLabel;
    icon->setAlignment(Qt::AlignCenter);
    icon->setPixmap(paintIcon("bars", QColor(Theme::muted()), 30).pixmap(30, 30));
    col->addWidget(icon);
    m_emptyTitle = new QLabel;
    m_emptyTitle->setObjectName("trackEmptyTitle");
    m_emptyTitle->setAlignment(Qt::AlignCenter);
    m_emptyTitle->setWordWrap(true);
    col->addWidget(m_emptyTitle);
    m_emptyText = new QLabel;
    m_emptyText->setObjectName("popupText");
    m_emptyText->setAlignment(Qt::AlignCenter);
    m_emptyText->setWordWrap(true);
    col->addWidget(m_emptyText);
    col->addSpacing(6);

    m_emptyProject = new QToolButton;
    m_emptyProject->setObjectName("bdayGhost");
    m_emptyProject->setCursor(Qt::PointingHandCursor);
    m_emptyProject->setFocusPolicy(Qt::TabFocus);
    connect(m_emptyProject, &QToolButton::clicked, this, [this] { emit projectEditRequested(nullptr); });
    auto *row = new QHBoxLayout;
    row->addStretch();
    row->addWidget(m_emptyProject);
    row->addStretch();
    col->addLayout(row);
    m_emptyHint = new QLabel;
    m_emptyHint->setObjectName("setFieldLabel");
    m_emptyHint->setAlignment(Qt::AlignCenter);
    m_emptyHint->setWordWrap(true);
    col->addWidget(m_emptyHint);
    col->addStretch();

    m_emptyPast = new QToolButton;
    m_emptyPast->setObjectName("noticeAction");
    m_emptyPast->setCursor(Qt::PointingHandCursor);
    m_emptyPast->setFocusPolicy(Qt::TabFocus);
    connect(m_emptyPast, &QToolButton::clicked, this, [this] { emit sessionEditRequested(nullptr); });
    auto *pastRow = new QHBoxLayout;
    pastRow->addStretch();
    pastRow->addWidget(m_emptyPast);
    pastRow->addStretch();
    col->addLayout(pastRow);
    return box;
}

QWidget *TrackingView::buildSummary() {
    auto *host = new QWidget;
    host->setObjectName("listHost");
    auto *col = new QVBoxLayout(host);
    col->setContentsMargins(0, 0, 4, 4);
    col->setSpacing(8);

    auto *controls = new QHBoxLayout;
    controls->setSpacing(4);
    m_weekBtn = new QToolButton;
    m_monthBtn = new QToolButton;
    for (QToolButton *b : {m_weekBtn, m_monthBtn}) {
        b->setObjectName("bdayTab");
        b->setCursor(Qt::PointingHandCursor);
        b->setFocusPolicy(Qt::TabFocus);
        controls->addWidget(b);
    }
    connect(m_weekBtn, &QToolButton::clicked, this, [this] {
        m_month = false;
        refreshSummary();
    });
    connect(m_monthBtn, &QToolButton::clicked, this, [this] {
        m_month = true;
        refreshSummary();
    });
    controls->addStretch();
    auto *prev = iconButton("chevronLeft", L("Anterior"), 24);
    auto *next = iconButton("chevronRight", L("Siguiente"), 24);
    m_periodLabel = new QLabel;
    m_periodLabel->setObjectName("trackPeriod");
    connect(prev, &QToolButton::clicked, this, [this] {
        m_anchor = m_month ? m_anchor.addMonths(-1) : m_anchor.addDays(-7);
        refreshSummary();
    });
    connect(next, &QToolButton::clicked, this, [this] {
        m_anchor = m_month ? m_anchor.addMonths(1) : m_anchor.addDays(7);
        refreshSummary();
    });
    controls->addWidget(prev);
    controls->addWidget(m_periodLabel);
    controls->addWidget(next);
    m_csvBtn = iconButton("download", QString(), 24);
    m_csvBtn->setObjectName("heroIcon");
    connect(m_csvBtn, &QToolButton::clicked, this, [this] { emit exportRequested(periodFrom(), periodTo()); });
    controls->addSpacing(4);
    controls->addWidget(m_csvBtn);
    col->addLayout(controls);

    auto tile = [](QLabel **caption, QLabel **value, QLabel **sub) {
        auto *f = new QFrame;
        f->setObjectName("timerHero");
        auto *l = new QVBoxLayout(f);
        l->setContentsMargins(11, 9, 11, 9);
        l->setSpacing(2);
        *caption = new QLabel;
        (*caption)->setObjectName("pageSection");
        (*caption)->setStyleSheet("padding: 0;");
        *value = new QLabel;
        (*value)->setObjectName("trackTileValue");
        *sub = new QLabel;
        (*sub)->setObjectName("timerSub");
        (*sub)->setWordWrap(true);
        l->addWidget(*caption);
        l->addWidget(*value);
        l->addWidget(*sub);
        return f;
    };
    auto *tiles = new QHBoxLayout;
    tiles->setSpacing(8);
    tiles->addWidget(tile(&m_hoursCaption, &m_hoursValue, &m_hoursSub), 1);
    tiles->addWidget(tile(&m_moneyCaption, &m_moneyValue, &m_moneySub), 1);
    col->addLayout(tiles);

    m_daySection = section(QString());
    col->addWidget(m_daySection);
    m_bars = new DayBars(&m_theme);
    col->addWidget(m_bars);
    m_projectSection = section(QString());
    col->addWidget(m_projectSection);
    m_breakdown = new Breakdown(&m_theme);
    col->addWidget(m_breakdown);
    col->addStretch();
    return host;
}

QWidget *TrackingView::buildProjects() {
    auto *pane = new QWidget;
    auto *col = new QVBoxLayout(pane);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(8);
    m_projectModel = new ProjectModel(this);
    m_projectView = new ProjectView(m_projectModel);
    m_projectView->setItemDelegate(new ProjectDelegate(&m_theme, m_projectView));
    m_projectView->edit = [this](Project *p) { emit projectEditRequested(p); };
    col->addWidget(m_projectView, 1);
    m_newProject = new QToolButton;
    m_newProject->setObjectName("bdayGhost");
    m_newProject->setIcon(paintIcon("plus", QColor(Theme::muted()), 14));
    m_newProject->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_newProject->setCursor(Qt::PointingHandCursor);
    m_newProject->setFocusPolicy(Qt::TabFocus);
    m_newProject->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_newProject, &QToolButton::clicked, this, [this] { emit projectEditRequested(nullptr); });
    col->addWidget(m_newProject);
    return pane;
}

void TrackingView::setSources(const QList<Session *> *sessions, const QList<Project *> *projects) {
    m_sessions = sessions;
    m_projects = projects;
    refresh();
}

void TrackingView::setTheme(const Theme &theme) {
    m_theme = theme;
    m_clock->restyle();
    refresh();
}

void TrackingView::retranslate() {
    m_clock->retranslate();
    refresh();
}

const Project *TrackingView::project(const QString &id) const {
    if (!m_projects || id.isEmpty()) return nullptr;
    for (const Project *p : *m_projects)
        if (p->id == id) return p;
    return nullptr;
}

QString TrackingView::lastProjectFor(const QString &task) const {
    if (!m_sessions || task.isEmpty()) return {};
    const Session *last = nullptr;
    for (const Session *s : *m_sessions)
        if (s->task.compare(task, Qt::CaseInsensitive) == 0 && (!last || s->startMs > last->startMs))
            last = s;
    const Project *p = last ? project(last->project) : nullptr;
    return p && !p->archived ? p->id : QString();
}

void TrackingView::setTab(Tab tab) {
    m_tab = tab;
    applyLayout();
    if (tab == Summary) refreshSummary();
}

void TrackingView::focusTask() {
    if (m_tab == Projects) setTab(Log);
    m_clock->field()->setFocus(Qt::ShortcutFocusReason);
}

void TrackingView::setPendingProject(const QString &id) {
    m_pending = id;
    m_pendingPicked = true;
    m_clock->setPending(project(id));
}

QDate TrackingView::periodFrom() const {
    return m_month ? QDate(m_anchor.year(), m_anchor.month(), 1) : Track::weekStart(m_anchor);
}

QDate TrackingView::periodTo() const {
    return m_month ? periodFrom().addMonths(1).addDays(-1) : periodFrom().addDays(6);
}

void TrackingView::resizeEvent(QResizeEvent *e) {
    QWidget::resizeEvent(e);
    const bool wide = width() >= kWideFrom;
    if (wide != m_wide) {
        m_wide = wide;
        applyLayout();
    }
}

/// Which panes show: narrow, the tab's; wide, Registro and Resumen together.
void TrackingView::applyLayout() {
    const bool projects = m_tab == Projects;
    m_projectsPane->setVisible(projects);
    m_split->setVisible(!projects);
    m_logStack->setVisible(!projects && (m_wide || m_tab == Log));
    m_summary->setVisible(!projects && (m_wide || m_tab == Summary));
    m_clock->setVisible(!projects);
    m_clock->setCompact(!m_wide && m_tab == Summary);
    refreshRecents();
    refreshTabs();
}

void TrackingView::refreshTabs() {
    const QList<const char *> names{"Registro", "Resumen", "Proyectos"};
    for (int i = 0; i < m_tabButtons.size(); ++i) {
        QToolButton *b = m_tabButtons.at(i);
        b->setText(L(names.at(i)));
        // Wide, Registro and Resumen are one view: both tabs are lit.
        const bool on = i == m_tab || (m_wide && m_tab != Projects && i != Projects);
        if (b->property("chosen").toBool() != on) {
            b->setProperty("chosen", on);
            repolish(b);
        }
    }
}

void TrackingView::refresh() {
    if (!m_sessions || !m_projects) return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    // Registro: newest day first, today's line under its heading.
    QList<Session *> sorted = *m_sessions;
    std::sort(sorted.begin(), sorted.end(),
              [](const Session *a, const Session *b) { return a->startMs > b->startMs; });
    QList<LogModel::Row> rows;
    Session *selected = m_logView->current();
    for (int i = 0; i < sorted.size();) {
        const QDate day = sorted.at(i)->day();
        QList<Session *> group;
        while (i < sorted.size() && sorted.at(i)->day() == day) group << sorted.at(i++);
        rows << LogModel::Row{LogModel::Row::Day, day, nullptr, group};
        if (day == QDate::currentDate()) rows << LogModel::Row{LogModel::Row::Line, day, nullptr, group};
        for (Session *s : group) rows << LogModel::Row{LogModel::Row::Item, day, s, {}};
    }
    m_logModel->setRows(rows);
    if (const int i = m_logModel->indexOf(selected); i >= 0)
        m_logView->setCurrentIndex(m_logModel->index(i));
    m_logStack->setCurrentIndex(sorted.isEmpty() ? 0 : 1);

    // Proyectos: active ones by hours this month, then the archived.
    const QDate month(QDate::currentDate().year(), QDate::currentDate().month(), 1);
    QHash<QString, qint64> monthMs;
    for (const Session *s : *m_sessions)
        if (s->day() >= month) monthMs[s->project] += s->durationMs(now);
    QList<ProjectModel::Row> projectRows, archived;
    for (Project *p : *m_projects)
        (p->archived ? archived : projectRows) << ProjectModel::Row{p, monthMs.value(p->id)};
    const auto byHours = [](const ProjectModel::Row &a, const ProjectModel::Row &b) {
        return a.monthMs != b.monthMs ? a.monthMs > b.monthMs
                                      : a.project->name.localeAwareCompare(b.project->name) < 0;
    };
    std::sort(projectRows.begin(), projectRows.end(), byHours);
    std::sort(archived.begin(), archived.end(), byHours);
    if (!archived.isEmpty()) projectRows << ProjectModel::Row{nullptr, 0} << archived;
    m_projectModel->setRows(projectRows);

    // Every task logged, newest first, for the field's completion.
    QStringList tasks;
    for (const Session *s : sorted)
        if (!s->task.isEmpty() && !tasks.contains(s->task, Qt::CaseInsensitive)) tasks << s->task;
    static_cast<QStringListModel *>(m_completer->model())->setStringList(tasks);

    if (!m_pending.isEmpty() && !project(m_pending)) {
        m_pending.clear();
        m_pendingPicked = false;
    }
    m_clock->setPending(project(m_pending));

    m_emptyTitle->setText(L("Escribe en qué estás y pulsa Intro"));
    m_emptyText->setText(L("Cada vez que detienes el reloj queda una sesión. Aquí verás las de "
                           "hoy, y en Resumen, cuánto suma cada proyecto."));
    m_emptyProject->setText(L("Crear un proyecto"));
    m_emptyHint->setText(L("Para separar horas por cliente o calcular lo que facturas."));
    m_emptyPast->setText(L("¿Se te olvidó arrancarlo? Añadir una sesión pasada"));
    m_newProject->setText(L("Nuevo proyecto"));
    if (auto *label = m_recents->findChild<QLabel *>("pageSection"))
        label->setText(L("Recientes").toUpper());
    m_weekBtn->setText(L("Semana"));
    m_monthBtn->setText(L("Mes"));
    m_csvBtn->setToolTip(L("Exportar a CSV"));

    refreshTabs();
    refreshRecents();
    refreshSummary();
    tick();
}

/// Up to four: the last tasks logged, each with its project, without the one
/// that is running. One click stops what runs and starts it.
void TrackingView::refreshRecents() {
    while (QLayoutItem *item = m_recentGrid->takeAt(0)) {
        if (QWidget *w = item->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete item;
    }
    if (!m_sessions) return;
    QList<const Session *> sorted(m_sessions->begin(), m_sessions->end());
    std::sort(sorted.begin(), sorted.end(),
              [](const Session *a, const Session *b) { return a->startMs > b->startMs; });
    const Session *running = nullptr;
    for (const Session *s : sorted)
        if (s->running()) running = s;
    QList<const Session *> picks;
    for (const Session *s : sorted) {
        if (s->task.isEmpty()) continue;
        const auto same = [s](const Session *o) {
            return o->task.compare(s->task, Qt::CaseInsensitive) == 0 && o->project == s->project;
        };
        if ((running && same(running)) || std::any_of(picks.begin(), picks.end(), same)) continue;
        picks << s;
        if (picks.size() == 4) break;
    }
    // Hidden on Proyectos, on the compact Resumen, and with nothing to offer.
    const bool show = !picks.isEmpty() && m_tab != Projects && (m_wide || m_tab == Log);
    m_recents->setVisible(show);
    if (!show) return;

    const int cols = m_wide ? 4 : 2;
    for (int i = 0; i < picks.size(); ++i) {
        const Session *s = picks.at(i);
        const Project *p = project(s->project);
        auto *chip = new QToolButton;
        chip->setObjectName("trackRecent");
        chip->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        chip->setIcon(paintIcon("glyph-dot", p ? p->tint() : kNoProject, 12));
        chip->setIconSize(QSize(12, 12));
        chip->setCursor(Qt::PointingHandCursor);
        chip->setFocusPolicy(Qt::TabFocus);
        chip->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        chip->setMinimumWidth(40);   // see *Card widths*
        chip->setToolTip(p ? L("%1 · %2").arg(s->task, p->name) : s->task);
        const int w = qMax(60, (width() - 18 - (cols - 1) * 6) / cols - 30);
        chip->setText(QFontMetrics(chip->font()).elidedText(s->task, Qt::ElideRight, w));
        const QString task = s->task, proj = p ? p->id : QString();
        connect(chip, &QToolButton::clicked, this, [this, task, proj] { emit startRequested(task, proj); });
        m_recentGrid->addWidget(chip, i / cols, i % cols);
    }
}

void TrackingView::tick() {
    if (!m_sessions) return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const Session *running = nullptr;
    qint64 todayMs = 0;
    const QDate today = QDate::currentDate();
    for (const Session *s : *m_sessions) {
        if (s->running() && (!running || s->startMs > running->startMs)) running = s;
        if (s->day() == today) todayMs += s->durationMs(now);
    }
    m_clock->present(running, running ? project(running->project) : nullptr, todayMs);
    m_logView->viewport()->update();
    if (running && m_summary->isVisible() && running->day() >= periodFrom() && running->day() <= periodTo())
        refreshSummary();
}

void TrackingView::refreshSummary() {
    if (!m_sessions || !m_weekBtn) return;
    m_weekBtn->setProperty("chosen", !m_month);
    m_monthBtn->setProperty("chosen", m_month);
    repolish(m_weekBtn);
    repolish(m_monthBtn);
    const QDate from = periodFrom(), to = periodTo();
    m_periodLabel->setText(periodName(from, to, m_month));

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QList<DayBars::Day> days;
    for (QDate d = from; d <= to; d = d.addDays(1))
        days << DayBars::Day{m_month ? QString::number(d.day()) : Lang::weekdayInitial(d.dayOfWeek() - 1),
                             d, {}, 0};

    struct Sum {
        qint64 ms = 0;
        double amount = 0;
        bool rated = false;
    };
    QHash<QString, Sum> perProject;
    QStringList order;
    qint64 total = 0, ratedMs = 0;
    double money = 0;
    for (const Session *s : Track::inRange(*m_sessions, from, to)) {
        const qint64 ms = s->durationMs(now);
        total += ms;
        if (s->hasRate()) {
            ratedMs += ms;
            money += s->amount(now);
        }
        if (!perProject.contains(s->project)) order << s->project;
        Sum &sum = perProject[s->project];
        sum.ms += ms;
        sum.amount += s->amount(now);
        sum.rated |= s->hasRate();
        DayBars::Day &day = days[int(from.daysTo(s->day()))];
        day.total += ms;
        const Project *p = project(s->project);
        const QColor c = p ? p->tint() : kNoProject;
        bool merged = false;
        for (auto &part : day.parts)
            if (part.first == c) {
                part.second += ms;
                merged = true;
            }
        if (!merged) day.parts << qMakePair(c, ms);
    }
    m_bars->setDays(days);

    int worked = 0;
    for (const DayBars::Day &d : days) worked += d.total > 0 ? 1 : 0;
    m_hoursCaption->setText(L("Horas").toUpper());
    m_hoursValue->setText(Track::words(total));
    m_hoursSub->setText(worked == 0 ? L("Nada registrado")
                        : (worked == 1 ? L("1 día") : L("%1 días").arg(worked)) + " · " +
                              L("%1 h de media").arg(Track::hm(total / worked)));
    m_moneyCaption->setText(L("Importe estimado").toUpper());
    m_moneyValue->setText(ratedMs > 0 ? Track::money(money) : QStringLiteral("—"));
    // The amount only adds what has a rate, and says how much that is.
    m_moneySub->setText(ratedMs > 0 ? L("%1 h con tarifa").arg(Track::hm(ratedMs))
                                    : L("Ningún proyecto con tarifa"));
    m_daySection->setText(L("Por día").toUpper());
    m_projectSection->setText(L("Por proyecto").toUpper());

    std::sort(order.begin(), order.end(), [&](const QString &a, const QString &b) {
        return perProject.value(a).ms > perProject.value(b).ms;
    });
    QList<Breakdown::Row> rows;
    for (const QString &id : order) {
        const Sum sum = perProject.value(id);
        const Project *p = project(id);
        QStringList sub;
        if (p) {
            sub << (p->client.isEmpty() ? L("Sin cliente") : p->client);
            sub << (p->hasRate() ? Track::rateLabel(p->rate) : L("sin tarifa"));
        }
        rows << Breakdown::Row{p ? p->tint() : kNoProject, p ? p->name : L("Sin proyecto"),
                               sub.join(" · "), Track::hm(sum.ms) + " h",
                               sum.rated ? Track::money(sum.amount) : QString(),
                               total > 0 ? qreal(sum.ms) / total : 0};
    }
    m_breakdown->setRows(rows);
}

void TrackingView::openProjectPicker(QWidget *anchor, bool forRunning) {
    auto *menu = new Popup(m_theme, this);
    const auto pick = [this, forRunning](const QString &id) {
        if (forRunning) {
            emit runningProjectPicked(id);
            return;
        }
        m_pending = id;
        m_pendingPicked = true;
        m_clock->setPending(project(id));
    };
    menu->addItem("minus", L("Sin proyecto"), QString(), [pick] { pick(QString()); });
    if (m_projects)
        for (const Project *p : *m_projects) {
            if (p->archived) continue;
            QStringList sub;
            if (!p->client.isEmpty()) sub << p->client;
            if (p->hasRate()) sub << Track::rateLabel(p->rate);
            const QString id = p->id;
            menu->addColorItem(p->tint(), p->name, sub.join(" · "), [pick, id] { pick(id); });
        }
    menu->addSeparator();
    menu->addItem("plus", L("Nuevo proyecto…"), QString(), [this] { emit projectEditRequested(nullptr); });
    menu->showBelow(anchor);
}

/// ⋯ or a right click on a session.
void TrackingView::openSessionMenu(Session *s, const QPoint &globalPos) {
    auto *menu = new Popup(m_theme, this);
    menu->addHeader(s->task.isEmpty() ? L("Sin tarea") : s->task);
    menu->addShortcutItem("pencil", L("Editar…"), L("Intro"), [this, s] { emit sessionEditRequested(s); });
    if (!s->running())
        menu->addShortcutItem("play", L("Relanzar ahora"), QString(), [this, s] { emit relaunchRequested(s); });
    menu->addShortcutItem("copy", L("Duplicar"), "Ctrl+D", [this, s] { emit sessionDuplicateRequested(s); });
    menu->addShortcutItem("folder", L("Mover a proyecto…"), QString(), [this, s, globalPos] {
        // After the first menu has closed: a popup cannot open from inside another's action.
        QTimer::singleShot(0, this, [this, s, globalPos] {
            if (!m_sessions || !m_sessions->contains(s)) return;
            auto *to = new Popup(m_theme, this);
            to->addHeader(L("Mover a"));
            to->addItem("minus", L("Sin proyecto"), QString(),
                        [this, s] { emit sessionMoveRequested(s, QString()); });
            for (const Project *p : *m_projects) {
                if (p->archived || p->id == s->project) continue;
                const QString id = p->id;
                to->addColorItem(p->tint(), p->name, p->client,
                                 [this, s, id] { emit sessionMoveRequested(s, id); });
            }
            to->showAt(globalPos);
        });
    });
    menu->addSeparator();
    menu->addShortcutItem("trash", L("Borrar"), L("Supr"), [this, s] { emit sessionDeleteRequested(s); });
    menu->showAt(globalPos);
}
