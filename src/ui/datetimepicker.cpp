#include "ui/datetimepicker.hpp"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include "core/lang.hpp"
#include "ui/keynav.hpp"

namespace {

constexpr int kCell = 26;        ///< Day cell, square.
constexpr int kGridHead = 22;    ///< Month name and arrows.
constexpr int kDowRow = 18;      ///< Weekday initials.
constexpr int kTimeRow = 24;
constexpr int kTimeWidth = 70;
constexpr int kTimeHeight = 6 * kCell + kDowRow + 4;   ///< Lines up with the grid.

QColor withAlpha(QColor c, int alpha) {
    c.setAlpha(alpha);
    return c;
}

QFont monoFont(const QFont &base, qreal px, bool medium = false) {
    QFont f = base;
    f.setFamily("IBM Plex Mono");
    f.setStyleHint(QFont::Monospace);
    f.setPixelSize(qRound(px));
    f.setWeight(medium ? QFont::Medium : QFont::Normal);
    return f;
}

QDate gridStart(const QDate &month) {
    const QDate first(month.year(), month.month(), 1);
    return first.addDays(1 - first.dayOfWeek());
}

/// A time snapped down to the step, so it is one of the rows of the list.
QTime snapped(const QTime &t, int step) {
    const int m = t.hour() * 60 + t.minute();
    const int s = m - m % step;
    return QTime(s / 60, s % 60);
}

QToolButton *button(const QString &object, const QString &text) {
    auto *b = new QToolButton;
    b->setObjectName(object);
    b->setText(text);
    b->setCursor(Qt::PointingHandCursor);
    b->setFocusPolicy(Qt::TabFocus);
    return b;
}

} // namespace

namespace picker {

/// The month grid. Painted, like the planner's MiniMonth: the cells have no
/// state worth a widget each. Days before @c minDate cannot be picked, which
/// is what "no reminders in the past" looks like here.
class DayGrid : public QWidget {
public:
    std::function<void(const QDate &)> onPick;
    std::function<void()> onEnter;
    QDate minDate;

    explicit DayGrid(const Theme &theme) : m_theme(theme) {
        setFixedSize(7 * kCell, kGridHead + 4 + kDowRow + 6 * kCell);
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
    }

    void setSelected(const QDate &d) {
        m_selected = d;
        m_shown = QDate(d.year(), d.month(), 1);
        update();
    }

protected:
    QRect cellRect(int i) const {
        return QRect((i % 7) * kCell, kGridHead + 4 + kDowRow + (i / 7) * kCell, kCell, kCell);
    }
    QRect arrowRect(bool next) const {
        return next ? QRect(width() - 22, 0, 22, 22) : QRect(width() - 46, 0, 22, 22);
    }
    bool pickable(const QDate &d) const { return !minDate.isValid() || d >= minDate; }

    void shiftMonth(int months) {
        m_shown = m_shown.addMonths(months);
        update();
    }

    /// Moves the selection by @p days, never below the minimum: the arrows stop
    /// at today instead of jumping over it.
    void moveBy(int days) {
        QDate d = m_selected.addDays(days);
        if (!pickable(d)) d = minDate;
        if (onPick) onPick(d);
    }

    void mouseReleaseEvent(QMouseEvent *e) override {
        const QPoint at = e->position().toPoint();
        if (arrowRect(false).contains(at)) { shiftMonth(-1); return; }
        if (arrowRect(true).contains(at)) { shiftMonth(1); return; }
        const QDate start = gridStart(m_shown);
        for (int i = 0; i < 42; ++i) {
            const QDate d = start.addDays(i);
            if (cellRect(i).contains(at) && pickable(d) && onPick) onPick(d);
        }
    }

    void mouseMoveEvent(QMouseEvent *e) override {
        const QPoint at = e->position().toPoint();
        bool hot = arrowRect(false).contains(at) || arrowRect(true).contains(at);
        const QDate start = gridStart(m_shown);
        for (int i = 0; i < 42 && !hot; ++i)
            hot = cellRect(i).contains(at) && pickable(start.addDays(i));
        setCursor(hot ? Qt::PointingHandCursor : Qt::ArrowCursor);
    }

