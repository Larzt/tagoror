#include "ui/planner.hpp"

#include "core/lang.hpp"
#include "ui/elidedlabel.hpp"
#include "ui/keynav.hpp"

#include <cmath>

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

// Por debajo de esto el lateral no cabe al lado de una semana legible.
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

// El lunes de la semana de ese día: la rejilla empieza en lunes en los dos
// idiomas, igual que la de antes.
QDate mondayOf(const QDate &d) { return d.addDays(1 - d.dayOfWeek()); }

// Primer día de la rejilla de 6x7 de un mes: el lunes de la semana del día 1.
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

}  // namespace

// ===========================================================================
// TimeGrid: día o semana, con las horas en vertical.
//
// Se pinta entero en un widget, como el mes de antes: los bloques no tienen
// estado propio que merezca un widget cada uno, y así el solapamiento, la
// línea de "ahora" y la franja de día entero se colocan con la misma cuenta.
// Para el teclado lleva su propio índice de foco (ver focusNextPrevChild).
// ===========================================================================

class TimeGrid : public QWidget {
public:
    static constexpr int kGutter = 46;
    static constexpr int kHour = 42;

    std::function<QList<Item>(const QDate &)> source;
    std::function<void(const Item &)> onActivate;
    std::function<void(const Item &)> onToggle;
    std::function<void(const QDate &, qreal)> onEmpty;
    std::function<void(const QRect &)> onReveal;   // que el scroll lo enseñe

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

    // Los de día entero no van aquí sino en la cabecera (WeekHead): arriba
    // del todo de la rejilla quedaban fuera de la vista en cuanto se bajaba
    // a las ocho de la mañana, que es donde se abre.
    int yFor(qreal hours) const { return int(hours * kHour); }

    QSize sizeHint() const override { return {kGutter + 7 * 40, 24 * kHour + 1}; }
    QSize minimumSizeHint() const override {
        return {kGutter + int(m_days.size()) * 24, 24 * kHour + 1};
    }

protected:
    void paintEvent(QPaintEvent *) override;

    void mouseMoveEvent(QMouseEvent *e) override {
        const int hit = blockAt(e->position().toPoint());
        if (hit != m_hover) {
            m_hover = hit;
            update();
        }
        setCursor(hit >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
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
        if (const int hit = blockAt(at); hit >= 0) {
            const Block &b = m_blocks.at(hit);
            if (b.item.task && checkRect(b.rect).adjusted(-4, -4, 4, 4).contains(at)) {
                if (onToggle) onToggle(b.item);
            } else if (onActivate) {
                onActivate(b.item);
            }
            return;
        }
        // Un hueco: un evento nuevo a esa hora, redondeada a la media hora.
        const int col = columnAt(at.x());
        if (col < 0) return;
        const qreal hours = qBound<qreal>(0, at.y() / qreal(kHour), 23.5);
        if (onEmpty) onEmpty(m_days.at(col), std::floor(hours * 2) / 2.0);
    }

    // Tab recorre los bloques uno a uno antes de salir de la rejilla. Es lo
    // que hace alcanzables con el teclado unos bloques que no son widgets.
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

    // Los que se pisan se reparten el ancho de la columna. Se agrupan por
    // racimos (cadenas de solapes) y dentro de cada uno cada bloque va a la
    // primera subcolumna libre: así tres eventos donde solo dos coinciden a la
    // vez usan dos carriles, no tres.
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

    // Un recordatorio es un instante: se le da un alto mínimo para que se vea
    // y se pueda pulsar, sin que cuente como media hora ocupada de verdad.
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
};

void TimeGrid::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor muted(Theme::muted());
    const QColor fg(Theme::fg());
    const QColor faint(255, 255, 255, 14);
    const int w = columnWidth();

