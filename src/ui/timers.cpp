#include "ui/timers.hpp"

#include "core/lang.hpp"
#include "ui/elidedlabel.hpp"
#include "ui/keynav.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

const QColor kRed("#ff7a6b");

// mm:ss, o h:mm:ss a partir de una hora. Redondea hacia arriba: un
// temporizador que enseña 00:00 tiene que haber terminado ya, no estar a
// punto.
QString clock(qint64 ms) {
    const qint64 total = (ms + 999) / 1000;
    const qint64 h = total / 3600, m = (total / 60) % 60, s = total % 60;
    if (h > 0)
        return QString("%1:%2:%3").arg(h).arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
    return QString("%1:%2").arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
}

QColor stateColor(const Timer *t, const QColor &accent) {
    if (t->state == Timer::Running) return accent;
    if (t->state == Timer::Done) return kRed;
    return QColor(Theme::muted());
}

// Anillo de progreso: lo que queda, en el color del estado, sobre una pista
// apagada. En grande lleva dentro la hora y el estado, pintados aquí mismo
// para que tick() solo tenga que pedir un repintado.
class Ring : public QWidget {
public:
    Ring(int size, qreal thickness, QWidget *parent = nullptr)
        : QWidget(parent), m_thick(thickness) {
        setFixedSize(size, size);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

    void set(qreal fraction, const QColor &color) {
        m_fraction = qBound<qreal>(0, fraction, 1);
        m_color = color;
        update();
    }
    void setTexts(const QString &big, const QString &small, qreal bigPx) {
        m_big = big;
        m_small = small;
        m_bigPx = bigPx;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(m_thick / 2 + 0.5, m_thick / 2 + 0.5,
                                                 -m_thick / 2 - 0.5, -m_thick / 2 - 0.5);
        p.setPen(QPen(QColor(255, 255, 255, 26), m_thick, Qt::SolidLine, Qt::RoundCap));
        p.drawEllipse(r);
        if (m_fraction > 0) {
            p.setPen(QPen(m_color, m_thick, Qt::SolidLine, Qt::RoundCap));
            // Desde las doce, en el sentido de las agujas: lo que queda.
            p.drawArc(r, 90 * 16, -int(360 * 16 * m_fraction));
        }
        if (m_big.isEmpty()) return;

        QFont f = font();
        f.setFamily("IBM Plex Mono");
        f.setStyleHint(QFont::Monospace);
        f.setPixelSize(int(m_bigPx));
        p.setFont(f);
        p.setPen(QColor(Theme::fg()));
        const qreal mid = height() / 2.0;
        p.drawText(QRectF(0, mid - m_bigPx * 0.95, width(), m_bigPx * 1.3),
                   Qt::AlignHCenter | Qt::AlignVCenter, m_big);
        f.setPixelSize(10);
        f.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
        p.setFont(f);
        p.setPen(m_color == QColor(Theme::muted()) ? QColor(Theme::muted()) : m_color);
        p.drawText(QRectF(0, mid + m_bigPx * 0.45, width(), 16),
                   Qt::AlignHCenter | Qt::AlignVCenter, m_small.toUpper());
    }

private:
    qreal m_thick;
    qreal m_fraction = 0;
    QColor m_color;
    QString m_big;
    QString m_small;
    qreal m_bigPx = 28;
};

// Fila pulsable: abrir el temporizador en grande. Los botones de dentro se
// quedan con su propio clic.
class TimerRow : public QWidget {
public:
    explicit TimerRow(std::function<void()> onClick, QWidget *parent = nullptr)
        : QWidget(parent), m_click(std::move(onClick)) {
        setObjectName("timerRow");
        setAttribute(Qt::WA_StyledBackground, true);
        setAttribute(Qt::WA_Hover, true);
        setCursor(Qt::PointingHandCursor);
        keynav::activatable(this, [this] { if (m_click) m_click(); });
    }

protected:
    void mouseReleaseEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton && rect().contains(e->position().toPoint()) && m_click)
            m_click();
    }

private:
    std::function<void()> m_click;
};

