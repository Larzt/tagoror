#include "ui/panel.hpp"
#include "audio/alarm.hpp"
#include "ui/alertcenter.hpp"
#include "ui/areatabs.hpp"
#include "audio/recorder.hpp"
#include "ui/birthdays.hpp"
#include "ui/elidedlabel.hpp"
#include "ui/planner.hpp"
#include "ui/settings.hpp"
#include "ui/timers.hpp"
#include "ui/dragwidgets.hpp"
#include "ui/keynav.hpp"
#include "ui/navbar.hpp"
#include "ui/navigator.hpp"
#include "ui/alertzone.hpp"
#include "ui/palette.hpp"
#include "ui/mdpage.hpp"
#include "ui/splash.hpp"
#include "ui/dock.hpp"
#include "ui/notecard.hpp"
#include "ui/popup.hpp"
#include "ui/workarea.hpp"

#include "core/calendarsync.hpp"
#include "core/drivesync.hpp"
#include "core/synccontroller.hpp"
#include "core/folderbackend.hpp"
#include "core/syncengine.hpp"
#include "core/webdavbackend.hpp"
#include "core/lang.hpp"
#include "core/updater.hpp"

#include <QAction>
#include <QApplication>
#include <QAudioDevice>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
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
#include <QResizeEvent>
#include <QPushButton>
#include <QScreen>
#include <QScrollBar>
#include <QSettings>
#include <QScrollArea>
#include <QShortcut>
#include <QStackedWidget>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QTimer>
#include <QPointer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

/// Backup frequencies offered. Zero ("manual only") goes first because it is
/// the switch: whoever wants no schedule turns it off there.
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
    // The Spanish label is stored as is: it is the key retranslate() uses to
    // translate it again without rebuilding the header.
    b->setProperty("tip", tip);
    return b;
}

/// mm:ss (or h:mm:ss) left on a timer, rounded up: 00:00 must mean finished.
QString timerClock(qint64 ms) {
    const qint64 total = (ms + 999) / 1000;
    const qint64 h = total / 3600, m = (total / 60) % 60, sec = total % 60;
    if (h > 0)
        return QString("%1:%2:%3").arg(h).arg(m, 2, 10, QChar('0')).arg(sec, 2, 10, QChar('0'));
    return QString("%1:%2").arg(m, 2, 10, QChar('0')).arg(sec, 2, 10, QChar('0'));
}

/// The label shown for an instant.
QString dueLabel(const QDateTime &when) {
    return Lang::locale().toString(when, "ddd d MMM HH:mm");
}

/// Tray icon while something rings, built at several sizes: the tray picks
/// its own by panel and scale, and a single bitmap blurs when it does not
/// match.
QIcon alertIcon() {
    QIcon icon;
    for (int px : {16, 22, 24, 32, 48})
        icon.addPixmap(paintIcon("bell", QColor("#ff7a6b"), px).pixmap(px, px));
    return icon;
}

}  // namespace

Panel::Panel() {
    setAttribute(Qt::WA_TranslucentBackground);
    // Enter clicks the focused button, not only Space.
    keynav::installButtonEnter();

    // The Store resolved the data folder and the migrations in its constructor,
    // before any disk access.
    m_store.load();
    m_theme.accent = m_store.prefs().accent;
    m_theme.opacity = m_store.prefs().opacity;
    m_theme.textScale = m_store.prefs().textScale;
    m_theme.compact = m_store.prefs().compact;
    m_systemFont = QApplication::font();
    applyFont();
    m_nav = new Navigator(this, &m_store, this);
    VoiceRecorder::setPreferredInput(m_store.prefs().input);

    connect(&m_store, &Store::reloaded, this, &Panel::onStoreReloaded);

    m_alerts = new AlertCenter(&m_store, this);
    applyAlarmPrefs();
    m_dueTimer = new QTimer(this);
    m_dueTimer->setInterval(5000);
    connect(m_dueTimer, &QTimer::timeout, this, &Panel::checkReminders);
    // The same heartbeat watches the data folder (a stat call), so a drive
    // mounted ten minutes later is picked up on its own.
    connect(m_dueTimer, &QTimer::timeout, this, &Panel::pollDataDir);
    connect(m_dueTimer, &QTimer::timeout, this, &Panel::pollBackup);
    connect(m_dueTimer, &QTimer::timeout, this, &Panel::pollUpdates);
    m_dueTimer->start();

    // Timers show seconds, so the 5 s heartbeat is too coarse. Half a second, not
    // one, so drift never skips a second. Runs only while something counts.
    m_tick = new QTimer(this);
    m_tick->setInterval(500);
    connect(m_tick, &QTimer::timeout, this, &Panel::tickTimers);

    m_updater = new Updater(this);
    connect(m_updater, &Updater::finished, this, &Panel::onUpdateChecked);

    // Before buildShell(): the settings page shows it.
    m_sync = new SyncController(&m_store, this);
    m_sync->setCanApply([this] { return userIdle(); });
    m_drive = m_sync->drive();
    connect(m_drive, &DriveSync::openUrl, this, [](const QUrl &url) { QDesktopServices::openUrl(url); });
    connect(m_sync->engine(), &SyncEngine::attachmentsArrived, this, [this] {
        // The cards were built without those images or that recording.
        if (userIdle()) rebuildList();
    });
    connect(&m_store, &Store::merged, this, &Panel::onStoreMerged);
    // Google Calendar only touches events: no need to rebuild the cards (and take
    // the cursor from someone typing); the planner and the alarms suffice. What
    // was deleted there may be what was ringing here.
    connect(m_drive->calendar(), &CalendarSync::eventsChanged, this, [this] {
        m_alerts->stopIfQuiet();
        refreshNotices();
        applyBadgeAlert();
        refreshPlanner();
        refreshNav();
        checkReminders();
    });

    buildShell();
    applyAppModeChrome();
    buildTray();
    rebuildList();
    applyTheme();
    refreshSettings();
    refreshNotices();
    refreshNotices();   // with what the last check found
    checkReminders();   // something may have come due while the app was closed
    tickTimers();       // and a timer may have reached zero

    qApp->installEventFilter(this);
    buildResizeGrips();   // resizing from any edge

    m_nav->start(m_store.prefs().windowSize);
    applyWindowFlags();
    // After applyWindowFlags(): changing flags destroys the native window, and
    // placing it before that places a window about to be recreated.
    m_nav->restoreWindowPos();

}

/// What runs once the window exists: a sync pass and the daily update check,
/// right away rather than on the first heartbeat. Only if one of them is
/// actually going to happen, and the window is on screen, does the splash
/// show what is being waited for; a start where everything is local goes
/// straight to the notes.
void Panel::startUp(bool visible) {
    // Changes made while the app was closed, or an upload that failed last time.
    const bool sync = m_sync->active() && m_store.available();
    const bool updates = updateCheckDue();
    if (sync) m_sync->syncNow();
    if (updates) pollUpdates();
    if (!visible || (!sync && !updates)) return;

    QList<Splash::Step> steps;
    steps << Splash::Step{L("Cargando notas…"), nullptr, 300};
    if (sync)
        steps << Splash::Step{L("Sincronizando…"),
                              [this] { return m_sync->engine()->state() != SyncEngine::Syncing; },
                              4000};
    if (updates)
        steps << Splash::Step{L("Buscando actualizaciones…"),
                              [this] { return !m_updater->busy(); }, 2500};
    auto *splash = new Splash(&m_theme, m_shell);
    splash->run(steps);
}

Panel::~Panel() {
    // The Store saves on destruction and emits saved(); by then this part of the
    // panel is gone, so nobody may answer that signal.
    disconnect(&m_store, nullptr, this, nullptr);
    disconnect(&m_store, nullptr, m_sync, nullptr);
    // Same for the hook: the Store would call it after the panel is gone.
    m_store.beforeSave = nullptr;
    m_nav->syncPrefs();
    m_store.save();
}

void Panel::buildShell() {
    m_store.beforeSave = [this] { m_nav->syncPrefs(); };

    auto *outer = new QVBoxLayout(this);
    m_outer = outer;
    outer->setContentsMargins(shadowMargin(), shadowMargin(), shadowMargin(), shadowMargin());

    m_stack = new QStackedWidget;
    outer->addWidget(m_stack);

    m_shell = new QFrame;
    m_shell->setObjectName("shell");
    m_shell->setMinimumWidth(Navigator::kShellMinWidth);   // no longer fixed: resizable

    // The shadow is set in applyAppModeChrome(): in app mode the window manager
    // draws it and there is no room for it here.

    // A row so the navigation can stand on the left as a rail; the column holds
    // the rest, and the navigation along its bottom while it is a bar.
    m_shellRow = new QHBoxLayout(m_shell);
    m_shellRow->setContentsMargins(0, 0, 0, 0);
    m_shellRow->setSpacing(0);
    m_mainCol = new QVBoxLayout;
    m_mainCol->setContentsMargins(0, 0, 0, 0);
    m_mainCol->setSpacing(0);
    m_shellRow->addLayout(m_mainCol, 1);

    // A fixed chrome of 128px (40 + 36 + 52): nothing appears or disappears
    // pushing the list; notices float over it (see AlertZone).
    m_mainCol->addWidget(buildHeader());
    m_mainCol->addWidget(buildTools());
    m_mainCol->addWidget(buildBody(), 1);

    m_navBar = new NavBar(&m_theme);
    m_navBar->setItems({{m_scroll, "notes", "Notas", "Ctrl+1"},
                        {m_planner, "calendar", "Planner", "Ctrl+2"},
                        {m_timerView, "timer", "Timers", "Ctrl+3"},
                        {m_birthdays, "cake", "Cumpleaños", "Ctrl+4"}});
    m_navBar->activated = [this](QWidget *page) {
        if (page == m_scroll) {
            showNotes();
            return;
        }
        // The countdown in the item is the running timer: going there shows it.
        if (page == m_timerView)
            for (Timer *t : m_store.timers())
                if (t->state == Timer::Running) {
                    m_timerView->focusTimer(t);
                    break;
                }
        openPage(page);
    };
    m_mainCol->addWidget(m_navBar);
    m_stack->addWidget(m_shell);

    m_badge = buildBadge();
    m_stack->addWidget(m_badge);

    m_nav->setStacks(m_stack, m_shell, m_badge, m_body, m_scroll);
    m_nav->addPage(m_planner, m_navBar->buttonFor(m_planner), "planner", "Planner");
    m_nav->addPage(m_timerView, m_navBar->buttonFor(m_timerView), "timers", "Timers");
    m_nav->addPage(m_birthdays, m_navBar->buttonFor(m_birthdays), "birthdays", "Cumpleaños");
    // Settings has no item in the bar: it comes from "⋯", and leaving parks the
    // focus there.
    m_nav->addPage(m_settings, m_moreBtn, "settings", "Ajustes");
    // A Markdown note opens on its own page; back returns to the list.
    m_nav->addPage(m_mdPage, m_backBtn, "markdown", "Nota Markdown");
    // The rail takes its width out of the planner, which then widens by as much.
    m_nav->setWidePage(m_planner, PlannerView::kPreferredWidth + NavBar::kRailWidth);
    m_nav->setListOnly({m_tools});
    m_nav->initBody();

    // Over the whole shell, dimming it; created last so it stacks above the rest.
    m_palette = new CommandPalette(&m_theme, m_shell);

    installShortcuts();
    setMinimumSize(Navigator::kShellMinWidth + shadowMargin() * 2, Navigator::kShellMinHeight);
}

void Panel::installShortcuts() {
    const auto key = [this](const QKeySequence &seq, std::function<void()> fn) {
        auto *sc = new QShortcut(seq, this);
        connect(sc, &QShortcut::activated, this, std::move(fn));
    };
    key(QKeySequence(Qt::CTRL | Qt::Key_K), [this] { openPalette(); });
    // Pages: the bar's order. Settings has the platform's usual key.
    key(QKeySequence(Qt::CTRL | Qt::Key_1), [this] { showNotes(); });
    key(QKeySequence(Qt::CTRL | Qt::Key_2), [this] { openPage(m_planner); });
    key(QKeySequence(Qt::CTRL | Qt::Key_3), [this] { openPage(m_timerView); });
    key(QKeySequence(Qt::CTRL | Qt::Key_4), [this] { openPage(m_birthdays); });
    key(QKeySequence(Qt::CTRL | Qt::Key_Comma), [this] { openPage(m_settings); });
    key(QKeySequence(Qt::CTRL | Qt::Key_M), [this] {
        if (appMode()) showMinimized();
        else collapse();
    });

    // Areas moved from Ctrl+1…9 to Alt+1…9 when the pages took Ctrl.
    key(QKeySequence(Qt::CTRL | Qt::Key_T), [this] { newArea(); });
    for (int i = 1; i <= 9; ++i)
        key(QKeySequence(QKeyCombination(Qt::AltModifier, Qt::Key(Qt::Key_0 + i))), [this, i] {
            const QList<Area *> &areas = m_store.areas();
            if (i <= areas.size()) switchArea(areas.at(i - 1)->id);
        });

    // Notes: Ctrl+N is a text note straight away; the others name their type.
    key(QKeySequence(Qt::CTRL | Qt::Key_F), [this] { focusSearch(); });
    key(QKeySequence(Qt::CTRL | Qt::Key_N), [this] { showNotes(); addNote(Note::Text); });
    key(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_L), [this] { showNotes(); addNote(Note::Check); });
    key(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R), [this] { showNotes(); addNote(Note::Reminder); });
    key(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V), [this] { showNotes(); addNote(Note::Voice); });
    key(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M), [this] { showNotes(); addNote(Note::Markdown); });
}