    // Horas: la etiqueta a la izquierda y una línea tenue por la rejilla.
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
    // Separadores de día.
    for (int c = 0; c <= m_days.size(); ++c) {
        p.setPen(faint);
        p.drawLine(kGutter + c * w, 0, kGutter + c * w, height());
    }
    // Hoy, un poco más claro, para encontrarlo en una semana.
    const QDate today = QDate::currentDate();
    for (int c = 0; c < m_days.size(); ++c)
        if (m_days.at(c) == today && m_days.size() > 1)
            p.fillRect(QRect(kGutter + c * w + 1, 0, w - 1, height()),
                       withAlpha(m_theme->accent, 10));

    // Bloques.
    for (int i = 0; i < m_blocks.size(); ++i) {
        Block &b = m_blocks[i];
        b.rect = blockRect(b);
        const Item &it = b.item;
        const QRect r = b.rect;
        const bool hot = i == m_hover;
        const QColor color = it.alert ? kRed : it.color;

        QPainterPath path;
        path.addRoundedRect(QRectF(r), 7, 7);
        p.fillPath(path, withAlpha(color, hot ? 78 : (it.done ? 26 : 50)));
        QPen border(withAlpha(color, hot ? 200 : 120), 1);
        if (it.source == Item::FromReminder) border.setStyle(Qt::DashLine);
        p.setPen(border);
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 7, 7);
        // Barra del color a la izquierda: se lee aunque el bloque sea bajito.
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

    // La hora de ahora, en rojo, sobre la columna de hoy.
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

    // Un día sin nada: se dice, en vez de dejar solo una rejilla vacía.
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

// ===========================================================================
// Cabecera de la semana: el día y su número encima de cada columna. Va fuera
// del desplazamiento para no perderse al bajar por las horas.
// ===========================================================================

class WeekHead : public QWidget {
public:
    static constexpr int kDays = 38;
    static constexpr int kBand = 24;

    std::function<void(const QDate &)> onPick;
    std::function<QList<Item>(const QDate &)> source;
    std::function<void(const Item &)> onActivate;