QToolButton *smallButton(const QString &kind, const QColor &color, const QString &tip) {
    auto *b = new QToolButton;
    b->setIcon(paintIcon(kind, color, 13));
    b->setIconSize(QSize(13, 13));
    b->setFixedSize(24, 24);
    b->setCursor(Qt::PointingHandCursor);
    b->setToolTip(tip);
    return b;
}

QLineEdit *durationField(const QString &placeholder, int max) {
    auto *e = new QLineEdit;
    e->setObjectName("timerField");
    e->setPlaceholderText(placeholder);
    e->setAlignment(Qt::AlignCenter);
    e->setValidator(new QIntValidator(0, max, e));
    e->setMaxLength(2);
    e->setMinimumWidth(24);
    return e;
}

}  // namespace

// ---------------------------------------------------------------------------

TimerView::TimerView(const Theme &theme, QWidget *parent) : QWidget(parent), m_theme(theme) {
    setObjectName("timers");

    auto *col = new QVBoxLayout(this);
    col->setContentsMargins(9, 8, 9, 8);
    col->setSpacing(0);

    auto *host = new QWidget;
    host->setObjectName("listHost");
    m_layout = new QVBoxLayout(host);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(4);
    m_layout->addStretch();

    // Todo dentro del desplazamiento, como en ajustes y cumpleaños: el alto
    // mínimo de la página no depende de cuántos temporizadores haya.
    auto *scroll = new QScrollArea;
    scroll->setWidget(host);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setMinimumHeight(150);
    scroll->viewport()->setAutoFillBackground(false);
    scroll->viewport()->setObjectName("scrollViewport");
    col->addWidget(scroll, 1);

    addCreator();
    refresh();
}

void TimerView::setSource(const QList<Timer *> *timers) {
    m_timers = timers;
    refresh();
}

void TimerView::setTheme(const Theme &theme) {
    m_theme = theme;
    refresh();
}

void TimerView::focusTimer(Timer *t) {
    m_focus = t;
    refresh();
}

void TimerView::retranslate() {
    m_layout->removeWidget(m_creator);
    m_creator->hide();
    m_creator->deleteLater();
    addCreator();
    refresh();
}

QString TimerView::subtitle(const Timer *t) const {
    switch (t->state) {
        case Timer::Running: return L("en marcha");
        case Timer::Paused:  return L("en pausa");
        case Timer::Done:    return L("tiempo cumplido");
        default:             return L("guardado · %1").arg(clock(t->totalMs));
    }
}

void TimerView::refresh() {
    m_live.clear();
    // Todo menos el formulario de alta y el stretch final.
    for (int i = m_layout->count() - 1; i >= 0; --i) {
        QLayoutItem *item = m_layout->itemAt(i);
        QWidget *w = item->widget();
        if (!w || w == m_creator) continue;
        m_layout->takeAt(i);
        w->hide();          // ver *Removing rows*: quitarla del layout no la borra
        w->deleteLater();
        delete item;
    }
    if (!m_timers) return;
    if (m_focus && !m_timers->contains(m_focus)) m_focus = nullptr;

    if (m_focus) addFocusCard(m_focus);

    QList<Timer *> active, paused, saved;
    for (Timer *t : *m_timers) {
        if (t->state == Timer::Running || t->state == Timer::Done) active << t;
        else if (t->state == Timer::Paused) paused << t;
        else saved << t;
    }
    const QList<QPair<QString, QList<Timer *>>> groups = {
        {L("ACTIVOS"), active}, {L("EN PAUSA"), paused}, {L("GUARDADOS"), saved}};
    for (const auto &[title, list] : groups) {
        if (list.isEmpty()) continue;
        addSection(title, int(list.size()));
        for (Timer *t : list) addRow(t);
    }
    if (m_timers->isEmpty()) {
        auto *empty = new QLabel(L("Todavía no hay temporizadores. Crea uno abajo o elige un tiempo rápido."));
        empty->setObjectName("meta");
        empty->setWordWrap(true);
        empty->setMinimumWidth(24);   // ver *Card widths*
        empty->setContentsMargins(4, 6, 4, 6);
        m_layout->insertWidget(m_layout->indexOf(m_creator), empty);
    }
    tick();
}

