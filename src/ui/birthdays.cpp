#include "ui/birthdays.hpp"

#include "core/lang.hpp"
#include "ui/elidedlabel.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>

namespace {

constexpr int kAvatarRow = 28;     // círculo de una fila
constexpr int kAvatarCard = 38;    // el de la tarjeta de hoy
constexpr int kSoonDays = 7;       // hasta aquí, la cuenta atrás va en el acento

// Tono estable por persona. Sale del nombre y no de la posición en la lista
// para que el color de alguien no cambie al añadir o quitar a otro; y se
// calcula aquí en vez de con qHash porque Qt6 la siembra al azar en cada
// arranque, así que el mismo nombre saldría de un color distinto cada vez que
// se abre el panel.
QColor nameTint(const QString &name) {
    unsigned h = 2166136261u;
    for (const QChar &c : name) {
        h ^= unsigned(c.unicode());
        h *= 16777619u;
    }
    // Saturación y brillo fijos: lo único que cambia es el tono, así ninguna
    // persona sale con un color que se lea peor que el de las demás sobre el
    // fondo oscuro.
    return QColor::fromHsv(int(h % 360u), 95, 205);
}

// El nombre del mes, en el idioma de la interfaz y en mayúsculas. Nunca
// QLocale::system(): el idioma lo manda el ajuste, no el entorno.
QString monthName(int month) {
    return Lang::locale().toString(QDate(2000, month, 1), "MMMM").toUpper();
}

// Círculo con las iniciales. Es un QWidget pintado a mano y no un QLabel
// porque un QLabel pide el ancho de su texto (ver *Card widths* en CLAUDE.md)
// y aquí el ancho tiene que ser exactamente el del círculo.
class Avatar : public QWidget {
public:
    Avatar(const QString &initials, const QColor &tint, int px, QWidget *parent = nullptr)
        : QWidget(parent), m_text(initials), m_tint(tint), m_px(px) {
        setFixedSize(px, px);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        QColor fill = m_tint;
        fill.setAlpha(46);
        p.setPen(QPen(QColor(m_tint.red(), m_tint.green(), m_tint.blue(), 110), 1.0));
        p.setBrush(fill);
        p.drawEllipse(QRectF(0.5, 0.5, m_px - 1.0, m_px - 1.0));

        QFont f = font();
        f.setPixelSize(m_px <= 30 ? 11 : 14);
        f.setWeight(QFont::DemiBold);
        p.setFont(f);
        p.setPen(m_tint);
        p.drawText(rect(), Qt::AlignCenter, m_text);
    }

private:
    QString m_text;
    QColor m_tint;
    int m_px;
};

// Fila de la lista. Igual que DayRow en el calendario: un QWidget liso con
// WA_StyledBackground para que la hoja de estilos le pinte el realce.
class BirthdayRow : public QWidget {
public:
    explicit BirthdayRow(QWidget *parent = nullptr) : QWidget(parent) {
        setObjectName("bdayRow");
        setAttribute(Qt::WA_StyledBackground, true);
        setAttribute(Qt::WA_Hover, true);
        setCursor(Qt::PointingHandCursor);
    }

    // Se asigna después de construir: el popup se ancla a la propia fila, que
    // hasta entonces no existe.
    std::function<void()> click;

protected:
    void mouseReleaseEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton && rect().contains(e->position().toPoint()) && click)
            click();
    }
};

}  // namespace

// ---------------------------------------------------------------------------

BirthdayView::BirthdayView(const Theme &theme, QWidget *parent)
    : QWidget(parent), m_theme(theme) {
    setObjectName("birthdays");

    auto *col = new QVBoxLayout(this);
    col->setContentsMargins(9, 8, 9, 8);
    col->setSpacing(0);

    auto *host = new QWidget;
    host->setObjectName("listHost");
    m_layout = new QVBoxLayout(host);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(3);
    m_layout->addStretch();

    auto *scroll = new QScrollArea;
    scroll->setWidget(host);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setMinimumHeight(150);
    scroll->viewport()->setAutoFillBackground(false);
    scroll->viewport()->setObjectName("scrollViewport");
    col->addWidget(scroll, 1);

    refresh();
}

void BirthdayView::setSource(const QList<Birthday *> *list) {
    m_list = list;
    refresh();
}

void BirthdayView::setTheme(const Theme &theme) {
    m_theme = theme;
    refresh();
}

void BirthdayView::setByMonth(bool on) {
    if (on == m_byMonth) return;
    m_byMonth = on;
    refresh();
}