    void keyPressEvent(QKeyEvent *e) override {
        switch (e->key()) {
            case Qt::Key_Left:  moveBy(-1); return;
            case Qt::Key_Right: moveBy(1); return;
            case Qt::Key_Up:    moveBy(-7); return;
            case Qt::Key_Down:  moveBy(7); return;
            case Qt::Key_PageUp: {
                QDate d = m_selected.addMonths(-1);
                if (onPick) onPick(pickable(d) ? d : minDate);
                return;
            }
            case Qt::Key_PageDown:
                if (onPick) onPick(m_selected.addMonths(1));
                return;
            case Qt::Key_Home:
                if (onPick) onPick(minDate.isValid() ? minDate : QDate::currentDate());
                return;
            case Qt::Key_Return:
            case Qt::Key_Enter:
                if (onEnter) onEnter();
                return;
            default:
                QWidget::keyPressEvent(e);
        }
    }

    void focusInEvent(QFocusEvent *e) override { QWidget::focusInEvent(e); update(); }
    void focusOutEvent(QFocusEvent *e) override { QWidget::focusOutEvent(e); update(); }

    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QColor fg(Theme::fg());
        const QColor muted(Theme::muted());
        const QDate today = QDate::currentDate();

        QFont head = font();
        head.setPixelSize(12);
        head.setWeight(QFont::DemiBold);
        p.setFont(head);
        p.setPen(fg);
        QString label = Lang::locale().toString(m_shown, "MMMM yyyy");
        if (!label.isEmpty()) label[0] = label[0].toUpper();
        p.drawText(QRect(2, 0, width() - 50, kGridHead), Qt::AlignVCenter | Qt::AlignLeft, label);
        paintIcon("chevronLeft", muted, 12).paint(&p, arrowRect(false).adjusted(5, 5, -5, -5));
        paintIcon("chevronRight", muted, 12).paint(&p, arrowRect(true).adjusted(5, 5, -5, -5));

        p.setFont(monoFont(font(), 10));
        p.setPen(withAlpha(muted, 170));
        for (int c = 0; c < 7; ++c)
            p.drawText(QRect(c * kCell, kGridHead + 4, kCell, kDowRow), Qt::AlignCenter,
                       Lang::weekdayInitial(c));

        const QDate start = gridStart(m_shown);
        for (int i = 0; i < 42; ++i) {
            const QDate d = start.addDays(i);
            // Days of the neighbouring months are left blank, as in the design: a
            // grey 30 next to a white 1 reads as two different months anyway.
            if (d.month() != m_shown.month()) continue;
            const QRectF r = QRectF(cellRect(i)).adjusted(1, 1, -1, -1);
            const bool past = !pickable(d);
            const bool sel = d == m_selected;
            bool medium = false;
            if (sel) {
                p.setPen(hasFocus() ? QPen(QColor(fg), 1) : QPen(Qt::NoPen));
                p.setBrush(m_theme.accent);
                p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 7, 7);
                medium = true;
            } else if (d == today) {
                p.setPen(QPen(m_theme.accent, 1));
                p.setBrush(Qt::NoBrush);
                p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 7, 7);
                medium = true;
            }
            p.setFont(monoFont(font(), 11, medium));
            if (sel) p.setPen(QColor("#0d1014"));
            else if (past) p.setPen(withAlpha(muted, 80));
            else if (d == today) p.setPen(m_theme.accent);
            else p.setPen(fg);
            p.drawText(r, Qt::AlignCenter, QString::number(d.day()));
        }
    }

private:
    const Theme &m_theme;
    QDate m_selected = QDate::currentDate();
    QDate m_shown = QDate(QDate::currentDate().year(), QDate::currentDate().month(), 1);
};

/// The column of times, one row per step. Painted inside a scroll area; rows
/// before @c earliest (today, past the current minute) are dimmed and skipped.
class TimeList : public QWidget {
public:
    std::function<void(const QTime &)> onPick;
    std::function<void()> onEnter;
    /// Scrolls the row at that y into view; the scroll area is the parent's.
    std::function<void(int, int)> ensureVisible;
    QTime earliest;   ///< Invalid: nothing is too early.

