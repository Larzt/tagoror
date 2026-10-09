#include "ui/timers.hpp"

#include "core/lang.hpp"
#include "ui/elidedlabel.hpp"
#include "ui/keynav.hpp"
#include "ui/popup.hpp"

#include <QDateTime>
#include <QFrame>
#include <QGridLayout>
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

/// mm:ss, or h:mm:ss from one hour on. Rounds up: a timer showing 00:00 must
/// already have finished, not be about to.
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

/// Progress ring: time left in the state's colour over a dim track. The large
/// one draws the time and state inside, so tick() only requests a repaint.
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
            // From twelve o'clock, clockwise: what is left.
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

/// Clickable row that opens the timer large; the buttons inside keep their
/// own clicks.
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

    // Everything scrolls, as in settings and birthdays, so the page's minimum
    // height does not depend on how many timers there are.
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
        case Timer::Running:
            return L("En marcha · termina %1")
                .arg(QDateTime::fromMSecsSinceEpoch(t->endsAtMs).toString("HH:mm"));
        case Timer::Paused:  return L("En pausa a falta de %1").arg(clock(t->remainingMs()));
        case Timer::Done:    return L("Tiempo cumplido");
        default:             return L("Guardado");
    }
}

QString TimerView::heroSubtitle(const Timer *t) const {
    QString state;
    switch (t->state) {
        case Timer::Running:
            state = L("termina %1").arg(QDateTime::fromMSecsSinceEpoch(t->endsAtMs).toString("HH:mm"));
            break;
        case Timer::Paused: state = L("en pausa"); break;
        case Timer::Done:   state = L("tiempo cumplido"); break;
        default:            state = L("listo"); break;
    }
    return L("de %1 · %2").arg(clock(t->totalMs), state);
}

void TimerView::toggleCreator() {
    m_creatorOpen = !m_creatorOpen;
    m_creator->setVisible(m_creatorOpen);
    if (m_creatorOpen) m_name->setFocus(Qt::OtherFocusReason);
}

void TimerView::append(QWidget *w) { m_layout->insertWidget(m_layout->count() - 1, w); }

void TimerView::refresh() {
    m_live.clear();
    // Everything but the creation form and the trailing stretch.
    for (int i = m_layout->count() - 1; i >= 0; --i) {
        QLayoutItem *item = m_layout->itemAt(i);
        QWidget *w = item->widget();
        if (!w || w == m_creator) continue;
        m_layout->takeAt(i);
        w->hide();          // see *Removing rows*: taking it out of the layout does not delete it
        w->deleteLater();
        delete item;
    }
    if (!m_timers) return;
    if (m_focus && !m_timers->contains(m_focus)) m_focus = nullptr;

    // On top: the one picked; else whatever rings or runs; else a paused one.
    Timer *hero = m_focus;
    for (Timer *t : *m_timers)
        if (!hero && (t->state == Timer::Done || t->state == Timer::Running)) hero = t;
    for (Timer *t : *m_timers)
        if (!hero && t->state == Timer::Paused) hero = t;
    if (hero) addHero(hero);

    // With nothing saved yet the form is what there is to do: open.
    m_creator->setVisible(m_creatorOpen || m_timers->isEmpty());

    QList<Timer *> rest;
    for (Timer *t : *m_timers)
        if (t != hero) rest << t;
    if (!rest.isEmpty()) {
        addSection(L("Guardados"));
        for (Timer *t : rest) addRow(t);
    }
    addSection(L("Rápido"));
    addQuick();
    tick();
}

void TimerView::tick() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const Live &live : m_live) {
        const QColor color = stateColor(live.timer, m_theme.accent);
        if (live.time) live.time->setText(clock(live.timer->remainingMs(now)));
        if (live.sub) live.sub->setText(live.hero ? heroSubtitle(live.timer) : subtitle(live.timer));
        if (auto *ring = static_cast<Ring *>(live.ring)) {
            ring->set(live.timer->fraction(now), color);
            if (live.hero) ring->setTexts(clock(live.timer->remainingMs(now)), QString(), 20);
        }
    }
}

