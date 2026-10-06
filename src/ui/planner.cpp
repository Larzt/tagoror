#include "ui/planner.hpp"

#include "core/lang.hpp"
#include "ui/elidedlabel.hpp"
#include "ui/keynav.hpp"
#include "ui/popup.hpp"

#include <algorithm>
#include <cmath>

#include <QApplication>
#include <QCheckBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QStyle>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>

using Item = PlannerView::Item;

namespace {

const QColor kRed("#ff7a6b");
const QColor kAmber("#f2b757");
const QColor kPink("#ff8fc2");

/// Below this width the side panel does not fit next to a readable week.
constexpr int kWideFrom = 620;

QString hhmm(qreal hours) {
    const int total = qRound(hours * 60);
    return QString("%1:%2").arg(total / 60, 2, 10, QChar('0')).arg(total % 60, 2, 10, QChar('0'));
}

QColor withAlpha(QColor c, int alpha) {
    c.setAlpha(alpha);
    return c;
}

QFont monoFont(const QFont &base, qreal px) {
    QFont f = base;
    f.setFamily("IBM Plex Mono");
    f.setStyleHint(QFont::Monospace);
    f.setPixelSize(qMax(7, qRound(px)));
    return f;
}

QFont sansFont(const QFont &base, qreal px, bool bold = false) {
    QFont f = base;
    f.setPixelSize(qMax(7, qRound(px)));
    f.setWeight(bold ? QFont::DemiBold : QFont::Normal);
    return f;
}

/// Monday of that day's week: the grid starts on Monday in both languages.
QDate mondayOf(const QDate &d) { return d.addDays(1 - d.dayOfWeek()); }

/// First day of a month's 6x7 grid: the Monday of the week of the 1st.
QDate gridStart(const QDate &anyDay) { return mondayOf(QDate(anyDay.year(), anyDay.month(), 1)); }

QToolButton *navButton(const QString &kind, const QString &tip) {
    auto *b = new QToolButton;
    b->setObjectName("calNav");
    b->setIcon(paintIcon(kind, QColor(Theme::muted()), 14));
    b->setIconSize(QSize(14, 14));
    b->setFixedSize(26, 26);
    b->setCursor(Qt::PointingHandCursor);
    b->setToolTip(tip);
    b->setProperty("tip", tip);
    return b;
}

QToolButton *segButton(const QString &text) {
    auto *b = new QToolButton;
    b->setObjectName("segButton");
    b->setText(text);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

void setChosen(QToolButton *b, bool on) {
    b->setProperty("chosen", on);
    b->style()->unpolish(b);
    b->style()->polish(b);
}

/// Alert lead time in minutes; -1 is "no alert". Three per row in the form:
/// one row does not fit at the panel's minimum width.
struct AlertChoice {
    int minutes;
    const char *label;
};
constexpr AlertChoice kAlertChoices[] = {
    {-1, "Sin aviso"}, {0, "Al empezar"}, {5, "5 min"},
    {10, "10 min"},    {15, "15 min"},    {30, "30 min"},
    {60, "1 h"},       {120, "2 h"},      {24 * 60, "1 día"},
};

/// What can be dragged to another time or day. Not a birthday: it is a
/// person's date, not an appointment.
bool movable(const Item &it) {
    return it.source == Item::FromEvent || it.source == Item::FromReminder;
}

/// The same element on the same day: the dragged item, recognised again
/// after a refresh rebuilds the list.
bool sameItem(const Item &a, const Item &b) {
    return a.source == b.source && a.event == b.event && a.note == b.note && a.day == b.day;
}

/// Drags move in quarter-hour steps.
qreal snapQuarter(qreal hours) { return std::round(hours * 4) / 4.0; }

QIcon colorDot(const QColor &color) {
    QPixmap dot(16, 16);
    dot.fill(Qt::transparent);
    QPainter p(&dot);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawRoundedRect(QRectF(2, 2, 12, 12), 3.5, 3.5);
    p.end();
    return QIcon(dot);
}

}  // namespace

/// Day or week view, hours running down.
///
/// Painted whole in one widget: the blocks have no state worth a widget each,
/// and overlaps, the "now" line and the layout share the same maths. It keeps
/// its own focus index for the keyboard (see focusNextPrevChild()).

class TimeGrid : public QWidget {
public:
    static constexpr int kGutter = 46;
    static constexpr int kHour = 42;

    std::function<QList<Item>(const QDate &)> source;
    std::function<void(const Item &)> onActivate;
    std::function<void(const Item &)> onToggle;
    std::function<void(const QDate &, qreal)> onEmpty;
    std::function<void(const QRect &)> onReveal;   ///< Asks the scroll area to show a rect.
    /// Called on drop after a drag: the new day and hours (decimal).
    std::function<void(const Item &, const QDate &, qreal, qreal)> onMove;

    explicit TimeGrid(const Theme *theme, QWidget *parent = nullptr)
        : QWidget(parent), m_theme(theme) {
        setMouseTracking(true);
        setFocusPolicy(Qt::TabFocus);
        setAttribute(Qt::WA_Hover);
    }

    void setDays(const QList<QDate> &days) {
        m_days = days;
        reload();
    }
    const QList<QDate> &days() const { return m_days; }

    void reload() {
        m_blocks.clear();
        if (!source) return;
        for (int col = 0; col < m_days.size(); ++col) {
            QList<Item> timed;
            for (const Item &it : source(m_days.at(col)))
                if (!it.allDay) timed.append(it);
            layoutColumn(col, timed);
        }
        m_focus = qMin(m_focus, int(m_blocks.size()) - 1);
        updateGeometry();
        update();
    }

    // All-day items are not here but in WeekHead: at the top of the grid they
    // were out of view as soon as it scrolled to 08:00, which is where it opens.
    int yFor(qreal hours) const { return int(hours * kHour); }

    QSize sizeHint() const override { return {kGutter + 7 * 40, 24 * kHour + 1}; }
    QSize minimumSizeHint() const override {
        return {kGutter + int(m_days.size()) * 24, 24 * kHour + 1};
    }

protected:
    void paintEvent(QPaintEvent *) override;

    // A press only records the block: moving before release is a drag (move, or
    // resize from the bottom edge), otherwise the usual click on release.
    void mousePressEvent(QMouseEvent *e) override {
        m_pressed = false;
        if (e->button() != Qt::LeftButton) return;
        const QPoint at = e->position().toPoint();
        const int hit = blockAt(at);
        if (hit < 0) return;
        const Block &b = m_blocks.at(hit);
        const QRect r = blockRect(b);
        if (!movable(b.item) || (b.item.task && checkRect(r).adjusted(-4, -4, 4, 4).contains(at)))
            return;
        m_pressed = true;
        m_dragItem = b.item;
        m_pressPos = at;
        m_grab = at.y() / qreal(kHour) - b.item.start;
        m_resizing = onResizeEdge(b, at);
    }

