#include "ui/panel.hpp"
#include "audio/alarm.hpp"
#include "audio/recorder.hpp"
#include "ui/birthdays.hpp"
#include "ui/elidedlabel.hpp"
#include "ui/planner.hpp"
#include "ui/settings.hpp"
#include "ui/timers.hpp"
#include "ui/dragwidgets.hpp"
#include "ui/keynav.hpp"
#include "ui/notecard.hpp"
#include "ui/popup.hpp"
#include "ui/workarea.hpp"

#include "core/drivesync.hpp"
#include "core/lang.hpp"
#include "core/updater.hpp"

#include <QApplication>
#include <QAudioDevice>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsDropShadowEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QCoreApplication>
#include <QLayout>
#include <QLineEdit>
#include <QLocale>
#include <QMediaDevices>
#include <QMenu>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QPushButton>
#include <QScreen>
#include <QScrollBar>
#include <QSettings>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

constexpr int kShadowMargin = 22;   // hueco alrededor del marco para la sombra
constexpr int kShellMinWidth = 300;
constexpr int kShellMinHeight = 340;   // el calendario necesita más alto que la lista

// Las rutas de datos son largas y la fila del menú las corta por la mitad;
// bajo el home se muestran con ~ para que se lea la parte que importa.
// Cada cuánto se puede pedir la copia. El cero es "solo a mano" y va primero
// porque es el interruptor: quien no quiere programación lo apaga ahí.
const QList<int> kBackupPeriods{0, 1, 3, 7, 15, 30};

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

