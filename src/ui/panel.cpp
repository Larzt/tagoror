#include "ui/panel.hpp"
#include "audio/alarm.hpp"
#include "audio/recorder.hpp"
#include "ui/calendar.hpp"
#include "ui/dragwidgets.hpp"
#include "ui/notecard.hpp"
#include "ui/popup.hpp"
#include "ui/workarea.hpp"

#include "core/lang.hpp"

#include <QApplication>
#include <QAudioDevice>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMediaDevices>
#include <QMenu>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

constexpr int kShadowMargin = 22;   // hueco alrededor del marco para la sombra
constexpr int kShellMinWidth = 300;
constexpr int kShellMinHeight = 340;   // el calendario necesita más alto que la lista

// Las rutas de datos son largas y la fila del menú las corta por la mitad;
// bajo el home se muestran con ~ para que se lea la parte que importa.
QString prettyPath(const QString &path) {
    const QString home = QDir::homePath();
    return path.startsWith(home + "/") ? "~" + path.mid(home.size()) : path;
}

QToolButton *iconButton(const QString &kind, const QString &tip) {
    auto *b = new QToolButton;
    b->setIcon(paintIcon(kind, QColor(Theme::muted())));
    b->setIconSize(QSize(16, 16));
    b->setFixedSize(26, 26);
    b->setCursor(Qt::PointingHandCursor);
    b->setToolTip(L(tip));
    b->setProperty("iconKind", kind);
    // El rótulo en español se guarda tal cual: es la clave con la que
    // retranslate() lo vuelve a traducir sin rehacer la cabecera.
    b->setProperty("tip", tip);
    return b;
}

// Etiqueta que ve el usuario para un instante concreto.
QString dueLabel(const QDateTime &when) {
    return Lang::locale().toString(when, "ddd d MMM HH:mm");
}

// El icono de la bandeja cuando hay un aviso sonando. Se compone a varios
// tamaños porque la bandeja elige el suyo según el panel y la escala, y un
// solo mapa de bits se ve borroso en cuanto no coincide.
QIcon alertIcon() {
    QIcon icon;
    for (int px : {16, 22, 24, 32, 48})
        icon.addPixmap(paintIcon("bell", QColor("#ff7a6b"), px).pixmap(px, px));
    return icon;
}

// Un eje del anclaje. La ventana crece y encoge dejando quieta su esquina
// superior izquierda, que es lo que espera cualquiera; solo cuando por ahí no
// cabe se ancla el extremo contrario, y entonces el panel se abre hacia atrás:
// con el dock pegado al borde derecho, hacia la izquierda; pegado al inferior,
// hacia arriba. Si no cabe de ninguna de las dos maneras, se recorta.
//
// Que la primera opción sea "no moverse" es lo que hace que plegar no mueva el
// dock ni un píxel y que desplegar devuelva el panel justo donde estaba: si el
// panel cabía ahí, el dock que sale de su esquina también, y al revés.
// `end` es el primer punto que ya queda fuera.
int anchorAxis(int pos, int len, int newLen, int lo, int end) {
    const int keepStart = pos;
    const int keepEnd = pos + len - newLen;
    if (keepStart >= lo && keepStart + newLen <= end) return keepStart;
    if (keepEnd >= lo && keepEnd + newLen <= end) return keepEnd;
    return qBound(lo, keepStart, qMax(lo, end - newLen));
}

// Empuja un rectángulo adentro del sitio donde se admite colocarlo.
QPoint clampInto(QPoint pos, const QSize &size, const QRect &area) {
    if (!area.isValid()) return pos;
    return {qBound(area.left(), pos.x(), qMax(area.left(), area.right() + 1 - size.width())),
            qBound(area.top(), pos.y(), qMax(area.top(), area.bottom() + 1 - size.height()))};
}

}  // namespace

// ---------------------------------------------------------------------------

Panel::Panel() {
    setAttribute(Qt::WA_TranslucentBackground);

    // El Store resuelve la carpeta de datos y las migraciones en su
    // constructor, antes de que nadie toque disco.
    m_store.load();
    m_theme.accent = m_store.prefs().accent;
    m_theme.opacity = m_store.prefs().opacity;
    m_expandedSize = m_store.prefs().windowSize;
    VoiceRecorder::setPreferredInput(m_store.prefs().input);

    m_alarm = new Alarm(this);
    m_dueTimer = new QTimer(this);
    m_dueTimer->setInterval(5000);
    connect(m_dueTimer, &QTimer::timeout, this, &Panel::checkReminders);
    m_dueTimer->start();

    buildShell();
    buildTray();
    rebuildList();
    applyTheme();
    checkReminders();   // puede haber vencido algo con la app cerrada

    if (!m_expandedSize.isValid())
        m_expandedSize = QSize(352 + kShadowMargin * 2, 560);
    resize(m_expandedSize);
    showPage(m_shell);
    applyWindowFlags();
}

Panel::~Panel() {
    // El Store vuelve a guardar al destruirse, y para entonces este panel ya
    // no existe: se le quita el gancho antes de que pueda llamarlo.
    m_store.beforeSave = nullptr;
    syncPrefs();
    m_store.save();
}