    void mouseMoveEvent(QMouseEvent *e) override {
        const QPoint at = e->position().toPoint();
        if (m_pressed && (e->buttons() & Qt::LeftButton)) {
            if (!m_dragging &&
                (at - m_pressPos).manhattanLength() >= QApplication::startDragDistance()) {
                m_dragging = true;
                m_hover = -1;
                QToolTip::hideText();
            }
            if (m_dragging) {
                dragTo(at);
                return;
            }
        }
        const int hit = blockAt(at);
        if (hit != m_hover) {
            m_hover = hit;
            update();
        }
        if (hit >= 0 && onResizeEdge(m_blocks.at(hit), at)) setCursor(Qt::SizeVerCursor);
        else setCursor(hit >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
    }

    void leaveEvent(QEvent *) override {
        m_hover = -1;
        update();
    }

    bool event(QEvent *e) override {
        if (e->type() == QEvent::ToolTip) {
            auto *he = static_cast<QHelpEvent *>(e);
            const int hit = blockAt(he->pos());
            if (hit >= 0) {
                const Item &it = m_blocks.at(hit).item;
                QString tip = it.title + "\n" + hhmm(it.start);
                if (it.source == Item::FromEvent) tip += "–" + hhmm(it.end);
                if (it.event && !it.event->description.isEmpty())
                    tip += "\n" + it.event->description.left(200);
                QToolTip::showText(he->globalPos(), tip, this);
            } else {
                QToolTip::hideText();
            }
            return true;
        }
        return QWidget::event(e);
    }

    void mouseReleaseEvent(QMouseEvent *e) override {
        if (e->button() != Qt::LeftButton) return;
        const QPoint at = e->position().toPoint();
        if (m_dragging) {
            finishDrag();
            return;
        }
        m_pressed = false;
        if (const int hit = blockAt(at); hit >= 0) {
            const Block &b = m_blocks.at(hit);
            if (b.item.task && checkRect(b.rect).adjusted(-4, -4, 4, 4).contains(at)) {
                if (onToggle) onToggle(b.item);
            } else if (onActivate) {
                onActivate(b.item);
            }
            return;
        }
        // An empty slot: a new event at that time, rounded to the half hour.
        const int col = columnAt(at.x());
        if (col < 0) return;
        const qreal hours = qBound<qreal>(0, at.y() / qreal(kHour), 23.5);
        if (onEmpty) onEmpty(m_days.at(col), std::floor(hours * 2) / 2.0);
    }

    // Tab walks the blocks one by one before leaving the grid, which is what makes
    // blocks that are not widgets reachable by keyboard.
    bool focusNextPrevChild(bool next) override {
        if (!hasFocus() || m_blocks.isEmpty()) return QWidget::focusNextPrevChild(next);
        const int to = m_focus + (next ? 1 : -1);
        if (to < 0 || to >= m_blocks.size()) {
            m_focus = -1;
            update();
            return QWidget::focusNextPrevChild(next);
        }
        m_focus = to;
        reveal();
        update();
        return true;
    }

    void focusInEvent(QFocusEvent *e) override {
        QWidget::focusInEvent(e);
        if (m_blocks.isEmpty()) m_focus = -1;
        else if (e->reason() == Qt::BacktabFocusReason) m_focus = int(m_blocks.size()) - 1;
        else if (e->reason() == Qt::TabFocusReason) m_focus = 0;
        reveal();
        update();
    }

    void focusOutEvent(QFocusEvent *e) override {
        QWidget::focusOutEvent(e);
        update();
    }

    void keyPressEvent(QKeyEvent *e) override {
        const bool valid = m_focus >= 0 && m_focus < m_blocks.size();
        switch (e->key()) {
            case Qt::Key_Return:
            case Qt::Key_Enter:
                if (valid && onActivate) onActivate(m_blocks.at(m_focus).item);
                else if (!valid && onEmpty && !m_days.isEmpty()) onEmpty(m_days.first(), 9);
                return;
            case Qt::Key_Space:
                if (valid && m_blocks.at(m_focus).item.task && onToggle)
                    onToggle(m_blocks.at(m_focus).item);
                return;
            case Qt::Key_Escape:
                if (m_dragging) {   // drop without moving anything
                    m_pressed = m_dragging = false;
                    update();
                    return;
                }
                QWidget::keyPressEvent(e);
                return;
            default:
                QWidget::keyPressEvent(e);
        }
    }

private:
    struct Block {
        Item item;
        int col = 0;
        int lane = 0;
        int laneCount = 1;
        QRect rect;
    };

    /// Resizing: only an event (a reminder is an instant) that starts and ends
    /// on the same day, grabbed by the block's last pixels.
    bool onResizeEdge(const Block &b, const QPoint &at) const {
        if (b.item.source != Item::FromEvent || !b.item.event) return false;
        const Event *e = b.item.event;
        if (e->endDate.isValid() && e->endDate > e->date) return false;
        const QRect r = blockRect(b);
        return r.height() >= 18 && at.y() >= r.bottom() - 5 && r.contains(at);
    }

    void dragTo(const QPoint &at) {
        const qreal y = at.y() / qreal(kHour);
        if (m_resizing) {
            m_dragCol = qMax(0, int(m_days.indexOf(m_dragItem.day)));
            m_dragStart = m_dragItem.start;
            m_dragEnd = qBound(m_dragStart + 0.25, snapQuarter(y), 24.0);
        } else {
            const int w = columnWidth();
            m_dragCol = w > 0 ? qBound(0, (at.x() - kGutter) / w, int(m_days.size()) - 1) : 0;
            const qreal length = m_dragItem.end - m_dragItem.start;
            // A reminder has no end: the last quarter hour is its latest start.
            const qreal latest = length > 0 ? 24.0 - length : 23.75;
            m_dragStart = qBound<qreal>(0, snapQuarter(y - m_grab), qMax<qreal>(0, latest));
            m_dragEnd = m_dragStart + length;
        }
        setCursor(m_resizing ? Qt::SizeVerCursor : Qt::ClosedHandCursor);
        // Near the edge of the visible area, scroll along.
        if (onReveal) onReveal(QRect(at.x(), at.y() - 10, 1, 20));
        update();
    }

    void finishDrag() {
        const Item item = m_dragItem;
        const QDate day = m_days.value(m_dragCol);
        const qreal start = m_dragStart, end = m_dragEnd;
        m_pressed = m_dragging = false;
        setCursor(Qt::ArrowCursor);
        update();
        const bool changed = day != item.day || std::abs(start - item.start) > 1e-6 ||
                             std::abs(end - item.end) > 1e-6;
        if (changed && day.isValid() && onMove) onMove(item, day, start, end);
    }

    /// Overlapping blocks share the column width. They are grouped in clusters of
    /// transitive overlaps and each block takes the first free lane, so three
    /// events where only two coincide use two lanes, not three.
    void layoutColumn(int col, QList<Item> items) {
        std::sort(items.begin(), items.end(), [](const Item &a, const Item &b) {
            return a.start != b.start ? a.start < b.start : a.end > b.end;
        });
        int i = 0;
        while (i < items.size()) {
            int j = i;
            qreal clusterEnd = visualEnd(items.at(i));
            QList<qreal> laneEnds;
            QList<int> lanes;
            while (j < items.size() && (j == i || items.at(j).start < clusterEnd)) {
                const Item &it = items.at(j);
                int lane = 0;
                while (lane < laneEnds.size() && laneEnds.at(lane) > it.start) ++lane;
                if (lane == laneEnds.size()) laneEnds.append(visualEnd(it));
                else laneEnds[lane] = visualEnd(it);
                lanes.append(lane);
                clusterEnd = qMax(clusterEnd, visualEnd(it));
                ++j;
            }
            for (int k = i; k < j; ++k)
                m_blocks.append({items.at(k), col, lanes.at(k - i), int(laneEnds.size()), {}});
            i = j;
        }
    }

    /// A reminder is an instant: it gets a minimum height to be visible and
    /// clickable without counting as busy time.
    static qreal visualEnd(const Item &it) {
        return qMax(it.end, it.start + 0.4);
    }

    int columnWidth() const {
        return m_days.isEmpty() ? 0 : (width() - kGutter) / int(m_days.size());
    }
    int columnAt(int x) const {
        const int w = columnWidth();
        if (w <= 0 || x < kGutter) return -1;
        const int c = (x - kGutter) / w;
        return c < m_days.size() ? c : -1;
    }

    QRect blockRect(const Block &b) const {
        const int w = columnWidth();
        const int x0 = kGutter + b.col * w;
        const int laneW = w / qMax(1, b.laneCount);
        const int top = yFor(b.item.start);
        const int bottom = qMax(yFor(visualEnd(b.item)) - 2, top + 20);
        return QRect(x0 + b.lane * laneW + 2, top + 1, laneW - 4, bottom - top);
    }
    static QRect checkRect(const QRect &block) {
        return QRect(block.left() + 7, block.top() + 6, 12, 12);
    }

    int blockAt(const QPoint &p) const {
        for (int i = int(m_blocks.size()) - 1; i >= 0; --i)
            if (blockRect(m_blocks.at(i)).contains(p)) return i;
        return -1;
    }

    void reveal() {
        if (m_focus >= 0 && m_focus < m_blocks.size() && onReveal)
            onReveal(blockRect(m_blocks.at(m_focus)));
    }

    const Theme *m_theme;
    QList<QDate> m_days;
    QList<Block> m_blocks;
    int m_hover = -1;
    int m_focus = -1;
    // Drag state: a copy of the item, not an index, since the 5 s heartbeat may
    // rebuild the blocks mid-drag.
    bool m_pressed = false;
    bool m_dragging = false;
    bool m_resizing = false;
    Item m_dragItem;
    QPoint m_pressPos;
    qreal m_grab = 0;   ///< Where it was grabbed, in hours from the block's start.
    int m_dragCol = 0;
    qreal m_dragStart = 0;
    qreal m_dragEnd = 0;
};

void TimeGrid::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor muted(Theme::muted());
    const QColor fg(Theme::fg());
    const QColor faint(255, 255, 255, 14);
    const int w = columnWidth();

    // Hours: label on the left and a faint line across the grid.
    p.setFont(monoFont(font(), 9.5));
    for (int h = 0; h <= 24; ++h) {
        const int y = h * kHour;
        p.setPen(faint);
        p.drawLine(kGutter, y, width(), y);
        if (h < 24) {
            p.setPen(muted);
            p.drawText(QRect(0, y + 2, kGutter - 8, 14), Qt::AlignRight | Qt::AlignTop,
                       QString("%1:00").arg(h, 2, 10, QChar('0')));
        }
    }
    // Day separators.
    for (int c = 0; c <= m_days.size(); ++c) {
        p.setPen(faint);
        p.drawLine(kGutter + c * w, 0, kGutter + c * w, height());
    }
    // Today slightly lighter, to find it in a week.
    const QDate today = QDate::currentDate();
    for (int c = 0; c < m_days.size(); ++c)
        if (m_days.at(c) == today && m_days.size() > 1)
            p.fillRect(QRect(kGutter + c * w + 1, 0, w - 1, height()),
                       withAlpha(m_theme->accent, 10));

    for (int i = 0; i < m_blocks.size(); ++i) {
        Block &b = m_blocks[i];
        b.rect = blockRect(b);
        const Item &it = b.item;
        const QRect r = b.rect;
        const bool hot = i == m_hover;
        // The dragged block stays in place, dimmed, while its shadow shows where it
        // goes.
        p.setOpacity(m_dragging && sameItem(it, m_dragItem) ? 0.35 : 1.0);
        const QColor color = it.alert ? kRed : it.color;

        QPainterPath path;
        path.addRoundedRect(QRectF(r), 7, 7);
        p.fillPath(path, withAlpha(color, hot ? 78 : (it.done ? 26 : 50)));
        QPen border(withAlpha(color, hot ? 200 : 120), 1);
        if (it.source == Item::FromReminder) border.setStyle(Qt::DashLine);
        p.setPen(border);
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 7, 7);
        // Colour bar on the left: readable even on a short block.
        p.save();
        p.setClipPath(path);
        p.fillRect(QRect(r.left(), r.top(), 3, r.height()), color);
        p.restore();

        if (keynav::showsFocus(this) && i == m_focus) {
            p.setPen(QPen(fg, 1.6));
            p.drawRoundedRect(QRectF(r).adjusted(-1.5, -1.5, 1.5, 1.5), 8, 8);
        }

        int textX = r.left() + 8;
        if (it.task) {
            const QRect box = checkRect(r);
            p.setPen(QPen(it.done ? color : muted, 1.3));
            p.setBrush(it.done ? QBrush(color) : QBrush(Qt::NoBrush));
            p.drawRoundedRect(QRectF(box), 3, 3);
            if (it.done) {
                p.setPen(QPen(QColor("#12141a"), 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                p.drawPolyline(QPolygonF({QPointF(box.left() + 2.6, box.center().y() + 0.4),
                                          QPointF(box.left() + 5.0, box.bottom() - 2.6),
                                          QPointF(box.right() - 2.2, box.top() + 3.0)}));
            }
            textX = box.right() + 6;
        }

        const int textW = r.right() - textX - 4;
        if (textW < 8) continue;
        QFont tf = sansFont(font(), m_theme->fs(11), true);
        tf.setStrikeOut(it.done);
        p.setFont(tf);
        p.setPen(it.done ? muted : fg);
        const int lineH = QFontMetrics(tf).height();
        p.drawText(QRect(textX, r.top() + 4, textW, lineH), Qt::AlignLeft | Qt::AlignVCenter,
                   p.fontMetrics().elidedText(it.title, Qt::ElideRight, textW));
        if (r.height() >= lineH + 20) {
            p.setFont(monoFont(font(), 9.5));
            p.setPen(muted);
            const QString when = it.source == Item::FromEvent
                                     ? hhmm(it.start) + "–" + hhmm(it.end)
                                     : hhmm(it.start);
            p.drawText(QRect(textX, r.top() + 4 + lineH, textW, 14), Qt::AlignLeft | Qt::AlignVCenter,
                       p.fontMetrics().elidedText(when, Qt::ElideRight, textW));
        }
    }

    p.setOpacity(1.0);

    // Shadow of the dragged block, with the time it will get on drop.
    if (m_dragging && m_dragCol < m_days.size()) {
        const Item &it = m_dragItem;
        const int top = yFor(m_dragStart);
        const int bottom = qMax(yFor(qMax(m_dragEnd, m_dragStart + 0.4)) - 2, top + 20);
        const QRect r(kGutter + m_dragCol * w + 2, top + 1, w - 4, bottom - top);
        const QColor color = it.color;
        QPainterPath path;
        path.addRoundedRect(QRectF(r), 7, 7);
        p.fillPath(path, withAlpha(color, 95));
        p.setPen(QPen(color, 1.4));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 7, 7);
        const int textW = r.width() - 12;
        if (textW >= 8) {
            QFont tf = sansFont(font(), m_theme->fs(11), true);
            p.setFont(tf);
            p.setPen(fg);
            const int lineH = QFontMetrics(tf).height();
            p.drawText(QRect(r.left() + 8, r.top() + 4, textW, lineH), Qt::AlignLeft | Qt::AlignVCenter,
                       p.fontMetrics().elidedText(it.title, Qt::ElideRight, textW));
            const QString when = it.source == Item::FromEvent
                                     ? hhmm(m_dragStart) + "–" + hhmm(m_dragEnd)
                                     : hhmm(m_dragStart);
            p.setFont(monoFont(font(), 9.5));
            if (r.height() >= lineH + 20)
                p.drawText(QRect(r.left() + 8, r.top() + 4 + lineH, textW, 14), Qt::AlignLeft | Qt::AlignVCenter,
                           p.fontMetrics().elidedText(when, Qt::ElideRight, textW));
            else
                p.drawText(QRect(r.left() + 8, r.top() + 4, textW, lineH),
                           Qt::AlignRight | Qt::AlignVCenter, when);
        }
    }