void TimerView::tick() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const Live &live : m_live) {
        const QColor color = stateColor(live.timer, m_theme.accent);
        if (live.time) live.time->setText(clock(live.timer->remainingMs(now)));
        if (auto *ring = static_cast<Ring *>(live.ring)) {
            ring->set(live.timer->fraction(now), color);
            if (!live.time)
                ring->setTexts(clock(live.timer->remainingMs(now)), subtitle(live.timer), 30);
        }
    }
}

void TimerView::addSection(const QString &title, int count) {
    auto *head = new QWidget;
    auto *hl = new QHBoxLayout(head);
    hl->setContentsMargins(2, 8, 2, 1);
    auto *label = new QLabel(title);
    label->setObjectName("setSection");
    label->setContentsMargins(0, 0, 0, 0);
    auto *n = new QLabel(QString::number(count));
    n->setObjectName("meta");
    hl->addWidget(label);
    hl->addStretch();
    hl->addWidget(n);
    m_layout->insertWidget(m_layout->indexOf(m_creator), head);
}

void TimerView::addRow(Timer *t) {
    auto *row = new TimerRow([this, t] { focusTimer(t); });
    row->setProperty("done", t->state == Timer::Done);
    row->setProperty("chosen", t == m_focus);
    row->setToolTip(L("Abrir en grande"));

    auto *l = new QHBoxLayout(row);
    l->setContentsMargins(8, 6, 6, 6);
    l->setSpacing(8);

    auto *ring = new Ring(22, 3);
    l->addWidget(ring, 0, Qt::AlignVCenter);

    auto *texts = new QVBoxLayout;
    texts->setContentsMargins(0, 0, 0, 0);
    texts->setSpacing(0);
    auto *name = new ElidedLabel(t->name.isEmpty() ? L("Temporizador") : t->name,
                                 QColor(Theme::fg()));
    name->setObjectName("timerName");
    name->setToolTip(t->name);
    auto *sub = new ElidedLabel(subtitle(t), t->state == Timer::Done ? kRed
                                                                     : QColor(Theme::muted()));
    sub->setObjectName("meta");
    texts->addWidget(name);
    texts->addWidget(sub);
    l->addLayout(texts, 1);

    auto *time = new QLabel;
    time->setObjectName(t->state == Timer::Running ? "timerTimeLive" : "timerTime");
    l->addWidget(time, 0, Qt::AlignVCenter);

    const bool running = t->state == Timer::Running;
    auto *toggle = smallButton(running ? "pause" : "play", QColor(Theme::fg()),
                               running ? L("Pausar") : L("Iniciar"));
    connect(toggle, &QToolButton::clicked, this, [this, t] { emit toggleRequested(t); });
    auto *reset = smallButton("repeat", QColor(Theme::muted()), L("Reiniciar"));
    connect(reset, &QToolButton::clicked, this, [this, t] { emit resetRequested(t); });
    auto *remove = smallButton("trash", kRed, L("Eliminar"));
    connect(remove, &QToolButton::clicked, this, [this, t] { emit removeRequested(t); });
    l->addWidget(toggle);
    l->addWidget(reset);
    l->addWidget(remove);

    m_live.append({t, time, ring});
    m_layout->insertWidget(m_layout->indexOf(m_creator), row);
}