QFrame *Panel::buildHeader() {
    auto *header = new DragBar;
    header->setObjectName("header");
    header->setCursor(Qt::OpenHandCursor);
    header->setFixedHeight(40);
    m_header = header;

    auto *l = new QHBoxLayout(header);
    l->setContentsMargins(14, 0, 6, 0);
    l->setSpacing(4);

    // Back, only on settings: the one page with no item in the bar.
    m_backBtn = iconButton("chevronLeft", "Volver");
    m_backBtn->setFixedSize(28, 28);
    m_backBtn->hide();
    connect(m_backBtn, &QToolButton::clicked, this, &Panel::showNotes);
    l->addWidget(m_backBtn);

    // Elidable: a label demanding its width would widen the whole window (see
    // *Card widths*).
    m_titleLabel = new ElidedLabel(L("Notas"), QColor(Theme::fg()));
    m_titleLabel->setObjectName("title");
    l->addWidget(m_titleLabel, 1);

    // What each page adds to the header: a new timer, a new birthday.
    m_timerAddBtn = iconButton("plus", "Nuevo temporizador");
    m_timerAddBtn->setFixedSize(28, 28);
    m_timerAddBtn->hide();
    connect(m_timerAddBtn, &QToolButton::clicked, this, [this] { m_timerView->toggleCreator(); });
    l->addWidget(m_timerAddBtn);
    m_bdayAddBtn = iconButton("plus", "Añadir cumpleaños");
    m_bdayAddBtn->setFixedSize(28, 28);
    m_bdayAddBtn->hide();
    connect(m_bdayAddBtn, &QToolButton::clicked, this,
            [this] { openBirthdayEditor(nullptr, m_bdayAddBtn); });
    l->addWidget(m_bdayAddBtn);

    // Folding is one click away on every page, next to "⋯"; in app mode it
    // minimises instead (applyAppModeChrome() swaps the icon and the tip, and
    // moves it after "⋯").
    m_foldBtn = iconButton(appMode() ? "minus" : "dock", appMode() ? "Minimizar" : "Plegar a icono");
    m_foldBtn->setFixedSize(28, 28);
    connect(m_foldBtn, &QToolButton::clicked, this, [this] {
        if (appMode()) showMinimized();
        else collapse();
    });
    l->addWidget(m_foldBtn);

    m_moreBtn = iconButton("more", "Más opciones");
    m_moreBtn->setFixedSize(28, 28);
    connect(m_moreBtn, &QToolButton::clicked, this, [this] { openMoreMenu(m_moreBtn); });
    l->addWidget(m_moreBtn);

    // App mode has no system frame: the window's own controls go here, after
    // "⋯", in Tagoror's style (applyAppModeChrome() shows them).
    m_maxBtn = iconButton("maximize", "Maximizar");
    m_maxBtn->setFixedSize(28, 28);
    connect(m_maxBtn, &QToolButton::clicked, this, &Panel::toggleMaximized);
    l->addWidget(m_maxBtn);
    m_closeBtn = iconButton("close", "Cerrar");
    m_closeBtn->setObjectName("winClose");
    m_closeBtn->setFixedSize(28, 28);
    connect(m_closeBtn, &QToolButton::clicked, this, &Panel::close);
    l->addWidget(m_closeBtn);
    return header;
}

/// One 36px row over the list: area tabs, search and new note. The search
/// field unfolds over the tabs rather than next to them, which at 352px would
/// leave neither readable.
QWidget *Panel::buildTools() {
    auto *row = new QFrame;
    row->setObjectName("toolsRow");
    row->setFixedHeight(36);
    m_tools = row;
    auto *l = new QHBoxLayout(row);
    l->setContentsMargins(0, 0, 6, 0);
    l->setSpacing(4);

    m_areaTabs = new AreaTabs(&m_theme);
    m_areaTabs->activated = [this](const QString &id) { switchArea(id); };
    m_areaTabs->addRequested = [this] { newArea(); };
    m_areaTabs->menuRequested = [this](const QString &id, const QPoint &at) {
        openAreaMenu(id, at);
    };
    m_areaTabs->overflowRequested = [this](const QPoint &at) { openAreaOverflow(at); };
    m_areaTabs->renamed = [this](const QString &id, const QString &name) {
        if (Area *a = m_store.area(id)) {
            a->name = name;
            save();
        }
        refreshAreaTabs();
    };
    m_areaTabs->deleteRequested = [this](const QString &id) { confirmDeleteArea(id); };
    m_areaTabs->moveRequested = [this](const QString &id, int steps) {
        if (Area *a = m_store.area(id)) m_store.moveArea(a, steps);
        refreshAreaTabs();
    };
    l->addWidget(m_areaTabs, 1);

    m_search = new QLineEdit;
    m_search->setObjectName("searchField");
    m_search->setClearButtonEnabled(true);
    m_search->setMinimumWidth(24);   // see *Card widths*: it must not demand its width
    m_search->setContextMenuPolicy(Qt::NoContextMenu);
    m_search->installEventFilter(this);   // Escape closes it
    m_search->hide();
    connect(m_search, &QLineEdit::textChanged, this, &Panel::applyFilter);
    l->addWidget(m_search, 1);
    l->addSpacing(4);

    m_searchBtn = iconButton("search", "Buscar · Ctrl+F");
    m_searchBtn->setFixedSize(28, 28);
    connect(m_searchBtn, &QToolButton::clicked, this, [this] {
        if (m_search->isVisible()) closeSearch();
        else focusSearch();
    });
    l->addWidget(m_searchBtn);

    m_addBtn = iconButton("plus", "Nueva nota");
    m_addBtn->setObjectName("addNote");
    m_addBtn->setFixedSize(28, 28);
    connect(m_addBtn, &QToolButton::clicked, this, [this] { openNewNoteMenu(m_addBtn); });
    l->addWidget(m_addBtn);
    return row;
}

QWidget *Panel::buildBody() {
    m_listHost = new QWidget;
    m_listHost->setObjectName("listHost");
    m_listLayout = new QVBoxLayout(m_listHost);
    m_listLayout->setContentsMargins(8, 8, 8, 8);
    m_listLayout->setSpacing(7);
    m_listLayout->addStretch();

    // Placeholder for when no note is left: an empty list gave no hint of where
    // to start.
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
    m_emptyBtn->setFocusPolicy(Qt::TabFocus);   // ring only with the keyboard
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
    m_scroll->setMinimumHeight(150);   // no maximum: grows with the window
    m_scroll->viewport()->setAutoFillBackground(false);
    // With a selector: a bare rule is inherited by every child and overrides
    // their background (it left the empty-state button without its fill).
    m_scroll->viewport()->setObjectName("scrollViewport");

    m_planner = new PlannerView(m_theme);
    m_planner->setView(PlannerView::View(m_store.prefs().plannerView));
    m_planner->setHidden(m_store.prefs().plannerHidden);
    m_planner->setSources(&m_store.events(), &m_store.notes(), &m_store.birthdays(),
                          &m_store.categories());
    connect(m_planner, &PlannerView::categoryCreated, this, [this](Event::Category *c) {
        m_store.addCategory(c);   // the Store takes ownership and saves it
    });
    connect(m_planner, &PlannerView::categoryChanged, this, [this](Event::Category *) { save(); });
    connect(m_planner, &PlannerView::categoryDeleted, this, &Panel::removeCategory);
    connect(m_planner, &PlannerView::eventCreated, this, [this](Event *e) {
        m_store.addEvent(e);   // the Store takes ownership and saves it
        refreshPlanner();
        refreshNav();
        checkReminders();      // one starting right now must ring right now
    });
    connect(m_planner, &PlannerView::eventChanged, this, [this](Event *) {
        // One dragged to another time stops ringing (moveItem() clears ringingMs): if
        // it was the only one, the tone and the red go with it.
        m_alerts->stopIfQuiet();
        refreshNotices();
        applyBadgeAlert();
        save();
        checkReminders();
    });
    connect(m_planner, &PlannerView::eventDeleted, this, [this](Event *e) {
        // As with a note: if it was the only thing ringing, the tone stops.
        const bool wasRinging = e->ringingMs != 0;
        m_store.removeEvent(e);
        if (wasRinging) m_alerts->stopIfQuiet();
        refreshNotices();
        applyBadgeAlert();
        refreshPlanner();
        refreshNav();
    });
    connect(m_planner, &PlannerView::reminderCreated, this, &Panel::createReminder);
    // Dragged in the planner: the same as changing the date on its card
    // (NoteCard::applyDue()), postponing included.
    connect(m_planner, &PlannerView::reminderMoved, this, [this](Note *n, qint64 dueAtMs) {
        n->dueAtMs = dueAtMs;
        n->due = dueLabel(n->dueAt());
        n->fired = false;
        n->ringing = false;
        m_alerts->stopIfQuiet();
        refreshDueCards();
        refreshNotices();
        applyBadgeAlert();
        save();
        checkReminders();
    });
    connect(m_planner, &PlannerView::noteActivated, this, &Panel::revealNote);
    connect(m_planner, &PlannerView::birthdayActivated, this, [this](Birthday *) {
        openPage(m_birthdays);
    });
    connect(m_planner, &PlannerView::viewChanged, this, [this](int v) {
        m_store.prefs().plannerView = v;
        scheduleSave();
    });
    connect(m_planner, &PlannerView::hiddenChanged, this, [this](const QStringList &cats) {
        m_store.prefs().plannerHidden = cats;
        scheduleSave();
    });

    m_timerView = new TimerView(m_theme);
    m_timerView->setSource(&m_store.timers());
    connect(m_timerView, &TimerView::createRequested, this, &Panel::createTimer);
    connect(m_timerView, &TimerView::toggleRequested, this, &Panel::toggleTimer);
    connect(m_timerView, &TimerView::resetRequested, this, &Panel::resetTimer);
    connect(m_timerView, &TimerView::removeRequested, this, &Panel::removeTimer);
    connect(m_timerView, &TimerView::plusRequested, this, [this](Timer *t) {
        t->addTime(60 * 1000);
        m_alerts->stopIfQuiet();
        save();
        onTimersChanged();
    });

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

    m_settings = new SettingsView(m_theme);
    m_settings->setSource(&m_store);
    connect(m_settings, &SettingsView::accentPicked, this, [this](const QColor &c) {
        m_theme.accent = c;
        m_store.prefs().accent = c;
        applyTheme();
        rebuildList();   // the cards carry the accent inline
        save();
    });
    connect(m_settings, &SettingsView::accentEditorRequested, this, &Panel::openAccentEditor);
    connect(m_settings, &SettingsView::fontEditorRequested, this,
            [this](QWidget *anchor) { openFontEditor(anchor); });
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
        // The sheet updates everything that inherits it; what is painted by hand
        // (planner, timers) picks it up in setTheme().
        applyTheme();
        refreshSettings();   // the chosen button and the sample
        save();
    });
    connect(m_settings, &SettingsView::compactPicked, this, [this](bool compact) {
        if (compact == m_theme.compact) return;
        m_theme.compact = compact;
        m_store.prefs().compact = compact;
        applyTheme();
        rebuildList();   // the cards take their margins when built
        refreshSettings();
        save();
    });
    connect(m_settings, &SettingsView::alarmToneRequested, this,
            [this](QWidget *anchor) { openAlarmToneMenu(anchor); });
    connect(m_settings, &SettingsView::alarmVolumeChanged, this, [this](int v) {
        m_store.prefs().alarmVolume = v;
        m_alerts->alarm()->setVolume(v);
        scheduleSave();
    });
    connect(m_settings, &SettingsView::alarmPreviewRequested, this,
            [this] { m_alerts->alarm()->preview(); });
    connect(m_settings, &SettingsView::appModeToggled, this, &Panel::setAppMode);
    connect(m_settings, &SettingsView::onTopToggled, this, &Panel::setOnTop);
    connect(m_settings, &SettingsView::sizePerPageToggled, this, [this](bool on) {
        m_nav->setSizePerPage(on);
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
        if (on) pollUpdates();   // if a check was due, it happens now
    });
    connect(m_settings, &SettingsView::checkUpdatesRequested, this, &Panel::checkUpdatesNow);
    connect(m_settings, &SettingsView::openLatestRequested, this, &Panel::openLatestRelease);
    connect(m_settings, &SettingsView::quitRequested, qApp, &QApplication::quit);
    m_settings->setSync(m_sync);
    connect(m_sync, &SyncController::changed, m_settings, &SettingsView::refreshSync);
    connect(m_sync, &SyncController::changed, this, [this] { refreshNotices(); });
    connect(m_settings, &SettingsView::googleConnectRequested, m_drive, &DriveSync::connectAccount);
    connect(m_settings, &SettingsView::googleCancelRequested, m_drive, &DriveSync::cancel);
    connect(m_settings, &SettingsView::googleDisconnectRequested, m_drive, &DriveSync::disconnectAccount);
    connect(m_settings, &SettingsView::syncProviderPicked, this, [this](const QString &id) {
        m_sync->setProvider(id);
        m_settings->refresh();
        m_sync->syncNow();
    });
    connect(m_settings, &SettingsView::syncWhenPicked, this, [this](int when) {
        m_sync->setWhen(SyncController::When(when));
        m_settings->refresh();
    });
    connect(m_settings, &SettingsView::webdavSaveRequested, this,
            [this](const QString &server, const QString &user, const QString &password) {
        m_sync->webdav()->setCredentials(server, user, password);
        m_settings->refresh();
        m_sync->syncNow();   // the first pass also tells whether the credentials work
    });
    connect(m_settings, &SettingsView::webdavForgetRequested, this, [this] {
        m_sync->webdav()->forget();
        m_settings->refresh();
    });
    connect(m_settings, &SettingsView::folderChooseRequested, this, [this] {
        const QString start = m_sync->folder()->path().isEmpty() ? QDir::homePath()
                                                                 : m_sync->folder()->path();
        const QString to = QFileDialog::getExistingDirectory(
            this, L("Carpeta donde dejar la copia sincronizada"), start);
        if (to.isEmpty()) return;
        if (m_sync->folder()->setPath(to)) m_sync->syncNow();
        m_settings->refresh();
    });
    connect(m_settings, &SettingsView::calendarToggled, this, [this](bool on) {
        m_drive->calendar()->setEnabled(on);
        // Without the scope, enabling it means re-authorising the account.
        if (on && !m_drive->hasCalendarScope()) m_drive->connectAccount();
        else if (on) m_sync->syncNow();
        m_settings->refresh();
    });
    connect(m_settings, &SettingsView::calendarGrantRequested, m_drive, &DriveSync::connectAccount);
    connect(m_settings, &SettingsView::calendarFollowToggled, this,
            [this](const QString &calId, bool on) {
        CalendarSync *cal = m_drive->calendar();
        if (on) {
            cal->follow(calId);
            refreshPlanner();
            m_sync->syncNow();
        } else if (Event::Category *c = cal->categoryFor(calId)) {
            removeCategory(c);
        }
    });
    connect(m_settings, &SettingsView::calendarTargetRequested, this, &Panel::openCalendarTarget);
    connect(m_settings, &SettingsView::syncNowRequested, this, [this] {
        if (!m_store.available()) return;
        save();                 // the latest edits, before uploading
        m_sync->syncNow();
    });

    m_mdPage = new MarkdownPage(m_theme);
    connect(m_mdPage, &MarkdownPage::edited, this, &Panel::scheduleSave);
    connect(m_mdPage, &MarkdownPage::closeRequested, this, &Panel::showNotes);

    m_body = new QStackedWidget;
    m_body->addWidget(m_scroll);
    m_body->addWidget(m_planner);
    m_body->addWidget(m_timerView);
    m_body->addWidget(m_birthdays);
    m_body->addWidget(m_settings);
    m_body->addWidget(m_mdPage);

    // The notices float over the bottom of the page instead of taking height in
    // the layout: m_bodyHost carries them as an unmanaged child (placeAlertZone()).
    m_bodyHost = new QWidget;
    auto *hl = new QVBoxLayout(m_bodyHost);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->addWidget(m_body);
    m_alertZone = new AlertZone(&m_theme, m_bodyHost);
    m_alertZone->changed = [this] { placeAlertZone(); };
    return m_bodyHost;
}

