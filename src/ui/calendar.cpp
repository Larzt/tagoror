#include "ui/calendar.hpp"

#include "core/lang.hpp"
#include "ui/elidedlabel.hpp"

#include <QDateTime>
#include <QEnterEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <functional>

namespace {

constexpr int kWeekHeaderH = 18;   // franja de iniciales de la semana, sobre la rejilla
constexpr int kRows = 6;           // semanas visibles: 6 cubren cualquier mes
constexpr int kCols = 7;
constexpr int kMaxDots = 3;        // más avisos en un día no caben; se cuentan igual
constexpr qreal kNumberH = 14.0;   // alto reservado al número del día
constexpr qreal kDotBand = 6.0;    // franja de los puntos, justo debajo
// Por debajo de este alto la rejilla y la lista del día no caben las dos: la
// lista se pliega sola y deja la vista del mes entera (ver resizeEvent).
//
// El umbral va por encima del mínimo que pide la vista entera (unos 320 px con
// la lista dentro): si fuera igual o menor, no se alcanzaría nunca -- el layout
// no puede encoger el widget por debajo de su propio mínimo, así que ese alto
// no llega a darse y el plegado no saltaría jamás. Plegada, la lista deja de
// contar para ese mínimo y la ventana puede seguir bajando.
constexpr int kTightHeight = 360;

QString monthTitle(const QDate &month) {
    QString s = Lang::locale().toString(month, "MMMM yyyy");
    if (!s.isEmpty()) s[0] = s[0].toUpper();
    return s;
}

QString dayTitle(const QDate &d) {
    return Lang::locale().toString(d, "ddd d MMM").toUpper();
}

// Fila de la lista del día: icono, hora y título. Se pinta con widgets (y no
// a mano) para que la hoja de estilos del panel la coloree como al resto.
class DayRow : public QWidget {
public:
    explicit DayRow(QWidget *parent = nullptr) : QWidget(parent) {
        setObjectName("dayRow");
        // Sin esto un QWidget liso ignora el fondo de la hoja de estilos, así
        // que no habría realce al pasar por encima.
        setAttribute(Qt::WA_StyledBackground, true);
        setAttribute(Qt::WA_Hover, true);
        setCursor(Qt::PointingHandCursor);
    }

    // Pública para poder asignarla después de construir la fila: el popup de
    // la hora se ancla a la propia fila y hasta entonces no existe.
    std::function<void()> click;

protected:
    void mouseReleaseEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton && rect().contains(e->position().toPoint()) && click)
            click();
    }
};

}  // namespace

// ---------------------------------------------------------------------------

// Rejilla del mes. Se pinta entera en un solo widget en vez de con 42 celdas:
// no hay estado que guardar por día y así el marcado de avisos es un dibujo
// más, no un widget más.
class MonthGrid : public QWidget {
public:
    struct Mark {
        int count = 0;
        // Estado de cada punto, en orden de hora: un día con uno pasado y
        // otro por llegar no puede pintarse entero de rojo.
        QList<bool> alerts;
    };

    explicit MonthGrid(QWidget *parent = nullptr) : QWidget(parent) {
        setObjectName("monthGrid");
        setMinimumHeight(160);
        setMouseTracking(true);
        setCursor(Qt::PointingHandCursor);
    }

    void setTheme(const Theme &theme) { m_theme = theme; update(); }
    void setMarks(const QHash<QDate, Mark> &marks) { m_marks = marks; update(); }

    void setMonth(const QDate &firstDay) {
        m_month = firstDay;
        // El mes empieza en la columna de su día de la semana; lo anterior
        // se rellena con la cola del mes pasado.
        m_first = firstDay.addDays(-(firstDay.dayOfWeek() - 1));
        update();
    }

    void setSelected(const QDate &d) { m_selected = d; update(); }

    std::function<void(const QDate &)> onPick;
    std::function<void(int)> onScroll;      // ±1 mes con la rueda

protected:
    void mouseReleaseEvent(QMouseEvent *e) override {
        const int i = indexAt(e->position());
        if (i >= 0 && onPick) onPick(m_first.addDays(i));
    }

    void mouseMoveEvent(QMouseEvent *e) override {
        const int i = indexAt(e->position());
        if (i == m_hover) return;
        m_hover = i;
        update();
    }

    void leaveEvent(QEvent *) override {
        m_hover = -1;
        update();
    }

    void wheelEvent(QWheelEvent *e) override {
        if (!onScroll) return;
        const int steps = e->angleDelta().y();
        if (steps) onScroll(steps > 0 ? -1 : 1);
    }

    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        const qreal cw = width() / qreal(kCols);
        const qreal ch = (height() - kWeekHeaderH) / qreal(kRows);
        const QDate today = QDate::currentDate();