void TimerView::addSection(const QString &title) {
    auto *label = new QLabel(title.toUpper());
    label->setObjectName("pageSection");
    append(label);
}

/// One row per timer: name and what it is doing, the time left, start/pause.
/// Clicking puts it on top; the menu (right click, Menu key) resets or deletes.
void TimerView::addRow(Timer *t) {
    auto *row = new TimerRow([this, t] { focusTimer(t); });
    row->setProperty("done", t->state == Timer::Done);
    row->setToolTip(L("Ponerlo arriba"));
    row->setFixedHeight(44);
    row->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(row, &QWidget::customContextMenuRequested, this,
            [this, t, row](const QPoint &at) { openRowMenu(t, row->mapToGlobal(at)); });

    auto *l = new QHBoxLayout(row);
    l->setContentsMargins(12, 0, 6, 0);
    l->setSpacing(10);

    auto *texts = new QVBoxLayout;
    texts->setContentsMargins(0, 0, 0, 0);
    texts->setSpacing(0);
    auto *name = new ElidedLabel(t->name.isEmpty() ? L("Temporizador") : t->name,
                                 QColor(Theme::fg()));
    name->setObjectName("timerName");
    name->setToolTip(t->name);
    auto *sub = new ElidedLabel(subtitle(t), t->state == Timer::Done ? kRed
                                                                     : QColor(Theme::muted()));
    sub->setObjectName("timerSub");
    texts->addWidget(name);
    texts->addWidget(sub);
    l->addLayout(texts, 1);

    auto *time = new QLabel;
    time->setObjectName(t->state == Timer::Running ? "timerTimeLive" : "timerTime");
    l->addWidget(time, 0, Qt::AlignVCenter);

    const bool running = t->state == Timer::Running;
    auto *toggle = smallButton(running ? "pause" : "play", QColor(Theme::fg()),
                               running ? L("Pausar") : L("Iniciar"));
    toggle->setFixedSize(30, 30);
    connect(toggle, &QToolButton::clicked, this, [this, t] { emit toggleRequested(t); });
    l->addWidget(toggle);

    m_live.append({t, time, nullptr, sub, false});
    append(row);
}

void TimerView::openRowMenu(Timer *t, const QPoint &globalPos) {
    auto *menu = new Popup(m_theme, this);
    menu->addHeader(t->name.isEmpty() ? L("Temporizador") : t->name);
    const bool running = t->state == Timer::Running;
    menu->addItem(running ? "pause" : "play", running ? L("Pausar") : L("Iniciar"), QString(),
                  [this, t] { emit toggleRequested(t); });
    menu->addItem("repeat", L("Reiniciar"), L("Vuelve a su duración"),
                  [this, t] { emit resetRequested(t); });
    menu->addSeparator();
    menu->addItem("trash", L("Eliminar"), QString(), [this, t] { emit removeRequested(t); });
    menu->showAt(globalPos);
}