void Panel::buildShell() {
    m_store.beforeSave = [this] { syncPrefs(); };

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(kShadowMargin, kShadowMargin, kShadowMargin, kShadowMargin);

    m_stack = new QStackedWidget;
    outer->addWidget(m_stack);

    // ---- panel expandido --------------------------------------------------
    m_shell = new QFrame;
    m_shell->setObjectName("shell");
    m_shell->setMinimumWidth(kShellMinWidth);   // ya no es fijo: se redimensiona

    auto *shadow = new QGraphicsDropShadowEffect(m_shell);
    shadow->setBlurRadius(56);
    shadow->setOffset(0, 20);
    shadow->setColor(QColor(0, 0, 0, 160));
    m_shell->setGraphicsEffect(shadow);

    auto *col = new QVBoxLayout(m_shell);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);

    col->addWidget(buildHeader());

    // barra de búsqueda (oculta por defecto)
    m_searchBar = new QWidget;
    auto *sl = new QHBoxLayout(m_searchBar);
    sl->setContentsMargins(10, 8, 10, 8);
    m_search = new QLineEdit;
    m_search->setPlaceholderText(L("Filtrar notas…"));
    connect(m_search, &QLineEdit::textChanged, this, &Panel::applyFilter);
    sl->addWidget(m_search);
    m_searchBar->hide();
    col->addWidget(m_searchBar);

    col->addWidget(buildBody(), 1);
    col->addWidget(buildFooter());
    m_stack->addWidget(m_shell);

    // ---- icono plegado ----------------------------------------------------
    m_badge = buildBadge();
    m_stack->addWidget(m_badge);

    setMinimumSize(kShellMinWidth + kShadowMargin * 2, kShellMinHeight);
}

QFrame *Panel::buildHeader() {
    auto *header = new DragBar;
    header->setObjectName("header");
    header->setCursor(Qt::OpenHandCursor);

    auto *l = new QHBoxLayout(header);
    l->setContentsMargins(12, 8, 10, 8);
    l->setSpacing(4);

    m_titleLabel = new QLabel(L("Códice"));
    m_titleLabel->setObjectName("title");
    l->addWidget(m_titleLabel);
    l->addStretch();

    auto *search = iconButton("search", "Buscar");
    auto *add = iconButton("plus", "Nueva nota");
    m_calendarBtn = iconButton("calendar", "Calendario");
    auto *gear = iconButton("gear", "Ajustes");
    auto *min = iconButton("minus", "Plegar a icono");
    m_headerButtons = {m_calendarBtn, search, add, gear, min};

    connect(search, &QToolButton::clicked, this, &Panel::toggleSearch);
    connect(min, &QToolButton::clicked, this, &Panel::collapse);
    connect(add, &QToolButton::clicked, this, [this, add] { openNewNoteMenu(add); });
    connect(gear, &QToolButton::clicked, this, [this, gear] { openSettings(gear); });
    connect(m_calendarBtn, &QToolButton::clicked, this, &Panel::toggleCalendar);

    for (QToolButton *b : m_headerButtons) l->addWidget(b);
    return header;
}

QWidget *Panel::buildBody() {
    // ---- lista de notas ---------------------------------------------------
    m_listHost = new QWidget;
    m_listHost->setObjectName("listHost");
    m_listLayout = new QVBoxLayout(m_listHost);
    m_listLayout->setContentsMargins(8, 8, 8, 8);
    m_listLayout->setSpacing(7);
    m_listLayout->addStretch();

    // Cartel para cuando no queda ninguna nota: con la lista vacía el panel
    // no ofrecía ninguna pista de por dónde empezar.
    m_empty = new QWidget;
    auto *el = new QVBoxLayout(m_empty);
    el->setContentsMargins(0, 34, 0, 34);
    el->setSpacing(10);

    auto *emptyIcon = new QLabel;
    emptyIcon->setAlignment(Qt::AlignCenter);
    emptyIcon->setPixmap(paintIcon("notes", QColor(Theme::muted()), 34).pixmap(34, 34));
    el->addWidget(emptyIcon);

    m_emptyText = new QLabel(L("Todavía no hay notas"));
    m_emptyText->setObjectName("meta");
    m_emptyText->setAlignment(Qt::AlignCenter);
    el->addWidget(m_emptyText);

    m_emptyBtn = new QPushButton(L("Crear la primera"));
    m_emptyBtn->setCursor(Qt::PointingHandCursor);
    connect(m_emptyBtn, &QPushButton::clicked, this, [this] { openNewNoteMenu(m_emptyBtn); });

    auto *btnRow = new QHBoxLayout;
    btnRow->addStretch();
    btnRow->addWidget(m_emptyBtn);
    btnRow->addStretch();
    el->addLayout(btnRow);

    m_listLayout->insertWidget(0, m_empty);
    m_empty->hide();

    m_scroll = new QScrollArea;
    m_scroll->setWidget(m_listHost);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setMinimumHeight(150);   // sin tope máximo: crece con la ventana
    m_scroll->viewport()->setAutoFillBackground(false);
    // Con selector, no sin él: una regla suelta se hereda por todos los hijos
    // y les pisa el fondo (dejaba el botón de "lista vacía" sin relleno).
    m_scroll->viewport()->setObjectName("scrollViewport");

    // ---- calendario -------------------------------------------------------
    m_calendar = new CalendarView(m_theme);
    m_calendar->setSource(&m_store.notes());
    connect(m_calendar, &CalendarView::createRequested, this, &Panel::askReminderTime);
    connect(m_calendar, &CalendarView::noteActivated, this, &Panel::revealNote);
    connect(m_calendar, &CalendarView::dismissRequested, this, &Panel::dismissNote);

    m_body = new QStackedWidget;
    m_body->addWidget(m_scroll);
    m_body->addWidget(m_calendar);
    return m_body;
}