    explicit TimeList(const Theme &theme) : m_theme(theme) {
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
        setStep(15);
    }

    void setStep(int step) {
        m_step = step;
        setFixedSize(kTimeWidth - 8, (24 * 60 / step) * (kTimeRow + 1));
        update();
    }
    void setSelected(const QTime &t) {
        m_selected = t;
        update();
    }
    int rowY(const QTime &t) const {
        return ((t.hour() * 60 + t.minute()) / m_step) * (kTimeRow + 1);
    }
    bool allowed(const QTime &t) const { return !earliest.isValid() || t >= earliest; }
    int step() const { return m_step; }
    /// The first row at or after @p t that may be picked; invalid if none is left.
    QTime firstAllowedFrom(const QTime &t) const {
        for (int m = t.hour() * 60 + t.minute(); m < 24 * 60; m += m_step) {
            const QTime c = snapped(QTime(m / 60, m % 60), m_step);
            if (allowed(c)) return c;
        }
        return QTime();
    }

protected:
    QTime timeAt(int y) const {
        const int row = qBound(0, y / (kTimeRow + 1), 24 * 60 / m_step - 1);
        const int m = row * m_step;
        return QTime(m / 60, m % 60);
    }

    void moveBy(int rows) {
        const int count = 24 * 60 / m_step;
        int row = (m_selected.hour() * 60 + m_selected.minute()) / m_step;
        for (int i = 0; i < count; ++i) {
            row = qBound(0, row + rows, count - 1);
            const QTime t(row * m_step / 60, row * m_step % 60);
            if (allowed(t)) {
                if (onPick) onPick(t);
                return;
            }
            if (row == 0 || row == count - 1) return;
        }
    }

    void mouseReleaseEvent(QMouseEvent *e) override {
        const QTime t = timeAt(e->position().toPoint().y());
        if (allowed(t) && onPick) onPick(t);
    }
    void mouseDoubleClickEvent(QMouseEvent *e) override {
        // A double click is "this one, done", like Enter.
        const QTime t = timeAt(e->position().toPoint().y());
        if (allowed(t) && onEnter) onEnter();
    }
    void mouseMoveEvent(QMouseEvent *e) override {
        const QTime t = timeAt(e->position().toPoint().y());
        setCursor(allowed(t) ? Qt::PointingHandCursor : Qt::ArrowCursor);
        if (t != m_hover) { m_hover = t; update(); }
    }
    void leaveEvent(QEvent *) override { m_hover = QTime(); update(); }

    void keyPressEvent(QKeyEvent *e) override {
        switch (e->key()) {
            case Qt::Key_Up:       moveBy(-1); return;
            case Qt::Key_Down:     moveBy(1); return;
            case Qt::Key_PageUp:   moveBy(-60 / m_step); return;
            case Qt::Key_PageDown: moveBy(60 / m_step); return;
            case Qt::Key_Return:
            case Qt::Key_Enter:
                if (onEnter) onEnter();
                return;
            default:
                QWidget::keyPressEvent(e);
        }
    }

    void focusInEvent(QFocusEvent *e) override { QWidget::focusInEvent(e); update(); }
    void focusOutEvent(QFocusEvent *e) override { QWidget::focusOutEvent(e); update(); }

    void paintEvent(QPaintEvent *e) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QColor fg(Theme::fg());
        const QColor muted(Theme::muted());
        const int first = e->rect().top() / (kTimeRow + 1);
        const int last = qMin(24 * 60 / m_step - 1, e->rect().bottom() / (kTimeRow + 1));
        for (int row = first; row <= last; ++row) {
            const QTime t(row * m_step / 60, row * m_step % 60);
            const QRectF r(0, row * (kTimeRow + 1), width(), kTimeRow);
            const bool sel = t == m_selected;
            const bool ok = allowed(t);
            if (sel) {
                p.setPen(hasFocus() ? QPen(fg, 1) : QPen(Qt::NoPen));
                p.setBrush(m_theme.accent);
                p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
            } else if (t == m_hover && ok) {
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(255, 255, 255, 18));
                p.drawRoundedRect(r, 6, 6);
            }
            p.setFont(monoFont(font(), 11.5, sel));
            if (sel) p.setPen(QColor("#0d1014"));
            else p.setPen(ok ? fg : withAlpha(muted, 80));
            p.drawText(r, Qt::AlignCenter, t.toString("HH:mm"));
        }
    }