    // Current time in red over today's column.
    const QTime now = QTime::currentTime();
    for (int c = 0; c < m_days.size(); ++c) {
        if (m_days.at(c) != today) continue;
        const int y = yFor(now.msecsSinceStartOfDay() / 3600000.0);
        p.setPen(QPen(kRed, 1.5));
        p.drawLine(kGutter + c * w, y, kGutter + (c + 1) * w, y);
        p.setPen(Qt::NoPen);
        p.setBrush(kRed);
        p.drawEllipse(QPointF(kGutter + c * w, y), 3.5, 3.5);
    }

    // An empty day says so instead of showing a bare grid.
    if (m_blocks.isEmpty() && m_days.size() == 1) {
        const int y = yFor(9);
        p.setFont(sansFont(font(), 12, true));
        p.setPen(fg);
        p.drawText(QRect(kGutter, y, width() - kGutter, 22), Qt::AlignCenter, L("Día libre"));
        p.setFont(sansFont(font(), 10.5));
        p.setPen(muted);
        p.drawText(QRect(kGutter, y + 22, width() - kGutter, 18), Qt::AlignCenter,
                   L("Pulsa en una hora para añadir algo"));
    }
}

/// The week header: weekday and number above each column, plus a band with
/// the all-day items. Outside the scroll area so it never scrolls away.

class WeekHead : public QWidget {
public:
    static constexpr int kDays = 38;
    static constexpr int kBand = 24;

    std::function<void(const QDate &)> onPick;
    std::function<QList<Item>(const QDate &)> source;
    std::function<void(const Item &)> onActivate;
    /// Called on drop on another day: same item, same time, another date.
    std::function<void(const Item &, const QDate &)> onMove;

    WeekHead(const Theme *theme, QWidget *parent = nullptr) : QWidget(parent), m_theme(theme) {
        setFixedHeight(kDays);
        setMouseTracking(true);
    }
    void setDays(const QList<QDate> &days, const QDate &selected) {
        m_days = days;
        m_selected = selected;
        m_band.clear();
        for (int c = 0; c < days.size() && source; ++c)
            for (const Item &it : source(days.at(c)))
                if (it.allDay) m_band.append({it, c});
        setFixedHeight(kDays + (m_band.isEmpty() ? 0 : kBand));
        update();
    }
    /// The grid's scrollbar takes width on the right: columns here must match.
    void setRightInset(int px) {
        m_inset = px;
        update();
    }

protected:
    void mousePressEvent(QMouseEvent *e) override {
        m_pressed = false;
        if (e->button() != Qt::LeftButton) return;
        const QPoint at = e->position().toPoint();
        for (int i = 0; i < m_band.size(); ++i)
            if (bandRect(i).contains(at) && movable(m_band.at(i).first)) {
                m_pressed = true;
                m_dragItem = m_band.at(i).first;
                m_pressPos = at;
                return;
            }
    }

    void mouseReleaseEvent(QMouseEvent *e) override {
        const QPoint at = e->position().toPoint();
        if (m_dragging) {
            const Item item = m_dragItem;
            const QDate day = m_days.value(m_dragCol);
            m_pressed = m_dragging = false;
            setCursor(Qt::ArrowCursor);
            update();
            if (day.isValid() && day != item.day && onMove) onMove(item, day);
            return;
        }
        m_pressed = false;
        for (int i = 0; i < m_band.size(); ++i)
            if (bandRect(i).contains(at)) {
                if (onActivate) onActivate(m_band.at(i).first);
                return;
            }
        const int c = columnAt(at.x());
        if (c >= 0 && at.y() < kDays && onPick) onPick(m_days.at(c));
    }
    void mouseMoveEvent(QMouseEvent *e) override {
        const QPoint at = e->position().toPoint();
        if (m_pressed && (e->buttons() & Qt::LeftButton)) {
            if ((at - m_pressPos).manhattanLength() >= QApplication::startDragDistance())
                m_dragging = true;
            if (m_dragging) {
                const int w = columnWidth();
                m_dragCol = w > 0 ? qBound(0, (at.x() - TimeGrid::kGutter) / w, int(m_days.size()) - 1)
                                  : 0;
                setCursor(Qt::ClosedHandCursor);
                update();
                return;
            }
        }
        bool hot = columnAt(at.x()) >= 0 && at.y() < kDays;
        for (int i = 0; i < m_band.size() && !hot; ++i) hot = bandRect(i).contains(at);
        setCursor(hot ? Qt::PointingHandCursor : Qt::ArrowCursor);
    }

    QRect bandRect(int index) const {
        const int col = m_band.at(index).second;
        int nth = 0, total = 0;
        for (int i = 0; i < m_band.size(); ++i) {
            if (m_band.at(i).second != col) continue;
            if (i < index) ++nth;
            ++total;
        }
        const int w = columnWidth();
        const int laneW = w / qMax(1, total);
        return QRect(TimeGrid::kGutter + col * w + nth * laneW + 2, kDays + 1, laneW - 4, kBand - 4);
    }

    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QDate today = QDate::currentDate();
        const int w = columnWidth();
        for (int c = 0; c < m_days.size(); ++c) {
            const QDate d = m_days.at(c);
            const QRect r(TimeGrid::kGutter + c * w, 2, w, height() - 4);
            if (d == m_selected && m_days.size() > 1) {
                p.setPen(Qt::NoPen);
                p.setBrush(withAlpha(m_theme->accent, 26));
                p.drawRoundedRect(r.adjusted(2, 0, -2, 0), 7, 7);
            }
            p.setFont(monoFont(font(), 8.5));
            p.setPen(QColor(Theme::muted()));
            p.drawText(QRect(r.left(), r.top() + 2, r.width(), 13), Qt::AlignCenter,
                       Lang::locale().toString(d, "ddd").toUpper().remove('.'));
            p.setFont(monoFont(font(), 13));
            QFont f = p.font();
            f.setBold(d == today);
            p.setFont(f);
            p.setPen(d == today ? m_theme->accent : QColor(Theme::fg()));
            p.drawText(QRect(r.left(), r.top() + 15, r.width(), 18), Qt::AlignCenter,
                       QString::number(d.day()));
        }
        p.setFont(sansFont(font(), m_theme->fs(10), true));
        for (int i = 0; i < m_band.size(); ++i) {
            const Item &it = m_band.at(i).first;
            const QRect r = bandRect(i);
            p.setOpacity(m_dragging && sameItem(it, m_dragItem) ? 0.35 : 1.0);
            p.setPen(Qt::NoPen);
            p.setBrush(withAlpha(it.alert ? kRed : it.color, 60));
            p.drawRoundedRect(r, 5, 5);
            p.fillRect(QRect(r.left(), r.top() + 3, 2, r.height() - 6), it.color);
            p.setPen(QColor(Theme::fg()));
            p.drawText(r.adjusted(7, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft,
                       p.fontMetrics().elidedText(it.title, Qt::ElideRight, r.width() - 11));
        }
        p.setOpacity(1.0);
        // The shadow, across the column it would land in.
        if (m_dragging && m_dragCol < m_days.size()) {
            const int w = columnWidth();
            const QRect r(TimeGrid::kGutter + m_dragCol * w + 2, kDays + 1, w - 4, kBand - 4);
            p.setPen(QPen(m_dragItem.color, 1.2));
            p.setBrush(withAlpha(m_dragItem.color, 95));
            p.drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 5, 5);
            p.setPen(QColor(Theme::fg()));
            p.drawText(r.adjusted(7, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft,
                       p.fontMetrics().elidedText(m_dragItem.title, Qt::ElideRight, r.width() - 11));
        }
    }

private:
    int columnWidth() const {
        return m_days.isEmpty() ? 0 : (width() - m_inset - TimeGrid::kGutter) / int(m_days.size());
    }
    int columnAt(int x) const {
        const int w = columnWidth();
        if (w <= 0 || x < TimeGrid::kGutter) return -1;
        const int c = (x - TimeGrid::kGutter) / w;
        return c < m_days.size() ? c : -1;
    }

    const Theme *m_theme;
    QList<QDate> m_days;
    QDate m_selected;
    QList<QPair<Item, int>> m_band;
    int m_inset = 0;
    bool m_pressed = false;
    bool m_dragging = false;
    Item m_dragItem;
    QPoint m_pressPos;
    int m_dragCol = 0;
};

/// The month view, with what falls on each day written inside.

class MonthBoard : public QWidget {
public:
    std::function<QList<Item>(const QDate &)> source;
    std::function<void(const QDate &)> onPick;      ///< Opens that day.
    std::function<void(const QDate &)> onSelect;    ///< Only selects it.
    std::function<void(int)> onShift;               ///< Previous/next month.
    /// An item dropped on another day: same time, another date.
    std::function<void(const Item &, const QDate &)> onMove;

    explicit MonthBoard(const Theme *theme, QWidget *parent = nullptr)
        : QWidget(parent), m_theme(theme) {
        setMouseTracking(true);
        setFocusPolicy(Qt::TabFocus);
        // Low rows at minimum: the month must not make the panel taller than the
        // note list needs (see *Window behavior*).
        setMinimumSize(7 * 30, 20 + 6 * 32);
    }

    void setMonth(const QDate &cursor) {
        m_cursor = cursor;
        m_start = gridStart(cursor);
        m_items.clear();
        for (int i = 0; i < 42; ++i)
            m_items.append(source ? source(m_start.addDays(i)) : QList<Item>());
        update();
    }

protected:
    static constexpr int kHead = 20;

    QRect cellRect(int i) const {
        const qreal cw = width() / 7.0;
        const qreal ch = (height() - kHead) / 6.0;
        return QRectF((i % 7) * cw, kHead + (i / 7) * ch, cw, ch).toRect().adjusted(1, 1, -1, -1);
    }
    int cellAt(const QPoint &p) const {
        for (int i = 0; i < 42; ++i)
            if (cellRect(i).contains(p)) return i;
        return -1;
    }

    // Chip geometry, the same maths paintEvent() uses.
    int chipHeight() const { return QFontMetrics(sansFont(font(), m_theme->fs(9.5))).height() + 3; }
    int shownIn(int cell) const {
        const int room = qMax(0, (cellRect(cell).height() - 20) / chipHeight());
        const int count = int(m_items.value(cell).size());
        return count > room ? qMax(0, room - 1) : count;
    }
    QRect chipRect(int cell, int k) const {
        const QRect r = cellRect(cell);
        const int chipH = chipHeight();
        return QRect(r.left() + 3, r.top() + 18 + k * chipH, r.width() - 6, chipH - 2);
    }

    void mousePressEvent(QMouseEvent *e) override {
        m_pressed = false;
        if (e->button() != Qt::LeftButton) return;
        const QPoint at = e->position().toPoint();
        const int c = cellAt(at);
        if (c < 0) return;
        for (int k = 0; k < shownIn(c); ++k)
            if (chipRect(c, k).contains(at) && movable(m_items.at(c).at(k))) {
                m_pressed = true;
                m_dragItem = m_items.at(c).at(k);
                m_pressPos = at;
                return;
            }
    }