QFrame *Panel::buildFooter() {
    auto *footer = new QFrame;
    footer->setObjectName("footer");
    auto *l = new QHBoxLayout(footer);
    l->setContentsMargins(11, 7, 7, 5);
    l->setSpacing(8);

    auto *dot = new QLabel;
    dot->setFixedSize(6, 6);
    dot->setStyleSheet("background:#6fcf97; border-radius:3px;");
    l->addWidget(dot);

    m_footerText = new QLabel;
    m_footerText->setObjectName("meta");
    l->addWidget(m_footerText);
    l->addStretch();

    m_footerHint = new QLabel(L("clic dcho · opciones"));
    m_footerHint->setObjectName("meta");
    l->addWidget(m_footerHint);
    l->addWidget(new GripCorner, 0, Qt::AlignBottom);
    return footer;
}

QWidget *Panel::buildBadge() {
    auto *host = new QWidget;
    auto *hl = new QHBoxLayout(host);
    hl->setContentsMargins(0, 0, 0, 0);

    auto *btn = new DragButton;
    btn->setObjectName("badge");
    btn->setFixedSize(56, 56);
    btn->setIcon(paintIcon("notes", QColor(Theme::fg()), 22));
    btn->setIconSize(QSize(22, 22));
    btn->setCursor(Qt::PointingHandCursor);
    btn->setToolTip(L("Abrir Códice · arrastra para mover"));
    btn->setStyleSheet(QString("QToolButton#badge { background:%1; border:1px solid %2;"
                               "border-radius:18px; } QToolButton#badge:hover { background:%3; }")
                           .arg(m_theme.card(), Theme::line(), Theme::hover()));

    auto *shadow = new QGraphicsDropShadowEffect(btn);
    shadow->setBlurRadius(40);
    shadow->setOffset(0, 14);
    shadow->setColor(QColor(0, 0, 0, 140));
    btn->setGraphicsEffect(shadow);
    connect(btn, &QToolButton::clicked, this, &Panel::expand);

    m_badgeCount = new QLabel(btn);
    m_badgeCount->setAlignment(Qt::AlignCenter);
    m_badgeCount->setFixedSize(20, 20);
    m_badgeCount->move(40, -2);

    // Arriba a la izquierda: la ventana plegada mide exactamente lo que el
    // dock, así que aquí no sobra sitio, y de la esquina de la pantalla por la
    // que se pliega y se abre ya se encarga anchoredTopLeft.
    hl->addWidget(btn, 0, Qt::AlignTop | Qt::AlignLeft);
    hl->addStretch();
    return host;
}

// ---------------------------------------------------------------------------

void Panel::applyTheme() {
    setStyleSheet(m_theme.sheet());

    for (QToolButton *b : m_headerButtons)
        b->setIcon(paintIcon(b->property("iconKind").toString(), QColor(Theme::muted())));
    if (m_calendarBtn && m_calendarBtn->property("active").toBool())
        setCalendarActive(true);      // el activo va en color de acento

    if (m_calendar) m_calendar->setTheme(m_theme);
    applyBadgeAlert();

    // El dock se pinta en línea porque su fondo depende de la opacidad actual.
    if (auto *badge = m_badge->findChild<QToolButton *>("badge"))
        badge->setStyleSheet(QString("QToolButton#badge { background:%1; border:1px solid %2;"
                                     "border-radius:18px; }"
                                     "QToolButton#badge:hover { background:%3; }")
                                 .arg(m_theme.card(), Theme::line(), Theme::hover()));
}

void Panel::setLanguage(Lang::Code code) {
    if (code == m_store.prefs().lang) return;
    m_store.prefs().lang = code;
    Lang::setCurrent(code);
    retranslate();
    save();
}