// Ordenados por lo que falta para cada uno: en diciembre lo próximo es enero, y
// ordenar por mes del año dejaría los de enero al final de la página. Es el
// orden de "qué viene ahora", y también el que decide quién va destacado.
//
// Con los días empatados manda el nombre, para que dos personas del mismo día
// no bailen de sitio entre un repintado y el siguiente.
QList<Birthday *> BirthdayView::sorted() const {
    QList<Birthday *> found;
    if (!m_list) return found;

    for (Birthday *b : *m_list)
        if (b->isValid()) found.append(b);

    std::sort(found.begin(), found.end(), [](Birthday *a, Birthday *b) {
        const int da = a->daysUntil(), db = b->daysUntil();
        return da != db ? da < db : a->name.localeAwareCompare(b->name) < 0;
    });
    return found;
}

// El otro orden: el calendario de siempre, de enero a diciembre, sin que el mes
// de hoy sea nada especial. Es la agenda del año entera, que es como se escribe
// una lista de cumpleaños en un papel.
QList<Birthday *> BirthdayView::byCalendar() const {
    QList<Birthday *> found;
    if (!m_list) return found;

    for (Birthday *b : *m_list)
        if (b->isValid()) found.append(b);

    std::sort(found.begin(), found.end(), [](Birthday *a, Birthday *b) {
        if (a->month != b->month) return a->month < b->month;
        if (a->day != b->day) return a->day < b->day;
        return a->name.localeAwareCompare(b->name) < 0;
    });
    return found;
}