    void mouseMoveEvent(QMouseEvent *e) override {
        const QPoint at = e->position().toPoint();
        if (m_pressed && (e->buttons() & Qt::LeftButton)) {
            if ((at - m_pressPos).manhattanLength() >= QApplication::startDragDistance())
                m_dragging = true;
            if (m_dragging) {
                m_dropCell = cellAt(at);
                m_dragPos = at;
                setCursor(Qt::ClosedHandCursor);
                update();
                return;
            }
        }
        const int c = cellAt(at);
        if (c != m_hover) {
            m_hover = c;
            update();
        }
        setCursor(c >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
    }
    void leaveEvent(QEvent *) override {
        m_hover = -1;
        update();
    }
    void mouseReleaseEvent(QMouseEvent *e) override {
        const int c = cellAt(e->position().toPoint());
        if (m_dragging) {
            const Item item = m_dragItem;
            m_pressed = m_dragging = false;
            setCursor(Qt::ArrowCursor);
            update();
            const QDate day = c >= 0 ? m_start.addDays(c) : QDate();
            if (day.isValid() && day != item.day && onMove) onMove(item, day);
            return;
        }
        m_pressed = false;
        if (c >= 0 && onPick) onPick(m_start.addDays(c));
    }
    void wheelEvent(QWheelEvent *e) override {
        // Wheel: month by month.
        if (onShift && e->angleDelta().y() != 0) onShift(e->angleDelta().y() > 0 ? -1 : 1);
    }
    void focusInEvent(QFocusEvent *e) override { QWidget::focusInEvent(e); update(); }
    void focusOutEvent(QFocusEvent *e) override { QWidget::focusOutEvent(e); update(); }

    // Arrows move between days, Enter opens one, PageUp/PageDown change month.
    void keyPressEvent(QKeyEvent *e) override {
        int step = 0;
        switch (e->key()) {
            case Qt::Key_Left:  step = -1; break;
            case Qt::Key_Right: step = 1; break;
            case Qt::Key_Up:    step = -7; break;
            case Qt::Key_Down:  step = 7; break;
            case Qt::Key_PageUp:   if (onShift) onShift(-1); return;
            case Qt::Key_PageDown: if (onShift) onShift(1); return;
            case Qt::Key_Return:
            case Qt::Key_Enter:
            case Qt::Key_Space:
                if (onPick) onPick(m_cursor);
                return;
            default:
                QWidget::keyPressEvent(e);
                return;
        }
        if (onSelect) onSelect(m_cursor.addDays(step));
    }

    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QColor muted(Theme::muted());
        const QColor fg(Theme::fg());
        const QDate today = QDate::currentDate();
        const qreal cw = width() / 7.0;

        p.setFont(monoFont(font(), 9));
        p.setPen(muted);
        for (int c = 0; c < 7; ++c)
            p.drawText(QRectF(c * cw, 0, cw, kHead), Qt::AlignCenter,
                       Lang::locale().dayName(c + 1, QLocale::ShortFormat).toUpper().remove('.'));

        const QFont chipFont = sansFont(font(), m_theme->fs(9.5));
        const int chipH = QFontMetrics(chipFont).height() + 3;

        for (int i = 0; i < 42; ++i) {
            const QDate d = m_start.addDays(i);
            const QRect r = cellRect(i);
            const bool out = d.month() != m_cursor.month();
            const bool sel = d == m_cursor;

            QColor bg = out ? QColor(0, 0, 0, 0) : QColor(255, 255, 255, 8);
            if (i == m_hover) bg = QColor(255, 255, 255, 18);
            if (sel) bg = withAlpha(m_theme->accent, 26);
            const bool drop = m_dragging && i == m_dropCell;
            if (drop) bg = withAlpha(m_theme->accent, 40);
            p.setPen(drop  ? QPen(m_theme->accent, 1.4)
                     : sel ? QPen(withAlpha(m_theme->accent, 130), 1)
                     : d == today ? QPen(QColor(255, 255, 255, 40), 1) : QPen(Qt::NoPen));
            p.setBrush(bg);
            p.drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
            if (sel && keynav::showsFocus(this)) {
                p.setPen(QPen(fg, 1.6));
                p.setBrush(Qt::NoBrush);
                p.drawRoundedRect(QRectF(r).adjusted(-0.5, -0.5, 0.5, 0.5), 8, 8);
            }

            QFont nf = monoFont(font(), 10.5);
            nf.setBold(d == today);
            p.setFont(nf);
            p.setPen(d == today ? m_theme->accent : (out ? withAlpha(muted, 120) : fg));
            p.drawText(r.adjusted(6, 3, -4, 0), Qt::AlignLeft | Qt::AlignTop,
                       QString::number(d.day()));

            // What fits, and "+N more" if not everything does.
            const QList<Item> &items = m_items.at(i);
            const int room = qMax(0, (r.height() - 20) / chipH);
            const bool overflow = items.size() > room;
            const int shown = overflow ? qMax(0, room - 1) : int(items.size());
            p.setFont(chipFont);
            for (int k = 0; k < shown; ++k) {
                const Item &it = items.at(k);
                const QRect chip(r.left() + 3, r.top() + 18 + k * chipH, r.width() - 6, chipH - 2);
                p.setOpacity(m_dragging && sameItem(it, m_dragItem) ? 0.35 : 1.0);
                const QColor color = it.alert ? kRed : it.color;
                p.setPen(Qt::NoPen);
                p.setBrush(withAlpha(color, out ? 26 : 44));
                p.drawRoundedRect(chip, 4, 4);
                p.fillRect(QRect(chip.left(), chip.top(), 2, chip.height()), color);
                QString text = it.allDay ? it.title : hhmm(it.start) + " " + it.title;
                QFont cf = chipFont;
                cf.setStrikeOut(it.done);
                p.setFont(cf);
                p.setPen(out ? muted : fg);
                p.drawText(chip.adjusted(5, 0, -2, 0), Qt::AlignVCenter | Qt::AlignLeft,
                           QFontMetrics(cf).elidedText(text, Qt::ElideRight, chip.width() - 7));
            }
            p.setOpacity(1.0);
            if (overflow && items.size() - shown > 0) {
                p.setFont(monoFont(font(), 9));
                p.setPen(muted);
                p.drawText(QRect(r.left() + 5, r.top() + 18 + shown * chipH, r.width() - 8, chipH),
                           Qt::AlignVCenter | Qt::AlignLeft,
                           L("+%1 más").arg(items.size() - shown));
            }
        }

        // The dragged chip follows the pointer, one day wide.
        if (m_dragging) {
            const Item &it = m_dragItem;
            const int cw = qMax(40, int(width() / 7.0) - 6);
            QRect chip(0, 0, cw, chipH - 2);
            chip.moveCenter(m_dragPos);
            p.setPen(QPen(it.color, 1.2));
            p.setBrush(withAlpha(it.color, 110));
            p.drawRoundedRect(QRectF(chip).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
            p.setFont(chipFont);
            p.setPen(fg);
            const QString text = it.allDay ? it.title : hhmm(it.start) + " " + it.title;
            p.drawText(chip.adjusted(5, 0, -2, 0), Qt::AlignVCenter | Qt::AlignLeft,
                       QFontMetrics(chipFont).elidedText(text, Qt::ElideRight, chip.width() - 7));
        }
    }

private:
    const Theme *m_theme;
    QDate m_cursor;
    QDate m_start;
    QList<QList<Item>> m_items;
    int m_hover = -1;
    bool m_pressed = false;
    bool m_dragging = false;
    Item m_dragItem;
    QPoint m_pressPos;
    QPoint m_dragPos;
    int m_dropCell = -1;
};

/// The small month in the side panel, with a dot on days that have
/// something. Picking a day moves the view without changing it.

class MiniMonth : public QWidget {
public:
    std::function<bool(const QDate &)> busy;
    std::function<void(const QDate &)> onPick;
    std::function<void(int)> onShift;

    explicit MiniMonth(const Theme *theme, QWidget *parent = nullptr)
        : QWidget(parent), m_theme(theme) {
        setFixedHeight(kHead + 16 + 6 * kCell);
        setMouseTracking(true);
    }
    void setCursor(const QDate &d) {
        m_cursor = d;
        m_shown = QDate(d.year(), d.month(), 1);
        update();
    }

protected:
    static constexpr int kHead = 24;
    static constexpr int kCell = 24;

    QRect cellRect(int i) const {
        const qreal cw = width() / 7.0;
        return QRectF((i % 7) * cw, kHead + 16 + (i / 7) * kCell, cw, kCell).toRect();
    }
    QRect arrowRect(bool next) const {
        return next ? QRect(width() - 22, 2, 20, 20) : QRect(width() - 46, 2, 20, 20);
    }

    void mouseReleaseEvent(QMouseEvent *e) override {
        const QPoint at = e->position().toPoint();
        if (arrowRect(false).contains(at)) { if (onShift) onShift(-1); return; }
        if (arrowRect(true).contains(at)) { if (onShift) onShift(1); return; }
        const QDate start = gridStart(m_shown);
        for (int i = 0; i < 42; ++i)
            if (cellRect(i).contains(at) && onPick) onPick(start.addDays(i));
    }
    void mouseMoveEvent(QMouseEvent *e) override {
        const QPoint at = e->position().toPoint();
        bool hot = arrowRect(false).contains(at) || arrowRect(true).contains(at);
        for (int i = 0; i < 42 && !hot; ++i) hot = cellRect(i).contains(at);
        QWidget::setCursor(hot ? Qt::PointingHandCursor : Qt::ArrowCursor);
    }

    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QColor muted(Theme::muted());
        const QColor fg(Theme::fg());
        const QDate today = QDate::currentDate();

        p.setFont(sansFont(font(), 12, true));
        p.setPen(fg);
        QString label = Lang::locale().toString(m_shown, "MMMM yyyy");
        if (!label.isEmpty()) label[0] = label[0].toUpper();
        p.drawText(QRect(2, 0, width() - 50, kHead), Qt::AlignVCenter | Qt::AlignLeft, label);
        paintIcon("chevronLeft", muted, 14).paint(&p, arrowRect(false).adjusted(3, 3, -3, -3));
        paintIcon("chevronRight", muted, 14).paint(&p, arrowRect(true).adjusted(3, 3, -3, -3));

        p.setFont(monoFont(font(), 8.5));
        p.setPen(muted);
        const qreal cw = width() / 7.0;
        for (int c = 0; c < 7; ++c)
            p.drawText(QRectF(c * cw, kHead, cw, 14), Qt::AlignCenter, Lang::weekdayInitial(c));