private:
    const Theme &m_theme;
    int m_step = 15;
    QTime m_selected;
    QTime m_hover;
};

} // namespace picker

DateTimePicker::DateTimePicker(const Theme &theme, Mode mode, const QDateTime &initial,
                               const Quick &quick, QWidget *parent)
    : QWidget(parent), m_theme(theme), m_mode(mode) {
    const int step = QSettings().value("picker/step", 15).toInt() == 5 ? 5 : 15;
    m_date = initial.date().isValid() ? initial.date() : QDate::currentDate();
    m_time = snapped(initial.time().isValid() ? initial.time() : QTime(9, 0), step);

    auto *col = new QVBoxLayout(this);
    col->setContentsMargins(7, 4, 7, 5);
    col->setSpacing(9);

    m_summary = new QLabel;
    m_summary->setObjectName("pickSummary");
    col->addWidget(m_summary);

    if (!quick.isEmpty()) {
        auto *chips = new QGridLayout;
        chips->setSpacing(4);
        for (int i = 0; i < quick.size(); ++i) {
            auto *chip = button("popupChip", quick.at(i).first);
            chip->setToolTip(Lang::locale().toString(quick.at(i).second, "ddd d MMM HH:mm"));
            chip->setFixedHeight(24);
            chip->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            const QDateTime when = quick.at(i).second;
            connect(chip, &QToolButton::clicked, this, [this, when] {
                if (onCommit) onCommit(when);
            });
            chips->addWidget(chip, i / 2, i % 2);
        }
        col->addLayout(chips);
    }

    auto *body = new QHBoxLayout;
    body->setSpacing(10);

    if (mode == DateAndTime) {
        m_grid = new picker::DayGrid(m_theme);
        m_grid->minDate = QDate::currentDate();
        m_grid->onPick = [this](const QDate &d) { setDate(d); };
        m_grid->onEnter = [this] { commit(); };
        body->addWidget(m_grid, 0, Qt::AlignTop);
    }

    auto *timeCol = new QVBoxLayout;
    timeCol->setSpacing(4);

    auto *track = new QFrame;
    track->setObjectName("segTrack");
    auto *tl = new QHBoxLayout(track);
    tl->setContentsMargins(2, 2, 2, 2);
    tl->setSpacing(2);
    m_step5 = button("segOption", QStringLiteral("5′"));
    m_step15 = button("segOption", QStringLiteral("15′"));
    m_step5->setToolTip(L("Cada 5 minutos"));
    m_step15->setToolTip(L("Cada 15 minutos"));
    for (QToolButton *b : {m_step5, m_step15}) {
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        tl->addWidget(b);
    }
    connect(m_step5, &QToolButton::clicked, this, [this] { setStep(5); });
    connect(m_step15, &QToolButton::clicked, this, [this] { setStep(15); });
    timeCol->addWidget(track);

    m_times = new picker::TimeList(m_theme);
    m_times->setStep(step);
    m_times->onPick = [this](const QTime &t) { setTime(t); };
    m_times->onEnter = [this] { commit(); };

    m_timeScroll = new QScrollArea;
    m_timeScroll->setObjectName("pickTimes");
    m_timeScroll->setWidget(m_times);
    // QScrollArea turns autofill on for its widget, which paints the palette's
    // window colour (white offscreen) behind the rows.
    m_times->setObjectName("pickTimeList");
    m_times->setAutoFillBackground(false);
    m_timeScroll->setFrameShape(QFrame::NoFrame);
    m_timeScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_timeScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_timeScroll->setFocusPolicy(Qt::NoFocus);
    m_timeScroll->viewport()->setObjectName("pickTimesViewport");
    m_timeScroll->setFixedSize(kTimeWidth - 8, kTimeHeight - 28);
    timeCol->addWidget(m_timeScroll);
    timeCol->addStretch();

    if (mode == TimeOnly) {
        // Alone, the column would be a narrow strip at the left of the popup.
        body->addStretch();
        body->addLayout(timeCol);
        body->addStretch();
    } else {
        body->addLayout(timeCol);
    }
    col->addLayout(body);

    auto *rule = new QFrame;
    rule->setObjectName("setDivider");
    rule->setFixedHeight(1);
    col->addWidget(rule);

    auto *foot = new QHBoxLayout;
    foot->setSpacing(6);
    foot->addStretch();
    auto *cancel = button("pickCancel", L("Cancelar"));
    m_save = button("pickSave", L("Guardar"));
    connect(cancel, &QToolButton::clicked, this, [this] { if (onCancel) onCancel(); });
    connect(m_save, &QToolButton::clicked, this, [this] { commit(); });
    foot->addWidget(cancel);
    foot->addWidget(m_save);
    col->addLayout(foot);

    setTabOrder(m_grid ? static_cast<QWidget *>(m_grid) : m_step5, m_step5);
    setTabOrder(m_step5, m_step15);
    setTabOrder(m_step15, m_times);
    setTabOrder(m_times, cancel);
    setTabOrder(cancel, m_save);

    if (m_grid) m_grid->setSelected(m_date);
    setTime(m_time);
}