// Los textos fijos de la ventana se vuelven a poner uno a uno; las tarjetas y
// el calendario se rehacen enteros, que sale más simple que ir buscando cada
// etiqueta dentro de ellos y aquí no hay nada que perder salvo el foco.
void Panel::retranslate() {
    m_titleLabel->setText(L("Códice"));
    m_search->setPlaceholderText(L("Filtrar notas…"));
    m_emptyText->setText(L("Todavía no hay notas"));
    m_emptyBtn->setText(L("Crear la primera"));
    m_footerHint->setText(L("clic dcho · opciones"));

    for (QToolButton *b : m_headerButtons)
        b->setToolTip(L(b->property("tip").toString()));
    // El del calendario además dice en qué página estás, así que se rehace
    // por su propio camino.
    setCalendarActive(m_calendarBtn->property("active").toBool());

    buildTrayMenu();
    applyBadgeAlert();   // la ayuda del icono de la bandeja lleva texto
    if (m_calendar) m_calendar->retranslate();
    // Rehace las tarjetas y, de paso, el pie, el calendario y el dock.
    rebuildList();
}

QList<NoteCard *> Panel::cards() const {
    QList<NoteCard *> found;
    for (int i = 0; i < m_listLayout->count(); ++i)
        if (auto *card = qobject_cast<NoteCard *>(m_listLayout->itemAt(i)->widget()))
            found.append(card);
    return found;
}

void Panel::rebuildList() {
    // limpiar tarjetas existentes (el cartel de vacío y el stretch se quedan)
    for (int i = m_listLayout->count() - 1; i >= 0; --i) {
        QWidget *w = m_listLayout->itemAt(i)->widget();
        if (!qobject_cast<NoteCard *>(w)) continue;
        delete m_listLayout->takeAt(i);
        w->deleteLater();
    }

    for (Note *n : m_store.notes()) {
        auto *card = new NoteCard(n, m_theme);
        connect(card, &NoteCard::dirty, this, &Panel::scheduleSave);
        connect(card, &NoteCard::dirty, this, &Panel::refreshCalendar);
        connect(card, &NoteCard::deleteRequested, this, &Panel::removeNote);
        connect(card, &NoteCard::dismissRequested, this, &Panel::dismissNote);
        m_listLayout->insertWidget(m_listLayout->count() - 2, card);
    }

    m_empty->setVisible(m_store.notes().isEmpty());
    m_badgeCount->setText(QString::number(m_store.count()));
    refreshFooter();
    refreshCalendar();
    applyBadgeAlert();
    if (m_search && !m_search->text().isEmpty())
        applyFilter(m_search->text());
}

void Panel::refreshFooter() {
    if (!m_footerText) return;

    if (m_body && m_body->currentWidget() == m_calendar) {
        int scheduled = 0;
        for (Note *n : m_store.notes())
            if (n->isScheduled()) ++scheduled;
        m_footerText->setText(L("%1 CON FECHA").arg(scheduled));
        return;
    }
    m_footerText->setText(L("%1 EN EL CÓDICE").arg(m_store.count()));
}

void Panel::addNote(Note::Type type) {
    auto *n = new Note;
    n->type = type;
    n->title = type == Note::Check    ? L("Nueva lista")
             : type == Note::Reminder ? L("Nuevo recordatorio")
             : type == Note::Voice    ? L("Nota de voz")
                                      : L("Nueva nota");
    if (type == Note::Reminder) {
        // Con instante real desde el principio: así el recordatorio recién
        // creado suena y además aparece en el calendario.
        QDateTime when(QDate::currentDate(), QTime(18, 0));
        if (when <= QDateTime::currentDateTime()) when = when.addDays(1);
        n->dueAtMs = when.toMSecsSinceEpoch();
        n->due = dueLabel(when);
    }
    m_store.add(n);
    rebuildList();
}

void Panel::removeNote(Note *n) {
    m_store.remove(n);
    rebuildList();
}

void Panel::toggleSearch() {
    m_searchBar->setVisible(!m_searchBar->isVisible());
    if (m_searchBar->isVisible()) {
        showNotes();          // filtrar con el calendario delante no se ve
        m_search->setFocus();
    } else {
        m_search->clear();
        applyFilter(QString());
    }
}

void Panel::applyFilter(const QString &q) {
    int shown = 0;
    for (NoteCard *card : cards()) {
        const bool ok = card->note()->matches(q);
        card->setVisible(ok);
        if (ok) ++shown;
    }
    if (q.isEmpty()) refreshFooter();
    else m_footerText->setText(L("%1 DE %2").arg(shown).arg(m_store.count()));
}

void Panel::bringToFront() {
    expand();
    show();
    raise();
    activateWindow();
}

// --- calendario ------------------------------------------------------------

void Panel::toggleCalendar() {
    if (m_body->currentWidget() == m_calendar) {
        showNotes();
        return;
    }
    m_searchBar->hide();
    m_body->setCurrentWidget(m_calendar);
    m_calendar->refresh();
    setCalendarActive(true);
    refreshFooter();
}

void Panel::showNotes() {
    if (m_body->currentWidget() != m_calendar) return;

    m_body->setCurrentWidget(m_scroll);
    setCalendarActive(false);
    refreshFooter();
}

