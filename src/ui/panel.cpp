#include "ui/panel.hpp"
#include "audio/alarm.hpp"
#include "audio/recorder.hpp"
#include "ui/calendar.hpp"
#include "ui/dragwidgets.hpp"
#include "ui/notecard.hpp"
#include "ui/popup.hpp"

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
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
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
    b->setToolTip(tip);
    b->setProperty("iconKind", kind);
    return b;
}

// Etiqueta que ve el usuario para un instante concreto.
QString dueLabel(const QDateTime &when) {
    return when.toString("ddd d MMM HH:mm");
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
    m_search->setPlaceholderText("Filtrar notas…");
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

    auto *title = new QLabel("Códice");
    title->setObjectName("title");
    l->addWidget(title);
    l->addStretch();

    auto *search = iconButton("search", "Buscar");
    auto *add = iconButton("plus", "Nueva nota");
    m_calendarBtn = iconButton("calendar", "Calendario");
    auto *gear = iconButton("gear", "Ajustes");
    auto *min = iconButton("minus", "Plegar a icono");
    m_headerButtons = {search, add, m_calendarBtn, gear, min};

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

    auto *emptyText = new QLabel("Todavía no hay notas");
    emptyText->setObjectName("meta");
    emptyText->setAlignment(Qt::AlignCenter);
    el->addWidget(emptyText);

    auto *emptyBtn = new QPushButton("Crear la primera");
    emptyBtn->setCursor(Qt::PointingHandCursor);
    connect(emptyBtn, &QPushButton::clicked, this, [this, emptyBtn] { openNewNoteMenu(emptyBtn); });

    auto *btnRow = new QHBoxLayout;
    btnRow->addStretch();
    btnRow->addWidget(emptyBtn);
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

    auto *hint = new QLabel("clic dcho · opciones");
    hint->setObjectName("meta");
    l->addWidget(hint);
    l->addWidget(new GripCorner, 0, Qt::AlignBottom);
    return footer;
}

QWidget *Panel::buildBadge() {
    auto *host = new QWidget;
    auto *hl = new QHBoxLayout(host);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->addStretch();

    auto *btn = new DragButton;
    btn->setObjectName("badge");
    btn->setFixedSize(56, 56);
    btn->setIcon(paintIcon("notes", QColor(Theme::fg()), 22));
    btn->setIconSize(QSize(22, 22));
    btn->setCursor(Qt::PointingHandCursor);
    btn->setToolTip("Abrir Códice · arrastra para mover");
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
    hl->addWidget(btn);
    return host;
}

// ---------------------------------------------------------------------------

void Panel::applyTheme() {
    setStyleSheet(m_theme.sheet());

    for (QToolButton *b : m_headerButtons)
        b->setIcon(paintIcon(b->property("iconKind").toString(), QColor(Theme::muted())));

    if (m_calendar) m_calendar->setTheme(m_theme);
    applyBadgeAlert();

    // El dock se pinta en línea porque su fondo depende de la opacidad actual.
    if (auto *badge = m_badge->findChild<QToolButton *>("badge"))
        badge->setStyleSheet(QString("QToolButton#badge { background:%1; border:1px solid %2;"
                                     "border-radius:18px; }"
                                     "QToolButton#badge:hover { background:%3; }")
                                 .arg(m_theme.card(), Theme::line(), Theme::hover()));
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
        m_footerText->setText(QString("%1 CON FECHA").arg(scheduled));
        return;
    }
    m_footerText->setText(QString("%1 EN EL CÓDICE").arg(m_store.count()));
}

void Panel::addNote(Note::Type type) {
    auto *n = new Note;
    n->type = type;
    n->title = type == Note::Check    ? "Nueva lista"
             : type == Note::Reminder ? "Nuevo recordatorio"
             : type == Note::Voice    ? "Nota de voz"
                                      : "Nueva nota";
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
    else m_footerText->setText(QString("%1 DE %2").arg(shown).arg(m_store.count()));
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

    // El mismo botón lleva de vuelta, así que cambia de cara.
    m_calendarBtn->setProperty("iconKind", "notes");
    m_calendarBtn->setIcon(paintIcon("notes", QColor(Theme::muted())));
    m_calendarBtn->setToolTip("Ver notas");
    refreshFooter();
}

void Panel::showNotes() {
    if (m_body->currentWidget() != m_calendar) return;

    m_body->setCurrentWidget(m_scroll);
    m_calendarBtn->setProperty("iconKind", "calendar");
    m_calendarBtn->setIcon(paintIcon("calendar", QColor(Theme::muted())));
    m_calendarBtn->setToolTip("Calendario");
    refreshFooter();
}

void Panel::refreshCalendar() {
    // Cada tecleo en una tarjeta pasa por aquí: si el calendario no está a la
    // vista no hay nada que repintar, y al volver a él ya se refresca.
    if (m_calendar && m_body->currentWidget() == m_calendar) m_calendar->refresh();
}