        const QDate start = gridStart(m_shown);
        for (int i = 0; i < 42; ++i) {
            const QDate d = start.addDays(i);
            const QRect r = cellRect(i).adjusted(2, 1, -2, -1);
            const bool out = d.month() != m_shown.month();
            if (d == m_cursor) {
                p.setPen(QPen(withAlpha(m_theme->accent, 150), 1));
                p.setBrush(withAlpha(m_theme->accent, 40));
                p.drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
            } else if (d == today) {
                p.setPen(QPen(QColor(255, 255, 255, 50), 1));
                p.setBrush(Qt::NoBrush);
                p.drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
            }
            p.setFont(monoFont(font(), 9.5));
            p.setPen(out ? withAlpha(muted, 90) : (d == today ? m_theme->accent : fg));
            p.drawText(r.adjusted(0, 0, 0, -4), Qt::AlignCenter, QString::number(d.day()));
            if (busy && busy(d)) {
                p.setPen(Qt::NoPen);
                p.setBrush(out ? withAlpha(m_theme->accent, 90) : m_theme->accent);
                p.drawEllipse(QPointF(r.center().x() + 0.5, r.bottom() - 3), 1.6, 1.6);
            }
        }
    }

private:
    const Theme *m_theme;
    QDate m_cursor = QDate::currentDate();
    QDate m_shown = QDate(QDate::currentDate().year(), QDate::currentDate().month(), 1);
};

PlannerView::PlannerView(const Theme &theme, QWidget *parent)
    : QWidget(parent), m_theme(theme) {
    setObjectName("planner");
    build();
}

void PlannerView::build() {
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    buildSide();
    root->addWidget(m_side);

    auto *sep = new QFrame;
    sep->setObjectName("calSeparator");
    sep->setFixedWidth(1);
    root->addWidget(sep);
    m_side->setProperty("sep", QVariant::fromValue<QObject *>(sep));

    auto *main = new QWidget;
    auto *col = new QVBoxLayout(main);
    col->setContentsMargins(9, 8, 9, 8);
    col->setSpacing(7);
    buildToolbar(col);

    m_stack = new QStackedWidget;
    col->addWidget(m_stack, 1);
    root->addWidget(main, 1);

    // Day and week
    m_gridPage = new QWidget;
    auto *gl = new QVBoxLayout(m_gridPage);
    gl->setContentsMargins(0, 0, 0, 0);
    gl->setSpacing(2);
    auto *head = new WeekHead(&m_theme);
    head->onPick = [this](const QDate &d) {
        m_cursor = d;
        setView(Day);
    };
    head->source = [this](const QDate &d) { return itemsOn(d); };
    head->onActivate = [this](const Item &it) { activate(it); };
    head->onMove = [this](const Item &it, const QDate &d) { moveItem(it, d, -1, -1); };
    m_weekHead = head;
    gl->addWidget(head);

    m_grid = new TimeGrid(&m_theme);
    m_grid->source = [this](const QDate &d) { return itemsOn(d); };
    m_grid->onActivate = [this](const Item &it) { activate(it); };
    m_grid->onToggle = [this](const Item &it) { toggleDone(it); };
    m_grid->onEmpty = [this](const QDate &d, qreal h) { openEditor(nullptr, d, h); };
    m_grid->onMove = [this](const Item &it, const QDate &d, qreal start, qreal end) {
        moveItem(it, d, start, end);
    };
    m_gridScroll = new QScrollArea;
    m_gridScroll->setWidget(m_grid);
    // setWidget() turns background filling on, and the grid came out in the
    // palette's white instead of showing the translucent panel.
    m_grid->setAutoFillBackground(false);
    m_gridScroll->setWidgetResizable(true);
    m_gridScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_gridScroll->setMinimumHeight(120);
    m_gridScroll->viewport()->setAutoFillBackground(false);
    m_gridScroll->viewport()->setObjectName("scrollViewport");
    m_grid->onReveal = [this](const QRect &r) {
        m_gridScroll->ensureVisible(r.center().x(), r.center().y(), 0, r.height() / 2 + 20);
    };
    gl->addWidget(m_gridScroll, 1);
    m_stack->addWidget(m_gridPage);

    // Month
    m_month = new MonthBoard(&m_theme);
    m_month->source = [this](const QDate &d) { return itemsOn(d); };
    m_month->onPick = [this](const QDate &d) {
        m_cursor = d;
        setView(Day);
    };
    m_month->onSelect = [this](const QDate &d) { goTo(d); };
    m_month->onShift = [this](int dir) { shift(dir); };
    m_month->onMove = [this](const Item &it, const QDate &d) { moveItem(it, d, -1, -1); };
    m_stack->addWidget(m_month);

    buildEditor();
    m_stack->addWidget(m_editor);

    setView(m_view);
}

void PlannerView::buildSide() {
    m_side = new QWidget;
    m_side->setObjectName("plannerSide");
    m_side->setFixedWidth(206);

    auto *host = new QWidget;
    host->setObjectName("listHost");
    auto *col = new QVBoxLayout(host);
    col->setContentsMargins(11, 10, 10, 10);
    col->setSpacing(6);

    m_mini = new MiniMonth(&m_theme);
    m_mini->busy = [this](const QDate &d) { return !itemsOn(d).isEmpty(); };
    m_mini->onPick = [this](const QDate &d) { goTo(d); };
    m_mini->onShift = [this](int dir) {
        const QDate d = m_cursor.addMonths(dir);
        goTo(QDate(d.year(), d.month(), qMin(m_cursor.day(), d.daysInMonth())));
    };
    col->addWidget(m_mini);

    auto *today = new QLabel(L("TAREAS DE HOY"));
    today->setObjectName("setSection");
    today->setProperty("tip", "TAREAS DE HOY");
    col->addWidget(today);
    m_todayList = new QVBoxLayout;
    m_todayList->setSpacing(2);
    col->addLayout(m_todayList);

    auto *cats = new QLabel(L("CATEGORÍAS"));
    cats->setObjectName("setSection");
    cats->setProperty("tip", "CATEGORÍAS");
    col->addWidget(cats);
    m_catList = new QVBoxLayout;
    m_catList->setSpacing(1);
    col->addLayout(m_catList);
    col->addStretch();

    auto *scroll = new QScrollArea;
    scroll->setWidget(host);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->viewport()->setAutoFillBackground(false);
    scroll->viewport()->setObjectName("scrollViewport");
    auto *sl = new QVBoxLayout(m_side);
    sl->setContentsMargins(0, 0, 0, 0);
    sl->addWidget(scroll);
}

void PlannerView::buildToolbar(QVBoxLayout *col) {
    m_bar1 = new QHBoxLayout;
    m_bar1->setSpacing(5);

    auto *prev = navButton("chevronLeft", "Anterior");
    auto *next = navButton("chevronRight", "Siguiente");
    connect(prev, &QToolButton::clicked, this, [this] { shift(-1); });
    connect(next, &QToolButton::clicked, this, [this] { shift(1); });
    m_bar1->addWidget(prev);
    m_bar1->addWidget(next);

    m_todayBtn = new QToolButton;
    m_todayBtn->setObjectName("todayBtn");
    m_todayBtn->setText(L("Hoy"));
    m_todayBtn->setCursor(Qt::PointingHandCursor);
    connect(m_todayBtn, &QToolButton::clicked, this, [this] { goTo(QDate::currentDate()); });
    m_bar1->addWidget(m_todayBtn);

    // The range must not demand its width (see *Card widths*): it is elided.
    m_range = new ElidedLabel(QString(), QColor(Theme::fg()));
    m_range->setObjectName("calMonth");
    m_bar1->addWidget(m_range, 1);

    m_views = new QWidget;
    auto *vl = new QHBoxLayout(m_views);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(3);
    const char *names[] = {"Día", "Semana", "Mes"};
    for (int i = 0; i < 3; ++i) {
        auto *b = segButton(L(names[i]));
        b->setProperty("tip", names[i]);
        connect(b, &QToolButton::clicked, this, [this, i] { setView(View(i)); });
        m_viewButtons << b;
        vl->addWidget(b);
    }
    m_bar1->addWidget(m_views);

    m_newBtn = new QToolButton;
    m_newBtn->setObjectName("segButton");
    m_newBtn->setProperty("chosen", true);
    m_newBtn->setIcon(paintIcon("plus", m_theme.accent, 13));
    m_newBtn->setIconSize(QSize(13, 13));
    m_newBtn->setToolTip(L("Nuevo evento o tarea"));
    m_newBtn->setCursor(Qt::PointingHandCursor);
    connect(m_newBtn, &QToolButton::clicked, this, [this] {
        const qreal hour = m_cursor == QDate::currentDate()
                               ? qMin(22.0, std::ceil(QTime::currentTime().hour() + 1.0))
                               : 10.0;
        openEditor(nullptr, m_cursor, hour);
    });
    m_bar1->addWidget(m_newBtn);
    col->addLayout(m_bar1);

    // Second row, only when narrow: the view buttons move here.
    m_bar2Host = new QWidget;
    m_bar2 = new QHBoxLayout(m_bar2Host);
    m_bar2->setContentsMargins(0, 0, 0, 0);
    m_bar2Host->hide();
    col->addWidget(m_bar2Host);
}

void PlannerView::buildEditor() {
    m_editor = new QWidget;
    auto *outer = new QVBoxLayout(m_editor);
    outer->setContentsMargins(0, 0, 0, 0);

    auto *host = new QWidget;
    host->setObjectName("listHost");
    auto *col = new QVBoxLayout(host);
    col->setContentsMargins(2, 2, 2, 2);
    col->setSpacing(8);

    auto *head = new QHBoxLayout;
    m_editorTitle = new QLabel;
    m_editorTitle->setObjectName("calMonth");
    head->addWidget(m_editorTitle, 1);
    auto *close = new QToolButton;
    close->setIcon(paintIcon("minus", QColor(Theme::muted()), 14));
    close->setToolTip(L("Cerrar sin guardar"));
    close->setCursor(Qt::PointingHandCursor);
    connect(close, &QToolButton::clicked, this, [this] { closeEditor(); });
    head->addWidget(close);
    col->addLayout(head);

    m_fTitle = new QLineEdit;
    m_fTitle->setObjectName("plannerTitleEdit");
    m_fTitle->setPlaceholderText(L("Título"));
    col->addWidget(m_fTitle);

    // Kind: event, task or reminder. The third creates a regular reminder note.
    auto *kinds = new QHBoxLayout;
    kinds->setSpacing(4);
    const char *kindNames[] = {"Evento", "Tarea", "Recordatorio"};
    for (int i = 0; i < 3; ++i) {
        auto *b = segButton(L(kindNames[i]));
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        b->setProperty("tip", kindNames[i]);
        connect(b, &QToolButton::clicked, this, [this, i] { setEditorKind(i); });
        m_kindButtons << b;
        kinds->addWidget(b, 1);
    }
    col->addLayout(kinds);

    // Date and times as text fields, the same format reminders already use; a
    // QDateEdit spinner is unusable in a 300px card.
    auto *when = new QGridLayout;
    when->setHorizontalSpacing(6);
    when->setVerticalSpacing(2);
    auto field = [](const QString &placeholder) {
        auto *e = new QLineEdit;
        e->setObjectName("plannerField");
        e->setPlaceholderText(placeholder);
        e->setMinimumWidth(24);
        return e;
    };
    auto caption = [](const char *text) {
        auto *l = new QLabel(L(text));
        l->setObjectName("meta");
        l->setProperty("tip", text);
        return l;
    };
    m_fDate = field(L("dd/mm/aaaa"));
    m_fStart = field("09:00");
    m_fEnd = field("10:00");
    // An all-day event has no times: in their place goes its last day.
    m_fLastDay = field(L("dd/mm/aaaa"));
    when->addWidget(caption("FECHA"), 0, 0);
    m_fStartBox = caption("INICIO");
    when->addWidget(m_fStartBox, 0, 1);
    m_fEndBox = caption("FIN");
    when->addWidget(m_fEndBox, 0, 2);
    m_fLastDayBox = caption("ÚLTIMO DÍA");
    when->addWidget(m_fLastDayBox, 0, 1, 1, 2);
    when->addWidget(m_fDate, 1, 0);
    when->addWidget(m_fStart, 1, 1);
    when->addWidget(m_fEnd, 1, 2);
    when->addWidget(m_fLastDay, 1, 1, 1, 2);
    when->setColumnStretch(0, 5);
    when->setColumnStretch(1, 3);
    when->setColumnStretch(2, 3);
    col->addLayout(when);

    m_fAllDayBtn = segButton(L("Todo el día"));
    m_fAllDayBtn->setProperty("tip", "Todo el día");
    m_fAllDayBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_fAllDayBtn, &QToolButton::clicked, this, [this] {
        m_fAllDay = !m_fAllDay;
        setEditorKind(m_fKind);
    });
    col->addWidget(m_fAllDayBtn);