/// Above the navigation, 8px from the edges, over whatever page is open. The
/// list gets as much bottom padding, so its last card scrolls clear of it.
void Panel::placeAlertZone() {
    if (!m_alertZone || !m_bodyHost) return;
    int h = 0;
    if (m_alertZone->hasNotices()) {
        const int w = m_bodyHost->width() - 16;
        h = m_alertZone->heightFor(w);
        m_alertZone->setGeometry(8, m_bodyHost->height() - h - 8, w, h);
        m_alertZone->raise();
    }
    if (m_listLayout) m_listLayout->setContentsMargins(8, 8, 8, h > 0 ? h + 16 : 8);
}

/// A bar while narrow, a rail on the left from NavBar::kRailFrom: at 352px
/// width is what is scarce, beside the planner it is height.
void Panel::updateNavLayout() {
    if (!m_navBar || !m_shell) return;
    const bool rail = m_shell->width() >= NavBar::kRailFrom;
    if (rail == (m_navBar->layoutMode() == NavBar::Rail)) return;
    if (rail) {
        m_mainCol->removeWidget(m_navBar);
        m_shellRow->insertWidget(0, m_navBar);
    } else {
        m_shellRow->removeWidget(m_navBar);
        m_mainCol->addWidget(m_navBar);
    }
    m_navBar->setLayoutMode(rail ? NavBar::Rail : NavBar::Bar);
}

QWidget *Panel::buildBadge() {
    auto *host = new QWidget;
    auto *hl = new QHBoxLayout(host);
    hl->setContentsMargins(0, 0, 0, 0);

    m_dock = new DockButton(&m_theme);
    m_dock->setObjectName("badge");
    m_dock->setToolTip(L("Abrir Tagoror · arrastra para mover"));
    auto *shadow = new QGraphicsDropShadowEffect(m_dock);
    shadow->setBlurRadius(40);
    shadow->setOffset(0, 14);
    shadow->setColor(QColor(0, 0, 0, 140));
    m_dock->setGraphicsEffect(shadow);
    connect(m_dock, &QToolButton::clicked, this, &Panel::expand);

    // Top-left: the folded window measures exactly the dock, so there is no room
    // to spare, and anchoredTopLeft() handles which screen corner it folds to.
    hl->addWidget(m_dock, 0, Qt::AlignTop | Qt::AlignLeft);
    hl->addStretch();
    return host;
}

void Panel::applyTheme() {
    setStyleSheet(m_theme.sheet());

    for (QToolButton *b : {m_backBtn, m_foldBtn, m_moreBtn, m_maxBtn, m_closeBtn, m_searchBtn,
                           m_timerAddBtn, m_bdayAddBtn})
        b->setIcon(paintIcon(b->property("iconKind").toString(), QColor(Theme::muted())));
    // The new-note button is filled with the accent, so its icon is dark.
    m_addBtn->setIcon(paintIcon("plus", QColor("#0c1222")));
    if (m_navBar) m_navBar->update();

    if (m_planner) m_planner->setTheme(m_theme);
    if (m_timerView) m_timerView->setTheme(m_theme);
    if (m_birthdays) m_birthdays->setTheme(m_theme);
    // Settings is not rebuilt: it repaints what carries the accent and keeps the
    // opacity slider alive, which is what just called here.
    if (m_settings) m_settings->setTheme(m_theme);
    if (m_mdPage) m_mdPage->setTheme(m_theme);
    applyBadgeAlert();
    if (m_dock) m_dock->update();   // the stones are in the accent
}

void Panel::setLanguage(Lang::Code code) {
    if (code == m_store.prefs().lang) return;
    m_store.prefs().lang = code;
    Lang::setCurrent(code);
    retranslate();
    save();
}

/// The window's fixed texts are set again one by one; cards and planner are
/// rebuilt whole, simpler than hunting each label inside them.
void Panel::retranslate() {
    m_emptyText->setText(L("Todavía no hay notas"));
    m_emptyBtn->setText(L("Crear la primera"));
    m_search->setPlaceholderText(L("Buscar en %1…").arg(QString()));

    for (QToolButton *b : {m_backBtn, m_foldBtn, m_moreBtn, m_maxBtn, m_closeBtn, m_searchBtn,
                           m_addBtn, m_timerAddBtn, m_bdayAddBtn})
        b->setToolTip(L(b->property("tip").toString()));
    m_navBar->retranslate();

    buildTrayMenu();
    applyBadgeAlert();   // the tray tooltip carries text, and so do the notices
    if (m_planner) {
        // After a Store reload the saved view and filter are different.
        m_planner->setView(PlannerView::View(m_store.prefs().plannerView));
        m_planner->setHidden(m_store.prefs().plannerHidden);
        m_planner->retranslate();
    }
    if (m_timerView) m_timerView->retranslate();
    if (m_birthdays) {
        // After a Store reload the preferences differ, and the list order is one of
        // them; retranslate() repaints the page anyway.
        m_birthdays->setByMonth(m_store.prefs().birthdaysByMonth);
        m_birthdays->retranslate();
    }
    if (m_settings) m_settings->retranslate();
    m_mdPage->retranslate();
    refreshTitle();
    // Rebuilds the cards and with them the planner and the dock.
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
    // clear the existing cards (the empty-state placeholder and the stretch stay)
    for (int i = m_listLayout->count() - 1; i >= 0; --i) {
        QWidget *w = m_listLayout->itemAt(i)->widget();
        if (!qobject_cast<NoteCard *>(w)) continue;
        delete m_listLayout->takeAt(i);
        // Hidden before release: until the deferred delete runs it would stay painted
        // over the new list (visible when switching areas).
        w->hide();
        w->deleteLater();
    }

    m_listLayout->setSpacing(m_theme.compact ? 4 : 7);
    const QList<Note *> shown = m_store.notesIn(currentArea());
    for (Note *n : shown) {
        auto *card = new NoteCard(n, m_theme);
        connect(card, &NoteCard::dirty, this, &Panel::scheduleSave);
        connect(card, &NoteCard::dirty, this, &Panel::refreshPlanner);
        connect(card, &NoteCard::deleteRequested, this, &Panel::removeNote);
        connect(card, &NoteCard::dismissRequested, this, &Panel::dismissNote);
        connect(card, &NoteCard::rescheduled, this, &Panel::rescheduleNote);
        connect(card, &NoteCard::moveRequested, this, &Panel::moveNote);
        connect(card, &NoteCard::areaMenuRequested, this, &Panel::openMoveNoteMenu);
        connect(card, &NoteCard::dragStarted, this, [this, card] { beginCardDrag(card); });
        connect(card, &NoteCard::dragMoved, this,
                [this, card](const QPoint &at) { dragCardTo(card, at); });
        connect(card, &NoteCard::dragFinished, this, [this, card] { endCardDrag(card); });
        connect(card, &NoteCard::openRequested, this, &Panel::openMarkdown);
        m_listLayout->insertWidget(m_listLayout->count() - 2, card);
    }

    m_empty->setVisible(shown.isEmpty());
    // With a single area it is the usual message; with several it names the
    // empty one.
    if (m_store.areas().size() > 1) {
        const Area *a = m_store.area(currentArea());
        m_emptyText->setText(L("Aún no hay notas en %1").arg(a ? a->name : QString()));
    } else {
        m_emptyText->setText(L("Todavía no hay notas"));
    }
    refreshNav();
    refreshPlanner();
    refreshBirthdays();
    applyBadgeAlert();
    if (m_search && !m_search->text().isEmpty())
        applyFilter(m_search->text());
}

/// The bar: which page is open (none on settings), the countdown of the timer
/// that ends first in place of "Timers", and a red dot on Notas while a
/// reminder rings and the list is not in front.
void Panel::refreshNav() {
    if (!m_navBar) return;
    QWidget *current = m_body ? m_body->currentWidget() : nullptr;
    // Settings is out of the bar; a Markdown note's page belongs to Notas.
    m_navBar->setActive(current == m_settings ? nullptr
                        : current == m_mdPage ? static_cast<QWidget *>(m_scroll)
                                              : current);

    const Timer *next = nullptr;
    for (const Timer *t : m_store.timers())
        if (t->state == Timer::Running && (!next || t->endsAtMs < next->endsAtMs)) next = t;
    m_navBar->setCountdown(m_timerView, next ? timerClock(next->remainingMs()) : QString());
    // Folded, the dock is all there is: it carries the same countdown.
    if (m_dock) m_dock->setCountdown(next ? timerClock(next->remainingMs()) : QString());

    bool reminder = false;
    for (const Note *n : m_store.notes()) reminder |= n->ringing;
    m_navBar->setDot(m_scroll, reminder && current != m_scroll);
}