QWidget *DateTimePicker::initialFocus() const {
    return m_grid ? static_cast<QWidget *>(m_grid) : m_times;
}

void DateTimePicker::setDate(const QDate &d) {
    m_date = d;
    if (m_grid) m_grid->setSelected(d);
    refresh();
    // Moving onto today with a time already gone takes the next free slot rather
    // than leaving *Guardar* off for no visible reason.
    if (!m_times->allowed(m_time)) {
        const QTime next = m_times->firstAllowedFrom(m_time);
        if (next.isValid()) setTime(next);
    }
}

void DateTimePicker::setTime(const QTime &t) {
    m_time = t;
    m_times->setSelected(t);
    const int y = m_times->rowY(t);
    // The selected row stays in view, a couple of rows from the top so what comes
    // before it shows too.
    QScrollBar *bar = m_timeScroll->verticalScrollBar();
    const int viewH = m_timeScroll->viewport()->height() > 0 ? m_timeScroll->viewport()->height()
                                                              : m_timeScroll->height();
    if (y < bar->value() || y + kTimeRow > bar->value() + viewH)
        bar->setValue(qMax(0, y - 2 * (kTimeRow + 1)));
    refresh();
}

void DateTimePicker::setStep(int minutes) {
    QSettings().setValue("picker/step", minutes);
    m_times->setStep(minutes);
    setTime(snapped(m_time, minutes));
}

void DateTimePicker::refresh() {
    const bool today = m_mode == DateAndTime && m_date == QDate::currentDate();
    m_times->earliest = today ? QTime::currentTime().addSecs(60) : QTime();
    m_times->update();

    const bool five = m_times->step() == 5;
    m_step5->setProperty("chosen", five);
    m_step15->setProperty("chosen", !five);
    for (QToolButton *b : {m_step5, m_step15}) {
        b->style()->unpolish(b);
        b->style()->polish(b);
    }

    QString text;
    if (m_mode == DateAndTime) {
        text = Lang::locale().toString(m_date, "ddd d MMM");
        if (!text.isEmpty()) text[0] = text[0].toUpper();
        text += QStringLiteral(" · ") + m_time.toString("HH:mm");
    } else {
        text = m_time.toString("HH:mm");
    }
    m_summary->setText(text);

    // A reminder in the past would ring the moment it is saved.
    const bool ok = m_mode == TimeOnly || QDateTime(m_date, m_time) > QDateTime::currentDateTime();
    m_save->setEnabled(ok);
    m_save->setToolTip(ok ? QString() : L("Esa hora ya ha pasado"));
}

void DateTimePicker::commit() {
    if (!m_save->isEnabled()) return;
    if (onCommit) onCommit(value());
}