// El botón no cambia de icono al abrir el calendario: se queda encendido.
// Así el icono siempre dice adónde lleva y el realce dice dónde estás.
void Panel::setCalendarActive(bool on) {
    m_calendarBtn->setProperty("active", on);
    m_calendarBtn->setToolTip(on ? L("Ver notas") : L("Calendario"));
    m_calendarBtn->setIcon(paintIcon("calendar",
                                     on ? m_theme.accent : QColor(Theme::muted())));
    // Una propiedad dinámica no repinta sola.
    m_calendarBtn->style()->unpolish(m_calendarBtn);
    m_calendarBtn->style()->polish(m_calendarBtn);
}

void Panel::refreshCalendar() {
    // Cada tecleo en una tarjeta pasa por aquí: si el calendario no está a la
    // vista no hay nada que repintar, y al volver a él ya se refresca.
    if (m_calendar && m_body->currentWidget() == m_calendar) m_calendar->refresh();
}

void Panel::askReminderTime(const QDate &day, QWidget *anchor) {
    auto *menu = new Popup(m_theme, this);
    menu->addHeader(Lang::locale().toString(day, "dddd d MMMM"));

    const QDateTime now = QDateTime::currentDateTime();
    for (const QTime &t : {QTime(9, 0), QTime(12, 0), QTime(15, 0), QTime(18, 0), QTime(21, 0)}) {
        const QDateTime when(day, t);
        menu->addItem("clock", t.toString("HH:mm"),
                      when <= now ? L("Ya pasado") : QString(),
                      [this, when] { createReminder(when); });
    }

    menu->addSeparator();
    menu->addHeader(L("A mano · HH:mm"));
    menu->addEditor(L("p. ej. 20:30"), QString(), [this, day](const QString &value) {
        const QTime t = QTime::fromString(value.trimmed(), "HH:mm");
        if (t.isValid()) createReminder(QDateTime(day, t));
    });
    menu->showUnder(anchor);
}

void Panel::createReminder(const QDateTime &when) {
    auto *n = new Note;
    n->type = Note::Reminder;
    n->title = L("Nuevo recordatorio");
    n->dueAtMs = when.toMSecsSinceEpoch();
    n->due = dueLabel(when);

    m_store.add(n);
    rebuildList();
    // Se queda en el calendario, sobre el día donde acaba de aparecer.
    m_calendar->goTo(when.date());
}

void Panel::revealNote(Note *n) {
    showNotes();
    for (NoteCard *card : cards()) {
        if (card->note() != n) continue;
        card->show();                       // pudo dejarlo oculto un filtro
        m_scroll->ensureWidgetVisible(card);
        card->focusTitle();
        return;
    }
}

// --- recordatorios ---------------------------------------------------------

bool Panel::anyRinging() const {
    for (Note *n : m_store.notes())
        if (n->ringing) return true;
    return false;
}

void Panel::checkReminders() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    bool started = false;
    for (Note *n : m_store.notes()) {
        if (n->isDue(now) && !n->ringing) {
            n->ringing = true;
            started = true;
        }
    }
    if (!started) return;

    m_alarm->start();
    refreshDueCards();
    refreshCalendar();
    applyBadgeAlert();
}

void Panel::dismissNote(Note *n) {
    n->ringing = false;
    n->fired = true;                 // no vuelve a sonar por su cuenta
    if (!anyRinging()) m_alarm->stop();
    refreshDueCards();
    refreshCalendar();
    applyBadgeAlert();
    save();
}

void Panel::refreshDueCards() {
    for (NoteCard *card : cards()) card->refreshDue();
}

void Panel::applyBadgeAlert() {
    const bool alert = anyRinging();

    auto *badge = m_badge->findChild<QToolButton *>("badge");
    if (badge) {
        // Plegado, el dock es lo único que se ve: cambia de icono y de color
        // para que se note que hay un aviso esperando.
        badge->setIcon(paintIcon(alert ? "bell" : "notes",
                                 QColor(alert ? "#ff7a6b" : Theme::fg()), 22));
        badge->setToolTip(alert ? L("Recordatorio vencido · clic para parar")
                                : L("Abrir Códice · arrastra para mover"));
    }
    if (m_tray) {
        // Escondida en la bandeja, el icono es la única señal de que algo ha
        // vencido; el mismo cambio que hace el dock en el escritorio.
        m_tray->setIcon(alert ? alertIcon() : qApp->windowIcon());
        m_tray->setToolTip(alert ? L("Recordatorio vencido") : L("Códice"));
    }
    if (m_badgeCount)
        m_badgeCount->setStyleSheet(
            QString("background:%1; color:#0d1014; border-radius:10px;"
                    "font-size:11px; font-weight:600;")
                .arg(alert ? QString("#ff7a6b") : m_theme.accent.name()));
}

// --- selectores ------------------------------------------------------------

void Panel::openNewNoteMenu(QWidget *anchor) {
    auto *menu = new Popup(m_theme, this);
    menu->addHeader(L("Nueva nota"));
    menu->addItem("text", L("Texto"), L("Una nota libre"),
                  [this] { addNote(Note::Text); });
    menu->addItem("check", L("Checklist"), L("Tareas con progreso"),
                  [this] { addNote(Note::Check); });
    menu->addItem("reminder", L("Recordatorio"), L("Con fecha y aviso"),
                  [this] { addNote(Note::Reminder); });
    menu->addItem("voice", L("Nota de voz"), L("Graba desde el micrófono"),
                  [this] { addNote(Note::Voice); });
    menu->showUnder(anchor);
}