void Panel::addNote(Note::Type type) {
    auto *n = new Note;
    n->type = type;
    n->title = type == Note::Check    ? L("Nueva lista")
             : type == Note::Reminder ? L("Nuevo recordatorio")
             : type == Note::Voice    ? L("Nota de voz")
             : type == Note::Markdown ? L("Nota Markdown")
                                      : L("Nueva nota");
    if (type == Note::Reminder) {
        // A real instant from the start, so the new reminder rings and appears in the
        // planner.
        QDateTime when(QDate::currentDate(), QTime(18, 0));
        if (when <= QDateTime::currentDateTime()) when = when.addDays(1);
        n->dueAtMs = when.toMSecsSinceEpoch();
        n->due = dueLabel(when);
    }
    m_store.setNoteArea(n, currentArea());
    m_store.add(n);
    rebuildList();
    // A Markdown note is written on its own page, straight away.
    if (type == Note::Markdown) openMarkdown(n);
}

void Panel::openMarkdown(Note *n) {
    if (!m_store.notes().contains(n)) return;
    m_mdPage->setNote(n);
    if (m_body->currentWidget() == m_mdPage) refreshTitle();
    else openPage(m_mdPage);
    m_mdPage->focusEditor();
}

void Panel::removeNote(Note *n) {
    // If the note was ringing and was the only one, the tone kept going with
    // nothing on screen to explain or stop it. Asked before deleting, since
    // afterwards there is no one to ask.
    const bool wasRinging = n->ringing;
    if (m_mdPage->note() == n) m_mdPage->forget();   // about to be freed
    m_store.remove(n);
    if (wasRinging) m_alerts->stopIfQuiet();
    if (m_body->currentWidget() == m_mdPage) showNotes();
    rebuildList();   // also repaints the dock and the tray
}

QString Panel::currentArea() const {
    const QString id = m_store.prefs().activeArea;
    if (m_store.area(id)) return id;
    const QList<Area *> &areas = m_store.areas();
    return areas.isEmpty() ? QString(Area::kDefaultId) : areas.first()->id;
}

void Panel::switchArea(const QString &id) {
    if (!m_store.area(id)) return;
    showNotes();   // Ctrl+1…9 also from another page
    if (id == currentArea()) {
        refreshAreaTabs();
        return;
    }
    m_store.prefs().activeArea = id;
    rebuildList();
    m_scroll->verticalScrollBar()->setValue(0);
    scheduleSave();
}

void Panel::refreshAreaTabs() {
    if (!m_areaTabs) return;
    QList<AreaTabs::Tab> tabs;
    for (const Area *a : m_store.areas()) {
        AreaTabs::Tab t{a->id, a->name, a->glyph, false};
        for (const Note *n : m_store.notes())
            if (n->ringing && m_store.areaOf(n) == a->id) t.ringing = true;
        tabs.append(t);
    }
    m_areaTabs->setTabs(tabs, currentArea());
}

/// Ctrl+T or "+": the area is created at once with its name being edited.
/// Enter confirms; Escape keeps the default name, which can be changed later.
void Panel::newArea() {
    showNotes();
    Area *a = m_store.addArea(L("Área nueva"));
    switchArea(a->id);
    m_areaTabs->beginRename(a->id);
}

void Panel::openAreaMenu(const QString &id, const QPoint &globalPos) {
    Area *a = m_store.area(id);
    if (!a) return;
    const QList<Area *> &areas = m_store.areas();
    const int index = int(areas.indexOf(a));

    auto *menu = new Popup(m_theme, m_areaTabs);
    menu->addHeader(a->name);
    menu->addItem("pencil", L("Renombrar"), "F2", [this, id] { m_areaTabs->beginRename(id); });

    QStringList kinds{"glyph-none"};
    QStringList tips{L("Sin glifo")};
    const QStringList names{L("Círculo"), L("Cuadrado"), L("Triángulo"), L("Rombo"),
                            L("Anillo"), L("Barra")};
    for (int i = 0; i < Area::glyphs().size(); ++i) {
        kinds << "glyph-" + Area::glyphs().at(i);
        tips << names.value(i);
    }
    menu->addIconChoice(kinds, tips, int(Area::glyphs().indexOf(a->glyph)) + 1,
                        [this, id](int i) {
                            Area *area = m_store.area(id);
                            if (!area) return;
                            area->glyph = i == 0 ? QString() : Area::glyphs().value(i - 1);
                            save();
                            refreshAreaTabs();
                        });

    if (areas.size() > 1) {
        menu->addSeparator();
        if (index > 0)
            menu->addItem("chevronLeft", L("Mover a la izquierda"), "Ctrl+⇧+←", [this, id] {
                if (Area *area = m_store.area(id)) m_store.moveArea(area, -1);
                refreshAreaTabs();
            });
        if (index < areas.size() - 1)
            menu->addItem("chevronRight", L("Mover a la derecha"), "Ctrl+⇧+→", [this, id] {
                if (Area *area = m_store.area(id)) m_store.moveArea(area, 1);
                refreshAreaTabs();
            });
        menu->addSeparator();
        menu->addItem("trash", L("Eliminar área…"), L("Supr"),
                      [this, id] { confirmDeleteArea(id); });
    }
    menu->showAt(globalPos);
}

/// "+N": every area, with how many notes each holds.
void Panel::openAreaOverflow(const QPoint &globalPos) {
    auto *menu = new Popup(m_theme, m_areaTabs);
    menu->addHeader(L("Áreas · %1").arg(m_store.areas().size()));
    const QString current = currentArea();
    for (const Area *a : m_store.areas()) {
        const QList<Note *> own = m_store.notesIn(a->id);
        bool ringing = false;
        for (const Note *n : own) ringing |= n->ringing;
        QString sub = L("%1 notas").arg(own.size());
        if (ringing) sub += " · " + L("Suena");
        if (a->id == current) sub += " · " + L("abierta");
        const QString id = a->id;
        menu->addItem(a->glyph.isEmpty() ? QString() : "glyph-" + a->glyph, a->name, sub,
                      [this, id] { switchArea(id); });
    }
    menu->addSeparator();
    menu->addItem("plus", L("Nueva área"), "Ctrl+T", [this] { newArea(); });
    menu->showAt(globalPos);
}

void Panel::confirmDeleteArea(const QString &id) {
    Area *a = m_store.area(id);
    // The last area cannot be deleted: the list always lives in one.
    if (!a || m_store.areas().size() < 2) return;

    const QList<Note *> own = m_store.notesIn(id);
    int reminders = 0;
    for (const Note *n : own)
        if (n->isScheduled()) ++reminders;

    auto remove = [this, id](const QString &moveTo) {
        Area *area = m_store.area(id);
        if (!area) return;
        m_store.removeArea(area, moveTo);
        // If deleted notes were ringing, the tone must not keep going alone.
        m_alerts->stopIfQuiet();
        if (!moveTo.isEmpty()) m_store.prefs().activeArea = moveTo;
        rebuildList();
        refreshPlanner();
    };

    auto *menu = new Popup(m_theme, m_areaTabs);
    menu->addHeader(L("Eliminar «%1»").arg(a->name));
    if (own.isEmpty()) {
        menu->addText(L("No tiene notas."));
        menu->addItem("trash", L("Eliminar área"), QString(), [remove] { remove(QString()); });
    } else {
        QString text = own.size() == 1 ? L("Tiene 1 nota") : L("Tiene %1 notas").arg(own.size());
        if (reminders == 1) text += L(", 1 con recordatorio");
        else if (reminders > 1) text += L(", %1 con recordatorio").arg(reminders);
        text += L(". Los ajustes, el planificador y los temporizadores no cambian.");
        menu->addText(text);
        menu->addHeader(L("Mover sus notas a"));
        for (const Area *other : m_store.areas()) {
            if (other->id == id) continue;
            const QString to = other->id;
            menu->addItem(other->glyph.isEmpty() ? QString() : "glyph-" + other->glyph,
                          other->name, L("Eliminar y mover"), [remove, to] { remove(to); });
        }
        menu->addSeparator();
        menu->addItem("trash", L("Eliminar también sus notas"), L("No se puede deshacer"),
                      [remove] { remove(QString()); });
    }
    menu->showAt(m_areaTabs->menuPoint(id));
}

void Panel::openMoveNoteMenu(Note *n, const QPoint &globalPos) {
    if (!m_store.notes().contains(n)) return;
    auto *menu = new Popup(m_theme, this);
    menu->addHeader(L("Mover a"));
    const QString from = m_store.areaOf(n);
    for (const Area *a : m_store.areas()) {
        if (a->id == from) continue;
        const QString to = a->id;
        menu->addItem(a->glyph.isEmpty() ? QString() : "glyph-" + a->glyph, a->name,
                      L("%1 notas").arg(m_store.notesIn(a->id).size()),
                      [this, n, to] { moveNoteToArea(n, to); });
    }
    if (m_store.areas().size() > 1) menu->addSeparator();
    menu->addItem("plus", L("Nueva área…"), QString(), [this, n] {
        if (!m_store.notes().contains(n)) return;
        Area *a = m_store.addArea(L("Área nueva"));
        moveNoteToArea(n, a->id);
        switchArea(a->id);
        m_areaTabs->beginRename(a->id);
    });
    menu->showAt(globalPos);
}

void Panel::moveNoteToArea(Note *n, const QString &areaId) {
    // The menu was opened earlier: the note (or the area) may have been deleted
    // meanwhile by a sync.
    if (!m_store.notes().contains(n) || !m_store.area(areaId)) return;
    m_store.setNoteArea(n, areaId);
    save();
    rebuildList();
}

void Panel::moveNote(Note *n, int steps) {
    // One step within its area: the neighbour that counts is the one on the same
    // tab, not the next one in the global list.
    const QList<Note *> area = m_store.notesIn(m_store.areaOf(n));
    const int at = int(area.indexOf(n));
    if (at < 0 || at + steps < 0 || at + steps >= area.size()) return;

    QList<Note *> order = m_store.notes();
    order.move(order.indexOf(n), order.indexOf(area.at(at + steps)));
    m_store.setOrder(order);
    rebuildList();
    save();
}

void Panel::beginCardDrag(NoteCard *card) {
    m_dragCard = card;
    card->setProperty("dragging", true);
    // A dynamic property does not repaint by itself.
    card->style()->unpolish(card);
    card->style()->polish(card);
    card->raise();
}

/// Reorders live during the drag: the card moves in the layout as soon as it
/// crosses another's centre, not on release, so it is visible where it lands.
void Panel::dragCardTo(NoteCard *card, const QPoint &globalPos) {
    if (!card || !m_listHost) return;

    // Over another area's tab: it is highlighted and the note moves there on
    // release. Meanwhile the list is not reordered.
    const QString over = m_areaTabs ? m_areaTabs->areaAt(globalPos) : QString();
    m_dropArea = over == currentArea() ? QString() : over;
    if (m_areaTabs) m_areaTabs->setDropTarget(m_dropArea);
    if (!m_dropArea.isEmpty()) return;

    // Near the edges the list scrolls along, or a card could not be dragged out
    // of the visible part.
    if (m_scroll) {
        const int y = m_scroll->viewport()->mapFromGlobal(globalPos).y();
        const int edge = 26;
        QScrollBar *bar = m_scroll->verticalScrollBar();
        if (y < edge) bar->setValue(bar->value() - 12);
        else if (y > m_scroll->viewport()->height() - edge) bar->setValue(bar->value() + 12);
    }

    // Only visible cards count: with a filter the hidden ones are still in the
    // layout, and dragging over them means nothing.
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
        // Upwards the first card below the cursor wins; downwards, the last above it.
        if (i < from && y < center) { to = i; break; }
        if (i > from && y > center) to = i;
    }
    if (to == from) return;

    QWidget *anchor = visible.at(to);
    m_listLayout->removeWidget(card);
    // The anchor's index is taken with the card already out; going down, the
    // card goes after it.
    int at = m_listLayout->indexOf(anchor);
    if (to > from) ++at;
    m_listLayout->insertWidget(at, card);
    commitOrder();
}

void Panel::endCardDrag(NoteCard *card) {
    m_dragCard = nullptr;
    const QString dropArea = m_dropArea;
    m_dropArea.clear();
    if (m_areaTabs) m_areaTabs->setDropTarget(QString());
    if (!card) return;
    if (!dropArea.isEmpty()) {
        // Rebuilds the list and this card with it: deletion is deferred, so the grip
        // that called here stays alive until return.
        moveNoteToArea(card->note(), dropArea);
        return;
    }
    card->setProperty("dragging", false);
    card->style()->unpolish(card);
    card->style()->polish(card);
    save();
}

/// The order on screen is the order saved.
void Panel::commitOrder() {
    // Only the open area's notes are on screen: they take the same slots they
    // had in the global list, in the new order, and the rest do not move.
    QList<Note *> onScreen;
    for (NoteCard *card : cards()) onScreen.append(card->note());
    QList<Note *> order = m_store.notes();
    int next = 0;
    for (Note *&n : order)
        if (onScreen.contains(n) && next < onScreen.size()) n = onScreen.at(next++);
    m_store.setOrder(order);
}

void Panel::focusSearch() {
    showNotes();          // filtering with the planner in front would not be visible
    if (m_search->isHidden()) {
        const Area *a = m_store.area(currentArea());
        m_search->setPlaceholderText(L("Buscar en %1…").arg(a ? a->name : QString()));
        m_areaTabs->hide();
        m_search->show();
    }
    m_search->setFocus(Qt::ShortcutFocusReason);
    m_search->selectAll();
}