/// The one that matters, large: its ring with the time inside, what it is
/// doing, and the three things to do with it.
void TimerView::addHero(Timer *t) {
    auto *card = new QFrame;
    card->setObjectName("timerHero");
    card->setProperty("done", t->state == Timer::Done);
    card->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(card, &QWidget::customContextMenuRequested, this,
            [this, t, card](const QPoint &at) { openRowMenu(t, card->mapToGlobal(at)); });
    auto *row = new QHBoxLayout(card);
    row->setContentsMargins(14, 14, 14, 14);
    row->setSpacing(16);

    auto *dial = new Ring(96, 5);
    row->addWidget(dial, 0, Qt::AlignVCenter);

    auto *col = new QVBoxLayout;
    col->setSpacing(8);
    auto *texts = new QVBoxLayout;
    texts->setSpacing(1);
    auto *name = new ElidedLabel(t->name.isEmpty() ? L("Temporizador") : t->name, QColor(Theme::fg()));
    name->setObjectName("timerHeroName");
    name->setToolTip(t->name);
    auto *sub = new ElidedLabel(heroSubtitle(t), t->state == Timer::Done ? kRed : QColor(Theme::muted()));
    sub->setObjectName("timerSub");
    texts->addWidget(name);
    texts->addWidget(sub);
    col->addLayout(texts);

    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(6);
    const bool running = t->state == Timer::Running;
    auto *toggle = new QToolButton;
    toggle->setObjectName("heroPrimary");
    toggle->setText(running ? L("Pausar") : t->state == Timer::Paused ? L("Seguir") : L("Iniciar"));
    toggle->setCursor(Qt::PointingHandCursor);
    toggle->setFocusPolicy(Qt::TabFocus);
    connect(toggle, &QToolButton::clicked, this, [this, t] {
        // A ringing one is silenced by starting it again from its full length.
        if (t->state == Timer::Done) emit resetRequested(t);
        emit toggleRequested(t);
    });
    auto *plus = new QToolButton;
    plus->setObjectName("heroGhost");
    plus->setText(L("+1 min"));
    plus->setCursor(Qt::PointingHandCursor);
    plus->setFocusPolicy(Qt::TabFocus);
    connect(plus, &QToolButton::clicked, this, [this, t] { emit plusRequested(t); });
    auto *reset = smallButton("repeat", QColor(Theme::muted()), L("Reiniciar"));
    reset->setObjectName("heroIcon");
    reset->setFixedSize(28, 28);
    connect(reset, &QToolButton::clicked, this, [this, t] { emit resetRequested(t); });
    buttons->addWidget(toggle);
    buttons->addWidget(plus);
    buttons->addWidget(reset);
    buttons->addStretch();
    col->addLayout(buttons);
    row->addLayout(col, 1);

    m_live.append({t, nullptr, dial, sub, true});
    append(card);
}

/// Chips that start a timer of that length straight away: the form is for a
/// named one.
void TimerView::addQuick() {
    auto *box = new QWidget;
    auto *grid = new QGridLayout(box);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(6);
    const QList<int> minutes{1, 3, 5, 10, 15, 25, 45};
    for (int i = 0; i < minutes.size(); ++i) {
        const int m = minutes.at(i);
        auto *chip = new QToolButton;
        chip->setObjectName("quickChip");
        chip->setText(QString("%1′").arg(m));
        chip->setToolTip(L("Empezar uno de %1 min").arg(m));
        chip->setCursor(Qt::PointingHandCursor);
        chip->setFocusPolicy(Qt::TabFocus);
        chip->setMinimumWidth(36);   // see *Card widths*
        chip->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        connect(chip, &QToolButton::clicked, this,
                [this, m] { emit createRequested(QString(), qint64(m) * 60 * 1000); });
        grid->addWidget(chip, i / 4, i % 4);
    }
    append(box);
}

/// Built once: it survives refresh() so what is being typed is not lost when
/// another timer changes state.
void TimerView::addCreator() {
    m_creator = new QWidget;
    auto *outer = new QVBoxLayout(m_creator);
    outer->setContentsMargins(0, 0, 0, 4);
    outer->setSpacing(4);

    auto *card = new QFrame;
    card->setObjectName("setCard");
    auto *col = new QVBoxLayout(card);
    col->setContentsMargins(9, 9, 9, 9);
    col->setSpacing(7);

    auto *title = new QLabel(L("Nuevo temporizador"));
    title->setObjectName("timerHeroName");
    col->addWidget(title);

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
        m_creatorOpen = false;   // done: refresh() hides it
    };
    connect(create, &QPushButton::clicked, this, submit);
    for (QLineEdit *e : {m_name, m_h, m_m, m_s}) connect(e, &QLineEdit::returnPressed, this, submit);
    outer->addWidget(card);

    // First: the header's "+" opens it on top of the page.
    m_layout->insertWidget(0, m_creator);
    m_creator->setVisible(m_creatorOpen);
}