void BirthdayView::refresh() {
    // Se vacía todo menos el stretch final. Se ocultan además de borrarlas:
    // sacarlas del layout no las quita de la pantalla, y siguen pintadas (y
    // aceptando eventos) hasta que corre deleteLater().
    while (m_layout->count() > 1) {
        QLayoutItem *item = m_layout->takeAt(0);
        if (QWidget *w = item->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete item;
    }

    // El destacado sale siempre del orden por cercanía, mande el que mande en
    // la lista: "quién es el siguiente" no depende de cómo se esté mirando.
    const QList<Birthday *> all = sorted();
    int at = 0;

    if (all.isEmpty()) {
        auto *empty = new QLabel(L("Todavía no hay cumpleaños"));
        empty->setObjectName("meta");
        empty->setWordWrap(true);   // ver *Card widths*: no puede pedir su ancho
        empty->setContentsMargins(7, 8, 7, 4);
        m_layout->insertWidget(at++, empty);
        m_layout->insertWidget(at++, buildAddRow());
        return;
    }

    // Destacados: los que empatan en ser los más cercanos. Casi siempre es uno
    // solo; son varios cuando dos personas caen el mismo día, y entonces
    // destacar a una y no a la otra sería elegir por el usuario.
    const int soonest = all.first()->daysUntil();
    int highlighted = 0;
    while (highlighted < all.size() && all.at(highlighted)->daysUntil() == soonest) {
        m_layout->insertWidget(at++, buildHighlightCard(all.at(highlighted)));
        ++highlighted;
    }

    auto *line = new QFrame;
    line->setObjectName("calSeparator");
    line->setFixedHeight(1);
    m_layout->insertWidget(at++, line);

    // Por meses la lista es la agenda del año entera, destacados incluidos:
    // faltar alguien de su mes porque está en la tarjeta de arriba se lee como
    // un fallo, no como un resumen. Por cercanía es la continuación de lo de
    // arriba, y entonces repetirlo sí sobra.
    const QList<Birthday *> list = m_byMonth ? byCalendar() : all.mid(highlighted);
    m_layout->insertWidget(at++, buildHeader(int(list.size())));

    int lastMonth = 0;
    for (Birthday *b : list) {
        // Por cercanía, el mes es el de la vuelta que toca —así tras diciembre
        // viene el enero que viene—; por calendario es el suyo y ya está.
        const int month = m_byMonth ? b->month : b->nextDate().month();
        if (month != lastMonth) {
            lastMonth = month;
            m_layout->insertWidget(at++, buildMonthHeader(month));
        }
        m_layout->insertWidget(at++, buildRow(b));
    }

    if (list.isEmpty()) {
        auto *empty = new QLabel(L("Ninguno más por ahora"));
        empty->setObjectName("meta");
        empty->setWordWrap(true);
        empty->setContentsMargins(7, 8, 7, 4);
        m_layout->insertWidget(at++, empty);
    }

    m_layout->insertWidget(at++, buildAddRow());
}

// Separador de mes: el nombre y una línea que llega hasta el borde. Es lo que
// convierte una lista larga en una agenda, y era como venía escrita la lista
// que estrenó la página.
QWidget *BirthdayView::buildMonthHeader(int month) {
    auto *row = new QWidget;
    auto *l = new QHBoxLayout(row);
    l->setContentsMargins(4, 9, 4, 3);
    l->setSpacing(7);

    auto *name = new QLabel(monthName(month));
    name->setObjectName("bdayMonth");
    l->addWidget(name);

    auto *rule = new QFrame;
    rule->setObjectName("bdayMonthRule");
    rule->setFixedHeight(1);
    l->addWidget(rule, 1);
    return row;
}

QWidget *BirthdayView::buildHeader(int listed) {
    auto *row = new QWidget;
    auto *l = new QHBoxLayout(row);
    l->setContentsMargins(4, 6, 4, 2);
    l->setSpacing(4);

    // Los dos órdenes, como dos pestañas pequeñas. El encendido hace de rótulo
    // de la sección, así que la cabecera dice a la vez qué estás mirando y cómo
    // cambiarlo, sin gastar una fila más.
    auto tab = [this, l](const QString &text, bool chosen, bool value) {
        auto *b = new QToolButton;
        b->setObjectName("bdayTab");
        b->setText(text);
        b->setProperty("chosen", chosen);
        b->setCursor(Qt::PointingHandCursor);
        connect(b, &QToolButton::clicked, this, [this, value] {
            if (value == m_byMonth) return;
            m_byMonth = value;
            refresh();
            emit orderChanged(m_byMonth);
        });
        l->addWidget(b);
    };
    tab(L("PRÓXIMOS"), !m_byMonth, false);
    tab(L("MESES"), m_byMonth, true);
    l->addStretch();

    // El recuento dice cuántos hay ahí abajo, y eso no es lo mismo en los dos
    // órdenes: por cercanía son los que quedan tras el destacado, por meses son
    // todos. Dos rótulos distintos lo dejan claro sin explicar nada.
    auto *count = new QLabel(listed == 0 ? QString()
                                         : (m_byMonth ? L("%1 EN TOTAL").arg(listed)
                                                      : L("%1 MÁS").arg(listed)));
    count->setObjectName("meta");
    count->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    l->addWidget(count);
    return row;
}

QWidget *BirthdayView::buildHighlightCard(Birthday *b) {
    const bool isToday = b->isToday();

    auto *card = new BirthdayRow;
    card->setObjectName("bdayToday");
    card->setCursor(Qt::PointingHandCursor);
    // Pulsar la tarjeta abre la misma ficha que pulsar una fila. Los botones de
    // dentro se quedan con su propio clic, así que no se pisan.
    card->click = [this, b, card] { emit editRequested(b, card); };

    auto *col = new QVBoxLayout(card);
    col->setContentsMargins(10, 9, 10, 9);
    col->setSpacing(8);

    auto *head = new QHBoxLayout;
    head->setContentsMargins(0, 0, 0, 0);
    head->setSpacing(9);
    head->addWidget(new Avatar(b->initials(), nameTint(b->name), kAvatarCard), 0,
                    Qt::AlignVCenter);

    auto *texts = new QVBoxLayout;
    texts->setContentsMargins(0, 0, 0, 0);
    texts->setSpacing(1);

    auto *name = new ElidedLabel(b->name.isEmpty() ? L("Sin nombre") : b->name,
                                 QColor(Theme::fg()));
    name->setObjectName("bdayName");
    name->setToolTip(b->name);
    texts->addWidget(name);

    // El de hoy no necesita fecha —es hoy—; el que está por venir sí, porque la
    // píldora solo dice cuánto falta y "en 3 semanas" no es un día del mes.
    QStringList meta;
    if (!isToday) meta << b->dateLabel();
    if (const QString sub = b->subtitle(); !sub.isEmpty()) meta << sub;
    auto *metaLabel = new ElidedLabel(meta.join(" · ").toUpper(), QColor(Theme::muted()));
    metaLabel->setObjectName("meta");
    texts->addWidget(metaLabel);
    head->addLayout(texts, 1);

    // Píldora: o ya está felicitado, o cuánto falta.
    auto *chip = new QLabel(b->greeted() ? L("FELICITADO") : b->whenLabel().toUpper());
    chip->setObjectName(b->greeted() ? "bdayDoneChip" : "bdayTodayChip");
    head->addWidget(chip, 0, Qt::AlignTop);
    col->addLayout(head);

    // Los botones van en una fila propia, no al lado del nombre: llevan texto,
    // y una fila de anchos fijos es justo la trampa de *Card widths*.
    auto *actions = new QHBoxLayout;
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(6);

    // Sonando manda pararlo: un botón de felicitar mientras suena la alarma
    // deja al usuario sin la única acción que quiere en ese momento.
    if (b->ringing) {
        auto *stop = new QPushButton(L("Detener aviso"));
        stop->setObjectName("bdayStop");   // el rojo vive en la hoja, no aquí
        stop->setCursor(Qt::PointingHandCursor);
        connect(stop, &QPushButton::clicked, this, [this, b] { emit dismissRequested(b); });
        actions->addWidget(stop);
    } else if (isToday) {
        // Solo el día que toca: felicitar a alguien con tres meses de antelación
        // no quiere decir nada, y la marca se guarda por año.
        auto *greet = new QPushButton(b->greeted() ? L("Sin felicitar") : L("Felicitar"));
        greet->setCursor(Qt::PointingHandCursor);
        greet->setToolTip(L("Marca que ya le has felicitado este año"));
        connect(greet, &QPushButton::clicked, this, [this, b] { emit greetToggled(b); });
        actions->addWidget(greet);
    }

    auto *remind = new QToolButton;
    remind->setObjectName("bdayGhost");
    remind->setText(b->remindAt.isValid()
                        ? L("Aviso a las %1").arg(b->remindAt.toString("HH:mm"))
                        : L("Avisarme…"));
    remind->setCursor(Qt::PointingHandCursor);
    connect(remind, &QToolButton::clicked, this,
            [this, b, remind] { emit remindRequested(b, remind); });
    actions->addWidget(remind);
    actions->addStretch();
    col->addLayout(actions);

    return card;
}

QWidget *BirthdayView::buildRow(Birthday *b) {
    auto *row = new BirthdayRow;
    row->click = [this, b, row] { emit editRequested(b, row); };

    auto *l = new QHBoxLayout(row);
    l->setContentsMargins(6, 5, 7, 5);
    l->setSpacing(9);
    l->addWidget(new Avatar(b->initials(), nameTint(b->name), kAvatarRow), 0,
                 Qt::AlignVCenter);

    auto *texts = new QVBoxLayout;
    texts->setContentsMargins(0, 0, 0, 0);
    texts->setSpacing(0);

    auto *name = new ElidedLabel(b->name.isEmpty() ? L("Sin nombre") : b->name,
                                 QColor(Theme::fg()));
    name->setObjectName("bdayRowName");
    name->setToolTip(b->name);
    texts->addWidget(name);

    // La segunda línea puede quedar vacía (sin año y sin relación): entonces no
    // se monta, en vez de dejar un hueco bajo el nombre.
    if (const QString sub = b->subtitle(); !sub.isEmpty()) {
        auto *meta = new ElidedLabel(sub, QColor(Theme::muted()));
        meta->setObjectName("meta");
        texts->addWidget(meta);
    }
    l->addLayout(texts, 1);

    // Campana pequeña si tiene aviso puesto: es lo que distingue un cumpleaños
    // que va a sonar de uno que solo está apuntado.
    if (b->remindAt.isValid()) {
        auto *bell = new QLabel;
        bell->setFixedSize(11, 11);
        bell->setPixmap(paintIcon("bell", QColor(Theme::muted()), 11).pixmap(11, 11));
        bell->setToolTip(L("Aviso a las %1").arg(b->remindAt.toString("HH:mm")));
        l->addWidget(bell, 0, Qt::AlignVCenter);
    }

    auto *right = new QVBoxLayout;
    right->setContentsMargins(0, 0, 0, 0);
    right->setSpacing(0);

    auto *date = new QLabel(b->dateLabel());
    date->setObjectName("bdayDate");
    date->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    right->addWidget(date);

    auto *when = new QLabel(b->whenLabel());
    when->setObjectName("bdayWhen");
    when->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    // Lo de esta semana en el acento; lo que queda lejos, apagado. Una página
    // entera de texto en color de acento no destaca nada.
    when->setProperty("soon", b->daysUntil() <= kSoonDays);
    right->addWidget(when);
    l->addLayout(right);

    return row;
}

// Fila de alta, con el mismo aspecto que las demás para que se lea como "la
// siguiente" y no como un botón suelto al final de la página.
QWidget *BirthdayView::buildAddRow() {
    auto *add = new BirthdayRow;
    auto *l = new QHBoxLayout(add);
    l->setContentsMargins(6, 6, 7, 6);
    l->setSpacing(9);

    auto *slot = new QLabel;
    slot->setFixedSize(kAvatarRow, kAvatarRow);
    slot->setPixmap(paintIcon("checkdots", QColor(Theme::muted()), kAvatarRow)
                        .pixmap(kAvatarRow, kAvatarRow));
    l->addWidget(slot);

    auto *text = new QLabel(L("Añadir un cumpleaños"));
    text->setObjectName("bdayRowName");
    l->addWidget(text, 1);

    add->click = [this, add] { emit addRequested(add); };
    return add;
}