/// Escape, the magnifier again, or switching area: the tabs come back and the
/// filter goes, since a filter nobody can see is a list that looks short.
void Panel::closeSearch() {
    if (m_search->isHidden()) return;
    m_search->clear();
    m_search->hide();
    m_areaTabs->show();
}

void Panel::applyFilter(const QString &q) {
    int shown = 0;
    for (NoteCard *card : cards()) {
        const bool ok = card->note()->matches(q);
        card->setVisible(ok);
        if (ok) ++shown;
    }
    // Nothing matching is said, not left as an empty list.
    const Area *a = m_store.area(currentArea());
    const QString name = a ? a->name : QString();
    if (!q.isEmpty()) {
        m_emptyText->setText(L("Nada coincide en %1").arg(name));
        m_emptyBtn->hide();
        m_empty->setVisible(shown == 0);
    } else {
        m_emptyBtn->show();
        m_empty->setVisible(cards().isEmpty());
        m_emptyText->setText(m_store.areas().size() > 1 ? L("Aún no hay notas en %1").arg(name)
                                                        : L("Todavía no hay notas"));
    }
}

void Panel::bringToFront() {
    // An open normal window is left alone: expand() would restore the saved size
    // even if the user maximised it.
    if (!appMode() || m_nav->folded()) expand();
    showRestored();
}

void Panel::showRestored() {
    // Minimised still counts as "visible" to Qt; the state must be cleared or
    // show() does not bring it back from the taskbar.
    setWindowState(windowState() & ~Qt::WindowMinimized);
    show();
    raise();
    activateWindow();
}

void Panel::setAppMode(bool on) {
    if (on == appMode()) return;
    // App mode has no shadow margin: the window shrinks or grows by those margins
    // so the panel inside keeps its size.
    const int delta = 2 * Navigator::kShadowMargin * (on ? -1 : 1);
    if (!on && isMaximized()) showNormal();   // the widget is never maximised
    m_store.prefs().appMode = on;
    applyAppModeChrome();
    applyWindowFlags();
    // The minimum first: with the old one in place, shrinking does not fit.
    m_nav->syncShellMinimum();
    m_nav->shiftSizes(delta);
    refreshSettings();
    save();
}

void Panel::applyAppModeChrome() {
    const int m = shadowMargin();
    m_outer->setContentsMargins(m, m, m, m);
    if (appMode()) {
        // The window manager draws shadow and rounded corners; ours would leave a
        // transparent gap between its frame and the panel.
        m_shell->setGraphicsEffect(nullptr);   // deletes it
        m_shellShadow = nullptr;
    } else if (!m_shellShadow) {
        m_shellShadow = new QGraphicsDropShadowEffect(m_shell);
        m_shellShadow->setBlurRadius(56);
        m_shellShadow->setOffset(0, 20);
        m_shellShadow->setColor(QColor(0, 0, 0, 160));
        m_shell->setGraphicsEffect(m_shellShadow);
    }
    m_shell->setProperty("app", appMode());
    m_shell->style()->unpolish(m_shell);
    m_shell->style()->polish(m_shell);
    placeResizeGrips();

    if (m_foldBtn) {
        const QString kind = appMode() ? "minus" : "dock";
        const QString tip = appMode() ? "Minimizar" : "Plegar a icono";
        m_foldBtn->setProperty("iconKind", kind);
        m_foldBtn->setProperty("tip", tip);
        m_foldBtn->setIcon(paintIcon(kind, QColor(Theme::muted())));
        m_foldBtn->setToolTip(L(tip));

        // As a window, minimise goes last, where window buttons usually sit; as a
        // panel, folding stays before "⋯".
        if (auto *row = qobject_cast<QBoxLayout *>(m_foldBtn->parentWidget()->layout());
            row && m_moreBtn) {
            row->removeWidget(m_foldBtn);
            const int more = row->indexOf(m_moreBtn);
            row->insertWidget(appMode() ? more + 1 : more, m_foldBtn);
        }
    }
    if (m_maxBtn) m_maxBtn->setVisible(appMode());
    if (m_closeBtn) m_closeBtn->setVisible(appMode());
    refreshMaxButton();
}

void Panel::toggleMaximized() {
    if (!appMode()) return;
    if (isMaximized()) showNormal();
    else showMaximized();
}

/// Maximise or restore, whichever the window is not.
void Panel::refreshMaxButton() {
    if (!m_maxBtn) return;
    const bool max = isMaximized();
    const QString kind = max ? "restore" : "maximize";
    const QString tip = max ? "Restaurar" : "Maximizar";
    m_maxBtn->setProperty("iconKind", kind);
    m_maxBtn->setProperty("tip", tip);
    m_maxBtn->setIcon(paintIcon(kind, QColor(Theme::muted())));
    m_maxBtn->setToolTip(L(tip));
}

void Panel::changeEvent(QEvent *e) {
    QWidget::changeEvent(e);
    if (e->type() == QEvent::WindowStateChange) {
        refreshMaxButton();
        placeResizeGrips();   // none while maximised
    }
}

/// Opens a page. The bar marks where you are; leaving is another item, Escape,
/// or (from settings) the back arrow.
void Panel::openPage(QWidget *page) {
    if (page == m_scroll) {
        showNotes();
        return;
    }
    if (m_body->currentWidget() == page) return;
    if (page == m_planner) m_planner->refresh();
    else if (page == m_timerView) m_timerView->refresh();
    else if (page == m_birthdays) m_birthdays->refresh();
    else if (page == m_settings) m_settings->refresh();
    closeSearch();
    m_nav->open(page);
    refreshNav();
    refreshTitle();
}

int Panel::shadowMargin() const { return m_nav->shadowMargin(); }

void Panel::collapse() { m_nav->collapse(); }

void Panel::expand() {
    // Opening the panel counts as acknowledging: the alarm is silenced.
    if (m_alerts->acknowledgeAll()) {
        refreshDueCards();
        refreshPlanner();
        refreshBirthdays();
        onTimersChanged();
        applyBadgeAlert();
        scheduleSave();
    }
    m_nav->expand();
}

void Panel::showNotes() {
    if (m_body->currentWidget() == m_scroll) return;
    // The card was built with the body from before the page edited it.
    const bool fromMarkdown = m_body->currentWidget() == m_mdPage;

    m_nav->open(m_scroll);
    if (fromMarkdown) {
        rebuildList();
        // Back at its card, but not in its title: revealNote() selects it, and a
        // key pressed next would overwrite the name. The list keeps the focus so
        // it does not drift to some other card's title either.
        if (Note *n = m_mdPage->note()) scrollToNote(n);
        m_scroll->setFocus(Qt::OtherFocusReason);
    }
    refreshNav();
    refreshTitle();
}

/// The open page's name; on the list, "Notas". Settings, the one page out of
/// the bar, gets a back arrow before it.
void Panel::refreshTitle() {
    if (!m_titleLabel || !m_body) return;
    QString title = L("Notas");
    for (const Navigator::Page &p : m_nav->pages())
        if (p.page == m_body->currentWidget()) title = L(p.name);
    if (m_body->currentWidget() == m_mdPage && m_mdPage->note())
        title = m_mdPage->note()->title.isEmpty() ? L("Sin título") : m_mdPage->note()->title;
    m_titleLabel->setText(title);
    m_titleLabel->update();   // ElidedLabel paints itself
    const bool back = m_body->currentWidget() == m_settings || m_body->currentWidget() == m_mdPage;
    m_timerAddBtn->setVisible(m_body->currentWidget() == m_timerView);
    m_bdayAddBtn->setVisible(m_body->currentWidget() == m_birthdays);
    m_backBtn->setVisible(back);
    m_header->layout()->setContentsMargins(back ? 6 : 14, 0, 6, 0);
}

void Panel::removeCategory(Event::Category *c) {
    const QString id = c->id;
    const bool google = id.startsWith("gcal:");
    const bool wasHidden = m_planner->hidden().contains(id);
    m_store.removeCategory(c);   // frees it: from here on only the id is valid
    // If it was hidden, the id must not linger in the preferences.
    if (wasHidden) {
        QStringList hidden = m_planner->hidden();
        hidden.removeAll(id);
        m_planner->setHidden(hidden);
        m_store.prefs().plannerHidden = hidden;
        scheduleSave();
    }
    if (google) {
        m_alerts->stopIfQuiet();
        refreshNotices();
        applyBadgeAlert();
        refreshNav();
        refreshSettings();   // that calendar's switch
    }
    refreshPlanner();
}

void Panel::openCalendarTarget(QWidget *anchor) {
    CalendarSync *cal = m_drive->calendar();
    auto *menu = new Popup(m_theme, this);
    menu->addHeader(L("Eventos nuevos de Tagoror"));
    // Only followed, writable calendars: uploading to an unfollowed one would
    // leave the event with nobody reading its changes back.
    for (const CalendarSync::Calendar &c : cal->calendars()) {
        if (!c.writable || !cal->isFollowed(c.id)) continue;
        menu->addItem(c.id == cal->target() ? "check" : "calendar", c.name, QString(),
                      [this, id = c.id] {
            m_drive->calendar()->setTarget(id);
            m_sync->syncNow();
        });
    }
    menu->addSeparator();
    menu->addItem(cal->target().isEmpty() ? "check" : "minus", L("Ninguno"),
                  L("Los eventos de Tagoror se quedan aquí"), [this] { m_drive->calendar()->setTarget(QString());
    });
    menu->showBelow(anchor);
}

void Panel::refreshPlanner() {
    // Every keystroke in a card lands here: if the planner is not visible there is
    // nothing to repaint, and it refreshes on return.
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
    // Stays in the planner, on the day where it just appeared.
    m_planner->goTo(when.date());
    checkReminders();
}

void Panel::createTimer(const QString &name, qint64 ms) {
    auto *t = new Timer;
    t->name = name.isEmpty() ? L("Temporizador %1").arg(timerClock(ms)) : name;
    t->totalMs = ms;
    t->leftMs = ms;
    t->start();              // creating starts it: Reset keeps it idle as a template
    m_store.addTimer(t);
    m_timerView->focusTimer(t);
    onTimersChanged();
}

void Panel::toggleTimer(Timer *t) {
    const bool wasRinging = t->ringing();
    if (t->state == Timer::Running) t->pause();
    else t->start();
    if (wasRinging) m_alerts->stopIfQuiet();
    save();
    onTimersChanged();
}

void Panel::resetTimer(Timer *t) {
    const bool wasRinging = t->ringing();
    t->reset();
    if (wasRinging) m_alerts->stopIfQuiet();
    save();
    onTimersChanged();
}

void Panel::removeTimer(Timer *t) {
    // As with notes: deleting the ringing one must not leave the tone on.
    const bool wasRinging = t->ringing();
    m_store.removeTimer(t);
    if (wasRinging) m_alerts->stopIfQuiet();
    onTimersChanged();
}

void Panel::onTimersChanged() {
    m_timerView->refresh();
    bool running = false;
    for (Timer *t : m_store.timers()) running |= t->state == Timer::Running;
    if (running && !m_tick->isActive()) m_tick->start();
    refreshNav();
    refreshNotices();
    applyBadgeAlert();
    if (m_body->currentWidget() == m_timerView) refreshNav();
}

void Panel::tickTimers() {
    bool running = false;
    if (m_alerts->tickTimers(&running)) {
        save();
        onTimersChanged();
    } else if (m_body->currentWidget() == m_timerView) {
        m_timerView->tick();
    }
    refreshNav();
    if (!running) m_tick->stop();
    else if (!m_tick->isActive()) m_tick->start();
}

void Panel::revealNote(Note *n) {
    showNotes();
    if (NoteCard *card = scrollToNote(n)) card->focusTitle();
}

NoteCard *Panel::scrollToNote(Note *n) {
    if (m_store.areaOf(n) != currentArea()) switchArea(m_store.areaOf(n));
    for (NoteCard *card : cards()) {
        if (card->note() != n) continue;
        card->show();                       // a filter may have hidden it
        m_scroll->ensureWidgetVisible(card);
        return card;
    }
    return nullptr;
}

/// The settings page shows things that change outside it (data folder,
/// backups, microphone), so it is repainted when they do.
void Panel::refreshSettings() {
    if (m_settings && m_body->currentWidget() == m_settings) m_settings->refresh();
}

void Panel::refreshBirthdays() {
    // As with the planner: every keystroke in a card goes through rebuildList(),
    // and if the page is not visible there is nothing to repaint.
    if (m_birthdays && m_body->currentWidget() == m_birthdays) m_birthdays->refresh();
}