void Panel::askReminderTime(const QDate &day, QWidget *anchor) {
    auto *menu = new Popup(m_theme, this);
    menu->addHeader(QLocale::system().toString(day, "dddd d MMMM"));

    const QDateTime now = QDateTime::currentDateTime();
    for (const QTime &t : {QTime(9, 0), QTime(12, 0), QTime(15, 0), QTime(18, 0), QTime(21, 0)}) {
        const QDateTime when(day, t);
        menu->addItem("clock", t.toString("HH:mm"),
                      when <= now ? "Ya pasado" : QString(),
                      [this, when] { createReminder(when); });
    }

    menu->addSeparator();
    menu->addHeader("A mano · HH:mm");
    menu->addEditor("p. ej. 20:30", QString(), [this, day](const QString &value) {
        const QTime t = QTime::fromString(value.trimmed(), "HH:mm");
        if (t.isValid()) createReminder(QDateTime(day, t));
    });
    menu->showUnder(anchor);
}

void Panel::createReminder(const QDateTime &when) {
    auto *n = new Note;
    n->type = Note::Reminder;
    n->title = "Nuevo recordatorio";
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
        badge->setToolTip(alert ? "Recordatorio vencido · clic para parar"
                                : "Abrir Códice · arrastra para mover");
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
    menu->addHeader("Nueva nota");
    menu->addItem("text", "Texto", "Una nota libre",
                  [this] { addNote(Note::Text); });
    menu->addItem("check", "Checklist", "Tareas con progreso",
                  [this] { addNote(Note::Check); });
    menu->addItem("reminder", "Recordatorio", "Con fecha y aviso",
                  [this] { addNote(Note::Reminder); });
    menu->addItem("voice", "Nota de voz", "Graba desde el micrófono",
                  [this] { addNote(Note::Voice); });
    menu->showUnder(anchor);
}

void Panel::openSettings(QWidget *anchor) {
    auto *menu = new Popup(m_theme, this);

    menu->addHeader("Acento");
    const QList<QColor> swatches = {QColor("#7c9cff"), QColor("#6fcf97"), QColor("#f2b757"),
                                    QColor("#ff7a6b"), QColor("#b98cff"), QColor("#4ecdc4")};
    menu->addSwatches(swatches, m_theme.accent, [this](const QColor &c) {
        m_theme.accent = c;
        m_store.prefs().accent = c;
        applyTheme();
        rebuildList();   // las tarjetas llevan el acento pintado en línea
        save();
    });

    menu->addItem("palette", "Color personalizado…",
                  m_theme.accent.name(), [this, anchor] { openAccentEditor(anchor); });

    menu->addSeparator();
    menu->addHeader("Opacidad");
    menu->addSlider(40, 100, m_theme.opacity, [this](int v) {
        m_theme.opacity = v;
        m_store.prefs().opacity = v;
        applyTheme();
        scheduleSave();
    });

    menu->addSeparator();
    menu->addHeader("Ventana");
    const bool onTop = m_store.prefs().onTop;
    menu->addItem(onTop ? "check" : "minus", "Siempre encima",
                  onTop ? "Activado · por encima de todo"
                        : "Desactivado · pegado al escritorio",
                  [this] {
                      m_store.prefs().onTop = !m_store.prefs().onTop;
                      applyWindowFlags();
                      save();
                  });

    menu->addSeparator();
    menu->addHeader("Datos");
    menu->addItem("notes", "Carpeta de guardado…", prettyPath(appDataDir()),
                  [this] { chooseDataFolder(); });

    menu->addSeparator();
    menu->addHeader("Micrófono");
    const QAudioDevice current = QMediaDevices::defaultAudioInput();
    const QByteArray chosenId = m_store.prefs().input;
    for (const QAudioDevice &dev : QMediaDevices::audioInputs()) {
        const bool chosen = chosenId.isEmpty() ? dev.id() == current.id() : dev.id() == chosenId;
        menu->addItem("mic", dev.description(), chosen ? "En uso" : QString(),
                      [this, id = dev.id()] {
                          m_store.prefs().input = id;
                          VoiceRecorder::setPreferredInput(id);
                          save();
                      });
    }

    menu->addSeparator();
    menu->addItem("power", "Salir", QString(), [] { qApp->quit(); });
    menu->showUnder(anchor);
}

void Panel::openAccentEditor(QWidget *anchor) {
    auto *menu = new Popup(m_theme, this);
    menu->addHeader("Color de acento");
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
        this, "Carpeta donde guardar las notas", appDataDir());
    m_store.changeDataDir(to);
}

// --- plegado ---------------------------------------------------------------

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
    setMinimumSize(0, 0);
    showPage(m_badge);
    resize(m_badge->sizeHint() + QSize(kShadowMargin * 2, kShadowMargin * 2));
}

void Panel::expand() {
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
    resize(m_expandedSize);
}

// ---------------------------------------------------------------------------

void Panel::syncPrefs() {
    // Plegado, el tamaño que vale es el que tenía desplegado.
    const bool folded = m_stack && m_stack->currentWidget() == m_badge;
    m_store.prefs().windowSize = folded ? m_expandedSize : size();
}

void Panel::scheduleSave() { m_store.scheduleSave(); }

void Panel::save() { m_store.save(); }