// mm:ss (o h:mm:ss) de lo que le queda a un temporizador, redondeado hacia
// arriba: 00:00 tiene que querer decir que ya ha terminado.
QString timerClock(qint64 ms) {
    const qint64 total = (ms + 999) / 1000;
    const qint64 h = total / 3600, m = (total / 60) % 60, sec = total % 60;
    if (h > 0)
        return QString("%1:%2:%3").arg(h).arg(m, 2, 10, QChar('0')).arg(sec, 2, 10, QChar('0'));
    return QString("%1:%2").arg(m, 2, 10, QChar('0')).arg(sec, 2, 10, QChar('0'));
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

// Tira pulsable bajo la cabecera. Es un QWidget liso, así que necesita
// WA_StyledBackground para que la hoja de estilos le pinte el fondo, igual que
// las filas del calendario y de los cumpleaños.
class ClickableBar : public QWidget {
public:
    explicit ClickableBar(std::function<void()> onClick, QWidget *parent = nullptr)
        : QWidget(parent), m_click(std::move(onClick)) {
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

}  // namespace

// ---------------------------------------------------------------------------

Panel::Panel() {
    setAttribute(Qt::WA_TranslucentBackground);
    // Intro pulsa el botón que tenga el foco, no solo la barra espaciadora.
    keynav::installButtonEnter();

    // El Store resuelve la carpeta de datos y las migraciones en su
    // constructor, antes de que nadie toque disco.
    m_store.load();
    m_theme.accent = m_store.prefs().accent;
    m_theme.opacity = m_store.prefs().opacity;
    m_theme.textScale = m_store.prefs().textScale;
    m_expandedSize = m_store.prefs().windowSize;
    VoiceRecorder::setPreferredInput(m_store.prefs().input);

    connect(&m_store, &Store::reloaded, this, &Panel::onStoreReloaded);

    m_alarm = new Alarm(this);
    m_dueTimer = new QTimer(this);
    m_dueTimer->setInterval(5000);
    connect(m_dueTimer, &QTimer::timeout, this, &Panel::checkReminders);
    // El mismo latido vigila la carpeta de datos: mirar si existe es una
    // llamada a stat, y es lo que hace que un pendrive montado a los diez
    // minutos se recoja solo en vez de quedarse el panel vacío.
    connect(m_dueTimer, &QTimer::timeout, this, &Panel::pollDataDir);
    connect(m_dueTimer, &QTimer::timeout, this, &Panel::pollBackup);
    connect(m_dueTimer, &QTimer::timeout, this, &Panel::pollUpdates);
    m_dueTimer->start();

    // Los temporizadores necesitan más que el latido de 5 s: enseñan segundos.
    // Medio segundo y no uno, para que la cuenta no se salte ninguno por el
    // desfase entre este reloj y el de la pared. Solo corre si algo cuenta.
    m_tick = new QTimer(this);
    m_tick->setInterval(500);
    connect(m_tick, &QTimer::timeout, this, &Panel::tickTimers);

    m_updater = new Updater(this);
    connect(m_updater, &Updater::finished, this, &Panel::onUpdateChecked);

    // Antes de buildShell: la página de ajustes la enseña.
    m_drive = new DriveSync(&m_store, this);
    m_drive->canApply = [this] { return userIdle(); };
    connect(m_drive, &DriveSync::openUrl, this, [](const QUrl &url) { QDesktopServices::openUrl(url); });
    m_driveTimer = new QTimer(this);
    m_driveTimer->setSingleShot(true);
    m_driveTimer->setInterval(20 * 1000);
    connect(m_driveTimer, &QTimer::timeout, this, [this] {
        // Nunca con la carpeta ausente: lo que hay en memoria no son las notas.
        if (m_store.available()) m_drive->syncNow();
    });
    connect(&m_store, &Store::saved, this, [this] {
        if (m_drive->connected() && m_drive->state() != DriveSync::Syncing) m_driveTimer->start();
    });
    // Mientras se escribe no se mezcla: se vuelve a intentar al poco.
    connect(m_drive, &DriveSync::deferred, this, [this] { m_driveTimer->start(); });
    connect(m_drive, &DriveSync::attachmentsArrived, this, [this] {
        // Las tarjetas se construyeron sin esas imágenes o esa grabación.
        if (userIdle()) rebuildList();
    });
    connect(&m_store, &Store::merged, this, &Panel::onStoreMerged);
    m_drivePoll = new QTimer(this);
    m_drivePoll->setInterval(90 * 1000);
    connect(m_drivePoll, &QTimer::timeout, this, [this] {
        if (m_drive->connected() && m_store.available()) m_drive->syncNow();
    });
    m_drivePoll->start();

    buildShell();
    buildTray();
    rebuildList();
    applyTheme();
    refreshDataWarning();
    refreshUpdateBanner();   // con lo que se supiera de la última comprobación
    checkReminders();   // puede haber vencido algo con la app cerrada
    tickTimers();       // y un temporizador puede haber llegado a cero

    if (!m_expandedSize.isValid())
        m_expandedSize = QSize(352 + kShadowMargin * 2, 560);
    resize(m_expandedSize);
    showPage(m_shell);
    applyWindowFlags();
    // Después de applyWindowFlags: cambiar de flags destruye la ventana nativa,
    // y colocarla antes de eso es colocar una ventana que se va a rehacer.
    restoreWindowPos();

    // Lo que se cambiara con la app cerrada, o una subida que falló la última
    // vez: se sube poco después de arrancar.
    if (m_drive->connected()) m_driveTimer->start();
}

Panel::~Panel() {
    // El Store guarda al morir y avisa con saved(), y para entonces esta
    // parte del panel ya no existe: nadie debe contestar a esa señal.
    disconnect(&m_store, nullptr, this, nullptr);
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

    // Aviso de carpeta ausente. Va aquí arriba, no en el pie, porque no es un
    // detalle: mientras esté puesto, escribir en el panel no guarda nada.
    m_dataWarn = new QLabel;
    m_dataWarn->setObjectName("warnBanner");
    m_dataWarn->setWordWrap(true);   // ver *Card widths*: no puede pedir su ancho
    m_dataWarn->hide();
    col->addWidget(m_dataWarn);

    // Aviso de versión nueva. Es una tira pulsable y no un diálogo: enterarse
    // no debería interrumpir a nadie, y quien no quiera saber nada lo apaga en
    // ajustes.
    auto *bar = new ClickableBar([this] { openLatestRelease(); });
    bar->setObjectName("updateBanner");
    auto *ul = new QHBoxLayout(bar);
    ul->setContentsMargins(10, 6, 10, 6);
    ul->setSpacing(7);
    m_updateText = new QLabel;
    m_updateText->setObjectName("updateBannerText");
    m_updateText->setWordWrap(true);   // ver *Card widths*: no puede pedir su ancho
    ul->addWidget(m_updateText, 1);
    m_updateBar = bar;
    m_updateBar->hide();
    col->addWidget(m_updateBar);

    // Temporizador cumplido o evento que empieza: rojo como un recordatorio
    // vencido, con su propio botón de parar porque no tienen tarjeta en la
    // lista donde ponerlo.
    m_alarmBar = new QWidget;
    m_alarmBar->setObjectName("alarmBar");
    m_alarmBar->setAttribute(Qt::WA_StyledBackground, true);
    auto *al = new QHBoxLayout(m_alarmBar);
    al->setContentsMargins(10, 6, 8, 6);
    al->setSpacing(6);
    auto *bell = new QLabel;
    bell->setPixmap(paintIcon("bell", QColor("#ff7a6b"), 14).pixmap(14, 14));
    al->addWidget(bell);
    m_alarmText = new QLabel;
    m_alarmText->setObjectName("alarmBarText");
    m_alarmText->setWordWrap(true);   // ver *Card widths*: no puede pedir su ancho
    m_alarmText->setMinimumWidth(24);
    al->addWidget(m_alarmText, 1);
    m_alarmPlus = new QToolButton;
    m_alarmPlus->setObjectName("alarmBtn");
    m_alarmPlus->setText(L("+1 min"));
    m_alarmPlus->setCursor(Qt::PointingHandCursor);
    connect(m_alarmPlus, &QToolButton::clicked, this, [this] {
        for (Timer *t : m_store.timers())
            if (t->ringing()) t->snooze(60 * 1000);
        onTimersChanged();
        if (!anyRinging()) m_alarm->stop();
        save();
    });
    al->addWidget(m_alarmPlus);
    auto *stop = new QToolButton;
    stop->setObjectName("alarmBtn");
    stop->setProperty("tip", "Detener");
    stop->setText(L("Detener"));
    stop->setCursor(Qt::PointingHandCursor);
    connect(stop, &QToolButton::clicked, this, &Panel::stopBarAlarms);
    al->addWidget(stop);
    m_alarmBar->hide();
    col->addWidget(m_alarmBar);

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

    // Recortable: con siete botones en la cabecera, "Temporizadores" ya no
    // cabe entero en el ancho mínimo, y una etiqueta que pide su ancho
    // ensancharía la ventana entera (ver *Card widths*).
    m_titleLabel = new ElidedLabel(L("Tagoror"), QColor(Theme::fg()));
    m_titleLabel->setObjectName("title");
    l->addWidget(m_titleLabel);
    l->addStretch();

    auto *search = iconButton("search", "Buscar");
    auto *add = iconButton("plus", "Nueva nota");
    m_calendarBtn = iconButton("calendar", "Calendario");
    m_timersBtn = iconButton("timer", "Temporizadores");
    m_birthdayBtn = iconButton("cake", "Cumpleaños");
    m_settingsBtn = iconButton("gear", "Ajustes");
    auto *min = iconButton("minus", "Plegar a icono");
    m_headerButtons = {m_calendarBtn, m_timersBtn, m_birthdayBtn, search, add, m_settingsBtn, min};

    connect(search, &QToolButton::clicked, this, &Panel::toggleSearch);
    connect(min, &QToolButton::clicked, this, &Panel::collapse);
    connect(add, &QToolButton::clicked, this, [this, add] { openNewNoteMenu(add); });
    // Las páginas se conectan en buildBody(), que es donde existen.

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
    m_emptyBtn->setFocusPolicy(Qt::TabFocus);   // el anillo, solo con el teclado
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

    // ---- planificador -----------------------------------------------------
    m_planner = new PlannerView(m_theme);
    m_planner->setView(PlannerView::View(m_store.prefs().plannerView));
    m_planner->setHidden(m_store.prefs().plannerHidden);
    m_planner->setSources(&m_store.events(), &m_store.notes(), &m_store.birthdays());
    connect(m_planner, &PlannerView::eventCreated, this, [this](Event *e) {
        m_store.addEvent(e);   // el Store se queda con él y lo guarda
        refreshPlanner();
        refreshFooter();
        checkReminders();      // uno que empieza ya tiene que sonar ya
    });
    connect(m_planner, &PlannerView::eventChanged, this, [this](Event *) {
        save();
        checkReminders();
    });
    connect(m_planner, &PlannerView::eventDeleted, this, [this](Event *e) {
        // Igual que una nota: si sonaba y era lo único, el tono se apaga.
        const bool wasRinging = e->ringingMs != 0;
        m_store.removeEvent(e);
        if (wasRinging && !anyRinging()) m_alarm->stop();
        refreshAlarmBar();
        applyBadgeAlert();
        refreshPlanner();
        refreshFooter();
    });
    connect(m_planner, &PlannerView::reminderCreated, this, &Panel::createReminder);
    connect(m_planner, &PlannerView::noteActivated, this, &Panel::revealNote);
    connect(m_planner, &PlannerView::birthdayActivated, this, [this](Birthday *) {
        togglePage(m_birthdays);
    });
    connect(m_planner, &PlannerView::viewChanged, this, [this](int v) {
        m_store.prefs().plannerView = v;
        scheduleSave();
    });
    connect(m_planner, &PlannerView::hiddenChanged, this, [this](const QStringList &cats) {
        m_store.prefs().plannerHidden = cats;
        scheduleSave();
    });

    // ---- temporizadores ---------------------------------------------------
    m_timerView = new TimerView(m_theme);
    m_timerView->setSource(&m_store.timers());
    connect(m_timerView, &TimerView::createRequested, this, &Panel::createTimer);
    connect(m_timerView, &TimerView::toggleRequested, this, &Panel::toggleTimer);
    connect(m_timerView, &TimerView::resetRequested, this, &Panel::resetTimer);
    connect(m_timerView, &TimerView::removeRequested, this, &Panel::removeTimer);

    // ---- cumpleaños -------------------------------------------------------
    m_birthdays = new BirthdayView(m_theme);
    m_birthdays->setByMonth(m_store.prefs().birthdaysByMonth);
    m_birthdays->setSource(&m_store.birthdays());
    connect(m_birthdays, &BirthdayView::addRequested, this,
            [this](QWidget *anchor) { openBirthdayEditor(nullptr, anchor); });
    connect(m_birthdays, &BirthdayView::editRequested, this, &Panel::openBirthdayEditor);
    connect(m_birthdays, &BirthdayView::remindRequested, this, &Panel::askBirthdayReminder);
    connect(m_birthdays, &BirthdayView::greetToggled, this, &Panel::toggleGreeted);
    connect(m_birthdays, &BirthdayView::dismissRequested, this, &Panel::dismissBirthday);
    connect(m_birthdays, &BirthdayView::orderChanged, this, [this](bool byMonth) {
        m_store.prefs().birthdaysByMonth = byMonth;
        save();
    });

    // ---- ajustes ----------------------------------------------------------
    m_settings = new SettingsView(m_theme);
    m_settings->setSource(&m_store);
    connect(m_settings, &SettingsView::accentPicked, this, [this](const QColor &c) {
        m_theme.accent = c;
        m_store.prefs().accent = c;
        applyTheme();
        rebuildList();   // las tarjetas llevan el acento pintado en línea
        save();
    });
    connect(m_settings, &SettingsView::accentEditorRequested, this, &Panel::openAccentEditor);
    connect(m_settings, &SettingsView::opacityChanged, this, [this](int v) {
        m_theme.opacity = v;
        m_store.prefs().opacity = v;
        applyTheme();
        scheduleSave();
    });
    connect(m_settings, &SettingsView::languagePicked, this, &Panel::setLanguage);
    connect(m_settings, &SettingsView::textScalePicked, this, [this](int percent) {
        if (percent == m_theme.textScale) return;
        m_theme.textScale = percent;
        m_store.prefs().textScale = percent;
        // La hoja cambia sola en todo lo que la hereda; lo que se pinta a mano
        // (el planificador, los temporizadores) lo recoge en setTheme().
        applyTheme();
        refreshSettings();   // el botón elegido y la muestra
        save();
    });
    connect(m_settings, &SettingsView::onTopToggled, this, [this](bool on) {
        m_store.prefs().onTop = on;
        applyWindowFlags();
        refreshSettings();
        save();
    });
    connect(m_settings, &SettingsView::x11Toggled, this, [this](bool on) {
        QSettings().setValue("platform", on ? "" : "wayland");
        refreshSettings();
    });
    connect(m_settings, &SettingsView::dataFolderRequested, this, &Panel::chooseDataFolder);
    connect(m_settings, &SettingsView::backupsRequested, this, &Panel::openBackups);
    connect(m_settings, &SettingsView::inputPicked, this, [this](const QByteArray &id) {
        m_store.prefs().input = id;
        VoiceRecorder::setPreferredInput(id);
        refreshSettings();
        save();
    });
    connect(m_settings, &SettingsView::updateCheckToggled, this, [this](bool on) {
        m_store.prefs().updateCheck = on;
        refreshSettings();
        save();
        if (on) pollUpdates();   // si tocaba, se mira ya
    });
    connect(m_settings, &SettingsView::checkUpdatesRequested, this, &Panel::checkUpdatesNow);
    connect(m_settings, &SettingsView::openLatestRequested, this, &Panel::openLatestRelease);
    connect(m_settings, &SettingsView::quitRequested, qApp, &QApplication::quit);
    m_settings->setDrive(m_drive);
    connect(m_drive, &DriveSync::changed, m_settings, &SettingsView::refreshDrive);
    connect(m_settings, &SettingsView::driveConnectRequested, m_drive, &DriveSync::connectAccount);
    connect(m_settings, &SettingsView::driveCancelRequested, m_drive, &DriveSync::cancel);
    connect(m_settings, &SettingsView::driveDisconnectRequested, m_drive, &DriveSync::disconnectAccount);
    connect(m_settings, &SettingsView::driveSyncRequested, this, [this] {
        if (!m_store.available()) return;
        save();                 // lo último escrito, antes de subir
        m_driveTimer->stop();   // ya se sube ahora
        m_drive->syncNow();
    });

    m_body = new QStackedWidget;
    m_body->addWidget(m_scroll);
    m_body->addWidget(m_planner);
    m_body->addWidget(m_timerView);
    m_body->addWidget(m_birthdays);
    m_body->addWidget(m_settings);

    for (const Page &p : pages())
        connect(p.button, &QToolButton::clicked, this, [this, page = p.page] { togglePage(page); });
    // Desde el principio, no solo al cambiar de página: el calendario pide más
    // alto que la lista y, sin esto, se lo impondría a la ventana ya al nacer.
    showBodyPage(m_scroll);
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

    m_footerHint = new QLabel;
    m_footerHint->setObjectName("meta");
    l->addWidget(m_footerHint);

    // Lo que le queda al temporizador en marcha, a la vista desde cualquier
    // página. Ocupa el sitio de la pista mientras cuenta: las dos juntas no
    // caben en el ancho mínimo.
    m_footerTimer = new QToolButton;
    m_footerTimer->setObjectName("footerTimer");
    m_footerTimer->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_footerTimer->setIconSize(QSize(12, 12));
    m_footerTimer->setCursor(Qt::PointingHandCursor);
    m_footerTimer->hide();
    connect(m_footerTimer, &QToolButton::clicked, this, [this] {
        for (Timer *t : m_store.timers())
            if (t->state == Timer::Running) {
                m_timerView->focusTimer(t);
                break;
            }
        if (m_body->currentWidget() != m_timerView) togglePage(m_timerView);
    });
    l->addWidget(m_footerTimer);
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
    btn->setToolTip(L("Abrir Tagoror · arrastra para mover"));
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

    // El de la página en la que se está va en el acento; los demás, en gris.
    for (QToolButton *b : m_headerButtons)
        b->setIcon(paintIcon(b->property("iconKind").toString(),
                             b->property("active").toBool() ? m_theme.accent
                                                            : QColor(Theme::muted())));

    if (m_planner) m_planner->setTheme(m_theme);
    if (m_timerView) m_timerView->setTheme(m_theme);
    if (m_birthdays) m_birthdays->setTheme(m_theme);
    // El de ajustes no se rehace: repinta lo que lleva el acento y deja en pie
    // el deslizador de la opacidad, que es quien acaba de llamar aquí.
    if (m_settings) m_settings->setTheme(m_theme);
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
    m_search->setPlaceholderText(L("Filtrar notas…"));
    m_emptyText->setText(L("Todavía no hay notas"));
    m_emptyBtn->setText(L("Crear la primera"));
    refreshFooterHint();
    refreshDataWarning();

    for (QToolButton *b : m_headerButtons)
        b->setToolTip(L(b->property("tip").toString()));
    // Los de las páginas además dicen en cuál estás, así que se rehacen por su
    // propio camino.
    refreshPageButtons();
    m_alarmPlus->setText(L("+1 min"));
    for (auto *b : m_alarmBar->findChildren<QToolButton *>())
        if (!b->property("tip").toString().isEmpty()) b->setText(L(b->property("tip").toString()));
    refreshAlarmBar();

    buildTrayMenu();
    applyBadgeAlert();   // la ayuda del icono de la bandeja lleva texto
    if (m_planner) {
        // Tras recargar el Store la vista y el filtro guardados son otros.
        m_planner->setView(PlannerView::View(m_store.prefs().plannerView));
        m_planner->setHidden(m_store.prefs().plannerHidden);
        m_planner->retranslate();
    }
    if (m_timerView) m_timerView->retranslate();
    if (m_birthdays) {
        // Tras recargar el Store las preferencias son otras, y el orden de la
        // lista es una de ellas; retranslate() repinta la página de todas formas.
        m_birthdays->setByMonth(m_store.prefs().birthdaysByMonth);
        m_birthdays->retranslate();
    }
    if (m_settings) m_settings->retranslate();
    refreshTitle();
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
        connect(card, &NoteCard::dirty, this, &Panel::refreshPlanner);
        connect(card, &NoteCard::deleteRequested, this, &Panel::removeNote);
        connect(card, &NoteCard::dismissRequested, this, &Panel::dismissNote);
        connect(card, &NoteCard::rescheduled, this, &Panel::rescheduleNote);
        connect(card, &NoteCard::moveRequested, this, &Panel::moveNote);
        connect(card, &NoteCard::dragStarted, this, [this, card] { beginCardDrag(card); });
        connect(card, &NoteCard::dragMoved, this,
                [this, card](const QPoint &at) { dragCardTo(card, at); });
        connect(card, &NoteCard::dragFinished, this, [this, card] { endCardDrag(card); });
        m_listLayout->insertWidget(m_listLayout->count() - 2, card);
    }

    m_empty->setVisible(m_store.notes().isEmpty());
    m_badgeCount->setText(QString::number(m_store.count()));
    refreshFooter();
    refreshPlanner();
    refreshBirthdays();
    applyBadgeAlert();
    if (m_search && !m_search->text().isEmpty())
        applyFilter(m_search->text());
}

// La pista del pie depende de dónde estés: en la lista, el menú de la tarjeta;
// en cualquier otra página, cómo se sale de ella.
void Panel::refreshFooterHint() {
    if (!m_footerHint) return;
    const bool onList = !m_body || m_body->currentWidget() == m_scroll;
    m_footerHint->setText(onList ? L("clic dcho · opciones") : L("esc · cerrar"));
}

void Panel::refreshFooter() {
    refreshFooterHint();
    if (!m_footerText) return;

    if (m_body && m_body->currentWidget() == m_planner) {
        m_footerText->setText(L("%1 EVENTOS").arg(m_store.events().size()));
        return;
    }
    if (m_body && m_body->currentWidget() == m_timerView) {
        m_footerText->setText(L("%1 TEMPORIZADORES").arg(m_store.timers().size()));
        return;
    }
    if (m_body && m_body->currentWidget() == m_birthdays) {
        m_footerText->setText(L("%1 CUMPLEAÑOS").arg(m_store.birthdayCount()));
        return;
    }
    if (m_body && m_body->currentWidget() == m_settings) {
        m_footerText->setText(L("AJUSTES"));
        return;
    }
    m_footerText->setText(L("%1 EN EL TAGOROR").arg(m_store.count()));
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
    // Igual que con los cumpleaños: si la nota estaba sonando y era la única,
    // el tono se quedaba puesto sin nada en la lista que lo explicara ni botón
    // con el que pararlo. Se pregunta antes de borrarla, porque después ya no
    // hay a quién.
    const bool wasRinging = n->ringing;
    m_store.remove(n);
    if (wasRinging && !anyRinging()) m_alarm->stop();
    rebuildList();   // de paso repinta el dock y la bandeja
}

// --- reordenar --------------------------------------------------------------

void Panel::moveNote(Note *n, int steps) {
    QList<Note *> order = m_store.notes();
    const int from = int(order.indexOf(n));
    const int to = from + steps;
    if (from < 0 || to < 0 || to >= order.size()) return;

    order.move(from, to);
    m_store.setOrder(order);
    rebuildList();
    save();
}

void Panel::beginCardDrag(NoteCard *card) {
    m_dragCard = card;
    card->setProperty("dragging", true);
    // Una propiedad dinámica no repinta sola.
    card->style()->unpolish(card);
    card->style()->polish(card);
    card->raise();
}

// Reordena en caliente mientras dura el arrastre: la tarjeta cambia de sitio
// en el layout en cuanto cruza el centro de otra, y no al soltar, para que se
// vea dónde va a caer.
void Panel::dragCardTo(NoteCard *card, const QPoint &globalPos) {
    if (!card || !m_listHost) return;

    // Cerca de los bordes, la lista acompaña: sin esto no se puede sacar una
    // tarjeta del trozo visible sin soltarla antes.
    if (m_scroll) {
        const int y = m_scroll->viewport()->mapFromGlobal(globalPos).y();
        const int edge = 26;
        QScrollBar *bar = m_scroll->verticalScrollBar();
        if (y < edge) bar->setValue(bar->value() - 12);
        else if (y > m_scroll->viewport()->height() - edge) bar->setValue(bar->value() + 12);
    }

    // Solo cuentan las visibles: con un filtro puesto, las escondidas siguen
    // en el layout y arrastrar por encima de ellas no significa nada.
    QList<NoteCard *> visible;
    for (NoteCard *c : cards())
        if (c->isVisible()) visible.append(c);

    const int from = int(visible.indexOf(card));
    if (from < 0) return;

    const int y = m_listHost->mapFromGlobal(globalPos).y();
    int to = from;
    for (int i = 0; i < visible.size(); ++i) {
        if (i == from) continue;
        const int center = visible.at(i)->geometry().center().y();
        // Hacia arriba manda la primera que quede por debajo del cursor;
        // hacia abajo, la última que quede por encima.
        if (i < from && y < center) { to = i; break; }
        if (i > from && y > center) to = i;
    }
    if (to == from) return;

    QWidget *anchor = visible.at(to);
    m_listLayout->removeWidget(card);
    // El índice del ancla se pregunta ya sin la tarjeta dentro; yendo hacia
    // abajo, la tarjeta va detrás de ella.
    int at = m_listLayout->indexOf(anchor);
    if (to > from) ++at;
    m_listLayout->insertWidget(at, card);
    commitOrder();
}

void Panel::endCardDrag(NoteCard *card) {
    m_dragCard = nullptr;
    if (!card) return;
    card->setProperty("dragging", false);
    card->style()->unpolish(card);
    card->style()->polish(card);
    save();
}

// El orden que se ve es el que se guarda.
void Panel::commitOrder() {
    QList<Note *> order;
    for (NoteCard *card : cards()) order.append(card->note());
    m_store.setOrder(order);
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

// --- páginas -----------------------------------------------------------------

QList<Panel::Page> Panel::pages() const {
    return {{m_planner, m_calendarBtn, "Calendario"},
            {m_timerView, m_timersBtn, "Temporizadores"},
            {m_birthdays, m_birthdayBtn, "Cumpleaños"},
            {m_settings, m_settingsBtn, "Ajustes"}};
}

// Abre una página, o vuelve a las notas si ya estaba abierta: el mismo botón
// entra y sale.
void Panel::togglePage(QWidget *page) {
    if (m_body->currentWidget() == page) {
        showNotes();
        return;
    }
    // Primero se devuelve el ancho: si no, la página que viene heredaría la
    // ventana ensanchada para el planificador.
    if (m_body->currentWidget() == m_planner) leaveWide();

    m_searchBar->hide();
    if (page == m_planner) m_planner->refresh();
    else if (page == m_timerView) m_timerView->refresh();
    else if (page == m_birthdays) m_birthdays->refresh();
    else if (page == m_settings) m_settings->refresh();
    showBodyPage(page);
    refreshPageButtons();
    refreshFooter();
    refreshTitle();
    if (page == m_planner) enterWide();
}

void Panel::showNotes() {
    if (m_body->currentWidget() == m_scroll) return;

    if (m_body->currentWidget() == m_planner) leaveWide();
    showBodyPage(m_scroll);
    refreshPageButtons();
    refreshFooter();
    refreshTitle();
}

// El botón no cambia de icono al abrir su página: se queda encendido. Así el
// icono siempre dice adónde lleva y el realce dice dónde estás.
void Panel::setPageActive(QToolButton *button, bool on,
                          const QString &tipOn, const QString &tipOff) {
    button->setProperty("active", on);
    button->setToolTip(on ? tipOn : tipOff);
    button->setIcon(paintIcon(button->property("iconKind").toString(),
                              on ? m_theme.accent : QColor(Theme::muted())));
    // Una propiedad dinámica no repinta sola.
    button->style()->unpolish(button);
    button->style()->polish(button);
}

void Panel::refreshPageButtons() {
    QWidget *current = m_body ? m_body->currentWidget() : nullptr;
    for (const Page &p : pages())
        setPageActive(p.button, p.page == current, L("Ver notas"), L(p.name));
}

// El rótulo de la cabecera nombra la página abierta. En la lista vuelve a ser
// el nombre de la aplicación, que es donde tiene sentido que esté.
void Panel::refreshTitle() {
    if (!m_titleLabel || !m_body) return;
    QString title = L("Tagoror");
    for (const Page &p : pages())
        if (p.page == m_body->currentWidget()) title = L(p.name);
    m_titleLabel->setText(title);
    m_titleLabel->update();   // ElidedLabel se pinta a mano
}

void Panel::refreshPlanner() {
    // Cada tecleo en una tarjeta pasa por aquí: si el planificador no está a
    // la vista no hay nada que repintar, y al volver a él ya se refresca.
    if (m_planner && m_body->currentWidget() == m_planner) m_planner->refresh();
}

void Panel::createReminder(const QString &title, const QDateTime &when) {
    auto *n = new Note;
    n->type = Note::Reminder;
    n->title = title.isEmpty() ? L("Nuevo recordatorio") : title;
    n->dueAtMs = when.toMSecsSinceEpoch();
    n->due = dueLabel(when);

    m_store.add(n);
    rebuildList();
    // Se queda en el planificador, sobre el día donde acaba de aparecer.
    m_planner->goTo(when.date());
    checkReminders();
}

// --- ancho del planificador ---------------------------------------------------

// Se ensancha hacia donde quepa (anchoredTopLeft: hacia la izquierda si está
// pegada al borde derecho) y se apunta cómo quedó, para poder devolverla.
void Panel::enterWide() {
    if (!m_stack || m_stack->currentWidget() != m_shell) return;
    int target = PlannerView::kPreferredWidth + kShadowMargin * 2;
    if (const QRect area = placementArea(); area.isValid()) target = qMin(target, area.width());
    if (width() >= target) return;

    const QRect before = geometry();
    const QSize size(target, height());
    setGeometry(QRect(anchoredTopLeft(before, size), size));
    keepOnScreen();
    m_narrowGeom = before;
    m_wideGeom = geometry();
}

// Solo si la ventana sigue siendo la que dejó enterWide: si el usuario la ha
// redimensionado entre medias, ese tamaño es suyo y se respeta.
void Panel::leaveWide() {
    if (m_narrowGeom.isValid() && geometry() == m_wideGeom) {
        setGeometry(m_narrowGeom);
        keepOnScreen();
    }
    m_narrowGeom = QRect();
    m_wideGeom = QRect();
}

// --- temporizadores -------------------------------------------------------------

void Panel::createTimer(const QString &name, qint64 ms) {
    auto *t = new Timer;
    t->name = name.isEmpty() ? L("Temporizador %1").arg(timerClock(ms)) : name;
    t->totalMs = ms;
    t->leftMs = ms;
    t->start();              // crear es lanzarlo: para guardarlo quieto está Reiniciar
    m_store.addTimer(t);
    m_timerView->focusTimer(t);
    onTimersChanged();
}

void Panel::toggleTimer(Timer *t) {
    const bool wasRinging = t->ringing();
    if (t->state == Timer::Running) t->pause();
    else t->start();
    if (wasRinging && !anyRinging()) m_alarm->stop();
    save();
    onTimersChanged();
}

void Panel::resetTimer(Timer *t) {
    const bool wasRinging = t->ringing();
    t->reset();
    if (wasRinging && !anyRinging()) m_alarm->stop();
    save();
    onTimersChanged();
}

void Panel::removeTimer(Timer *t) {
    // Como con las notas: borrar el que suena no puede dejar el tono puesto.
    const bool wasRinging = t->ringing();
    m_store.removeTimer(t);
    if (wasRinging && !anyRinging()) m_alarm->stop();
    onTimersChanged();
}

void Panel::onTimersChanged() {
    m_timerView->refresh();
    bool running = false;
    for (Timer *t : m_store.timers()) running |= t->state == Timer::Running;
    if (running && !m_tick->isActive()) m_tick->start();
    refreshFooterTimer();
    refreshAlarmBar();
    applyBadgeAlert();
    if (m_body->currentWidget() == m_timerView) refreshFooter();
}

void Panel::tickTimers() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    bool fired = false, running = false;
    for (Timer *t : m_store.timers()) {
        if (t->expired(now)) {
            t->state = Timer::Done;
            fired = true;
        }
        running |= t->state == Timer::Running;
    }
    if (fired) {
        m_alarm->start();
        save();
        onTimersChanged();
    } else if (m_body->currentWidget() == m_timerView) {
        m_timerView->tick();
    }
    refreshFooterTimer();
    if (!running) m_tick->stop();
    else if (!m_tick->isActive()) m_tick->start();
}

void Panel::refreshFooterTimer() {
    if (!m_footerTimer) return;
    // El que antes acaba es el que interesa.
    const Timer *next = nullptr;
    for (const Timer *t : m_store.timers())
        if (t->state == Timer::Running && (!next || t->endsAtMs < next->endsAtMs)) next = t;

    const bool show = next != nullptr;
    if (show) {
        m_footerTimer->setText(timerClock(next->remainingMs()));
        m_footerTimer->setIcon(paintIcon("timer", m_theme.accent, 12));
        m_footerTimer->setToolTip(next->name);
    }
    if (show == !m_footerTimer->isHidden()) return;
    m_footerTimer->setVisible(show);
    m_footerHint->setVisible(!show);
}

// Lo primero que suena: un temporizador, o si no un evento. Con varios a la
// vez se dice cuántos, y Detener los calla todos.
void Panel::refreshAlarmBar() {
    if (!m_alarmBar) return;
    QStringList what;
    bool timers = false;
    for (Timer *t : m_store.timers())
        if (t->ringing()) {
            what << L("%1 · tiempo cumplido").arg(t->name);
            timers = true;
        }
    for (Event *e : m_store.events())
        if (e->ringingMs)
            what << L("%1 · empieza a las %2")
                        .arg(e->title.isEmpty() ? L("Sin título") : e->title,
                             QDateTime::fromMSecsSinceEpoch(e->ringingMs).toString("HH:mm"));

    const bool show = !what.isEmpty();
    if (show) {
        QString text = what.first();
        if (what.size() > 1) text += " " + L("(y %1 más)").arg(what.size() - 1);
        m_alarmText->setText(text);
        m_alarmPlus->setVisible(timers);
    }
    if (show == !m_alarmBar->isHidden()) return;
    m_alarmBar->setVisible(show);
    syncShellMinimum();   // ocupa alto dentro del shell, como los otros avisos
}

void Panel::stopBarAlarms() {
    for (Timer *t : m_store.timers())
        if (t->ringing()) t->reset();
    for (Event *e : m_store.events())
        if (e->ringingMs) silenceEvent(e);
    if (!anyRinging()) m_alarm->stop();
    save();
    onTimersChanged();
    refreshPlanner();
}

void Panel::silenceEvent(Event *e) {
    // Se apunta qué vuelta sonó, no un "ya avisado" a secas: la semana que
    // viene la clase vuelve a avisar.
    e->firedMs = e->ringingMs;
    e->ringingMs = 0;
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

// --- cumpleaños -------------------------------------------------------------

// La página de ajustes enseña cosas que cambian por fuera de ella (la carpeta
// de datos, las copias, el micrófono), así que se repinta cuando pasa algo.
void Panel::refreshSettings() {
    if (m_settings && m_body->currentWidget() == m_settings) m_settings->refresh();
}

void Panel::refreshBirthdays() {
    // Igual que el calendario: cada tecleo en una tarjeta pasa por rebuildList,
    // y si la página no está a la vista no hay nada que repintar.
    if (m_birthdays && m_body->currentWidget() == m_birthdays) m_birthdays->refresh();
}

// Alta y edición por el mismo sitio, con b nulo para lo primero: el formato de
// la fecha y lo que se pide se escriben una sola vez, y así no pueden discrepar.
void Panel::openBirthdayEditor(Birthday *b, QWidget *anchor) {
    auto *menu = new Popup(m_theme, this);
    menu->addHeader(b ? L("Editar cumpleaños") : L("Nuevo cumpleaños"));
    menu->addFields({L("Nombre"), L("dd/mm o dd/mm/aaaa"), L("Relación (opcional)")},
                    {b ? b->name : QString(),
                     b ? b->dateText() : QString(),
                     b ? b->relation : QString()},
                    [this, b](const QStringList &values) {
                        int d = 0, m = 0, y = 0;
                        if (!Birthday::parseDate(values.value(1), &d, &m, &y)) return;
                        if (values.value(0).trimmed().isEmpty()) return;

                        Birthday *target = b ? b : new Birthday;
                        target->name = values.value(0).trimmed();
                        target->day = d;
                        target->month = m;
                        target->year = y;
                        target->relation = values.value(2).trimmed();
                        // La marca de felicitado es de una fecha concreta: si
                        // la fecha cambia, deja de querer decir nada.
                        if (!target->isToday()) target->greetedYear = 0;
                        // Se da de alta ya relleno: apuntarlo antes dejaría un
                        // "1 de enero" sin nombre escrito en disco.
                        if (!b) m_store.addBirthday(target);
                        m_birthdays->refresh();
                        refreshFooter();
                        save();
                    });

    if (!b) {
        menu->showUnder(anchor);
        return;
    }

    menu->addSeparator();
    menu->addItem(b->remindAt.isValid() ? "bell" : "clock",
                  b->remindAt.isValid()
                      ? L("Aviso a las %1").arg(b->remindAt.toString("HH:mm"))
                      : L("Avisarme ese día…"),
                  L("Suena una vez, el día que toca"),
                  [this, b, anchor] { askBirthdayReminder(b, anchor); });
    menu->addItem(b->greeted() ? "minus" : "check",
                  b->greeted() ? L("Sin felicitar") : L("Marcar como felicitado"),
                  QString(), [this, b] { toggleGreeted(b); });
    menu->addSeparator();
    menu->addItem("trash", L("Eliminar cumpleaños"), QString(),
                  [this, b] { removeBirthday(b); });
    menu->showUnder(anchor);
}

void Panel::askBirthdayReminder(Birthday *b, QWidget *anchor) {
    auto *menu = new Popup(m_theme, this);
    menu->addHeader(b->name.isEmpty() ? L("Sin nombre") : b->name);

    QStringList labels;
    QList<QTime> times;
    for (int h = 8; h <= 21; h += 2) {
        times << QTime(h, 0);
        labels << times.last().toString("HH:mm");
    }
    // addChoice y no addChips: la hora del aviso es un ajuste con estado, no
    // una lista de acciones, y el menú tiene que enseñar cuál está puesta.
    menu->addChoice(labels, int(times.indexOf(b->remindAt)), [this, b, times](int i) {
        b->remindAt = times.at(i);
        // La hora nueva vuelve a armar el aviso de este año: cambiarla justo
        // después de callarlo tiene que servir para algo.
        b->firedYear = 0;
        m_birthdays->refresh();
        save();
    });

    menu->addSeparator();
    menu->addHeader(L("A mano · HH:mm"));
    menu->addEditor(L("p. ej. 20:30"), QString(), [this, b](const QString &value) {
        const QTime t = QTime::fromString(value.trimmed(), "HH:mm");
        if (!t.isValid()) return;
        b->remindAt = t;
        b->firedYear = 0;
        m_birthdays->refresh();
        save();
    });

    if (b->remindAt.isValid()) {
        menu->addSeparator();
        menu->addItem("minus", L("Sin aviso"), L("Se queda solo apuntado"),
                      [this, b] {
                          b->remindAt = QTime();
                          silenceBirthday(b);
                          m_birthdays->refresh();
                          save();
                      });
    }
    menu->showBelow(anchor);
}

void Panel::removeBirthday(Birthday *b) {
    // Puede estar sonando justo cuando se borra: el tono se queda colgado si
    // nadie lo apaga antes de que desaparezca la única cosa que lo pedía.
    const bool wasRinging = b->ringing;
    m_store.removeBirthday(b);
    if (wasRinging && !anyRinging()) m_alarm->stop();
    m_birthdays->refresh();
    refreshFooter();
    applyBadgeAlert();
}

void Panel::toggleGreeted(Birthday *b) {
    b->greetedYear = b->greeted() ? 0 : QDate::currentDate().year();
    // Felicitar es enterarse: no tiene sentido que siga sonando.
    if (b->ringing) {
        silenceBirthday(b);
        if (!anyRinging()) m_alarm->stop();
        applyBadgeAlert();
    }
    m_birthdays->refresh();
    save();
}

void Panel::silenceBirthday(Birthday *b) {
    b->ringing = false;
    // Se apunta el año, no un "ya sonó" a secas: la marca tiene que caducar
    // sola para que el año que viene vuelva a avisar.
    b->firedYear = QDate::currentDate().year();
}

void Panel::dismissBirthday(Birthday *b) {
    silenceBirthday(b);
    if (!anyRinging()) m_alarm->stop();
    m_birthdays->refresh();
    applyBadgeAlert();
    save();
}

// --- recordatorios ---------------------------------------------------------

bool Panel::anyRinging() const {
    for (Note *n : m_store.notes())
        if (n->ringing) return true;
    for (Birthday *b : m_store.birthdays())
        if (b->ringing) return true;
    for (Timer *t : m_store.timers())
        if (t->ringing()) return true;
    for (Event *e : m_store.events())
        if (e->ringingMs) return true;
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
    // Los cumpleaños con hora puesta suenan por el mismo latido y con el mismo
    // tono: para quien lo oye es el mismo aviso, y duplicar la maquinaria solo
    // daría dos maneras de que se quedara sonando.
    const QDateTime nowAt = QDateTime::currentDateTime();
    for (Birthday *b : m_store.birthdays()) {
        if (b->alarmDue(nowAt) && !b->ringing) {
            b->ringing = true;
            started = true;
        }
    }
    // Los eventos del planificador con aviso, diez minutos antes. Mismo tono
    // otra vez: para quien lo oye es un aviso más.
    for (Event *e : m_store.events()) {
        const qint64 key = e->alarmDue(nowAt);
        if (key && e->ringingMs != key) {
            e->ringingMs = key;
            started = true;
        }
    }
    if (!started) return;

    m_alarm->start();
    refreshDueCards();
    refreshPlanner();
    refreshBirthdays();
    refreshAlarmBar();
    applyBadgeAlert();
}

// Callar un aviso. Uno normal queda marcado como avisado y no vuelve; uno que
// se repite salta a su siguiente vuelta, que es justamente lo que lo hará
// sonar otra vez la semana o el año que viene.
void Panel::silence(Note *n) {
    n->ringing = false;
    if (!n->repeats()) {
        n->fired = true;
        return;
    }
    n->dueAtMs = n->nextOccurrenceAfter(QDateTime::currentMSecsSinceEpoch());
    n->fired = false;
}

void Panel::dismissNote(Note *n) {
    silence(n);
    if (!anyRinging()) m_alarm->stop();
    refreshDueCards();
    refreshPlanner();
    applyBadgeAlert();
    save();
}

// Aplazar un aviso que estaba sonando. La tarjeta ya se ha quitado el
// 'ringing' y se ha repintado sola; aquí se apaga lo que vive fuera de ella,
// que es el tono y el rojo del dock, y se rehace el calendario -- que seguía
// enseñando el aviso como si sonara, ahora sobre su día nuevo.
void Panel::rescheduleNote(Note *) {
    if (!anyRinging()) m_alarm->stop();
    refreshPlanner();
    applyBadgeAlert();
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
                                : L("Abrir Tagoror · arrastra para mover"));
    }
    if (m_tray) {
        // Escondida en la bandeja, el icono es la única señal de que algo ha
        // vencido; el mismo cambio que hace el dock en el escritorio.
        m_tray->setIcon(alert ? alertIcon() : qApp->windowIcon());
        m_tray->setToolTip(alert ? L("Recordatorio vencido") : L("Tagoror"));
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
    // El diálogo abre donde estén los datos, salvo que ese sitio no exista
    // ahora mismo: apuntar a una carpeta ausente deja el selector en blanco.
    const QString start = m_store.available() ? appDataDir() : QDir::homePath();
    const QString to =
        QFileDialog::getExistingDirectory(this, L("Carpeta donde guardar las notas"), start);
    if (to.isEmpty()) return;

    // Si allí ya hay notas, hay que preguntar: llevarse las de aquí borra las
    // de allí, y es justo lo que se hace al apuntar a un pendrive que ya las
    // tiene. Si no las hay, no hay ambigüedad que resolver.
    if (QFile::exists(to + "/notes.json")) {
        confirmDataFolder(to);
        return;
    }
    m_store.changeDataDir(to);
    refreshSettings();   // la tarjeta enseña la ruta, y acaba de cambiar
}

void Panel::confirmDataFolder(const QString &to) {
    auto *menu = new Popup(m_theme, this);
    menu->addHeader(L("Esa carpeta ya tiene notas"));
    menu->addItem("notes", L("Abrir las de esa carpeta"),
                  L("Se quedan las que hay allí"),
                  [this, to] { m_store.adoptDataDir(to); refreshSettings(); });
    menu->addItem("copy", L("Llevar allí estas notas"),
                  L("Se sobrescriben las de allí"),
                  [this, to] { m_store.changeDataDir(to); refreshSettings(); });
    menu->addSeparator();
    menu->addItem("minus", L("Cancelar"), QString(), [] {});
    menu->showAt(mapToGlobal(rect().center()));
}

QString Panel::backupPeriodLabel(int days) {
    switch (days) {
        case 0: return L("Nunca");
        case 1: return L("Cada día");
        case 7: return L("Cada semana");
        case 30: return L("Cada mes");
        default: return L("Cada %1 días").arg(days);
    }
}

void Panel::openBackups(QWidget *anchor) {
    auto *menu = new Popup(m_theme, this);
    const Store::Prefs &prefs = m_store.prefs();
    const QList<Store::Backup> list = m_store.backups();

    // --- a mano -------------------------------------------------------------
    menu->addHeader(L("Copias de seguridad"));
    if (!m_store.available()) {
        // Sin carpeta no hay dónde copiar, y ofrecerlo sería mentir.
        menu->addItem("minus", L("Carpeta no disponible"),
                      L("No se puede copiar ahora mismo"), [] {});
        menu->showUnder(anchor);
        return;
    }
    menu->addItem("copy", L("Crear una copia ahora"),
                  list.isEmpty() ? L("Todavía no hay ninguna") : L("%1 guardadas").arg(list.size()),
                  [this, anchor] {
                      m_store.makeBackup();
                      save();
                      refreshSettings();     // la tarjeta lleva la cuenta
                      openBackups(anchor);   // el menú se reabre con el estado nuevo
                  });

    // --- cada cuánto --------------------------------------------------------
    menu->addSeparator();
    menu->addHeader(L("Cada cuánto"));
    QStringList periods;
    for (int d : kBackupPeriods) periods << (d == 0 ? L("Nunca") : L("%1 d").arg(d));
    menu->addChoice(periods, int(kBackupPeriods.indexOf(prefs.backupEveryDays)),
                    [this, anchor](int i) {
                        m_store.prefs().backupEveryDays = kBackupPeriods.at(i);
                        save();
                        refreshSettings();   // la tarjeta dice cada cuánto
                        openBackups(anchor);
                    });

    // --- a qué hora ---------------------------------------------------------
    // Solo cuando hay programación: una hora sin frecuencia no significa nada.
    if (prefs.backupEveryDays > 0) {
        menu->addHeader(L("A qué hora"));
        QStringList hours;
        QList<QTime> times;
        for (int h = 0; h < 24; h += 3) {
            times << QTime(h, 0);
            hours << times.last().toString("HH:mm");
        }
        menu->addChoice(hours, int(times.indexOf(prefs.backupAt)),
                        [this, anchor, times](int i) {
                            m_store.prefs().backupAt = times.at(i);
                            save();
                            openBackups(anchor);
                        });
        // Vacío a propósito: la hora puesta ya la dice el chip encendido, y
        // repetirla aquí solo sirve para que el campo abra con el texto
        // seleccionado y parezca que hay algo que corregir.
        menu->addEditor(L("otra hora · HH:mm"), QString(),
                        [this, anchor](const QString &value) {
                            const QTime t = QTime::fromString(value.trimmed(), "HH:mm");
                            if (!t.isValid()) return;
                            m_store.prefs().backupAt = t;
                            save();
                            openBackups(anchor);
                        });

        // Una programación que no dice cuándo va a actuar no se puede
        // comprobar; y si la hora de hoy ya pasó, la siguiente es mañana.
        const QDateTime due = m_store.nextBackupDue();
        if (due.isValid())
            menu->addItem("clock", backupPeriodLabel(prefs.backupEveryDays),
                          L("La siguiente: %1")
                              .arg(Lang::locale().toString(due, "ddd d MMM · HH:mm")),
                          [] {});
    }

    // --- volver a una -------------------------------------------------------
    if (!list.isEmpty()) {
        menu->addSeparator();
        menu->addHeader(L("Volver a una copia"));
        for (const Store::Backup &b : list) {
            // Nunca QLocale::system(): la fecha se escribe en el idioma
            // elegido en ajustes, como todo lo demás.
            const QString when = b.when.isValid()
                                     ? Lang::locale().toString(b.when, "d MMM yyyy · HH:mm")
                                     : QFileInfo(b.path).fileName();
            menu->addItem("copy", when, L("%1 notas").arg(b.notes),
                          [this, file = b.path] { confirmRestore(file); });
        }
    }
    menu->showUnder(anchor);
}

void Panel::confirmRestore(const QString &file) {
    auto *menu = new Popup(m_theme, this);
    menu->addHeader(L("Volver a esa copia"));
    menu->addItem("copy", L("Restaurar"), L("Lo de ahora queda guardado como copia"),
                  [this, file] { m_store.restoreBackup(file); refreshSettings(); });
    menu->addSeparator();
    menu->addItem("minus", L("Cancelar"), QString(), [] {});
    menu->showAt(mapToGlobal(rect().center()));
}

// --- carpeta de datos -------------------------------------------------------

void Panel::pollBackup() {
    // La copia programada la dispara esto cuando la app está abierta a esa
    // hora; si estaba cerrada, la recoge Store::load() al arrancar.
    if (m_store.backupIfDue()) scheduleSave();   // deja apuntada la fecha
}

// --- actualizaciones --------------------------------------------------------

bool Panel::updateAvailable() const {
    // Sin saber la versión propia no se puede afirmar que haya otra más nueva:
    // comparar contra una cadena vacía haría que cualquier número ganara y el
    // aviso se quedaría puesto para siempre.
    const QString mia = Updater::current();
    const QString latest = m_store.prefs().latestSeen;
    if (mia.isEmpty() || latest.isEmpty()) return false;
    return Updater::compare(latest, mia) > 0;
}

// Una vez al día, sobre el mismo latido que los recordatorios y las copias.
// Preguntar es una petición HTTP, así que la frecuencia la marca la fecha
// guardada y no el temporizador.
void Panel::pollUpdates() {
    if (!m_store.prefs().updateCheck || m_updater->busy()) return;

    constexpr qint64 kDayMs = 24LL * 3600 * 1000;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 last = m_store.prefs().lastUpdateMs;
    // Una fecha en el futuro (el reloj del equipo movido hacia atrás) valdría
    // por "nunca más": se trata como si no hubiera ninguna.
    if (last > 0 && last <= now && now - last < kDayMs) return;

    m_updater->check();
    if (m_settings) m_settings->setUpdateState(true, QString());
}

void Panel::checkUpdatesNow() {
    if (m_updater->busy()) return;
    m_updater->check();
    if (m_settings) m_settings->setUpdateState(true, QString());
}

void Panel::onUpdateChecked(const QString &version, const QString &url, const QString &error) {
    if (error.isEmpty()) {
        m_store.prefs().latestSeen = version;
        m_latestUrl = url;
        // Solo cuenta como comprobada la que contestó: si falló, mañana se
        // vuelve a intentar en vez de esperar un día entero.
        m_store.prefs().lastUpdateMs = QDateTime::currentMSecsSinceEpoch();
        save();
    }
    if (m_settings) m_settings->setUpdateState(false, error);
    refreshUpdateBanner();
}

void Panel::openLatestRelease() {
    // Sin URL guardada (la versión venía del fichero, no de esta sesión) se
    // abre la página de releases, que lleva al mismo sitio.
    QDesktopServices::openUrl(QUrl(m_latestUrl.isEmpty()
                                       ? QStringLiteral("https://github.com/Larzt/tagoror/releases/latest")
                                       : m_latestUrl));
}

void Panel::refreshUpdateBanner() {
    if (!m_updateBar) return;
    const bool show = updateAvailable();
    if (show)
        m_updateText->setText(L("Tagoror %1 ya está disponible · pulsa para verla")
                                  .arg(m_store.prefs().latestSeen));

    // isHidden(), no isVisible(): lo segundo también es falso con el panel
    // escondido en la bandeja, y la tira se estaría rehaciendo cada vez.
    if (show == !m_updateBar->isHidden()) return;
    m_updateBar->setVisible(show);
    // Ocupa alto dentro del shell: con la ventana en su mínimo, aparecer sin
    // rehacerlo la pintaría encima de la primera tarjeta.
    syncShellMinimum();
}

void Panel::pollDataDir() {
    if (m_store.available()) return;
    m_store.retryLoad();   // emite reloaded() la vez que lo consigue
}

void Panel::onStoreReloaded() {
    m_theme.accent = m_store.prefs().accent;
    m_theme.opacity = m_store.prefs().opacity;
    m_theme.textScale = m_store.prefs().textScale;
    VoiceRecorder::setPreferredInput(m_store.prefs().input);
    applyTheme();
    // El idioma lo deja puesto Store::load(); retranslate() reescribe la
    // ventana con él y rehace tarjetas, calendario y bandeja de una vez.
    retranslate();
    // El tamaño y la posición no se tocan a propósito: la ventana es donde el
    // usuario la tiene ahora, no donde estaba cuando se guardó ese fichero.
    // Las notas que sonaban ya no existen -- la lista es otra --, así que el
    // tono se apaga antes de ver si en la nueva hay algo que deba sonar.
    if (!anyRinging()) m_alarm->stop();
    checkReminders();
    tickTimers();
    onTimersChanged();
}

bool Panel::userIdle() const {
    // Con la ventana en segundo plano nadie está escribiendo en ella, aunque
    // el foco se haya quedado dentro de un campo.
    // Un menú abierto tampoco: sus acciones llevan dentro punteros a la nota o
    // al cumpleaños sobre el que se abrió, y la mezcla podría borrarlos.
    if (QApplication::activePopupWidget()) return false;
    if (!isActiveWindow()) return true;
    QWidget *f = QApplication::focusWidget();
    if (!f || f->window() != this) return true;
    return !qobject_cast<QLineEdit *>(f) && !qobject_cast<QTextEdit *>(f);
}

void Panel::onStoreMerged() {
    // Lo que sonaba aquí puede haberse callado en el otro equipo: la versión
    // de fuera trae el "ya avisó", y entonces aquí también se calla.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const int year = QDate::currentDate().year();
    for (Note *n : m_store.notes())
        if (n->ringing && !n->isDue(now)) n->ringing = false;
    for (Birthday *b : m_store.birthdays())
        if (b->ringing && b->firedYear == year) b->ringing = false;
    for (Event *e : m_store.events())
        if (e->ringingMs && e->firedMs >= e->ringingMs) e->ringingMs = 0;
    if (!anyRinging()) m_alarm->stop();

    rebuildList();          // tarjetas, pie, planificador, cumpleaños y dock
    if (m_birthdays) m_birthdays->refresh();
    tickTimers();           // uno que llega en marcha tiene que empezar a contar
    onTimersChanged();
    refreshAlarmBar();
    checkReminders();       // y uno que llega vencido, a sonar
}

void Panel::refreshDataWarning() {
    refreshSettings();   // la página enseña la carpeta y si está disponible
    if (!m_dataWarn) return;
    const bool missing = !m_store.available();
    if (missing) {
        // La ruta va en la ayuda emergente, no en el texto: metida dentro, el
        // cartel ocupa tres líneas de las que dos son una ruta que el usuario
        // ya tiene en la fila de ajustes. No es la trampa de *Card widths*:
        // medido, un cartel con la ruta pide 81px de mínimo, porque wordWrap
        // sí parte una palabra larga cuando no le cabe entera.
        m_dataWarn->setText(
            L("La carpeta de notas no está disponible. Nada de lo que escribas se guardará."));
        m_dataWarn->setToolTip(appDataDir());
    }
    // isHidden(), no isVisible(): lo segundo es falso también con el panel
    // escondido en la bandeja, y el cartel se estaría rehaciendo cada vez.
    if (missing == !m_dataWarn->isHidden()) return;
    m_dataWarn->setVisible(missing);
    // El cartel ocupa alto dentro del shell: si la ventana está en su mínimo,
    // aparecer sin rehacerlo lo pinta encima de la primera tarjeta. Ver
    // *Window behavior*: el mínimo sale del layout, nunca de una constante.
    syncShellMinimum();
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

    // Al mapear la ventana por primera vez el gestor la coloca donde le parece
    // y pisa la posición pedida antes de mostrarla -- la restauración de sesión
    // de KDE, que es la que corre al reiniciar el equipo, hace justo eso. Se
    // vuelve a pedir una sola vez, ya con ventana nativa; de ahí en adelante la
    // ventana es del usuario y aquí no se toca más.
    if (!m_posRestored) {
        m_posRestored = true;
        restoreWindowPos();
    }
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

// Misma idea, un nivel más adentro. Un QStackedWidget pide el mínimo de la
// mayor de sus páginas, así que sin esto el calendario -- que necesita bastante
// más alto -- le impondría su mínimo a la lista de notas, que no lo necesita.
void Panel::showBodyPage(QWidget *page) {
    for (int i = 0; i < m_body->count(); ++i) {
        QWidget *w = m_body->widget(i);
        const auto policy = (w == page) ? QSizePolicy::Preferred : QSizePolicy::Ignored;
        w->setSizePolicy(policy, policy);
    }
    m_body->setCurrentWidget(page);
    syncShellMinimum();
}

int Panel::shellMinimumHeight() const {
    QLayout *l = m_shell ? m_shell->layout() : nullptr;
    if (!l) return kShellMinHeight;

    // Hay que recalcular de dentro afuera, y a mano.
    //
    // Quien llama acaba de esconder o enseñar la lista del día, y lo que
    // invalida el layout del calendario es un LayoutRequest *encolado*: sin
    // esperarlo, minimumSize() contesta con el mínimo de antes. Plegar dejaba
    // así el alto mínimo de cuando estaba abierta y la ventana no encogía.
    // Invalidar solo el de fuera no basta: el de fuera pregunta al de dentro,
    // que sigue con su valor cacheado hasta que le llega ese evento.
    if (QWidget *page = m_body ? m_body->currentWidget() : nullptr) page->updateGeometry();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    l->invalidate();
    l->activate();
    return qMax(kShellMinHeight, l->minimumSize().height() + kShadowMargin * 2);
}

// El mínimo de la ventana lo pide el layout, no una constante.
//
// Un setMinimumSize por debajo de lo que el layout necesita no encoge nada:
// Qt reparte el alto que hay y las geometrías acaban solapándose. Así es como
// la lista del día se dibujaba encima de la rejilla del mes -- el mínimo eran
// 340 px fijos y el calendario, ya plegado, pedía más.
//
// Y al revés: abrir la lista del día en un panel pequeño ya no la mete a la
// fuerza donde no cabe, sino que hace crecer la ventana hacia abajo, que es de
// donde sale el sitio. Solo si por abajo se acaba el área de trabajo sube lo
// justo, la misma regla que anchoredTopLeft.
void Panel::syncShellMinimum() {
    if (!m_shell || !m_stack || m_stack->currentWidget() != m_shell) return;

    // setMinimumSize ya estira la ventana por su cuenta si se queda corta, y lo
    // hace dejando quieta la esquina superior izquierda: crece hacia abajo, que
    // es justo lo que se quiere. Aquí solo queda apuntar que el estirón es
    // nuestro y devolverla al área de trabajo si se ha salido por abajo.
    const QRect before = geometry();
    const int minH = shellMinimumHeight();
    setMinimumSize(kShellMinWidth + kShadowMargin * 2, minH);

    if (height() > before.height()) {
        // Solo se apunta el primer estirón: abrir la lista y cambiar de página
        // son dos crecidas seguidas, y lo que hay que devolver es el tamaño de
        // antes de la primera, no el de en medio.
        if (!m_grownFrom.isValid()) m_grownFrom = before;
        if (const QRect area = placementArea(); area.isValid())
            if (const QPoint p = clampInto(pos(), size(), area); p != pos()) move(p);
        m_grownTo = geometry();
        return;
    }

    // Cabe de sobra. Si la ventana está así de grande porque la estiramos
    // nosotros, plegar la lista la devuelve a lo que medía: crecer para hacer
    // sitio y no volver deja la ventana un poco más grande cada vez.
    //
    // El apunte solo vale mientras la ventana siga siendo la que dejamos: en
    // cuanto el usuario la toca, el tamaño es suyo y aquí ya no se decide.
    if (m_grownFrom.isValid() && geometry() == m_grownTo) {
        // Todavía no se puede devolver todo -- el plegado de la lista bajó el
        // mínimo, pero el calendario sigue pidiendo más que el tamaño de
        // partida --: se deja como está y el apunte sigue en pie, porque no ha
        // dejado de ser verdad. Borrarlo aquí era lo que hacía que un refresco
        // cualquiera por el medio se comiera la vuelta.
        if (m_grownFrom.height() < minH) return;
        setGeometry(m_grownFrom);
        keepOnScreen();
    }
    m_grownFrom = QRect();
    m_grownTo = QRect();
}

void Panel::collapse() {
    // El tamaño que se guarda es el de siempre, no el ensanchado para el
    // planificador: si no, desplegar el dock abriría la ventana ancha para
    // cualquier página.
    if (m_body->currentWidget() == m_planner) leaveWide();
    m_expandedSize = size();
    const QRect panel = geometry();
    // El apunte de la crecida no sobrevive al dock: la geometría con la que se
    // comparaba es la del panel abierto, que a partir de aquí ya no existe.
    m_grownFrom = QRect();
    m_grownTo = QRect();

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
            if (n->ringing) silence(n);
        for (Birthday *b : m_store.birthdays())
            if (b->ringing) silenceBirthday(b);
        for (Timer *t : m_store.timers())
            if (t->ringing()) t->reset();
        for (Event *e : m_store.events())
            if (e->ringingMs) silenceEvent(e);
        m_alarm->stop();
        refreshDueCards();
        refreshPlanner();
        refreshBirthdays();
        onTimersChanged();
        applyBadgeAlert();
        scheduleSave();
    }
    showPage(m_shell);
    setMinimumSize(kShellMinWidth + kShadowMargin * 2, shellMinimumHeight());

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
    // Se plegó con el planificador abierto: collapse() le devolvió el ancho
    // estrecho antes de guardarlo, y aquí se le vuelve a dar el suyo.
    if (m_body->currentWidget() == m_planner) enterWide();
}

// Dónde admite el gestor de ventanas que se ponga la ventana: la pantalla
// disponible, ensanchada con el margen de sombra (que no es marco visible y sí
// puede salirse), y recortada al área de trabajo del gestor. Ese recorte es el
// que manda: pedir una posición fuera de ella no falla, la corrige el gestor y
// la ventana aparece de un salto donde no se pidió.
QRect Panel::placementArea(const QScreen *sc) const {
    if (!sc) sc = screen();
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

// Vuelve al sitio donde quedó la ventana la última vez. Sin esto cada arranque
// -- y reiniciar el equipo es un arranque -- la deja donde le parece al gestor
// de ventanas, normalmente en una esquina que no es la que eligió el usuario.
//
// Vale lo mismo que para plegar y desplegar: en Wayland colocar la propia
// ventana es cosa del compositor y esto se queda en nada; en X11 se aplica.
void Panel::restoreWindowPos() {
    if (!m_store.prefs().hasWindowPos) return;
    const QPoint saved = m_store.prefs().windowPos;

    // La pantalla es la que hay bajo la ventana guardada, no la primaria: con
    // dos monitores, corregir contra la primaria se traería a ella una ventana
    // que vivía en el otro. Se busca por el centro, porque el margen de sombra
    // de la esquina bien puede asomar fuera de la pantalla. Si esa pantalla ya
    // no está (un portátil desenchufado del monitor), placementArea() usa la
    // actual y el recorte trae la ventana de vuelta a lo que hay.
    const QScreen *sc = QGuiApplication::screenAt(QRect(saved, size()).center());
    const QRect area = placementArea(sc);
    move(area.isValid() ? clampInto(saved, size(), area) : saved);
}

// Escape cierra la página en la que se esté y devuelve a las notas, que es lo
// que anuncia el pie. Solo llega aquí lo que no se ha quedado ningún hijo: el
// editor de una tarjeta ya usa Escape para cerrarse y se lo queda antes.
void Panel::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Escape && m_body && m_body->currentWidget() != m_scroll) {
        // En el planificador, Escape cierra antes el formulario si está abierto.
        if (m_body->currentWidget() == m_planner && m_planner->closeEditor()) {
            e->accept();
            return;
        }
        showNotes();
        e->accept();
        return;
    }
    QWidget::keyPressEvent(e);
}