/// Adding and editing share one path (null @p b for a new one), so the date
/// format and the fields are written once and cannot disagree.
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
                        // The greeted mark belongs to a specific date: if the date changes it no
                        // longer means anything.
                        if (!target->isToday()) target->greetedYear = 0;
                        // Added only once filled: adding earlier would write a nameless 1 January to
                        // disk.
                        if (!b) m_store.addBirthday(target);
                        m_birthdays->refresh();
                        refreshNav();
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

    // The same time column as a reminder's date, without the month: the day is
    // the birthday.
    menu->addTime(b->remindAt.isValid() ? b->remindAt : QTime(9, 0), [this, b](const QTime &t) {
        b->remindAt = t;
        // A new time re-arms this year's alarm: changing it right after silencing it
        // must do something.
        b->firedYear = 0;
        m_birthdays->refresh();
        save();
    });

    if (b->remindAt.isValid()) {
        menu->addSeparator();
        menu->addItem("minus", L("Sin aviso"), L("Se queda solo apuntado"),
                      [this, b] {
                          b->remindAt = QTime();
                          m_alerts->silenceBirthday(b);
                          m_birthdays->refresh();
                          save();
                      });
    }
    menu->showBelow(anchor);
}

void Panel::removeBirthday(Birthday *b) {
    // It may be ringing when deleted: the tone would hang if nobody stopped it
    // before the only thing asking for it disappears.
    const bool wasRinging = b->ringing;
    m_store.removeBirthday(b);
    if (wasRinging) m_alerts->stopIfQuiet();
    m_birthdays->refresh();
    refreshNav();
    applyBadgeAlert();
}

void Panel::toggleGreeted(Birthday *b) {
    b->greetedYear = b->greeted() ? 0 : QDate::currentDate().year();
    // Greeting is acknowledging: it makes no sense to keep ringing.
    if (b->ringing) {
        m_alerts->silenceBirthday(b);
        m_alerts->stopIfQuiet();
        applyBadgeAlert();
    }
    m_birthdays->refresh();
    save();
}

void Panel::dismissBirthday(Birthday *b) {
    m_alerts->silenceBirthday(b);
    m_alerts->stopIfQuiet();
    m_birthdays->refresh();
    applyBadgeAlert();
    save();
}

void Panel::checkReminders() {
    if (!m_alerts->check()) return;
    refreshDueCards();
    refreshPlanner();
    refreshBirthdays();
    refreshNotices();
    applyBadgeAlert();
}

void Panel::dismissNote(Note *n) {
    m_alerts->silence(n);
    m_alerts->stopIfQuiet();
    refreshDueCards();
    refreshPlanner();
    applyBadgeAlert();
    save();
}

/// Postpones a reminder that was ringing. The card already cleared its
/// @c ringing and repainted; here goes what lives outside it (the tone, the
/// red dock) and the planner, which still showed it ringing on its new day.
void Panel::rescheduleNote(Note *) {
    m_alerts->stopIfQuiet();
    refreshPlanner();
    applyBadgeAlert();
}

void Panel::refreshDueCards() {
    for (NoteCard *card : cards()) card->refreshDue();
}

void Panel::applyBadgeAlert() {
    const bool alert = m_alerts->anyRinging();
    refreshAreaTabs();   // each area's red dot comes from here too
    refreshNav();        // and the one on Notas
    refreshNotices();    // and the alarm notices

    if (m_dock) {
        // Folded, the dock is the only thing visible: it turns red, beats and counts
        // the alarms; its tooltip names the first one.
        int count = 0;
        QString first;
        for (const Note *n : m_store.notes())
            if (n->ringing && ++count == 1)
                first = n->title + "\n" + n->dueAt().toString("HH:mm") + " · " + L("sonando");
        for (const Birthday *b : m_store.birthdays())
            if (b->ringing && ++count == 1) first = b->name + "\n" + L("Cumpleaños");
        for (const Timer *t : m_store.timers())
            if (t->ringing() && ++count == 1) first = t->name + "\n" + L("Tiempo cumplido");
        for (const Event *e : m_store.events())
            if (e->ringingMs && ++count == 1) first = e->title + "\n" + L("Evento");
        m_dock->setAlarms(count);
        m_dock->setToolTip(count > 0 ? first + "\n" + L("Clic para abrir y parar")
                                     : L("Abrir Tagoror · arrastra para mover"));
    }
    if (m_tray) {
        // Hidden in the tray, the icon is the only sign that something is due.
        m_tray->setIcon(alert ? alertIcon() : qApp->windowIcon());
        m_tray->setToolTip(alert ? L("Recordatorio vencido") : L("Tagoror"));
    }
}

void Panel::openNewNoteMenu(QWidget *anchor) {
    auto *menu = new Popup(m_theme, this);
    menu->addShortcutItem("text", L("Nota de texto"), "Ctrl+N", [this] { addNote(Note::Text); });
    menu->addShortcutItem("check", L("Checklist"), "Ctrl+⇧L", [this] { addNote(Note::Check); });
    menu->addShortcutItem("bell", L("Recordatorio"), "Ctrl+⇧R", [this] { addNote(Note::Reminder); });
    menu->addShortcutItem("mic", L("Nota de voz"), "Ctrl+⇧V", [this] { addNote(Note::Voice); });
    menu->addShortcutItem("markdown", L("Nota Markdown"), "Ctrl+⇧M", [this] { addNote(Note::Markdown); });
    // Right-aligned under "+", which sits at the right end of the row.
    menu->adjustSize();
    menu->showAt(anchor->mapToGlobal(QPoint(anchor->width() - menu->width() + 36, anchor->height() + 2)));
}