    // Repetition and category in two-column grids: one row does not fit at the
    // panel's minimum width.
    auto *repTitle = caption("REPETICIÓN");
    col->addWidget(repTitle);
    m_repeatBox = new QWidget;
    auto *rg = new QGridLayout(m_repeatBox);
    rg->setContentsMargins(0, 0, 0, 0);
    rg->setSpacing(4);
    const char *repeatNames[] = {"No se repite", "Cada día", "Cada semana", "Cada mes",
                                 "Cada año"};
    for (int i = 0; i < 5; ++i) {
        auto *b = segButton(L(repeatNames[i]));
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        b->setProperty("tip", repeatNames[i]);
        connect(b, &QToolButton::clicked, this, [this, i] {
            m_fRepeat = i;
            refreshEditorChoices();
        });
        m_repeatButtons << b;
        rg->addWidget(b, i / 2, i % 2);
    }
    col->addWidget(m_repeatBox);

    col->addWidget(caption("CATEGORÍA"));
    // rebuildCatButtons() fills it: the list changes when the user creates or
    // deletes a category.
    m_catBox = new QWidget;
    auto *cg = new QGridLayout(m_catBox);
    cg->setContentsMargins(0, 0, 0, 0);
    cg->setSpacing(4);
    cg->setColumnStretch(0, 1);
    cg->setColumnStretch(1, 1);
    col->addWidget(m_catBox);

    // Alert lead time.
    col->addWidget(caption("AVISO"));
    auto *alertBox = new QWidget;
    alertBox->setObjectName("plannerAlertBox");
    auto *ag = new QGridLayout(alertBox);
    ag->setContentsMargins(0, 0, 0, 0);
    ag->setSpacing(4);
    for (int i = 0; i < int(std::size(kAlertChoices)); ++i) {
        const AlertChoice &a = kAlertChoices[i];
        auto *b = segButton(L(a.label));
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        b->setProperty("tip", a.label);
        b->setProperty("minutes", a.minutes);
        connect(b, &QToolButton::clicked, this, [this, m = a.minutes] {
            m_fAlert = m;
            refreshEditorChoices();
        });
        m_alertButtons << b;
        ag->addWidget(b, i / 3, i % 3);
    }
    col->addWidget(alertBox);

    m_fDesc = new QTextEdit;
    m_fDesc->setObjectName("plannerDesc");
    m_fDesc->setAcceptRichText(false);
    m_fDesc->setPlaceholderText(L("Descripción"));
    m_fDesc->setTabChangesFocus(true);
    m_fDesc->setFixedHeight(64);
    m_fDesc->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);   // see *Fixed height*
    col->addWidget(m_fDesc);

    m_fError = new QLabel;
    m_fError->setObjectName("plannerError");
    m_fError->setWordWrap(true);
    m_fError->setMinimumWidth(24);
    m_fError->hide();
    col->addWidget(m_fError);

    auto *foot = new QHBoxLayout;
    foot->setSpacing(6);
    m_fDelete = new QToolButton;
    m_fDelete->setObjectName("listDone");
    m_fDelete->setText(L("Eliminar"));
    m_fDelete->setCursor(Qt::PointingHandCursor);
    connect(m_fDelete, &QToolButton::clicked, this, [this] {
        Event *e = m_editing;
        closeEditor();
        if (e) emit eventDeleted(e);
    });
    foot->addWidget(m_fDelete);
    foot->addStretch();
    auto *cancel = segButton(L("Cancelar"));
    cancel->setProperty("tip", "Cancelar");
    connect(cancel, &QToolButton::clicked, this, [this] { closeEditor(); });
    foot->addWidget(cancel);
    auto *save = new QPushButton(L("Guardar"));
    save->setObjectName("plannerSave");
    save->setFocusPolicy(Qt::TabFocus);
    save->setCursor(Qt::PointingHandCursor);
    connect(save, &QPushButton::clicked, this, [this] { saveEditor(); });
    foot->addWidget(save);
    col->addLayout(foot);
    col->addStretch();

    for (QLineEdit *e : {m_fTitle, m_fDate, m_fStart, m_fEnd, m_fLastDay})
        connect(e, &QLineEdit::returnPressed, this, [this] { saveEditor(); });

    auto *scroll = new QScrollArea;
    scroll->setWidget(host);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->viewport()->setAutoFillBackground(false);
    scroll->viewport()->setObjectName("scrollViewport");
    outer->addWidget(scroll);
}

void PlannerView::setSources(const QList<Event *> *events, const QList<Note *> *notes,
                             const QList<Birthday *> *birthdays,
                             const QList<Event::Category *> *categories) {
    m_events = events;
    m_notes = notes;
    m_birthdays = birthdays;
    m_categories = categories;
    refresh();
}

QList<PlannerView::CatInfo> PlannerView::allCategories() const {
    QList<CatInfo> out;
    for (const Event::Category &c : Event::categories())
        out.append({c.id, L(c.label), c.color.isValid() ? c.color : m_theme.accent, nullptr});
    if (m_categories)
        for (Event::Category *c : *m_categories)
            out.append({c->id, c->label.isEmpty() ? L("Sin nombre") : c->label,
                        c->color.isValid() ? c->color : m_theme.accent, c});
    return out;
}

QColor PlannerView::categoryColor(const QString &id) const {
    return Event::categoryColor(id, m_theme.accent, m_categories);
}

const QList<QColor> &PlannerView::categoryPalette() {
    static const QList<QColor> list{
        QColor("#ff9f7a"), QColor("#ffd166"), QColor("#a8e063"), QColor("#6fcf97"),
        QColor("#4ecdc4"), QColor("#56b4f2"), QColor("#7c9cff"), QColor("#b98cff"),
        QColor("#f78fb3"), QColor("#a0a4ad"),
    };
    return list;
}

void PlannerView::setTheme(const Theme &theme) {
    m_theme = theme;
    m_newBtn->setIcon(paintIcon("plus", m_theme.accent, 13));
    refresh();
}

void PlannerView::setHidden(const QStringList &categories) {
    m_hidden = QSet<QString>(categories.begin(), categories.end());
    refresh();
}

void PlannerView::setView(View v) {
    m_view = v;
    if (m_stack->currentWidget() == m_editor) m_editing = nullptr;
    m_stack->setCurrentWidget(v == Month ? static_cast<QWidget *>(m_month) : m_gridPage);
    m_scrolledOnce = false;
    refresh();
    emit viewChanged(int(v));
}

void PlannerView::goTo(const QDate &day) {
    if (!day.isValid()) return;
    m_cursor = day;
    refresh();
}

void PlannerView::shift(int direction) {
    if (m_view == Month) {
        const QDate d = m_cursor.addMonths(direction);
        goTo(QDate(d.year(), d.month(), qMin(m_cursor.day(), d.daysInMonth())));
    } else {
        goTo(m_cursor.addDays(direction * (m_view == Week ? 7 : 1)));
    }
}

QList<Item> PlannerView::itemsOn(const QDate &day) const {
    QList<Item> out;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

    if (m_birthdays && !m_hidden.contains(kBirthdays)) {
        for (Birthday *b : *m_birthdays) {
            if (!b->isValid() || b->dateIn(day.year()) != day) continue;
            Item it;
            it.source = Item::FromBirthday;
            it.birthday = b;
            it.day = day;
            it.allDay = true;
            it.title = L("Cumple de %1").arg(b->name.isEmpty() ? L("Sin nombre") : b->name);
            it.color = kPink;
            it.alert = b->ringing;
            out.append(it);
        }
    }
    if (m_events) {
        for (Event *e : *m_events) {
            if (m_hidden.contains(e->category) || !e->occursOn(day)) continue;
            Item it;
            it.source = Item::FromEvent;
            it.event = e;
            it.day = day;
            it.start = e->startHours();
            it.end = e->endHours();
            it.title = e->title.isEmpty() ? L("Sin título") : e->title;
            it.color = categoryColor(e->category);
            it.allDay = e->allDay;
            it.task = e->kind == Event::Task;
            it.done = e->isDoneOn(day);
            it.alert = e->ringingMs != 0 && e->ringingMs == e->startOn(day).toMSecsSinceEpoch();
            out.append(it);
        }
    }
    if (m_notes && !m_hidden.contains(kReminders)) {
        for (Note *n : *m_notes) {
            if (!n->isScheduled() || !n->occursOn(day)) continue;
            const QDateTime at = n->occurrenceOn(day);
            Item it;
            it.source = Item::FromReminder;
            it.note = n;
            it.day = day;
            it.start = at.time().msecsSinceStartOfDay() / 3600000.0;
            it.end = it.start;
            it.title = n->title.isEmpty() ? L("Sin título") : n->title;
            it.color = kAmber;
            // Past is not overdue: only ringing, or a non-repeating one already past.
            it.alert = n->ringing || (!n->repeats() && n->dueAtMs <= nowMs);
            out.append(it);
        }
    }
    std::sort(out.begin(), out.end(), [](const Item &a, const Item &b) {
        if (a.allDay != b.allDay) return a.allDay;
        return a.start < b.start;
    });
    return out;
}

void PlannerView::refresh() {
    if (!m_stack) return;
    // After the Store reloads (restoring a backup, changing folder) the event
    // being edited may be gone: saving would write to freed memory.
    if (m_editing && m_events && !m_events->contains(m_editing)) {
        m_editing = nullptr;
        m_stack->setCurrentWidget(m_view == Month ? static_cast<QWidget *>(m_month) : m_gridPage);
    }
    rebuildCatButtons();
    refreshToolbar();
    refreshSide();

    if (m_view == Month) {
        m_month->setMonth(m_cursor);
    } else {
        QList<QDate> days;
        if (m_view == Day) {
            days << m_cursor;
        } else {
            const QDate monday = mondayOf(m_cursor);
            for (int i = 0; i < 7; ++i) days << monday.addDays(i);
        }
        // In the day view the toolbar already names the day; the head is only needed
        // for all-day items.
        m_weekHead->setVisible(m_view == Week ||
                               std::any_of(days.begin(), days.end(), [this](const QDate &d) {
                                   const QList<Item> items = itemsOn(d);
                                   return std::any_of(items.begin(), items.end(),
                                                      [](const Item &it) { return it.allDay; });
                               }));
        auto *head = static_cast<WeekHead *>(m_weekHead);
        head->setDays(days, m_cursor);
        m_grid->setDays(days);
        // The first time, scroll to a useful hour: now if today is visible, else
        // 08:00. After that the user's scroll is kept.
        if (!m_scrolledOnce) {
            m_scrolledOnce = true;
            const bool todayShown = days.contains(QDate::currentDate());
            const qreal hour = todayShown ? qMax(0.0, QTime::currentTime().hour() - 1.0) : 8.0;
            QTimer::singleShot(0, this, [this, hour] {
                m_gridScroll->verticalScrollBar()->setValue(m_grid->yFor(hour));
                static_cast<WeekHead *>(m_weekHead)
                    ->setRightInset(m_gridScroll->verticalScrollBar()->isVisible()
                                        ? m_gridScroll->verticalScrollBar()->width() : 0);
            });
        }
    }
}

void PlannerView::refreshToolbar() {
    for (int i = 0; i < m_viewButtons.size(); ++i) setChosen(m_viewButtons.at(i), i == m_view);

    QString label;
    const QLocale loc = Lang::locale();
    if (m_view == Month) {
        label = loc.toString(m_cursor, "MMMM yyyy");
    } else if (m_view == Week) {
        const QDate a = mondayOf(m_cursor), b = a.addDays(6);
        label = a.month() == b.month()
                    ? QString("%1–%2 %3").arg(a.day()).arg(b.day()).arg(loc.toString(b, "MMMM yyyy"))
                    : QString("%1 – %2").arg(loc.toString(a, "d MMM"), loc.toString(b, "d MMM yyyy"));
    } else {
        label = loc.toString(m_cursor, "dddd d MMMM");
    }
    if (!label.isEmpty()) label[0] = label[0].toUpper();
    m_range->setText(label);
    m_range->setToolTip(label);
}