void Panel::openSettings(QWidget *anchor) {
    auto *menu = new Popup(m_theme, this);

    menu->addHeader(L("Acento"));
    const QList<QColor> swatches = {QColor("#7c9cff"), QColor("#6fcf97"), QColor("#f2b757"),
                                    QColor("#ff7a6b"), QColor("#b98cff"), QColor("#4ecdc4")};
    menu->addSwatches(swatches, m_theme.accent, [this](const QColor &c) {
        m_theme.accent = c;
        m_store.prefs().accent = c;
        applyTheme();
        rebuildList();   // las tarjetas llevan el acento pintado en línea
        save();
    });

    menu->addItem("palette", L("Color personalizado…"),
                  m_theme.accent.name(), [this, anchor] { openAccentEditor(anchor); });

    menu->addSeparator();
    menu->addHeader(L("Opacidad"));
    menu->addSlider(40, 100, m_theme.opacity, [this](int v) {
        m_theme.opacity = v;
        m_store.prefs().opacity = v;
        applyTheme();
        scheduleSave();
    });

    menu->addSeparator();
    menu->addHeader(L("Idioma"));
    // Los nombres de los idiomas van en el suyo propio, no traducidos: quien
    // abre el menú con la interfaz en el idioma que no entiende tiene que
    // poder reconocer el otro.
    const Lang::Code lang = m_store.prefs().lang;
    for (const auto &[code, label] : {std::pair{Lang::Es, "Español"}, {Lang::En, "English"}})
        menu->addItem(code == lang ? "check" : "minus", label, QString(),
                      [this, code = code] { setLanguage(code); });

    menu->addSeparator();
    menu->addHeader(L("Ventana"));
    const bool onTop = m_store.prefs().onTop;
    menu->addItem(onTop ? "check" : "minus", L("Siempre encima"),
                  onTop ? L("Activado · por encima de todo")
                        : L("Desactivado · pegado al escritorio"),
                  [this] {
                      m_store.prefs().onTop = !m_store.prefs().onTop;
                      applyWindowFlags();
                      save();
                  });

#ifdef Q_OS_LINUX
    // Ver choosePlatform() en main.cpp: de esto depende que el panel pueda
    // abrirse hacia el centro de la pantalla y que "siempre encima" se cumpla.
    // Fuera de Linux no hay tal disyuntiva y la fila no pinta nada.
    const bool nativeWayland = QSettings().value("platform").toString() == "wayland";
    menu->addItem(nativeWayland ? "minus" : "check", L("Compatibilidad X11"),
                  nativeWayland ? L("Desactivada · Wayland nativo")
                                : L("Activada · %1").arg(qApp->platformName()),
                  [nativeWayland] {
                      QSettings().setValue("platform", nativeWayland ? "" : "wayland");
                  });
#endif

    menu->addSeparator();
    menu->addHeader(L("Datos"));
    menu->addItem("notes", L("Carpeta de guardado…"), prettyPath(appDataDir()),
                  [this] { chooseDataFolder(); });

    menu->addSeparator();
    menu->addHeader(L("Micrófono"));
    const QAudioDevice current = QMediaDevices::defaultAudioInput();
    const QByteArray chosenId = m_store.prefs().input;
    for (const QAudioDevice &dev : QMediaDevices::audioInputs()) {
        const bool chosen = chosenId.isEmpty() ? dev.id() == current.id() : dev.id() == chosenId;
        menu->addItem("mic", dev.description(), chosen ? L("En uso") : QString(),
                      [this, id = dev.id()] {
                          m_store.prefs().input = id;
                          VoiceRecorder::setPreferredInput(id);
                          save();
                      });
    }

    menu->addSeparator();
    menu->addItem("power", L("Salir"), QString(), [] { qApp->quit(); });
    menu->showUnder(anchor);
}

void Panel::openAccentEditor(QWidget *anchor) {
    auto *menu = new Popup(m_theme, this);
    menu->addHeader(L("Color de acento"));
    menu->addEditor("#7c9cff", m_theme.accent.name(), [this](const QString &text) {
        // QColor acepta también nombres ("teal"), no solo hexadecimal.
        const QColor picked(text.trimmed());
        if (!picked.isValid()) return;
        m_theme.accent = picked;
        m_store.prefs().accent = picked;
        applyTheme();
        rebuildList();
        save();
    });
    menu->showUnder(anchor);
}

void Panel::chooseDataFolder() {
    const QString to = QFileDialog::getExistingDirectory(
        this, L("Carpeta donde guardar las notas"), appDataDir());
    m_store.changeDataDir(to);
}

// --- plegado ---------------------------------------------------------------

// --- bandeja del sistema ----------------------------------------------------