void Panel::openAccentEditor(QWidget *anchor) {
    auto *menu = new Popup(m_theme, this);
    menu->addHeader(L("Color de acento"));
    menu->addEditor("#7c9cff", m_theme.accent.name(), [this](const QString &text) {
        // QColor also accepts names ("teal"), not only hex.
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

/// Only the family changes: sizes come from the stylesheet, and times and dates
/// keep IBM Plex Mono whatever is chosen. A family no longer installed falls
/// back to IBM Plex Sans rather than to whatever Qt would substitute.
void Panel::applyFont() {
    const QString pref = m_store.prefs().font;
    const QString bundled = qApp->property("bundledFont").toString();
    QFont font = m_systemFont;
    if (pref == "system") {
        // the system's, as it came
    } else if (!pref.isEmpty() && QFontDatabase::hasFamily(pref)) {
        font.setFamily(pref);
    } else if (!bundled.isEmpty()) {
        font.setFamily(bundled);
    }
    QApplication::setFont(font);
}

void Panel::pickFont(const QString &pref) {
    if (pref == m_store.prefs().font) return;
    m_store.prefs().font = pref;
    applyFont();
    // The sheet again so every rule picks up the new family; cards, tabs and the
    // planner measure their text, so they are rebuilt too.
    applyTheme();
    rebuildList();
    refreshAreaTabs();
    refreshPlanner();
    refreshSettings();
    save();
}

void Panel::openFontEditor(QWidget *anchor, const QString &notFound) {
    const QString current = m_store.prefs().font;
    auto *menu = new Popup(m_theme, this);
    menu->addHeader(L("Tipografía"));
    menu->addItem(current.isEmpty() ? "check" : "text", QStringLiteral("IBM Plex Sans"),
                  L("Incluida con Tagoror"), [this] { pickFont(QString()); });
    menu->addItem(current == "system" ? "check" : "text", L("La del sistema"),
                  m_systemFont.family(), [this] { pickFont("system"); });
    if (!current.isEmpty() && current != "system")
        menu->addItem("check", current, L("Instalada en el equipo"), nullptr);

    menu->addSeparator();
    menu->addHeader(L("Otra instalada"));
    if (!notFound.isEmpty()) menu->addText(L("No se encuentra «%1».").arg(notFound));
    menu->addEditor(L("Nombre, p. ej. Georgia"), QString(), [this, anchor](const QString &text) {
        const QString wanted = text.trimmed();
        if (wanted.isEmpty()) return;
        // The exact name in any case first, then the first family starting with it:
        // "fira" finds "Fira Sans" without having to type it whole.
        const QStringList families = QFontDatabase::families();
        QString found;
        for (const QString &f : families)
            if (f.compare(wanted, Qt::CaseInsensitive) == 0) { found = f; break; }
        if (found.isEmpty())
            for (const QString &f : families)
                if (f.startsWith(wanted, Qt::CaseInsensitive)) { found = f; break; }
        if (found.isEmpty()) {
            // Reopened once this one has closed, saying why nothing changed.
            QPointer<QWidget> at(anchor);
            QTimer::singleShot(0, this, [this, at, wanted] {
                if (at) openFontEditor(at, wanted);
            });
            return;
        }
        // The bundled one by name lands on its own entry, not on "installed".
        pickFont(found == qApp->property("bundledFont").toString() ? QString() : found);
    });
    menu->showUnder(anchor);
}

void Panel::openAlarmToneMenu(QWidget *anchor, const QString &error) {
    const QString current = m_store.prefs().alarmTone;
    auto *menu = new Popup(m_theme, this);
    menu->addHeader(L("Tono de alarma"));
    for (const QString &id : Alarm::tones()) {
        // The file's entry only while there is a file to play.
        if (id == "custom" && !QFile::exists(Alarm::customPath())) continue;
        menu->addItem(id == current ? "check" : "bell", alarmToneName(id),
                      id == "custom" ? L("WAV elegido") : QString(),
                      [this, id] { pickAlarmTone(id); });
    }
    menu->addSeparator();
    if (!error.isEmpty()) menu->addText(error);
    QPointer<QWidget> at(anchor);
    menu->addItem("folder", L("Elegir archivo WAV…"), QString(), [this, at] {
        // After the menu has closed: a modal dialog from inside its click would
        // open on top of a popup that is going away.
        QTimer::singleShot(0, this, [this, at] { chooseAlarmFile(at); });
    });
    menu->showUnder(anchor);
}

void Panel::applyAlarmPrefs() {
    m_alerts->alarm()->setTone(m_store.prefs().alarmTone);
    m_alerts->alarm()->setVolume(m_store.prefs().alarmVolume);
}

void Panel::pickAlarmTone(const QString &id) {
    m_store.prefs().alarmTone = id;
    m_alerts->alarm()->setTone(id);
    m_alerts->alarm()->preview();
    refreshSettings();   // the row names the tone
    save();
}

void Panel::chooseAlarmFile(QWidget *anchor) {
    // Settings may rebuild while the dialog is open, taking the button with it.
    QPointer<QWidget> at(anchor);
    const QString file = QFileDialog::getOpenFileName(this, L("Tono de alarma"), QDir::homePath(),
                                                      L("Sonido WAV (*.wav)"));
    if (file.isEmpty()) return;
    const QString error = Alarm::importCustom(file);
    if (!error.isEmpty()) {
        if (at) openAlarmToneMenu(at, error);
        return;
    }
    pickAlarmTone("custom");
}

void Panel::chooseDataFolder() {
    // The dialog opens where the data is, unless that place does not exist right
    // now: pointing at a missing folder leaves the picker blank.
    const QString start = m_store.available() ? appDataDir() : QDir::homePath();
    const QString to =
        QFileDialog::getExistingDirectory(this, L("Carpeta donde guardar las notas"), start);
    if (to.isEmpty()) return;

    // If notes already live there, ask: taking these there would erase those,
    // which is what happens when pointing at a USB stick that already has them.
    if (QFile::exists(to + "/notes.json")) {
        confirmDataFolder(to);
        return;
    }
    m_store.changeDataDir(to);
    refreshSettings();   // the card shows the path, which just changed
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

    // Manual
    menu->addHeader(L("Copias de seguridad"));
    if (!m_store.available()) {
        // Without a folder there is nowhere to copy, and offering it would be a lie.
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
                      refreshSettings();     // the card shows the count
                      openBackups(anchor);   // the menu reopens with the new state
                  });

    // Frequency
    menu->addSeparator();
    menu->addHeader(L("Cada cuánto"));
    QStringList periods;
    for (int d : kBackupPeriods) periods << (d == 0 ? L("Nunca") : L("%1 d").arg(d));
    menu->addChoice(periods, int(kBackupPeriods.indexOf(prefs.backupEveryDays)),
                    [this, anchor](int i) {
                        m_store.prefs().backupEveryDays = kBackupPeriods.at(i);
                        save();
                        refreshSettings();   // the card shows the frequency
                        openBackups(anchor);
                    });

    // Time of day: only with a schedule, since a time without a frequency means
    // nothing.
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
        // Empty on purpose: the lit chip already shows the time, and repeating it
        // here opens the field with its text selected as if something needed fixing.
        menu->addEditor(L("otra hora · HH:mm"), QString(),
                        [this, anchor](const QString &value) {
                            const QTime t = QTime::fromString(value.trimmed(), "HH:mm");
                            if (!t.isValid()) return;
                            m_store.prefs().backupAt = t;
                            save();
                            openBackups(anchor);
                        });

        // A schedule that does not say when it will act cannot be checked; if today's
        // time has passed, the next is tomorrow.
        const QDateTime due = m_store.nextBackupDue();
        if (due.isValid())
            menu->addItem("clock", backupPeriodLabel(prefs.backupEveryDays),
                          L("La siguiente: %1")
                              .arg(Lang::locale().toString(due, "ddd d MMM · HH:mm")),
                          [] {});
    }

    // Restore
    if (!list.isEmpty()) {
        menu->addSeparator();
        menu->addHeader(L("Volver a una copia"));
        for (const Store::Backup &b : list) {
            // Never QLocale::system(): the date is in the language chosen in settings.
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

void Panel::pollBackup() {
    // This triggers the scheduled backup while the app is open at that time;
    // if it was closed, Store::load() catches up on launch.
    if (m_store.backupIfDue()) scheduleSave();   // records the date
}

bool Panel::updateAvailable() const {
    // Without knowing our own version no newer one can be claimed: comparing
    // against an empty string makes any number win and the notice never leaves.
    const QString mia = Updater::current();
    const QString latest = m_store.prefs().latestSeen;
    if (mia.isEmpty() || latest.isEmpty()) return false;
    return Updater::compare(latest, mia) > 0;
}

/// Once a day, on the same heartbeat as reminders and backups. Asking is an
/// HTTP request, so the stored date sets the pace, not the timer.
bool Panel::updateCheckDue() const {
    if (!m_store.prefs().updateCheck || m_updater->busy()) return false;

    constexpr qint64 kDayMs = 24LL * 3600 * 1000;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 last = m_store.prefs().lastUpdateMs;
    // A date in the future (the clock moved back) would mean "never again": treat
    // it as none.
    return !(last > 0 && last <= now && now - last < kDayMs);
}

void Panel::pollUpdates() {
    if (!updateCheckDue()) return;
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
        // Only an answered check counts: after a failure it retries instead of
        // waiting a whole day.
        m_store.prefs().lastUpdateMs = QDateTime::currentMSecsSinceEpoch();
        save();
    }
    if (m_settings) m_settings->setUpdateState(false, error);
    refreshNotices();
}

void Panel::openLatestRelease() {
    // Without a stored URL (the version came from the file, not this session) the
    // releases page leads to the same place.
    QDesktopServices::openUrl(QUrl(m_latestUrl.isEmpty()
                                       ? QStringLiteral("https://github.com/Larzt/tagoror/releases/latest")
                                       : m_latestUrl));
}

void Panel::pollDataDir() {
    if (m_store.available()) return;
    m_store.retryLoad();   // emits reloaded() the time it succeeds
}

/// The open Markdown note was deleted or replaced underneath (a sync, a
/// reload): the page lets go of it rather than write into freed memory.
void Panel::dropMissingMarkdown() {
    Note *n = m_mdPage->note();
    if (!n || m_store.notes().contains(n)) return;
    m_mdPage->forget();
    if (m_body->currentWidget() == m_mdPage) showNotes();
}

void Panel::onStoreReloaded() {
    dropMissingMarkdown();
    m_theme.accent = m_store.prefs().accent;
    m_theme.opacity = m_store.prefs().opacity;
    m_theme.textScale = m_store.prefs().textScale;
    m_theme.compact = m_store.prefs().compact;
    VoiceRecorder::setPreferredInput(m_store.prefs().input);
    applyAlarmPrefs();
    applyFont();
    applyTheme();
    // Store::load() already set the language; retranslate() rewrites the window
    // with it and rebuilds cards, planner and tray in one go.
    retranslate();
    // Size and position are left alone on purpose: the window is where the user
    // has it now. The notes that were ringing no longer exist, so the tone stops
    // before checking whether anything in the new list should ring.
    m_alerts->stopIfQuiet();
    checkReminders();
    tickTimers();
    onTimersChanged();
}

bool Panel::userIdle() const {
    // With the window in the background nobody is typing in it, even if the focus
    // stayed in a field. Not with a menu open either: its actions hold pointers
    // to the note or birthday it was opened on, which the merge could delete.
    if (QApplication::activePopupWidget()) return false;
    if (!isActiveWindow()) return true;
    QWidget *f = QApplication::focusWidget();
    if (!f || f->window() != this) return true;
    return !qobject_cast<QLineEdit *>(f) && !qobject_cast<QTextEdit *>(f) &&
           !qobject_cast<QPlainTextEdit *>(f);
}

void Panel::onStoreMerged() {
    dropMissingMarkdown();
    m_alerts->reconcileMerged();

    rebuildList();          // cards, footer, planner, birthdays and dock
    if (m_birthdays) m_birthdays->refresh();
    tickTimers();           // one arriving while running must start counting
    onTimersChanged();
    refreshNotices();
    checkReminders();       // and one arriving overdue, ringing
}

/// Everything the zone may show, most urgent first: what rings (it has to be
/// stopped), then the data folder (nothing is being saved), a failed sync and
/// a new version. Built here because only the panel knows what each action
/// touches; the zone just shows them.
void Panel::refreshNotices() {
    if (!m_alertZone) return;
    QList<AlertZone::Notice> out;
    const auto areaName = [this](const Note *n) {
        const Area *a = m_store.area(m_store.areaOf(n));
        return a ? a->name : QString();
    };

    for (Note *n : m_store.notes()) {
        if (!n->ringing) continue;
        AlertZone::Notice x;
        x.kind = AlertZone::Notice::Alarm;
        const bool list = n->type == Note::Check;
        x.icon = list ? "check" : "bell";
        x.title = n->title.isEmpty() ? L("Sin título") : n->title;
        x.sub = (list ? L("Lista · %1 · %2") : L("Recordatorio · %1 · %2"))
                    .arg(n->dueAt().toString("HH:mm"), areaName(n));
        x.action = L("Detener");
        x.act = [this, n] { if (m_store.notes().contains(n)) dismissNote(n); };
        // A repeating reminder's date is its next turn: moving it ten minutes would
        // move the series.
        if (!n->repeats()) {
            x.second = L("Posponer 10 min");
            x.secondAct = [this, n] { if (m_store.notes().contains(n)) snoozeNote(n, 10); };
        }
        out << x;
    }
    for (Birthday *b : m_store.birthdays()) {
        if (!b->ringing) continue;
        AlertZone::Notice x;
        x.kind = AlertZone::Notice::Alarm;
        x.icon = "cake";
        x.title = b->name;
        x.sub = L("Cumpleaños · %1").arg(b->remindAt.toString("HH:mm"));
        x.action = L("Detener");
        x.act = [this, b] { if (m_store.birthdays().contains(b)) dismissBirthday(b); };
        out << x;
    }
    for (Timer *t : m_store.timers()) {
        if (!t->ringing()) continue;
        AlertZone::Notice x;
        x.kind = AlertZone::Notice::Alarm;
        x.icon = "timer";
        x.title = t->name;
        x.sub = L("Temporizador · tiempo cumplido");
        x.action = L("Detener");
        x.act = [this, t] { if (m_store.timers().contains(t)) resetTimer(t); };
        x.second = L("+1 min");
        x.secondAct = [this, t] {
            if (!m_store.timers().contains(t)) return;
            t->snooze(60 * 1000);
            m_alerts->stopIfQuiet();
            save();
            onTimersChanged();
        };
        out << x;
    }
    for (Event *e : m_store.events()) {
        if (!e->ringingMs) continue;
        AlertZone::Notice x;
        x.kind = AlertZone::Notice::Alarm;
        x.icon = "calendar";
        x.title = e->title.isEmpty() ? L("Sin título") : e->title;
        // With hours or a day of lead, "at 10:00" may be tomorrow: then say the day.
        const QDateTime at = QDateTime::fromMSecsSinceEpoch(e->ringingMs);
        x.sub = at.date() == QDate::currentDate()
                    ? L("Evento · empieza a las %1").arg(at.toString("HH:mm"))
                    : L("Evento · empieza el %1").arg(Lang::locale().toString(at, "ddd d, HH:mm"));
        x.action = L("Detener");
        x.act = [this, e] {
            if (!m_store.events().contains(e)) return;
            m_alerts->silenceEvent(e);
            m_alerts->stopIfQuiet();
            save();
            refreshPlanner();
            applyBadgeAlert();
        };
        out << x;
    }

    if (!m_store.available()) {
        // The path goes in the subtitle, elided: the old banner took three lines.
        AlertZone::Notice x;
        x.kind = AlertZone::Notice::Warn;
        x.icon = "folder";
        x.title = L("Carpeta de datos no disponible");
        x.sub = prettyPath(appDataDir()) + " · " + L("no se guarda nada");
        x.action = L("Reintentar");
        x.act = [this] { pollDataDir(); };
        out << x;
    }
    if (m_sync && m_sync->engine()->state() == SyncEngine::Failed) {
        AlertZone::Notice x;
        x.kind = AlertZone::Notice::Warn;
        x.icon = "cloud";
        x.title = L("Error de sincronización");
        x.sub = m_sync->engine()->lastError();
        x.action = L("Revisar");
        x.act = [this] {
            m_settings->setTab(SettingsView::Sync);
            openPage(m_settings);
        };
        out << x;
    }
    if (updateAvailable()) {
        AlertZone::Notice x;
        x.kind = AlertZone::Notice::Info;
        x.icon = "download";
        x.title = L("Tagoror %1 disponible").arg(m_store.prefs().latestSeen);
        x.sub = L("Novedades en GitHub");
        x.action = L("Ver");
        x.act = [this] { openLatestRelease(); };
        out << x;
    }
    m_alertZone->setNotices(out);
}

/// Posponer: the same as moving its date on the card (NoteCard::applyDue()),
/// which also counts as acknowledging it.
void Panel::snoozeNote(Note *n, int minutes) {
    const QDateTime when = QDateTime::currentDateTime().addSecs(minutes * 60);
    n->dueAtMs = when.toMSecsSinceEpoch();
    n->due = dueLabel(when);
    n->fired = false;
    n->ringing = false;
    m_alerts->stopIfQuiet();
    refreshDueCards();
    refreshPlanner();
    applyBadgeAlert();
    save();
}

/// "⋯": what used to be header buttons and has no place in the bar.
void Panel::openMoreMenu(QWidget *anchor) {
    auto *menu = new Popup(m_theme, this);
    menu->addShortcutItem("command", L("Paleta de comandos"), "Ctrl+K", [this] { openPalette(); });
    menu->addShortcutItem("gear", L("Ajustes"), "Ctrl+,", [this] { openPage(m_settings); });
    menu->addShortcutItem(appMode() ? "check" : "window", L("Modo aplicación"), QString(),
                          [this] { setAppMode(!appMode()); });
    if (appMode())
        menu->addShortcutItem("minus", L("Minimizar"), "Ctrl+M", [this] { showMinimized(); });
    else
        menu->addShortcutItem("dock", L("Plegar a icono"), "Ctrl+M", [this] { collapse(); });
    menu->addSeparator();
    menu->addShortcutItem("info", L("Acerca de Tagoror"), QString(), [this, anchor] {
        QPointer<QWidget> at(anchor);
        QTimer::singleShot(0, this, [this, at] { if (at) openAbout(at); });
    });
    menu->showUnder(anchor);
}

/// Ctrl+K from anywhere. The list is built on every open: the areas, and
/// whatever else changes, are read fresh.
void Panel::openPalette() {
    if (m_nav->folded()) expand();
    if (m_palette->isOpen()) {
        m_palette->close();
        return;
    }
    using Command = CommandPalette::Command;
    QList<Command> cmds;
    const QString go = L("Ir a"), create = L("Crear"), areas = L("Áreas");
    cmds << Command{go, "notes", L("Notas"), "Ctrl+1", [this] { showNotes(); }}
         << Command{go, "calendar", L("Planner"), "Ctrl+2", [this] { openPage(m_planner); }}
         << Command{go, "timer", L("Timers"), "Ctrl+3", [this] { openPage(m_timerView); }}
         << Command{go, "cake", L("Cumpleaños"), "Ctrl+4", [this] { openPage(m_birthdays); }}
         << Command{go, "gear", L("Ajustes"), "Ctrl+,", [this] { openPage(m_settings); }};
    const auto make = [this](Note::Type type) {
        return [this, type] {
            showNotes();
            addNote(type);
        };
    };
    cmds << Command{create, "text", L("Nueva nota de texto"), "Ctrl+N", make(Note::Text)}
         << Command{create, "check", L("Nueva checklist"), "Ctrl+⇧L", make(Note::Check)}
         << Command{create, "bell", L("Nuevo recordatorio"), "Ctrl+⇧R", make(Note::Reminder)}
         << Command{create, "mic", L("Nueva nota de voz"), "Ctrl+⇧V", make(Note::Voice)}
         << Command{create, "markdown", L("Nueva nota Markdown"), "Ctrl+⇧M", make(Note::Markdown)};
    const QList<Area *> &list = m_store.areas();
    const QString open = currentArea();
    for (int i = 0; i < list.size(); ++i) {
        if (list.at(i)->id == open) continue;   // already there
        const QString id = list.at(i)->id;
        cmds << Command{areas, list.at(i)->glyph.isEmpty() ? "notes" : "glyph-" + list.at(i)->glyph,
                        L("Cambiar a %1").arg(list.at(i)->name),
                        i < 9 ? QString("Alt+%1").arg(i + 1) : QString(),
                        [this, id] { switchArea(id); }};
    }
    cmds << Command{areas, "plus", L("Nueva área"), "Ctrl+T", [this] { newArea(); }};
    m_palette->open(cmds);
}

void Panel::openAbout(QWidget *anchor) {
    auto *menu = new Popup(m_theme, this);
    menu->addHeader(QStringLiteral("Tagoror ") + QCoreApplication::applicationVersion());
    menu->addText(L("Notas, recordatorios, planificador y temporizadores en un panel de "
                    "escritorio. Un tagoror es el círculo de piedras donde se reunía el "
                    "consejo guanche."));
    menu->addItem("link", L("Código y novedades"), QStringLiteral("github.com/Larzt/tagoror"),
                  [] { QDesktopServices::openUrl(QUrl("https://github.com/Larzt/tagoror")); });
    menu->addItem("text", L("Licencia MIT"), L("Tipografías: IBM Plex, OFL 1.1"), nullptr);
    menu->showUnder(anchor);
}

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
    applyBadgeAlert();   // in case something is already ringing at startup
}

/// A QMenu here, the one exception to the rule: the tray menu is not painted by
/// this process over the translucent frame but by the desktop (via DBusMenu on
/// Plasma), and QSystemTrayIcon accepts nothing else.
void Panel::buildTrayMenu() {
    if (!m_tray) return;

    delete m_trayMenu;                 // rebuilt whole on a language change
    m_trayMenu = new QMenu(this);      // owned: it goes with the panel

    QAction *toggle = m_trayMenu->addAction(L("Mostrar"));
    connect(toggle, &QAction::triggered, this, &Panel::toggleFromTray);
    QAction *front = m_trayMenu->addAction(L("Traer al frente"));
    connect(front, &QAction::triggered, this, &Panel::liftToFront);
    QAction *back = m_trayMenu->addAction(L("Enviar al fondo"));
    connect(back, &QAction::triggered, this, &Panel::sendToBack);
    QAction *fold = m_trayMenu->addAction(L("Plegar a icono"));
    connect(fold, &QAction::triggered, this, [this] {
        if (appMode()) {
            showMinimized();
            return;
        }
        if (m_nav->folded()) expand();
        else collapse();
        showRestored();
    });

    m_trayMenu->addSeparator();
    QMenu *create = m_trayMenu->addMenu(L("Nueva nota"));
    const QList<QPair<QString, Note::Type>> types = {
        {L("Texto"), Note::Text},
        {L("Checklist"), Note::Check},
        {L("Recordatorio"), Note::Reminder},
        {L("Nota de voz"), Note::Voice},
        {L("Nota Markdown"), Note::Markdown},
    };
    for (const auto &[label, type] : types)
        connect(create->addAction(label), &QAction::triggered, this, [this, type] {
            addNote(type);
            showRestored();
        });

    // Unfolded first: the page is then sized as if its header button was clicked.
    QMenu *open = m_trayMenu->addMenu(L("Abrir"));
    connect(open->addAction(L("Notas")), &QAction::triggered, this, [this] {
        bringToFront();
        showNotes();
    });
    for (const Navigator::Page &p : m_nav->pages())
        connect(open->addAction(L(p.name)), &QAction::triggered, this, [this, page = p.page] {
            bringToFront();
            if (m_body->currentWidget() != page) openPage(page);
        });

    m_trayMenu->addSeparator();
    QAction *onTop = m_trayMenu->addAction(L("Siempre encima"));
    onTop->setCheckable(true);
    connect(onTop, &QAction::triggered, this, &Panel::setOnTop);
    QAction *app = m_trayMenu->addAction(L("Modo aplicación"));
    app->setCheckable(true);
    connect(app, &QAction::triggered, this, [this](bool on) {
        // App mode has no dock: switching while folded would leave the window
        // as a bare badge with a frame around it.
        if (m_nav->folded()) expand();
        setAppMode(on);
        showRestored();
    });

    m_trayMenu->addSeparator();
    connect(m_trayMenu->addAction(L("Salir")), &QAction::triggered, qApp, &QApplication::quit);

    // The labels say what will happen, which depends on the window's state when
    // the menu opens.
    connect(m_trayMenu, &QMenu::aboutToShow, this,
            [this, toggle, front, back, fold, onTop, app] {
                const bool shown = isVisible() && !isMinimized();
                const bool folded = m_nav->folded();
                const bool pinned = m_store.prefs().onTop;
                toggle->setText(shown ? L("Ocultar") : L("Mostrar"));
                // Always on top already is the front, and nothing goes beneath it.
                front->setEnabled(!pinned);
                // The widget on the desktop layer is already at the back.
                back->setEnabled(!pinned && shown && (appMode() || m_lifted));
                fold->setText(appMode() ? L("Minimizar")
                              : folded  ? L("Desplegar")
                                        : L("Plegar a icono"));
                onTop->setChecked(pinned);
                app->setChecked(appMode());
            });

    m_tray->setContextMenu(m_trayMenu);
}

void Panel::toggleFromTray() {
    // Minimised counts as hidden: the click brings it back.
    if (isVisible() && !isMinimized()) {
        hide();
        return;
    }
    // It comes back as it was left, folded or not: hiding is not folding.
    showRestored();
}

void Panel::liftToFront() {
    // An ordinary window already stacks like any other; only the widget is held
    // down by its hint.
    if (!appMode() && !m_store.prefs().onTop && !m_lifted) {
        m_lifted = true;
        applyWindowFlags();
    }
    bringToFront();
}

void Panel::sendToBack() {
    if (m_lifted) {
        m_lifted = false;
        applyWindowFlags();   // back on the desktop layer
    }
    lower();
}

void Panel::closeEvent(QCloseEvent *e) {
    if (!m_tray) {
        // Without a tray icon there is no way to get it back, so closing quits (and
        // the destructor saves).
        e->accept();
        qApp->quit();
        return;
    }
    e->ignore();
    hide();
}

void Panel::showEvent(QShowEvent *e) {
    QWidget::showEvent(e);
    // In app mode it does belong in the taskbar: the property is not set, and
    // since changing flags recreates the native window, the old one is gone too.
    if (!appMode()) wmSkipTaskbar(winId());

    // On first map the window manager places the window wherever it likes,
    // overriding the requested position (KDE session restore, which runs on
    // reboot, does exactly that). It is asked again once, with a native window;
    // after that the window belongs to the user.
    m_nav->mapped();
}

void Panel::applyWindowFlags() {
    // By default the widget stays on the desktop, below other windows; "always on
    // top" is optional. On Wayland these hints are only a request.
    Qt::WindowFlags flags = Qt::FramelessWindowHint | Qt::Tool;
    if (m_store.prefs().onTop) flags |= Qt::WindowStaysOnTopHint;
    else if (!m_lifted) flags |= Qt::WindowStaysOnBottomHint;
    // App mode: an ordinary window, in the taskbar and Alt+Tab, but without the
    // system frame: the header is its title bar (drag, double click, buttons) and
    // the grips its edges. The min/max hints keep the taskbar's minimise and
    // the system's snapping. Without "always on top" it stacks like any other.
    if (appMode()) {
        flags = Qt::Window | Qt::FramelessWindowHint | Qt::WindowMinMaxButtonsHint;
        if (m_store.prefs().onTop) flags |= Qt::WindowStaysOnTopHint;
    }

    const bool wasVisible = isVisible();
    setWindowFlags(flags);
    if (wasVisible) show();   // setWindowFlags() hides the window
}

void Panel::setOnTop(bool on) {
    m_store.prefs().onTop = on;
    m_lifted = false;   // either way the preference decides the layer again
    applyWindowFlags();
    refreshSettings();
    save();
}

/// Escape closes the open page and returns to the notes. Only what no child
/// kept reaches here: a card's editor already uses Escape to close itself.
void Panel::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Escape && m_body && m_body->currentWidget() != m_scroll) {
        // On the planner, Escape closes the form first if it is open.
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

namespace {
constexpr int kResizeOut = 8;   ///< Grab band outside the visible frame (in the shadow).
constexpr int kResizeIn = 4;    ///< And inside it, over the frame's own edge.
constexpr int kResizeCorner = 16;   ///< How far along each edge a corner grip reaches.
}

void Panel::buildResizeGrips() {
    for (Qt::Edges e : {Qt::Edges(Qt::TopEdge), Qt::Edges(Qt::BottomEdge), Qt::Edges(Qt::LeftEdge),
                        Qt::Edges(Qt::RightEdge), Qt::TopEdge | Qt::LeftEdge, Qt::TopEdge | Qt::RightEdge,
                        Qt::BottomEdge | Qt::LeftEdge, Qt::BottomEdge | Qt::RightEdge})
        m_grips << new EdgeGrip(e, this);
}

/// Over the band around the visible frame; hidden when folded or maximised. In
/// app mode there is no shadow margin and only the inner part of the band is
/// inside the window, but that is enough to grab.
void Panel::placeResizeGrips() {
    if (m_grips.isEmpty() || !m_nav) return;   // still being built
    const bool on = !isMaximized() && !m_nav->folded();
    // The shadow margin is not frame: the edges are those of the shell.
    const int m = shadowMargin();
    const QRect f = rect().adjusted(m, m, -m, -m);
    const QRect band = f.adjusted(-kResizeOut, -kResizeOut, kResizeOut, kResizeOut);
    const int thick = kResizeOut + kResizeIn;
    const int corner = kResizeOut + kResizeCorner;
    for (EdgeGrip *g : std::as_const(m_grips)) {
        g->setVisible(on);
        if (!on) continue;
        const Qt::Edges e = g->edges();
        const bool horizontal = e & (Qt::LeftEdge | Qt::RightEdge);
        const bool vertical = e & (Qt::TopEdge | Qt::BottomEdge);
        QRect r;
        if (horizontal && vertical) {
            r = QRect(0, 0, corner, corner);
            r.moveLeft(e & Qt::LeftEdge ? band.left() : band.right() - corner + 1);
            r.moveTop(e & Qt::TopEdge ? band.top() : band.bottom() - corner + 1);
            // An L along both edges: the square's inner part would cover the
            // header's buttons in the corner.
            QRect inner(0, 0, corner - thick, corner - thick);
            inner.moveLeft(e & Qt::LeftEdge ? thick : 0);
            inner.moveTop(e & Qt::TopEdge ? thick : 0);
            g->setMask(QRegion(0, 0, corner, corner).subtracted(QRegion(inner)));
        } else if (vertical) {
            r = QRect(band.left() + corner, 0, band.width() - 2 * corner, thick);
            r.moveTop(e & Qt::TopEdge ? band.top() : band.bottom() - thick + 1);
        } else {
            r = QRect(0, band.top() + corner, thick, band.height() - 2 * corner);
            r.moveLeft(e & Qt::LeftEdge ? band.left() : band.right() - thick + 1);
        }
        g->setGeometry(r);
        g->raise();   // above the shell, and anything laid over it since
    }
}

void Panel::resizeEvent(QResizeEvent *e) {
    QWidget::resizeEvent(e);
    placeResizeGrips();
    // Folding and unfolding resize the window before or after the flag changes.
    QTimer::singleShot(0, this, &Panel::placeResizeGrips);
}

bool Panel::eventFilter(QObject *watched, QEvent *event) {
    // A double click on the header maximises, as on a title bar. Counted by hand:
    // the first press starts a system move, which swallows the release and, on
    // some platforms, turns the second press into a plain one.
    if (watched == m_header && appMode() &&
        (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick) &&
        static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
        const QPoint at = static_cast<QMouseEvent *>(event)->globalPosition().toPoint();
        const bool second = m_headerClick.isValid() &&
                            m_headerClick.elapsed() < QApplication::doubleClickInterval() &&
                            (at - m_headerClickAt).manhattanLength() < QApplication::startDragDistance() * 2;
        if (second) {
            m_headerClick.invalidate();
            toggleMaximized();
            return true;
        }
        m_headerClick.start();
        m_headerClickAt = at;
    }
    if (event->type() == QEvent::Resize) {
        if (watched == m_bodyHost) placeAlertZone();
        // Deferred: moving the bar between layouts inside the shell's own resize
        // would re-enter it.
        else if (watched == m_shell) QTimer::singleShot(0, this, &Panel::updateNavLayout);
    }
    if (watched == m_search && event->type() == QEvent::KeyPress &&
        static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
        closeSearch();
        m_searchBtn->setFocus(Qt::OtherFocusReason);
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void Panel::moveEvent(QMoveEvent *e) {
    QWidget::moveEvent(e);
    // Record the position as it changes. The destructor saves, but on shutdown
    // the session kills the process first; the Store's timer also merges the
    // stream of moves from a drag.
    if (isVisible()) scheduleSave();
}

void Panel::scheduleSave() { m_store.scheduleSave(); }

void Panel::save() { m_store.save(); }