void PlannerView::refreshSide() {
    if (!m_wide) return;   // hidden: filled when shown again
    m_mini->setCursor(m_cursor);

    auto clear = [](QVBoxLayout *l) {
        while (QLayoutItem *it = l->takeAt(0)) {
            if (QWidget *w = it->widget()) {
                w->hide();   // see *Removing rows*
                w->deleteLater();
            }
            delete it;
        }
    };

    // Today's tasks: the only actionable part of the side panel.
    clear(m_todayList);
    const QDate today = QDate::currentDate();
    int tasks = 0;
    for (const Item &it : itemsOn(today)) {
        if (!it.task) continue;
        ++tasks;
        auto *row = new QWidget;
        auto *rl = new QHBoxLayout(row);
        rl->setContentsMargins(0, 1, 0, 1);
        rl->setSpacing(6);
        auto *box = new QCheckBox;
        box->setChecked(it.done);
        box->setFocusPolicy(Qt::TabFocus);
        box->setCursor(Qt::PointingHandCursor);
        connect(box, &QCheckBox::toggled, this, [this, it] { toggleDone(it); });
        rl->addWidget(box);
        auto *title = new ElidedLabel(it.title, QColor(it.done ? Theme::muted() : Theme::fg()));
        title->setObjectName("plannerSideText");
        rl->addWidget(title, 1);
        auto *time = new QLabel(it.allDay ? QString() : hhmm(it.start));
        time->setObjectName("dayTime");
        rl->addWidget(time);
        m_todayList->addWidget(row);
    }
    if (tasks == 0) {
        auto *none = new QLabel(L("Sin tareas para hoy."));
        none->setObjectName("meta");
        none->setWordWrap(true);
        none->setMinimumWidth(24);
        m_todayList->addWidget(none);
    }

    // Category filter, with reminders and birthdays as two more: they may be
    // hidden too, to see only the schedule.
    clear(m_catList);
    // Tagoror's first (reminders and birthdays included) and, under their own
    // title, the followed Google calendars, which are created, edited and deleted
    // differently.
    QList<CatInfo> cats, google;
    for (const CatInfo &cat : allCategories())
        (cat.id.startsWith("gcal:") ? google : cats).append(cat);
    cats.append({kReminders, L("Recordatorios"), kAmber, nullptr});
    cats.append({kBirthdays, L("Cumpleaños"), kPink, nullptr});
    auto addCat = [&](const CatInfo &cat) {
        const QString &id = cat.id;
        auto *box = new QCheckBox(cat.name);
        box->setObjectName("plannerCat");
        box->setChecked(!m_hidden.contains(id));
        box->setFocusPolicy(Qt::TabFocus);
        box->setCursor(Qt::PointingHandCursor);
        box->setStyleSheet(QString("QCheckBox#plannerCat::indicator:checked { background:%1;"
                                   " border:1.4px solid %1; }"
                                   "QCheckBox#plannerCat::indicator { border:1.4px solid %1; }")
                               .arg(cat.color.name()));
        connect(box, &QCheckBox::toggled, this, [this, id = id](bool on) {
            if (on) m_hidden.remove(id);
            else m_hidden.insert(id);
            // Deferred: the refresh rebuilds this very checkbox.
            QTimer::singleShot(0, this, [this] {
                refresh();
                emit hiddenChanged(hidden());
            });
        });
        if (!cat.custom) {
            m_catList->addWidget(box);
            return;
        }
        // Custom ones get a pencil: name, colour and delete.
        auto *row = new QWidget;
        auto *rl = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(2);
        box->setMinimumWidth(24);   // see *Card widths*: the user picks the name
        box->setToolTip(cat.name);
        rl->addWidget(box, 1);
        auto *edit = new QToolButton;
        edit->setObjectName("calNav");
        edit->setIcon(paintIcon("pencil", QColor(Theme::muted()), 11));
        edit->setIconSize(QSize(11, 11));
        edit->setFixedSize(20, 20);
        edit->setCursor(Qt::PointingHandCursor);
        edit->setToolTip(L("Editar categoría"));
        edit->setFocusPolicy(Qt::TabFocus);
        connect(edit, &QToolButton::clicked, this, [this, edit, c = cat.custom] {
            openCategoryEditor(c, edit, false);
        });
        rl->addWidget(edit);
        m_catList->addWidget(row);
    };
    for (const CatInfo &cat : cats) addCat(cat);

    auto *add = new QToolButton;
    add->setObjectName("todayBtn");
    add->setText(L("Nueva categoría"));
    add->setIcon(paintIcon("plus", m_theme.accent, 11));
    add->setIconSize(QSize(11, 11));
    add->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    add->setCursor(Qt::PointingHandCursor);
    add->setFocusPolicy(Qt::TabFocus);
    connect(add, &QToolButton::clicked, this, [this, add] { openCategoryEditor(nullptr, add, false); });
    m_catList->addSpacing(4);
    m_catList->addWidget(add, 0, Qt::AlignLeft);

    if (google.isEmpty()) return;
    auto *head = new QLabel(L("GOOGLE CALENDAR"));
    head->setObjectName("setSection");
    m_catList->addSpacing(8);
    m_catList->addWidget(head);
    for (const CatInfo &cat : google) addCat(cat);
}

void PlannerView::rebuildCatButtons() {
    if (!m_catBox) return;
    const QList<CatInfo> cats = allCategories();
    QString sig = m_theme.accent.name();
    for (const CatInfo &c : cats) sig += '|' + c.id + ':' + c.name + ':' + c.color.name();
    // The chosen category may have been deleted (here or on another machine).
    if (!std::any_of(cats.begin(), cats.end(),
                     [this](const CatInfo &c) { return c.id == m_fCategory; }))
        m_fCategory = Event::kFallbackCategory;
    if (sig == m_catSignature) return;
    m_catSignature = sig;

    auto *grid = static_cast<QGridLayout *>(m_catBox->layout());
    while (QLayoutItem *it = grid->takeAt(0)) {
        if (QWidget *w = it->widget()) {
            w->hide();   // see *Removing rows*
            w->deleteLater();
        }
        delete it;
    }
    m_catButtons.clear();

    // As in the side panel: Tagoror's with their "New…" and, below with a title,
    // the Google calendars.
    int i = 0;
    int row = 0;
    auto addButton = [&](const CatInfo &c, int index) {
        auto *b = segButton(c.name);
        // Both columns get equal width whatever the text asks: a custom name can be
        // long and would eat the other column (see *Card widths*).
        b->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        b->setMinimumWidth(24);
        // Only built-in names are translated: a custom name is the user's.
        if (!c.custom) b->setProperty("tip", Event::categories().at(index).label);
        b->setProperty("category", c.id);
        b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        b->setIconSize(QSize(8, 8));
        b->setToolTip(c.name);
        connect(b, &QToolButton::clicked, this, [this, id = c.id] {
            m_fCategory = id;
            refreshEditorChoices();
        });
        if (c.custom) {
            // Right click edits it without leaving the form, the only way when the panel
            // is narrow and the side panel is hidden.
            b->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(b, &QToolButton::customContextMenuRequested, this,
                    [this, b, cat = c.custom] { openCategoryEditor(cat, b, false); });
        }
        m_catButtons << b;
        grid->addWidget(b, row + i / 2, i % 2);
        ++i;
    };
    QList<CatInfo> google;
    for (const CatInfo &c : cats) {
        if (c.id.startsWith("gcal:")) google << c;
        else addButton(c, i);
    }
    auto *add = segButton(L("Nueva…"));
    add->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    add->setMinimumWidth(24);
    add->setProperty("tip", "Nueva…");
    add->setToolTip(L("Nueva categoría"));
    connect(add, &QToolButton::clicked, this, [this, add] { openCategoryEditor(nullptr, add, true); });
    grid->addWidget(add, row + i / 2, i % 2);
    ++i;

    if (!google.isEmpty()) {
        row += (i + 1) / 2;
        i = 0;
        auto *head = new QLabel(L("GOOGLE CALENDAR"));
        head->setObjectName("meta");
        head->setContentsMargins(0, 4, 0, 0);
        grid->addWidget(head, row++, 0, 1, 2);
        for (const CatInfo &c : google) addButton(c, -1);
    }
    refreshEditorChoices();
}

void PlannerView::openCategoryEditor(Event::Category *c, QWidget *anchor, bool pickForForm) {
    const QList<QColor> &palette = categoryPalette();
    int chosen = 0;
    if (c) {
        chosen = int(palette.indexOf(c->color));
    } else {
        // The first colour nobody uses, so two new categories do not look the same.
        QList<QColor> used;
        for (const CatInfo &info : allCategories()) used << info.color;
        for (int i = 0; i < palette.size(); ++i)
            if (!used.contains(palette.at(i))) {
                chosen = i;
                break;
            }
    }

    auto *menu = new Popup(m_theme, this);
    menu->addHeader(c ? L("Editar categoría") : L("Nueva categoría"));
    // Checked on return: the popup may close after a sync removed the category.
    QPointer<PlannerView> self(this);
    menu->addNameColor(L("Nombre"), c ? c->label : QString(), palette, chosen,
                       [self, c, chosen, pickForForm](const QString &name, int color) {
        if (!self) return;
        const QColor picked = categoryPalette().value(color < 0 ? chosen : color);
        if (!c) {
            if (name.isEmpty()) return;   // nothing is created without a name
            auto *created = new Event::Category;
            created->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            created->label = name;
            created->color = picked.isValid() ? picked : categoryPalette().first();
            if (pickForForm) self->m_fCategory = created->id;
            emit self->categoryCreated(created);
            self->refresh();
            return;
        }
        if (!self->m_categories || !self->m_categories->contains(c)) return;
        // An empty name deletes nothing: that is what Delete is for.
        if (!name.isEmpty()) c->label = name;
        if (color >= 0 && picked.isValid()) c->color = picked;
        emit self->categoryChanged(c);
        self->refresh();
    });
    if (c) {
        menu->addSeparator();
        const bool google = c->id.startsWith("gcal:");
        menu->addItem("trash", google ? L("Dejar de seguir este calendario") : L("Eliminar categoría"),
                      google ? L("Sus eventos se quitan de aquí; en Google siguen")
                             : L("Sus eventos pasan a Otros"),
                      [self, c] {
            if (!self || !self->m_categories || !self->m_categories->contains(c)) return;
            emit self->categoryDeleted(c);
            self->refresh();
        });
    }
    menu->showBelow(anchor);
}

void PlannerView::retranslate() {
    m_todayBtn->setText(L("Hoy"));
    m_newBtn->setToolTip(L("Nuevo evento o tarea"));
    for (auto *w : findChildren<QWidget *>()) {
        const QString tip = w->property("tip").toString();
        if (tip.isEmpty()) continue;
        if (auto *l = qobject_cast<QLabel *>(w)) l->setText(L(tip));
        else if (auto *b = qobject_cast<QToolButton *>(w)) {
            if (b->objectName() == "calNav") b->setToolTip(L(tip));
            else b->setText(L(tip));
        }
    }
    m_fTitle->setPlaceholderText(L("Título"));
    m_fDate->setPlaceholderText(L("dd/mm/aaaa"));
    m_fDesc->setPlaceholderText(L("Descripción"));
    m_fDelete->setText(L("Eliminar"));
    if (auto *save = findChild<QPushButton *>("plannerSave")) save->setText(L("Guardar"));
    refresh();
}