        QFont f = font();
        f.setPixelSize(9);
        f.setWeight(QFont::DemiBold);
        p.setFont(f);
        p.setPen(QColor(Theme::muted()));
        for (int c = 0; c < kCols; ++c)
            p.drawText(QRectF(c * cw, 0, cw, kWeekHeaderH), Qt::AlignCenter,
                       Lang::weekdayInitial(c));

        for (int i = 0; i < kRows * kCols; ++i) {
            const QDate d = m_first.addDays(i);
            const QRectF cell(int(i % kCols) * cw, kWeekHeaderH + int(i / kCols) * ch, cw, ch);
            const QRectF pad = cell.adjusted(2, 2, -2, -2);

            const bool inMonth = d.month() == m_month.month() && d.year() == m_month.year();
            const bool isToday = d == today;
            const bool isSel = d == m_selected;

            // La celda elegida va enmarcada y con relleno; hoy, solo el marco.
            const QRectF box = pad.adjusted(0, 1, 0, -1);
            if (isSel) {
                QColor fill = m_theme.accent;
                fill.setAlpha(38);
                p.setPen(QPen(m_theme.accent, 1.3));
                p.setBrush(fill);
                p.drawRoundedRect(box, 8, 8);
            } else if (i == m_hover) {
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(255, 255, 255, 16));
                p.drawRoundedRect(box, 8, 8);
            } else if (isToday) {
                QColor ring = m_theme.accent;
                ring.setAlpha(90);
                p.setPen(QPen(ring, 1.2));
                p.setBrush(Qt::NoBrush);
                p.drawRoundedRect(box, 8, 8);
            }

            f.setPixelSize(11.5);
            f.setWeight(isToday || isSel ? QFont::DemiBold : QFont::Normal);
            p.setFont(f);
            QColor ink = isToday ? m_theme.accent : QColor(inMonth ? Theme::fg() : Theme::muted());
            if (!inMonth) ink.setAlpha(110);
            p.setPen(ink);

            // Número y puntos se centran como un bloque, en vez de subir el
            // número una cantidad fija: así un día sin avisos queda centrado
            // de verdad en su celda y uno con ellos no se descuelga.
            const Mark mark = m_marks.value(d);
            const int dots = int(mark.alerts.size());
            const qreal dotsH = dots > 0 ? kDotBand : 0.0;
            const qreal top = box.center().y() - (kNumberH + dotsH) / 2.0;
            p.drawText(QRectF(cell.left(), top, cw, kNumberH), Qt::AlignCenter,
                       QString::number(d.day()));
            if (dots <= 0) continue;

            p.setPen(Qt::NoPen);
            const qreal step = 5.0;
            const qreal y = top + kNumberH + kDotBand / 2.0;
            const qreal x0 = cell.center().x() - (dots - 1) * step / 2.0;
            for (int k = 0; k < dots; ++k) {
                QColor dot = mark.alerts.at(k) ? QColor("#ff7a6b") : m_theme.accent;
                if (!inMonth) dot.setAlpha(120);
                p.setBrush(dot);
                p.drawRoundedRect(QRectF(x0 + k * step - 1.6, y - 1.6, 3.2, 3.2), 1.0, 1.0);
            }
        }
    }

private:
    int indexAt(const QPointF &pos) const {
        if (pos.y() < kWeekHeaderH) return -1;
        const qreal cw = width() / qreal(kCols);
        const qreal ch = (height() - kWeekHeaderH) / qreal(kRows);
        if (cw <= 0 || ch <= 0) return -1;

        const int c = qBound(0, int(pos.x() / cw), kCols - 1);
        const int r = qBound(0, int((pos.y() - kWeekHeaderH) / ch), kRows - 1);
        return r * kCols + c;
    }

    Theme m_theme;
    QHash<QDate, Mark> m_marks;
    QDate m_month = QDate::currentDate();
    QDate m_first = QDate::currentDate();
    QDate m_selected;
    int m_hover = -1;
};

// ---------------------------------------------------------------------------