void Panel::buildTray() {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) return;

    m_tray = new QSystemTrayIcon(qApp->windowIcon(), this);
    buildTrayMenu();
    connect(m_tray, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger ||
                    reason == QSystemTrayIcon::DoubleClick)
                    toggleFromTray();
            });
    m_tray->show();
    applyBadgeAlert();   // por si ya hay algo sonando al arrancar
}

// Aquí sí un QMenu, que es la excepción a la regla del resto de la aplicación:
// el menú de la bandeja no lo pinta este proceso sobre el marco translúcido,
// lo dibuja el escritorio (por DBusMenu en Plasma), y QSystemTrayIcon no
// admite otra cosa.
void Panel::buildTrayMenu() {
    if (!m_tray) return;

    delete m_trayMenu;                 // al cambiar de idioma se rehace entero
    m_trayMenu = new QMenu(this);      // con dueño: se va con el panel

    QAction *toggle = m_trayMenu->addAction(L("Mostrar"));
    connect(toggle, &QAction::triggered, this, &Panel::toggleFromTray);
    // La etiqueta dice lo que va a pasar, y eso depende de cómo esté la
    // ventana en el momento de abrir el menú.
    connect(m_trayMenu, &QMenu::aboutToShow, this,
            [this, toggle] { toggle->setText(isVisible() ? L("Ocultar") : L("Mostrar")); });

    m_trayMenu->addSeparator();
    QMenu *create = m_trayMenu->addMenu(L("Nueva nota"));
    const QList<QPair<QString, Note::Type>> types = {
        {L("Texto"), Note::Text},
        {L("Checklist"), Note::Check},
        {L("Recordatorio"), Note::Reminder},
        {L("Nota de voz"), Note::Voice},
    };
    for (const auto &[label, type] : types)
        connect(create->addAction(label), &QAction::triggered, this, [this, type] {
            addNote(type);
            show();
            raise();
            activateWindow();
        });

    m_trayMenu->addSeparator();
    connect(m_trayMenu->addAction(L("Salir")), &QAction::triggered, qApp, &QApplication::quit);

    m_tray->setContextMenu(m_trayMenu);
}

void Panel::toggleFromTray() {
    if (isVisible()) {
        hide();
        return;
    }
    // Se vuelve tal como se dejó, plegada o desplegada: esconder no es lo
    // mismo que plegar y no tiene por qué deshacerlo.
    show();
    raise();
    activateWindow();
}

void Panel::closeEvent(QCloseEvent *e) {
    if (!m_tray) {
        // Sin icono en la bandeja no queda de dónde recuperarla, así que
        // cerrar es salir (y el destructor guarda).
        e->accept();
        qApp->quit();
        return;
    }
    e->ignore();
    hide();
}

void Panel::showEvent(QShowEvent *e) {
    QWidget::showEvent(e);
    wmSkipTaskbar(winId());
}

void Panel::applyWindowFlags() {
    // Por defecto el widget se queda en el escritorio, por debajo del resto de
    // ventanas; "siempre encima" es opcional. En Wayland estos avisos son solo
    // una sugerencia: manda el compositor.
    Qt::WindowFlags flags = Qt::FramelessWindowHint | Qt::Tool;
    flags |= m_store.prefs().onTop ? Qt::WindowStaysOnTopHint : Qt::WindowStaysOnBottomHint;

    const bool wasVisible = isVisible();
    setWindowFlags(flags);
    if (wasVisible) show();   // setWindowFlags esconde la ventana
}

void Panel::showPage(QWidget *page) {
    // Las páginas ocultas de un QStackedWidget siguen contando para el
    // sizeHint; ignorándolas, la ventana puede encoger hasta el dock.
    for (int i = 0; i < m_stack->count(); ++i) {
        QWidget *w = m_stack->widget(i);
        const auto policy = (w == page) ? QSizePolicy::Preferred : QSizePolicy::Ignored;
        w->setSizePolicy(policy, policy);
    }
    // Con la política en Ignored no basta: un minimumSize explícito sigue
    // sumando al mínimo del QStackedLayout, y el panel plegado se quedaba con
    // una franja invisible de 300 px al lado del dock que se comía los clics.
    m_shell->setMinimumWidth(page == m_shell ? kShellMinWidth : 0);
    m_stack->setCurrentWidget(page);
}

void Panel::collapse() {
    m_expandedSize = size();
    const QRect panel = geometry();

    setMinimumSize(0, 0);
    showPage(m_badge);

    // El dock vuelve al sitio del que salió el panel, no a su esquina superior
    // izquierda: si estaba abajo y el panel se abrió hacia arriba, plegar tiene
    // que devolverlo abajo. Con m_dockOffset a cero -- nada más arrancar, sin
    // ningún despliegue previo -- eso es la esquina superior izquierda.
    const QSize dock = m_badge->sizeHint() + QSize(kShadowMargin * 2, kShadowMargin * 2);
    const QPoint inside(qBound(0, m_dockOffset.x(), qMax(0, panel.width() - dock.width())),
                        qBound(0, m_dockOffset.y(), qMax(0, panel.height() - dock.height())));
    setGeometry(QRect(clampInto(panel.topLeft() + inside, dock, placementArea()), dock));
    keepOnScreen();
}