void TimerView::addFocusCard(Timer *t) {
    auto *card = new QFrame;
    card->setObjectName("setCard");
    auto *col = new QVBoxLayout(card);
    col->setContentsMargins(12, 14, 12, 12);
    col->setSpacing(10);

    auto *dial = new Ring(150, 7);
    col->addWidget(dial, 0, Qt::AlignHCenter);

    auto *name = new QLabel(t->name.isEmpty() ? L("Temporizador") : t->name);
    name->setObjectName("timerFocusName");
    name->setAlignment(Qt::AlignCenter);
    name->setWordWrap(true);
    name->setMinimumWidth(24);   // ver *Card widths*
    col->addWidget(name);

    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(6);
    const bool running = t->state == Timer::Running;
    auto *toggle = new QPushButton(running ? L("Pausar") : L("Iniciar"));
    toggle->setFocusPolicy(Qt::TabFocus);
    toggle->setCursor(Qt::PointingHandCursor);
    connect(toggle, &QPushButton::clicked, this, [this, t] { emit toggleRequested(t); });
    auto *reset = new QToolButton;
    reset->setObjectName("segButton");
    reset->setText(L("Reiniciar"));
    reset->setCursor(Qt::PointingHandCursor);
    connect(reset, &QToolButton::clicked, this, [this, t] { emit resetRequested(t); });
    auto *close = new QToolButton;
    close->setObjectName("segButton");
    close->setText(L("Cerrar"));
    close->setCursor(Qt::PointingHandCursor);
    connect(close, &QToolButton::clicked, this, [this] { focusTimer(nullptr); });
    buttons->addStretch();
    buttons->addWidget(toggle);
    buttons->addWidget(reset);
    buttons->addWidget(close);
    buttons->addStretch();
    col->addLayout(buttons);

    m_live.append({t, nullptr, dial});
    m_layout->insertWidget(m_layout->indexOf(m_creator), card);
}

// El formulario se monta una sola vez: sobrevive a refresh() para que lo que
// se esté escribiendo no se pierda cuando otro temporizador cambia de estado.
void TimerView::addCreator() {
    m_creator = new QWidget;
    auto *outer = new QVBoxLayout(m_creator);
    outer->setContentsMargins(0, 8, 0, 0);
    outer->setSpacing(4);

    auto *title = new QLabel(L("NUEVO TEMPORIZADOR"));
    title->setObjectName("setSection");
    outer->addWidget(title);

    auto *card = new QFrame;
    card->setObjectName("setCard");
    auto *col = new QVBoxLayout(card);
    col->setContentsMargins(9, 9, 9, 9);
    col->setSpacing(7);

    m_name = new QLineEdit;
    m_name->setPlaceholderText(L("Nombre (opcional)"));
    col->addWidget(m_name);

    auto *fields = new QHBoxLayout;
    fields->setSpacing(5);
    m_h = durationField(L("h"), 99);
    m_m = durationField(L("min"), 59);
    m_s = durationField(L("seg"), 59);
    m_m->setText("25");
    auto *create = new QPushButton(L("Crear"));
    create->setFocusPolicy(Qt::TabFocus);
    create->setCursor(Qt::PointingHandCursor);
    fields->addWidget(m_h, 1);
    fields->addWidget(m_m, 1);
    fields->addWidget(m_s, 1);
    fields->addWidget(create);
    col->addLayout(fields);

    auto submit = [this] {
        const qint64 ms = (m_h->text().toLongLong() * 3600 + m_m->text().toLongLong() * 60 +
                           m_s->text().toLongLong()) * 1000;
        if (ms <= 0) return;
        emit createRequested(m_name->text().trimmed(), ms);
        m_name->clear();
    };
    connect(create, &QPushButton::clicked, this, submit);
    for (QLineEdit *e : {m_name, m_h, m_m, m_s}) connect(e, &QLineEdit::returnPressed, this, submit);

    // Tiempos rápidos: rellenan los campos, no lanzan nada. Así se puede
    // poner nombre antes de crear, que es lo que pide un "Pomodoro".
    auto *quick = new QHBoxLayout;
    quick->setSpacing(4);
    for (int minutes : {1, 5, 10, 25}) {
        auto *chip = new QToolButton;
        chip->setObjectName("segButton");
        chip->setText(L("%1 min").arg(minutes));
        chip->setCursor(Qt::PointingHandCursor);
        connect(chip, &QToolButton::clicked, this, [this, minutes] {
            m_h->clear();
            m_m->setText(QString::number(minutes));
            m_s->clear();
        });
        quick->addWidget(chip, 1);
    }
    col->addLayout(quick);
    outer->addWidget(card);

    m_layout->insertWidget(m_layout->count() - 1, m_creator);
}