void Panel::moveEvent(QMoveEvent *e) {
    QWidget::moveEvent(e);
    // Apuntar el sitio en cuanto cambia. El destructor guarda, pero al apagar
    // el equipo la sesión mata el proceso y ese guardado no llega; el temporizador
    // del Store agrupa además el chorro de eventos de un arrastre.
    if (isVisible()) scheduleSave();
}

// ---------------------------------------------------------------------------

void Panel::syncPrefs() {
    // Plegado, el tamaño que vale es el que tenía desplegado.
    const bool folded = m_stack && m_stack->currentWidget() == m_badge;
    // Ensanchada para el planificador, lo que vale es lo que medía antes: al
    // arrancar se abre la lista, no el planificador.
    const bool wide = m_narrowGeom.isValid() && geometry() == m_wideGeom;
    m_store.prefs().windowSize = folded ? m_expandedSize : wide ? m_narrowGeom.size() : size();

    // La posición se guarda tal cual esté, plegada o no: al arrancar el panel
    // se abre por esa esquina, que es exactamente lo que hace desplegar el dock.
    m_store.prefs().windowPos = pos();
    m_store.prefs().hasWindowPos = true;
}

void Panel::scheduleSave() { m_store.scheduleSave(); }

void Panel::save() { m_store.save(); }