void Panel::expand() {
    // La geometría del dock, antes de tocar nada: setMinimumSize() más abajo
    // ya estira la ventana por su cuenta, y entonces esta esquina deja de ser
    // la del dock y el panel se abre desde donde no es.
    const QRect dock = geometry();

    // Abrir el panel cuenta como enterarse: se calla la alarma.
    if (anyRinging()) {
        for (Note *n : m_store.notes())
            if (n->ringing) { n->ringing = false; n->fired = true; }
        m_alarm->stop();
        refreshDueCards();
        refreshCalendar();
        applyBadgeAlert();
        scheduleSave();
    }
    showPage(m_shell);
    setMinimumSize(kShellMinWidth + kShadowMargin * 2, kShellMinHeight);

    // Un tamaño guardado mayor de lo que cabe (otro monitor, otra resolución,
    // un panel del escritorio que recorta el área de trabajo) no entra de
    // ninguna manera: se recorta antes de aplicarlo, porque si no lo recorta
    // el gestor por su cuenta y además mueve la ventana.
    QSize target = m_expandedSize;
    if (const QRect area = placementArea(); area.isValid())
        target = target.boundedTo(area.size());

    // El panel sale de la esquina superior izquierda del dock hacia abajo y a
    // la derecha, que es donde estaba antes de plegarse; solo cuando por ahí no
    // cabe (el dock arrastrado contra el borde derecho o el inferior) se abre
    // hacia el otro lado.
    const QPoint at = anchoredTopLeft(dock, target);
    setGeometry(QRect(at, target));

    // De qué punto del panel ha salido el dock, para meterlo por ahí al
    // plegar. Es lo que hace que un dock abajo a la izquierda siga abajo a la
    // izquierda después de abrir y cerrar el panel.
    m_dockOffset = QPoint(qBound(0, dock.x() - at.x(), qMax(0, target.width() - dock.width())),
                          qBound(0, dock.y() - at.y(), qMax(0, target.height() - dock.height())));

    keepOnScreen();
}

// Dónde admite el gestor de ventanas que se ponga la ventana: la pantalla
// disponible, ensanchada con el margen de sombra (que no es marco visible y sí
// puede salirse), y recortada al área de trabajo del gestor. Ese recorte es el
// que manda: pedir una posición fuera de ella no falla, la corrige el gestor y
// la ventana aparece de un salto donde no se pidió.
QRect Panel::placementArea() const {
    const QScreen *sc = screen();
    if (!sc) return {};

    QRect area = sc->availableGeometry().adjusted(-kShadowMargin, -kShadowMargin,
                                                  kShadowMargin, kShadowMargin);
    if (const QRect wm = wmWorkArea(); wm.isValid()) area &= wm;
    return area;
}

// Por dónde crece o encoge la ventana. Nada de "hacia el centro de la
// pantalla": esa regla mandaba el dock a la otra punta del monitor en cuanto el
// panel pasaba de la mitad, y con el área de trabajo recortada (ver
// placementArea) esa mitad no estaba donde uno la ve. Manda si cabe o no: se
// deja quieta la esquina superior izquierda, y solo se ancla el borde contrario
// cuando por ahí se saldría. Un dock en el borde derecho abre el panel hacia la
// izquierda y uno en el inferior hacia arriba, porque es la única manera de que
// quepa; en cualquier otro sitio ni el dock ni el panel se mueven.
QPoint Panel::anchoredTopLeft(const QRect &before, const QSize &after) const {
    const QRect area = placementArea();
    if (!area.isValid()) return before.topLeft();

    // El margen de sombra es igual en ambos lados, así que alinear los bordes
    // de la ventana alinea también los del marco visible.
    return {anchorAxis(before.left(), before.width(), after.width(),
                       area.left(), area.right() + 1),
            anchorAxis(before.top(), before.height(), after.height(),
                       area.top(), area.bottom() + 1)};
}

// Red de seguridad: tras plegar o desplegar, la ventana no puede quedar fuera
// del sitio donde se la admite. anchoredTopLeft ya lo tiene en cuenta, pero el
// tamaño restaurado o un cambio de pantalla pueden dejarla asomando.
//
// En Wayland colocar la propia ventana es cosa del compositor y move() puede
// quedarse en nada; en X11 se aplica tal cual.
void Panel::keepOnScreen() {
    const QRect area = placementArea();
    if (!area.isValid()) return;

    // qMin antes que qMax dentro de clampInto: si la ventana no cabe, se queda
    // anclada arriba a la izquierda en vez de irse por el otro lado.
    if (const QPoint p = clampInto(pos(), size(), area); p != pos()) move(p);
}

// ---------------------------------------------------------------------------

void Panel::syncPrefs() {
    // Plegado, el tamaño que vale es el que tenía desplegado.
    const bool folded = m_stack && m_stack->currentWidget() == m_badge;
    m_store.prefs().windowSize = folded ? m_expandedSize : size();
}

void Panel::scheduleSave() { m_store.scheduleSave(); }

void Panel::save() { m_store.save(); }