void PlannerView::resizeEvent(QResizeEvent *e) {
    QWidget::resizeEvent(e);
    applyWidth();
}

/// Wide or narrow. Wide: side panel visible and a one-row toolbar. Narrow: no
/// side panel and the view buttons on a second row, since one row does not
/// fit with a readable range (see *Card widths*).
void PlannerView::applyWidth() {
    const bool wide = width() >= kWideFrom;
    if (wide == m_wide) return;   // starts wide, which is how it is built
    m_wide = wide;
    m_side->setVisible(wide);
    if (auto *sep = qobject_cast<QWidget *>(m_side->property("sep").value<QObject *>()))
        sep->setVisible(wide);
    if (wide) {
        m_bar2->removeWidget(m_views);
        m_bar1->insertWidget(m_bar1->indexOf(m_newBtn), m_views);
        m_bar2Host->hide();
    } else {
        m_bar1->removeWidget(m_views);
        m_bar2->addWidget(m_views);
        m_bar2->addStretch();
        m_bar2Host->show();
    }
    if (wide) refreshSide();
}

void PlannerView::activate(const Item &item) {
    switch (item.source) {
        case Item::FromEvent:    openEditor(item.event, item.day, item.start); break;
        case Item::FromReminder: emit noteActivated(item.note); break;
        case Item::FromBirthday: emit birthdayActivated(item.birthday); break;
    }
}

void PlannerView::toggleDone(const Item &item) {
    if (!item.event || item.event->kind != Event::Task) return;
    item.event->setDoneOn(item.day, !item.event->isDoneOn(item.day));
    emit eventChanged(item.event);
    // Deferred: the caller may be the side panel's checkbox being rebuilt.
    QTimer::singleShot(0, this, [this] { refresh(); });
}

/// What the form does when the date or time changes, without opening it. An
/// occurrence of a repeating event moves the whole series, as editing does:
/// start, end and exceptions shift by the same days, so skipped occurrences
/// and done tasks stay the same ones.
void PlannerView::moveItem(const Item &item, const QDate &day, qreal start, qreal end) {
    auto clock = [](qreal hours) {
        const int min = qBound(0, int(std::lround(hours * 60)), 24 * 60);
        return min >= 24 * 60 ? QTime(23, 59) : QTime(0, 0).addSecs(min * 60);
    };
    const qint64 days = item.day.daysTo(day);
    if (item.source == Item::FromEvent) {
        Event *e = item.event;
        // A sync may have removed it during the drag.
        if (!m_events || !m_events->contains(e)) return;
        auto shiftDate = [days](QDate &d) {
            if (d.isValid()) d = d.addDays(days);
        };
        if (days != 0) {
            shiftDate(e->date);
            shiftDate(e->endDate);
            shiftDate(e->until);
            for (QDate &d : e->skip) shiftDate(d);
            for (QDate &d : e->doneOn) shiftDate(d);
        }
        if (start >= 0 && !e->allDay) {
            e->start = clock(start);
            const bool multiDay = e->endDate.isValid() && e->endDate > e->date;
            if (end >= 0 && !multiDay) e->end = clock(end);
        }
        // Another time is another alert, as when saving the form.
        e->firedMs = 0;
        e->ringingMs = 0;
        emit eventChanged(e);
    } else if (item.source == Item::FromReminder) {
        Note *n = item.note;
        if (!m_notes || !m_notes->contains(n)) return;
        const QDateTime from = n->occurrenceOn(item.day);
        const QDateTime to(day, start >= 0 ? clock(start) : from.time());
        // Shifted by the difference rather than set: for a repeating reminder dueAtMs
        // is the next turn, not the one dragged.
        emit reminderMoved(n, n->dueAtMs + from.msecsTo(to));
    } else {
        return;
    }
    // Deferred: the caller is the widget the refresh repaints.
    QTimer::singleShot(0, this, [this] { refresh(); });
}

void PlannerView::openEditor(Event *e, const QDate &day, qreal hour) {
    m_editing = e;
    m_editorTitle->setText(e ? L("Editar") : L("Nuevo evento o tarea"));
    m_fError->hide();
    m_fDelete->setVisible(e != nullptr);
    // An existing event does not turn into a reminder: that would be deleting one
    // and creating something else.
    m_kindButtons.at(2)->setVisible(e == nullptr);

    if (e) {
        m_fTitle->setText(e->title);
        m_fDate->setText(e->date.toString("dd/MM/yyyy"));
        m_fStart->setText(e->start.toString("HH:mm"));
        m_fEnd->setText(e->effectiveEnd().toString("HH:mm"));
        m_fDesc->setPlainText(e->description);
        m_fAlert = e->remind ? e->remindBeforeMin : -1;
        m_fKind = e->kind == Event::Task ? 1 : 0;
        m_fRepeat = int(e->repeat);
        m_fCategory = e->category;
        m_fAllDay = e->allDay;
        m_fLastDay->setText(e->endDate.isValid() && e->endDate > e->date
                                ? e->endDate.toString("dd/MM/yyyy")
                                : QString());
    } else {
        const QTime start = QTime(0, 0).addSecs(int(hour * 3600));
        const QTime end = start.addSecs(3600) > start ? start.addSecs(3600) : QTime(23, 59);
        m_fTitle->clear();
        m_fDate->setText(day.toString("dd/MM/yyyy"));
        m_fStart->setText(start.toString("HH:mm"));
        m_fEnd->setText(end.toString("HH:mm"));
        m_fDesc->clear();
        m_fAlert = -1;
        m_fKind = 0;
        m_fRepeat = 0;
        m_fCategory = "work";
        m_fAllDay = false;
        m_fLastDay->clear();
    }
    rebuildCatButtons();
    setEditorKind(m_fKind);
    m_stack->setCurrentWidget(m_editor);
    QTimer::singleShot(0, m_fTitle, [this] { m_fTitle->setFocus(Qt::OtherFocusReason); });
}

bool PlannerView::closeEditor() {
    if (m_stack->currentWidget() != m_editor) return false;
    m_editing = nullptr;
    m_stack->setCurrentWidget(m_view == Month ? static_cast<QWidget *>(m_month) : m_gridPage);
    refresh();
    return true;
}

void PlannerView::setEditorKind(int kind) {
    m_fKind = kind;
    // A reminder is an instant: no end, no category and not this repetition (its
    // own is chosen on the card).
    const bool reminder = kind == 2;
    const bool allDay = m_fAllDay && !reminder;
    // A single occurrence of a Google series does not repeat on its own.
    const bool instance = m_editing && m_editing->gcalInstance();
    m_fStart->setVisible(!allDay);
    m_fStartBox->setVisible(!allDay);
    m_fEnd->setVisible(!reminder && !allDay);
    m_fEndBox->setVisible(!reminder && !allDay);
    m_fLastDay->setVisible(allDay);
    m_fLastDayBox->setVisible(allDay);
    m_fAllDayBtn->setVisible(!reminder);
    m_repeatBox->setVisible(!reminder && !instance);
    m_catBox->setVisible(!reminder);
    if (auto *alerts = findChild<QWidget *>("plannerAlertBox")) alerts->setVisible(!reminder);
    for (QWidget *w : findChildren<QLabel *>())
        if (const QString tip = w->property("tip").toString();
            tip == QStringLiteral("CATEGORÍA") || tip == QStringLiteral("AVISO"))
            w->setVisible(!reminder);
        else if (tip == QStringLiteral("REPETICIÓN"))
            w->setVisible(!reminder && !instance);
    refreshEditorChoices();
}

void PlannerView::refreshEditorChoices() {
    for (int i = 0; i < m_kindButtons.size(); ++i) setChosen(m_kindButtons.at(i), i == m_fKind);
    setChosen(m_fAllDayBtn, m_fAllDay);
    for (int i = 0; i < m_repeatButtons.size(); ++i) setChosen(m_repeatButtons.at(i), i == m_fRepeat);
    for (QToolButton *b : m_alertButtons) setChosen(b, b->property("minutes").toInt() == m_fAlert);
    for (QToolButton *b : m_catButtons) {
        const QString id = b->property("category").toString();
        setChosen(b, id == m_fCategory);
        b->setIcon(colorDot(categoryColor(id)));
    }
}

void PlannerView::saveEditor() {
    const QDate date = QDate::fromString(m_fDate->text().trimmed(), "d/M/yyyy");
    const bool allDay = m_fAllDay && m_fKind != 2;
    // All-day counts from 00:00: that is where the alert counts from.
    const QTime start = allDay ? QTime(0, 0) : QTime::fromString(m_fStart->text().trimmed(), "H:mm");
    const QTime end = allDay ? QTime(23, 59) : QTime::fromString(m_fEnd->text().trimmed(), "H:mm");
    const QString lastText = m_fLastDay->text().trimmed();
    const QDate lastDay = QDate::fromString(lastText, "d/M/yyyy");
    auto fail = [this](const QString &why, QWidget *field) {
        m_fError->setText(why);
        m_fError->show();
        field->setFocus(Qt::OtherFocusReason);
    };
    if (!date.isValid()) return fail(L("La fecha tiene que ser dd/mm/aaaa."), m_fDate);
    if (!start.isValid()) return fail(L("La hora de inicio tiene que ser HH:mm."), m_fStart);

    const QString title = m_fTitle->text().trimmed();
    if (m_fKind == 2) {
        closeEditor();
        emit reminderCreated(title, QDateTime(date, start));
        goTo(date);
        return;
    }
    if (allDay && !lastText.isEmpty() && (!lastDay.isValid() || lastDay < date))
        return fail(L("El último día tiene que ser dd/mm/aaaa, y no antes de la fecha."), m_fLastDay);
    // A timed event from Google spanning days keeps its length: it moves whole
    // with its date, and its end is on the last day.
    int span = 0;
    if (!allDay && m_editing && !m_editing->allDay && m_editing->endDate.isValid())
        span = int(m_editing->date.daysTo(m_editing->endDate));
    if (!allDay && span <= 0 && (!end.isValid() || end <= start))
        return fail(L("El fin tiene que ser una hora posterior al inicio."), m_fEnd);
    if (!end.isValid()) return fail(L("El fin tiene que ser una hora posterior al inicio."), m_fEnd);

    Event *e = m_editing ? m_editing : new Event;
    const bool isNew = m_editing == nullptr;
    const bool moved = e->date != date || e->start != start;
    e->title = title;
    e->kind = m_fKind == 1 ? Event::Task : Event::Meeting;
    e->date = date;
    e->start = start;
    e->end = end;
    e->allDay = allDay;
    if (allDay) e->endDate = lastDay.isValid() && lastDay > date ? lastDay : QDate();
    else e->endDate = span > 0 ? date.addDays(span) : QDate();
    e->repeat = e->gcalInstance() ? Event::Once : Event::Repeat(m_fRepeat);
    e->category = m_fCategory;
    e->remind = m_fAlert >= 0;
    // Without an alert the previous lead is kept, so re-enabling it remembers it.
    if (m_fAlert >= 0) e->remindBeforeMin = m_fAlert;
    e->description = m_fDesc->toPlainText();
    // Another time is another alert: the one that rang belonged to the old time.
    if (moved) e->firedMs = 0;

    closeEditor();
    if (isNew) emit eventCreated(e);
    else emit eventChanged(e);
    goTo(date);
}