    WeekHead(const Theme *theme, QWidget *parent = nullptr) : QWidget(parent), m_theme(theme) {
        setFixedHeight(kDays);
        setMouseTracking(true);
    }
    // Con el día entero (los cumpleaños) en una franja debajo de los números:
    // aquí fuera del desplazamiento se ven siempre.
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
    // La barra de desplazamiento de la rejilla se come ancho por la derecha:
    // las columnas de aquí tienen que medir lo mismo que las de allí.
    void setRightInset(int px) {
        m_inset = px;
        update();
    }

protected:
    void mouseReleaseEvent(QMouseEvent *e) override {
        const QPoint at = e->position().toPoint();
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
            p.setPen(Qt::NoPen);
            p.setBrush(withAlpha(it.alert ? kRed : it.color, 60));
            p.drawRoundedRect(r, 5, 5);
            p.fillRect(QRect(r.left(), r.top() + 3, 2, r.height() - 6), it.color);
            p.setPen(QColor(Theme::fg()));
            p.drawText(r.adjusted(7, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft,
                       p.fontMetrics().elidedText(it.title, Qt::ElideRight, r.width() - 11));
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
};

// ===========================================================================
// MonthBoard: el mes en grande, con lo que cae cada día escrito dentro.
// ===========================================================================

class MonthBoard : public QWidget {
public:
    std::function<QList<Item>(const QDate &)> source;
    std::function<void(const QDate &)> onPick;      // abre ese día
    std::function<void(const QDate &)> onSelect;    // solo lo marca
    std::function<void(int)> onShift;               // mes anterior/siguiente

    explicit MonthBoard(const Theme *theme, QWidget *parent = nullptr)
        : QWidget(parent), m_theme(theme) {
        setMouseTracking(true);
        setFocusPolicy(Qt::TabFocus);
        // Filas bajas en mínimo: el panel no puede pedir más alto que la lista
        // de notas solo por estar en el mes (ver *Window behavior*).
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

    void mouseMoveEvent(QMouseEvent *e) override {
        const int c = cellAt(e->position().toPoint());
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
        if (c >= 0 && onPick) onPick(m_start.addDays(c));
    }
    void wheelEvent(QWheelEvent *e) override {
        // Rueda: de mes en mes, como el calendario de antes.
        if (onShift && e->angleDelta().y() != 0) onShift(e->angleDelta().y() > 0 ? -1 : 1);
    }
    void focusInEvent(QFocusEvent *e) override { QWidget::focusInEvent(e); update(); }
    void focusOutEvent(QFocusEvent *e) override { QWidget::focusOutEvent(e); update(); }

    // Flechas: moverse por los días; Intro: abrirlo; Re Pág / Av Pág: de mes.
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
            p.setPen(sel ? QPen(withAlpha(m_theme->accent, 130), 1)
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

            // Lo que cabe, y "+N más" si no cabe todo.
            const QList<Item> &items = m_items.at(i);
            const int room = qMax(0, (r.height() - 20) / chipH);
            const bool overflow = items.size() > room;
            const int shown = overflow ? qMax(0, room - 1) : int(items.size());
            p.setFont(chipFont);
            for (int k = 0; k < shown; ++k) {
                const Item &it = items.at(k);
                const QRect chip(r.left() + 3, r.top() + 18 + k * chipH, r.width() - 6, chipH - 2);
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
            if (overflow && items.size() - shown > 0) {
                p.setFont(monoFont(font(), 9));
                p.setPen(muted);
                p.drawText(QRect(r.left() + 5, r.top() + 18 + shown * chipH, r.width() - 8, chipH),
                           Qt::AlignVCenter | Qt::AlignLeft,
                           L("+%1 más").arg(items.size() - shown));
            }
        }
    }

private:
    const Theme *m_theme;
    QDate m_cursor;
    QDate m_start;
    QList<QList<Item>> m_items;
    int m_hover = -1;
};

// ===========================================================================
// MiniMonth: el mes en pequeño del lateral, con un punto en los días que
// tienen algo. Elegir un día mueve la vista sin cambiar de vista.
// ===========================================================================

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

// ===========================================================================

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

    // --- día y semana ---
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
    m_weekHead = head;
    gl->addWidget(head);

    m_grid = new TimeGrid(&m_theme);
    m_grid->source = [this](const QDate &d) { return itemsOn(d); };
    m_grid->onActivate = [this](const Item &it) { activate(it); };
    m_grid->onToggle = [this](const Item &it) { toggleDone(it); };
    m_grid->onEmpty = [this](const QDate &d, qreal h) { openEditor(nullptr, d, h); };
    m_gridScroll = new QScrollArea;
    m_gridScroll->setWidget(m_grid);
    // setWidget() le enciende el relleno de fondo, y la rejilla salía en el
    // blanco de la paleta en vez de dejar ver el panel translúcido.
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

    // --- mes ---
    m_month = new MonthBoard(&m_theme);
    m_month->source = [this](const QDate &d) { return itemsOn(d); };
    m_month->onPick = [this](const QDate &d) {
        m_cursor = d;
        setView(Day);
    };
    m_month->onSelect = [this](const QDate &d) { goTo(d); };
    m_month->onShift = [this](int dir) { shift(dir); };
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

    // El rango no puede pedir su ancho (ver *Card widths*): se recorta.
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

    // Segunda fila, solo en estrecho: la de las vistas se muda aquí.
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

    // Qué es: evento, tarea o recordatorio. El tercero crea una nota de las
    // de siempre, que es lo que hacía el calendario de antes al pulsar un día.
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

    // Fecha y horas en campos de texto: el mismo formato que ya se escribe en
    // los recordatorios, y sin la rueda de un QDateEdit que en una tarjeta de
    // 300 px no se deja manejar.
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
    when->addWidget(caption("FECHA"), 0, 0);
    when->addWidget(caption("INICIO"), 0, 1);
    m_fEndBox = caption("FIN");
    when->addWidget(m_fEndBox, 0, 2);
    when->addWidget(m_fDate, 1, 0);
    when->addWidget(m_fStart, 1, 1);
    when->addWidget(m_fEnd, 1, 2);
    when->setColumnStretch(0, 5);
    when->setColumnStretch(1, 3);
    when->setColumnStretch(2, 3);
    col->addLayout(when);

    // Repetición y categoría en rejillas de dos: en una fila no caben las
    // cuatro con sus nombres en el ancho mínimo del panel.
    auto *repTitle = caption("REPETICIÓN");
    col->addWidget(repTitle);
    m_repeatBox = new QWidget;
    auto *rg = new QGridLayout(m_repeatBox);
    rg->setContentsMargins(0, 0, 0, 0);
    rg->setSpacing(4);
    const char *repeatNames[] = {"No se repite", "Cada día", "Cada semana", "Cada mes"};
    for (int i = 0; i < 4; ++i) {
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
    m_catBox = new QWidget;
    auto *cg = new QGridLayout(m_catBox);
    cg->setContentsMargins(0, 0, 0, 0);
    cg->setSpacing(4);
    for (int i = 0; i < Event::categories().size(); ++i) {
        const Event::Category &c = Event::categories().at(i);
        auto *b = segButton(L(c.label));
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        b->setProperty("tip", c.label);
        b->setProperty("category", c.id);
        b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        b->setIconSize(QSize(8, 8));
        connect(b, &QToolButton::clicked, this, [this, id = c.id] {
            m_fCategory = id;
            refreshEditorChoices();
        });
        m_catButtons << b;
        cg->addWidget(b, i / 2, i % 2);
    }
    col->addWidget(m_catBox);

    m_fRemind = new QCheckBox(L("Avisar 10 min antes"));
    m_fRemind->setFocusPolicy(Qt::TabFocus);
    col->addWidget(m_fRemind);

    m_fDesc = new QTextEdit;
    m_fDesc->setObjectName("plannerDesc");
    m_fDesc->setAcceptRichText(false);
    m_fDesc->setPlaceholderText(L("Descripción"));
    m_fDesc->setTabChangesFocus(true);
    m_fDesc->setFixedHeight(64);
    m_fDesc->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);   // ver *Fixed height*
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

    for (QLineEdit *e : {m_fTitle, m_fDate, m_fStart, m_fEnd})
        connect(e, &QLineEdit::returnPressed, this, [this] { saveEditor(); });

    auto *scroll = new QScrollArea;
    scroll->setWidget(host);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->viewport()->setAutoFillBackground(false);
    scroll->viewport()->setObjectName("scrollViewport");
    outer->addWidget(scroll);
}

// ---------------------------------------------------------------------------

void PlannerView::setSources(const QList<Event *> *events, const QList<Note *> *notes,
                             const QList<Birthday *> *birthdays) {
    m_events = events;
    m_notes = notes;
    m_birthdays = birthdays;
    refresh();
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
            it.color = Event::categoryColor(e->category, m_theme.accent);
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
            // Pasado no es vencido: solo el que suena, o uno suelto que ya
            // pasó sin repetirse (la misma regla que el calendario de antes).
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
    // Tras recargar el Store (restaurar una copia, cambiar de carpeta) el que
    // se estaba editando puede no existir ya: guardarlo escribiría en memoria
    // liberada. Se cierra el formulario sin más.
    if (m_editing && m_events && !m_events->contains(m_editing)) {
        m_editing = nullptr;
        m_stack->setCurrentWidget(m_view == Month ? static_cast<QWidget *>(m_month) : m_gridPage);
    }
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
        // En el día el nombre ya lo dice la barra; la cabecera solo hace falta
        // si hay algo de día entero que enseñar.
        m_weekHead->setVisible(m_view == Week ||
                               std::any_of(days.begin(), days.end(), [this](const QDate &d) {
                                   const QList<Item> items = itemsOn(d);
                                   return std::any_of(items.begin(), items.end(),
                                                      [](const Item &it) { return it.allDay; });
                               }));
        auto *head = static_cast<WeekHead *>(m_weekHead);
        head->setDays(days, m_cursor);
        m_grid->setDays(days);
        // La primera vez, a una hora útil: la de ahora si hoy está a la vista,
        // si no las ocho. Después se respeta lo que haya desplazado el usuario.
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
    if (!m_wide) return;   // escondido: se rellena al volver a verse
    m_mini->setCursor(m_cursor);

    auto clear = [](QVBoxLayout *l) {
        while (QLayoutItem *it = l->takeAt(0)) {
            if (QWidget *w = it->widget()) {
                w->hide();   // ver *Removing rows*
                w->deleteLater();
            }
            delete it;
        }
    };

    // Las tareas de hoy: lo único del lateral sobre lo que se actúa.
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
        auto *time = new QLabel(hhmm(it.start));
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

    // Filtro de categorías, con los recordatorios y los cumpleaños como dos
    // más: también se quieren esconder para ver solo el horario.
    clear(m_catList);
    QList<QPair<QString, QPair<QString, QColor>>> cats;
    for (const Event::Category &c : Event::categories())
        cats.append({c.id, {L(c.label), c.color.isValid() ? c.color : m_theme.accent}});
    cats.append({kReminders, {L("Recordatorios"), kAmber}});
    cats.append({kBirthdays, {L("Cumpleaños"), kPink}});
    for (const auto &[id, info] : cats) {
        auto *box = new QCheckBox(info.first);
        box->setObjectName("plannerCat");
        box->setChecked(!m_hidden.contains(id));
        box->setFocusPolicy(Qt::TabFocus);
        box->setCursor(Qt::PointingHandCursor);
        box->setStyleSheet(QString("QCheckBox#plannerCat::indicator:checked { background:%1;"
                                   " border:1.4px solid %1; }"
                                   "QCheckBox#plannerCat::indicator { border:1.4px solid %1; }")
                               .arg(info.second.name()));
        connect(box, &QCheckBox::toggled, this, [this, id = id](bool on) {
            if (on) m_hidden.remove(id);
            else m_hidden.insert(id);
            // Diferido: el refresco rehace esta misma casilla.
            QTimer::singleShot(0, this, [this] {
                refresh();
                emit hiddenChanged(hidden());
            });
        });
        m_catList->addWidget(box);
    }
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
    m_fRemind->setText(L("Avisar 10 min antes"));
    m_fDelete->setText(L("Eliminar"));
    if (auto *save = findChild<QPushButton *>("plannerSave")) save->setText(L("Guardar"));
    refresh();
}

void PlannerView::resizeEvent(QResizeEvent *e) {
    QWidget::resizeEvent(e);
    applyWidth();
}

// Ancho o estrecho. En ancho, el lateral a la vista y la barra en una fila; en
// estrecho, sin lateral y con las vistas en una segunda fila, porque en una
// sola no caben con el rango legible (ver *Card widths*).
void PlannerView::applyWidth() {
    const bool wide = width() >= kWideFrom;
    if (wide == m_wide) return;   // arranca en ancho, que es como se construye
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

// ---------------------------------------------------------------------------

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
    // Diferido: quien llama puede ser la casilla del lateral que se rehace.
    QTimer::singleShot(0, this, [this] { refresh(); });
}

void PlannerView::openEditor(Event *e, const QDate &day, qreal hour) {
    m_editing = e;
    m_editorTitle->setText(e ? L("Editar") : L("Nuevo evento o tarea"));
    m_fError->hide();
    m_fDelete->setVisible(e != nullptr);
    // Un evento que ya existe no se convierte en recordatorio: eso sería
    // borrar uno y crear otra cosa, y para eso está Eliminar.
    m_kindButtons.at(2)->setVisible(e == nullptr);

    if (e) {
        m_fTitle->setText(e->title);
        m_fDate->setText(e->date.toString("dd/MM/yyyy"));
        m_fStart->setText(e->start.toString("HH:mm"));
        m_fEnd->setText(e->effectiveEnd().toString("HH:mm"));
        m_fDesc->setPlainText(e->description);
        m_fRemind->setChecked(e->remind);
        m_fKind = e->kind == Event::Task ? 1 : 0;
        m_fRepeat = int(e->repeat);
        m_fCategory = e->category;
    } else {
        const QTime start = QTime(0, 0).addSecs(int(hour * 3600));
        const QTime end = start.addSecs(3600) > start ? start.addSecs(3600) : QTime(23, 59);
        m_fTitle->clear();
        m_fDate->setText(day.toString("dd/MM/yyyy"));
        m_fStart->setText(start.toString("HH:mm"));
        m_fEnd->setText(end.toString("HH:mm"));
        m_fDesc->clear();
        m_fRemind->setChecked(false);
        m_fKind = 0;
        m_fRepeat = 0;
        m_fCategory = "work";
    }
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
    // Un recordatorio es un instante: sin fin, sin categoría y sin la
    // repetición de aquí (la suya se elige en la tarjeta, como siempre).
    const bool reminder = kind == 2;
    m_fEnd->setVisible(!reminder);
    m_fEndBox->setVisible(!reminder);
    m_repeatBox->setVisible(!reminder);
    m_catBox->setVisible(!reminder);
    m_fRemind->setVisible(!reminder);
    for (QWidget *w : findChildren<QLabel *>())
        if (const QString tip = w->property("tip").toString();
            tip == QStringLiteral("REPETICIÓN") || tip == QStringLiteral("CATEGORÍA"))
            w->setVisible(!reminder);
    refreshEditorChoices();
}

void PlannerView::refreshEditorChoices() {
    for (int i = 0; i < m_kindButtons.size(); ++i) setChosen(m_kindButtons.at(i), i == m_fKind);
    for (int i = 0; i < m_repeatButtons.size(); ++i) setChosen(m_repeatButtons.at(i), i == m_fRepeat);
    for (QToolButton *b : m_catButtons) {
        const QString id = b->property("category").toString();
        setChosen(b, id == m_fCategory);
        QPixmap dot(16, 16);
        dot.fill(Qt::transparent);
        QPainter p(&dot);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(Event::categoryColor(id, m_theme.accent));
        p.drawRoundedRect(QRectF(2, 2, 12, 12), 3.5, 3.5);
        p.end();
        b->setIcon(QIcon(dot));
    }
}

void PlannerView::saveEditor() {
    const QDate date = QDate::fromString(m_fDate->text().trimmed(), "d/M/yyyy");
    const QTime start = QTime::fromString(m_fStart->text().trimmed(), "H:mm");
    const QTime end = QTime::fromString(m_fEnd->text().trimmed(), "H:mm");
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
    if (!end.isValid() || end <= start)
        return fail(L("El fin tiene que ser una hora posterior al inicio."), m_fEnd);

    Event *e = m_editing ? m_editing : new Event;
    const bool isNew = m_editing == nullptr;
    const bool moved = e->date != date || e->start != start;
    e->title = title;
    e->kind = m_fKind == 1 ? Event::Task : Event::Meeting;
    e->date = date;
    e->start = start;
    e->end = end;
    e->repeat = Event::Repeat(m_fRepeat);
    e->category = m_fCategory;
    e->remind = m_fRemind->isChecked();
    e->description = m_fDesc->toPlainText();
    // Otra hora es otro aviso: el que ya sonó era el de la hora de antes.
    if (moved) e->firedMs = 0;

    closeEditor();
    if (isNew) emit eventCreated(e);
    else emit eventChanged(e);
    goTo(date);
}