CalendarView::CalendarView(const Theme &theme, QWidget *parent)
    : QWidget(parent), m_theme(theme) {
    setObjectName("calendar");

    m_selected = QDate::currentDate();
    m_month = QDate(m_selected.year(), m_selected.month(), 1);

    auto *col = new QVBoxLayout(this);
    col->setContentsMargins(9, 8, 9, 8);
    col->setSpacing(6);

    buildHeader(col);

    m_grid = new MonthGrid;
    m_grid->setTheme(m_theme);
    m_grid->onPick = [this](const QDate &d) {
        // Pinchar en la cola del mes vecino salta a ese mes, no solo selecciona.
        m_selected = d;
        showMonth(d);
    };
    m_grid->onScroll = [this](int months) { showMonth(m_month.addMonths(months)); };
    col->addWidget(m_grid, 1);

    auto *line = new QFrame;
    line->setObjectName("calSeparator");
    line->setFixedHeight(1);
    col->addWidget(line);

    buildDayHeader(col);

    auto *host = new QWidget;
    host->setObjectName("listHost");
    m_dayLayout = new QVBoxLayout(host);
    m_dayLayout->setContentsMargins(0, 0, 0, 0);
    m_dayLayout->setSpacing(1);
    m_dayLayout->addStretch();

    m_dayScroll = new QScrollArea;
    m_dayScroll->setWidget(host);
    m_dayScroll->setWidgetResizable(true);
    m_dayScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_dayScroll->setMinimumHeight(74);
    m_dayScroll->viewport()->setAutoFillBackground(false);
    m_dayScroll->viewport()->setObjectName("scrollViewport");
    col->addWidget(m_dayScroll);

    applyDayCollapsed();   // deja la flecha con su icono desde el principio
    showMonth(m_selected);
}

// Cabecera de la lista del día: es también el mando para plegarla. La fila
// entera responde al clic, no solo la flecha, que a 18 px es un blanco pequeño.
void CalendarView::buildDayHeader(QVBoxLayout *col) {
    auto *row = new DayRow;
    row->setObjectName("dayHeader");
    row->click = [this] { toggleDayList(); };
    m_dayHeader = row;

    auto *l = new QHBoxLayout(row);
    l->setContentsMargins(4, 1, 2, 1);
    l->setSpacing(6);

    m_dayToggle = new QToolButton;
    m_dayToggle->setIconSize(QSize(12, 12));
    m_dayToggle->setFixedSize(18, 18);
    m_dayToggle->setCursor(Qt::PointingHandCursor);
    connect(m_dayToggle, &QToolButton::clicked, this, &CalendarView::toggleDayList);
    l->addWidget(m_dayToggle, 0, Qt::AlignVCenter);

    m_dayLabel = new QLabel;
    m_dayLabel->setObjectName("meta");
    m_dayCount = new QLabel;
    m_dayCount->setObjectName("meta");
    m_dayCount->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    l->addWidget(m_dayLabel, 1);
    l->addWidget(m_dayCount);
    col->addWidget(row);
}

void CalendarView::toggleDayList() {
    m_dayCollapsed = !m_dayCollapsed;
    // A partir de aquí manda la elección del usuario: aunque el panel siga
    // estrecho, abrirla a mano tiene que abrirla, y volver a crecer no debe
    // deshacer un plegado pedido a propósito.
    m_autoCollapsed = false;
    applyDayCollapsed();
}

void CalendarView::applyDayCollapsed() {
    if (m_dayScroll) m_dayScroll->setVisible(!m_dayCollapsed);
    if (!m_dayToggle) return;
    m_dayToggle->setIcon(paintIcon(m_dayCollapsed ? "chevronRight" : "chevronDown",
                                   QColor(Theme::muted()), 12));
    m_dayToggle->setToolTip(m_dayCollapsed ? L("Mostrar los avisos del día")
                                           : L("Plegar los avisos del día"));
}

void CalendarView::resizeEvent(QResizeEvent *e) {
    QWidget::resizeEvent(e);

    const bool tight = height() < kTightHeight;
    if (tight == m_tight) return;
    m_tight = tight;

    if (tight && !m_dayCollapsed) {
        m_dayCollapsed = true;
        m_autoCollapsed = true;      // se deshace solo al recuperar el alto
        applyDayCollapsed();
    } else if (!tight && m_autoCollapsed) {
        m_dayCollapsed = false;
        m_autoCollapsed = false;
        applyDayCollapsed();
    }
}

void CalendarView::buildHeader(QVBoxLayout *col) {
    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(2);

    auto arrow = [](const QString &kind, const QString &tip) {
        auto *b = new QToolButton;
        b->setObjectName("calNav");
        b->setIcon(paintIcon(kind, QColor(Theme::muted())));
        b->setIconSize(QSize(14, 14));
        b->setFixedSize(24, 24);
        b->setCursor(Qt::PointingHandCursor);
        b->setToolTip(tip);
        return b;
    };

    m_prevBtn = arrow("chevronLeft", L("Mes anterior"));
    m_nextBtn = arrow("chevronRight", L("Mes siguiente"));
    QToolButton *prev = m_prevBtn;
    QToolButton *next = m_nextBtn;
    connect(prev, &QToolButton::clicked, this, [this] { showMonth(m_month.addMonths(-1)); });
    connect(next, &QToolButton::clicked, this, [this] { showMonth(m_month.addMonths(1)); });

    m_monthLabel = new QLabel;
    m_monthLabel->setObjectName("calMonth");
    m_monthLabel->setAlignment(Qt::AlignCenter);

    m_todayBtn = new QToolButton;
    QToolButton *today = m_todayBtn;
    today->setObjectName("todayBtn");
    today->setText(L("Hoy"));
    today->setCursor(Qt::PointingHandCursor);
    today->setToolTip(L("Volver a hoy"));
    connect(today, &QToolButton::clicked, this, [this] { goTo(QDate::currentDate()); });

    row->addWidget(prev);
    row->addWidget(m_monthLabel, 1);
    row->addWidget(next);
    row->addSpacing(4);
    row->addWidget(today);
    col->addLayout(row);
}

void CalendarView::setSource(const QList<Note *> *notes) {
    m_notes = notes;
    refresh();
}

// Los textos fijos de la vista; lo que depende de las notas (mes, día y sus
// filas) lo rehace refresh(), y la rejilla se repinta con las iniciales
// nuevas de la semana.
void CalendarView::retranslate() {
    m_prevBtn->setToolTip(L("Mes anterior"));
    m_nextBtn->setToolTip(L("Mes siguiente"));
    m_todayBtn->setText(L("Hoy"));
    m_todayBtn->setToolTip(L("Volver a hoy"));
    applyDayCollapsed();     // la ayuda de la flecha lleva texto
    if (m_grid) m_grid->update();
    refresh();
}

void CalendarView::setTheme(const Theme &theme) {
    m_theme = theme;
    if (m_grid) m_grid->setTheme(theme);
    refresh();
}

void CalendarView::goTo(const QDate &date) {
    m_selected = date;
    showMonth(date);
}

void CalendarView::showMonth(const QDate &anyDayOfMonth) {
    m_month = QDate(anyDayOfMonth.year(), anyDayOfMonth.month(), 1);
    m_grid->setMonth(m_month);
    refresh();
}

void CalendarView::refresh() {
    refreshMonthLabel();

    // Se recorren los días visibles preguntando a cada nota si cae ahí, y no
    // al revés: un recordatorio que se repite no tiene una fecha, tiene todas
    // las que encajan con su patrón, y solo las de este mes hay que pintar.
    //
    // Los avisos de cada día se ordenan por hora antes de convertirlos en
    // puntos: las notas llegan por antigüedad, no por cuándo suenan.
    QHash<QDate, MonthGrid::Mark> marks;
    if (m_notes) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const QDate first = m_month.addDays(-(m_month.dayOfWeek() - 1));

        for (int i = 0; i < kRows * kCols; ++i) {
            const QDate day = first.addDays(i);
            QList<QPair<qint64, bool>> found;
            for (Note *n : *m_notes) {
                if (!n->occursOn(day)) continue;
                const qint64 at = n->occurrenceOn(day).toMSecsSinceEpoch();
                // Una vuelta ya pasada de algo que se repite no es un aviso
                // vencido, es historia: solo el que suena o el que se quedó
                // sin sonar se pinta en rojo.
                found.append({at, n->ringing || (at <= now && !n->repeats())});
            }
            if (found.isEmpty()) continue;
            std::sort(found.begin(), found.end());

            MonthGrid::Mark mark;
            mark.count = int(found.size());
            for (int k = 0; k < qMin(int(found.size()), kMaxDots); ++k)
                mark.alerts.append(found.at(k).second);
            marks.insert(day, mark);
        }
    }
    m_grid->setMarks(marks);
    m_grid->setSelected(m_selected);
    refreshDayList();
}

void CalendarView::refreshMonthLabel() {
    if (m_monthLabel) m_monthLabel->setText(monthTitle(m_month));
}

QList<Note *> CalendarView::notesOn(const QDate &day) const {
    QList<Note *> found;
    if (!m_notes) return found;

    for (Note *n : *m_notes)
        if (n->occursOn(day)) found.append(n);

    // Por hora del día, no por instante: dos vueltas de recordatorios
    // repetidos caen aquí con fechas base distintas y lo que ordena la lista
    // es a qué hora suenan.
    std::sort(found.begin(), found.end(), [](Note *a, Note *b) {
        return a->dueAt().time() < b->dueAt().time();
    });
    return found;
}

void CalendarView::refreshDayList() {
    // Se vacía todo menos el stretch final. Se ocultan además de borrarlas:
    // sacarlas del layout no las quita de la pantalla, y siguen pintadas
    // (y aceptando eventos) hasta que corre deleteLater().
    while (m_dayLayout->count() > 1) {
        QLayoutItem *item = m_dayLayout->takeAt(0);
        if (QWidget *w = item->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete item;
    }

    const QList<Note *> today = notesOn(m_selected);
    const bool isToday = m_selected == QDate::currentDate();
    m_dayLabel->setText(isToday ? L("%1 · HOY").arg(dayTitle(m_selected))
                                : dayTitle(m_selected));
    m_dayCount->setText(today.isEmpty()
                            ? QString()
                            : (today.size() == 1 ? L("1 AVISO")
                                                 : L("%1 AVISOS").arg(today.size())));

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    int at = 0;

    for (Note *n : today) {
        // La hora sale de la vuelta que toca este día, no de dueAtMs: en un
        // recordatorio anual esa fecha es la del próximo año.
        const QDateTime when = n->occurrenceOn(m_selected);
        const bool alert = n->ringing ||
                           (when.toMSecsSinceEpoch() <= now && !n->repeats());
        const QColor tint = alert ? QColor("#ff7a6b") : m_theme.accent;

        auto *row = new DayRow;
        row->click = [this, n] { emit noteActivated(n); };

        auto *l = new QHBoxLayout(row);
        l->setContentsMargins(6, 4, 6, 4);
        l->setSpacing(8);

        auto *time = new QLabel(when.toString("HH:mm"));
        time->setObjectName("dayTime");
        l->addWidget(time, 0, Qt::AlignVCenter);

        // Barra de color en vez de un icono: dice el estado sin robar ancho.
        auto *bar = new QFrame;
        bar->setFixedWidth(2);
        bar->setMinimumHeight(15);
        bar->setStyleSheet(QString("background:%1; border:none; border-radius:1px;")
                               .arg(tint.name()));
        l->addWidget(bar);

        auto *title = new ElidedLabel(n->title.isEmpty() ? L("Sin título") : n->title,
                                      QColor(Theme::fg()));
        title->setObjectName("dayRowTitle");
        title->setToolTip(n->title);
        l->addWidget(title, 1);

        // Marca de "esto vuelve": sin ella, un cumpleaños y una cita de un
        // solo día se leen igual en la lista.
        if (n->repeats()) {
            auto *loop = new QLabel;
            loop->setFixedSize(12, 12);
            loop->setPixmap(paintIcon("repeat", m_theme.accent, 12).pixmap(12, 12));
            loop->setToolTip(n->repeatLabel());
            l->addWidget(loop, 0, Qt::AlignVCenter);
        }

        // Sonando manda el botón de parar; vencido y callado, un chip.
        if (n->ringing) {
            auto *stop = new QToolButton;
            stop->setIcon(paintIcon("stop", QColor("#ff7a6b")));
            stop->setIconSize(QSize(13, 13));
            stop->setFixedSize(20, 20);
            stop->setCursor(Qt::PointingHandCursor);
            stop->setToolTip(L("Detener aviso"));
            connect(stop, &QToolButton::clicked, this, [this, n] { emit dismissRequested(n); });
            l->addWidget(stop);
        } else if (alert) {
            auto *chip = new QLabel(L("VENCIDO"));
            chip->setObjectName("dayChip");
            l->addWidget(chip, 0, Qt::AlignVCenter);
        }

        m_dayLayout->insertWidget(at++, row);
    }

    if (today.isEmpty()) {
        auto *empty = new QLabel(L("Sin recordatorios este día"));
        empty->setObjectName("meta");
        empty->setContentsMargins(7, 4, 7, 2);
        m_dayLayout->insertWidget(at++, empty);
    }

    auto *add = new DayRow;
    auto *al = new QHBoxLayout(add);
    al->setContentsMargins(7, 5, 7, 5);
    al->setSpacing(8);

    auto *plus = new QLabel;
    plus->setFixedSize(14, 14);
    plus->setPixmap(paintIcon("plus", m_theme.accent, 14).pixmap(14, 14));
    al->addWidget(plus);

    auto *addText = new QLabel(L("Nuevo recordatorio"));
    addText->setObjectName("dayRowTitle");
    al->addWidget(addText, 1);

    // El popup de la hora se abre pegado a esta fila, así que necesita el
    // widget ya construido.
    add->click = [this, add] { emit createRequested(m_selected, add); };
    m_dayLayout->insertWidget(at++, add);
}
