#include "ui/panel.hpp"
#include "ui/areatabs.hpp"
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

#include "core/calendarsync.hpp"
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
#include <QShortcut>
#include <QStackedWidget>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

constexpr int kShadowMargin = 22;   // room around the frame for the shadow
constexpr int kShellMinWidth = 300;
constexpr int kShellMinHeight = 340;   // the planner needs more height than the list

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

/// One axis of the anchoring. The window keeps its top-left corner; only when
/// growing from there would not fit is the opposite edge anchored, so it
/// opens backwards (leftwards against the right edge, upwards against the
/// bottom). If neither fits it is clamped.
///
/// "Do not move" being the first choice is what makes a fold/unfold round
/// trip exact: if the panel fitted there, so does the dock coming out of its
/// corner, and vice versa. @p end is the first point already outside.
int anchorAxis(int pos, int len, int newLen, int lo, int end) {
    const int keepStart = pos;
    const int keepEnd = pos + len - newLen;
    if (keepStart >= lo && keepStart + newLen <= end) return keepStart;
    if (keepEnd >= lo && keepEnd + newLen <= end) return keepEnd;
    return qBound(lo, keepStart, qMax(lo, end - newLen));
}

/// Pushes a rectangle inside the area where it may be placed.
QPoint clampInto(QPoint pos, const QSize &size, const QRect &area) {
    if (!area.isValid()) return pos;
    return {qBound(area.left(), pos.x(), qMax(area.left(), area.right() + 1 - size.width())),
            qBound(area.top(), pos.y(), qMax(area.top(), area.bottom() + 1 - size.height()))};
}

/// Clickable strip under the header. A plain QWidget, so it needs
/// WA_StyledBackground for the stylesheet to paint its background.
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
    m_expandedSize = m_store.prefs().windowSize;
    m_listSize = m_expandedSize;
    VoiceRecorder::setPreferredInput(m_store.prefs().input);

    connect(&m_store, &Store::reloaded, this, &Panel::onStoreReloaded);

    m_alarm = new Alarm(this);
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
    m_drive = new DriveSync(&m_store, this);
    m_drive->canApply = [this] { return userIdle(); };
    connect(m_drive, &DriveSync::openUrl, this, [](const QUrl &url) { QDesktopServices::openUrl(url); });
    m_driveTimer = new QTimer(this);
    m_driveTimer->setSingleShot(true);
    m_driveTimer->setInterval(20 * 1000);
    connect(m_driveTimer, &QTimer::timeout, this, [this] {
        // Never with the folder missing: what is in memory is not the notes.
        if (m_store.available()) m_drive->syncNow();
    });
    connect(&m_store, &Store::saved, this, [this] {
        if (m_drive->connected() && m_drive->state() != DriveSync::Syncing) m_driveTimer->start();
    });
    // No merging while typing: retry shortly.
    connect(m_drive, &DriveSync::deferred, this, [this] { m_driveTimer->start(); });
    connect(m_drive, &DriveSync::attachmentsArrived, this, [this] {
        // The cards were built without those images or that recording.
        if (userIdle()) rebuildList();
    });
    connect(&m_store, &Store::merged, this, &Panel::onStoreMerged);
    // Google Calendar only touches events: no need to rebuild the cards (and take
    // the cursor from someone typing); the planner and the alarms suffice. What
    // was deleted there may be what was ringing here.
    connect(m_drive->calendar(), &CalendarSync::eventsChanged, this, [this] {
        if (!anyRinging()) m_alarm->stop();
        refreshAlarmBar();
        applyBadgeAlert();
        refreshPlanner();
        refreshFooter();
        checkReminders();
    });
    m_drivePoll = new QTimer(this);
    m_drivePoll->setInterval(90 * 1000);
    connect(m_drivePoll, &QTimer::timeout, this, [this] {
        if (m_drive->connected() && m_store.available()) m_drive->syncNow();
    });
    m_drivePoll->start();

    buildShell();
    applyAppModeChrome();
    buildTray();
    rebuildList();
    applyTheme();
    refreshDataWarning();
    refreshUpdateBanner();   // with what the last check found
    checkReminders();   // something may have come due while the app was closed
    tickTimers();       // and a timer may have reached zero

    if (!m_expandedSize.isValid())
        m_expandedSize = QSize(352 + shadowMargin() * 2, 560);
    m_listSize = m_expandedSize;
    resize(m_expandedSize);
    showPage(m_shell);
    applyWindowFlags();
    // After applyWindowFlags(): changing flags destroys the native window, and
    // placing it before that places a window about to be recreated.
    restoreWindowPos();

    // Changes made while the app was closed, or an upload that failed last time,
    // go up shortly after startup.
    if (m_drive->connected()) m_driveTimer->start();
}

Panel::~Panel() {
    // The Store saves on destruction and emits saved(); by then this part of the
    // panel is gone, so nobody may answer that signal.
    disconnect(&m_store, nullptr, this, nullptr);
    // Same for the hook: the Store would call it after the panel is gone.
    m_store.beforeSave = nullptr;
    syncPrefs();
    m_store.save();
}

void Panel::buildShell() {
    m_store.beforeSave = [this] { syncPrefs(); };

    auto *outer = new QVBoxLayout(this);
    m_outer = outer;
    outer->setContentsMargins(shadowMargin(), shadowMargin(), shadowMargin(), shadowMargin());

    m_stack = new QStackedWidget;
    outer->addWidget(m_stack);

    m_shell = new QFrame;
    m_shell->setObjectName("shell");
    m_shell->setMinimumWidth(kShellMinWidth);   // no longer fixed: resizable

    // The shadow is set in applyAppModeChrome(): in app mode the window manager
    // draws it and there is no room for it here.

    auto *col = new QVBoxLayout(m_shell);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);

    col->addWidget(buildHeader());

    // Workspace areas, attached to the header. Only while the list is shown: on
    // the other pages they mean nothing.
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
        refreshFooter();
    };
    m_areaTabs->deleteRequested = [this](const QString &id) { confirmDeleteArea(id); };
    m_areaTabs->moveRequested = [this](const QString &id, int steps) {
        if (Area *a = m_store.area(id)) m_store.moveArea(a, steps);
        refreshAreaTabs();
    };
    col->addWidget(m_areaTabs);

    // Area shortcuts, from anywhere in the panel.
    auto *newAreaKey = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_T), this);
    connect(newAreaKey, &QShortcut::activated, this, &Panel::newArea);
    for (int i = 1; i <= 9; ++i) {
        auto *key = new QShortcut(
            QKeySequence(QKeyCombination(Qt::ControlModifier, Qt::Key(Qt::Key_0 + i))), this);
        connect(key, &QShortcut::activated, this, [this, i] {
            const QList<Area *> &areas = m_store.areas();
            if (i <= areas.size()) switchArea(areas.at(i - 1)->id);
        });
    }

    // Missing-folder warning. Up here and not in the footer: while it shows,
    // nothing typed is saved.
    m_dataWarn = new QLabel;
    m_dataWarn->setObjectName("warnBanner");
    m_dataWarn->setWordWrap(true);   // see *Card widths*: it must not demand its width
    m_dataWarn->hide();
    col->addWidget(m_dataWarn);

    // New-version notice: a clickable strip, not a dialog. Finding out should not
    // interrupt anyone, and it can be turned off in settings.
    auto *bar = new ClickableBar([this] { openLatestRelease(); });
    bar->setObjectName("updateBanner");
    auto *ul = new QHBoxLayout(bar);
    ul->setContentsMargins(10, 6, 10, 6);
    ul->setSpacing(7);
    m_updateText = new QLabel;
    m_updateText->setObjectName("updateBannerText");
    m_updateText->setWordWrap(true);   // see *Card widths*: it must not demand its width
    ul->addWidget(m_updateText, 1);
    m_updateBar = bar;
    m_updateBar->hide();
    col->addWidget(m_updateBar);

    // Finished timer or starting event: red like an overdue reminder, with its
    // own stop button since they have no card in the list to put one on.
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
    m_alarmText->setWordWrap(true);   // see *Card widths*: it must not demand its width
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

    // search bar (hidden by default)
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

    m_badge = buildBadge();
    m_stack->addWidget(m_badge);

    setMinimumSize(kShellMinWidth + shadowMargin() * 2, kShellMinHeight);
}

QFrame *Panel::buildHeader() {
    auto *header = new DragBar;
    header->setObjectName("header");
    header->setCursor(Qt::OpenHandCursor);

    auto *l = new QHBoxLayout(header);
    l->setContentsMargins(12, 8, 10, 8);
    l->setSpacing(4);

    // Elidable: with seven header buttons "Temporizadores" no longer fits at the
    // minimum width, and a label demanding its width would widen the whole window
    // (see *Card widths*).
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
    m_minBtn = min;
    m_headerButtons = {m_calendarBtn, m_timersBtn, m_birthdayBtn, search, add, m_settingsBtn, min};

    connect(search, &QToolButton::clicked, this, &Panel::toggleSearch);
    connect(min, &QToolButton::clicked, this, [this] {
        // A normal window minimises to the taskbar; the dock is the widget's thing.
        if (appMode()) showMinimized();
        else collapse();
    });
    connect(add, &QToolButton::clicked, this, [this, add] { openNewNoteMenu(add); });
    // The page buttons are connected in buildBody(), where the pages exist.

    for (QToolButton *b : m_headerButtons) l->addWidget(b);
    return header;
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
        refreshFooter();
        checkReminders();      // one starting right now must ring right now
    });
    connect(m_planner, &PlannerView::eventChanged, this, [this](Event *) {
        // One dragged to another time stops ringing (moveItem() clears ringingMs): if
        // it was the only one, the tone and the red go with it.
        if (!anyRinging()) m_alarm->stop();
        refreshAlarmBar();
        applyBadgeAlert();
        save();
        checkReminders();
    });
    connect(m_planner, &PlannerView::eventDeleted, this, [this](Event *e) {
        // As with a note: if it was the only thing ringing, the tone stops.
        const bool wasRinging = e->ringingMs != 0;
        m_store.removeEvent(e);
        if (wasRinging && !anyRinging()) m_alarm->stop();
        refreshAlarmBar();
        applyBadgeAlert();
        refreshPlanner();
        refreshFooter();
    });
    connect(m_planner, &PlannerView::reminderCreated, this, &Panel::createReminder);
    // Dragged in the planner: the same as changing the date on its card
    // (NoteCard::applyDue()), postponing included.
    connect(m_planner, &PlannerView::reminderMoved, this, [this](Note *n, qint64 dueAtMs) {
        n->dueAtMs = dueAtMs;
        n->due = dueLabel(n->dueAt());
        n->fired = false;
        n->ringing = false;
        if (!anyRinging()) m_alarm->stop();
        refreshDueCards();
        refreshAlarmBar();
        applyBadgeAlert();
        save();
        checkReminders();
    });
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

    m_timerView = new TimerView(m_theme);
    m_timerView->setSource(&m_store.timers());
    connect(m_timerView, &TimerView::createRequested, this, &Panel::createTimer);
    connect(m_timerView, &TimerView::toggleRequested, this, &Panel::toggleTimer);
    connect(m_timerView, &TimerView::resetRequested, this, &Panel::resetTimer);
    connect(m_timerView, &TimerView::removeRequested, this, &Panel::removeTimer);

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
    connect(m_settings, &SettingsView::appModeToggled, this, &Panel::setAppMode);
    connect(m_settings, &SettingsView::onTopToggled, this, &Panel::setOnTop);
    connect(m_settings, &SettingsView::sizePerPageToggled, this, [this](bool on) {
        if (on) {
            // The list's size comes from the old rule (without the planner's widening or
            // the stretch), and the open page, settings, keeps its current one.
            syncPrefs();
            m_listSize = m_store.prefs().windowSize;
            m_store.prefs().sizePerPage = true;
            rememberPageSize(m_body->currentWidget());
            m_grownFrom = m_grownTo = m_narrowGeom = m_wideGeom = QRect();
        } else {
            // The recorded sizes are kept: turning it back on restores them.
            m_store.prefs().sizePerPage = false;
        }
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
    m_settings->setDrive(m_drive);
    connect(m_drive, &DriveSync::changed, m_settings, &SettingsView::refreshDrive);
    connect(m_settings, &SettingsView::driveConnectRequested, m_drive, &DriveSync::connectAccount);
    connect(m_settings, &SettingsView::driveCancelRequested, m_drive, &DriveSync::cancel);
    connect(m_settings, &SettingsView::driveDisconnectRequested, m_drive, &DriveSync::disconnectAccount);
    connect(m_drive->calendar(), &CalendarSync::changed, m_settings, &SettingsView::refreshDrive);
    connect(m_settings, &SettingsView::calendarToggled, this, [this](bool on) {
        m_drive->calendar()->setEnabled(on);
        // Without the scope, enabling it means re-authorising the account.
        if (on && !m_drive->hasCalendarScope()) m_drive->connectAccount();
        else if (on && m_store.available()) m_drive->syncNow();
    });
    connect(m_settings, &SettingsView::calendarGrantRequested, m_drive, &DriveSync::connectAccount);
    connect(m_settings, &SettingsView::calendarFollowToggled, this,
            [this](const QString &calId, bool on) {
        CalendarSync *cal = m_drive->calendar();
        if (on) {
            cal->follow(calId);
            refreshPlanner();
            if (m_store.available()) m_drive->syncNow();
        } else if (Event::Category *c = cal->categoryFor(calId)) {
            removeCategory(c);
        }
    });
    connect(m_settings, &SettingsView::calendarTargetRequested, this, &Panel::openCalendarTarget);
    connect(m_settings, &SettingsView::driveSyncRequested, this, [this] {
        if (!m_store.available()) return;
        save();                 // the latest edits, before uploading
        m_driveTimer->stop();   // uploading now
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
    // From the start, not only on page switches: the planner needs more height
    // than the list and would impose it on the window from birth.
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

    // Time left on the running timer, visible from any page. It takes the hint's
    // place while counting: both do not fit at the minimum width.
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

    // Top-left: the folded window measures exactly the dock, so there is no room
    // to spare, and anchoredTopLeft() handles which screen corner it folds to.
    hl->addWidget(btn, 0, Qt::AlignTop | Qt::AlignLeft);
    hl->addStretch();
    return host;
}

void Panel::applyTheme() {
    setStyleSheet(m_theme.sheet());

    // The open page's button in the accent, the rest grey.
    for (QToolButton *b : m_headerButtons)
        b->setIcon(paintIcon(b->property("iconKind").toString(),
                             b->property("active").toBool() ? m_theme.accent
                                                            : QColor(Theme::muted())));

    if (m_planner) m_planner->setTheme(m_theme);
    if (m_timerView) m_timerView->setTheme(m_theme);
    if (m_birthdays) m_birthdays->setTheme(m_theme);
    // Settings is not rebuilt: it repaints what carries the accent and keeps the
    // opacity slider alive, which is what just called here.
    if (m_settings) m_settings->setTheme(m_theme);
    applyBadgeAlert();

    // The dock is styled inline because its background depends on the opacity.
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

/// The window's fixed texts are set again one by one; cards and planner are
/// rebuilt whole, simpler than hunting each label inside them.
void Panel::retranslate() {
    m_search->setPlaceholderText(L("Filtrar notas…"));
    m_emptyText->setText(L("Todavía no hay notas"));
    m_emptyBtn->setText(L("Crear la primera"));
    refreshFooterHint();
    refreshDataWarning();

    for (QToolButton *b : m_headerButtons)
        b->setToolTip(L(b->property("tip").toString()));
    // The page buttons also say which page is open, so they go their own way.
    refreshPageButtons();
    m_alarmPlus->setText(L("+1 min"));
    for (auto *b : m_alarmBar->findChildren<QToolButton *>())
        if (!b->property("tip").toString().isEmpty()) b->setText(L(b->property("tip").toString()));
    refreshAlarmBar();

    buildTrayMenu();
    applyBadgeAlert();   // the tray tooltip carries text
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
    refreshTitle();
    // Rebuilds the cards and with them the footer, the planner and the dock.
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
    m_badgeCount->setText(QString::number(m_store.count()));
    refreshFooter();
    refreshPlanner();
    refreshBirthdays();
    applyBadgeAlert();
    if (m_search && !m_search->text().isEmpty())
        applyFilter(m_search->text());
}

/// The footer hint depends on where you are: on the list, the card menu; on
/// any other page, how to leave it.
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
    // The area name is elided: the footer label must not demand its width (see
    // *Card widths*), and the user writes the name.
    const Area *a = m_store.area(currentArea());
    const QString name = QFontMetrics(m_footerText->font())
                             .elidedText(a ? a->name.toUpper() : QString(), Qt::ElideRight, 90);
    m_footerText->setText(L("%1 · %2 NOTAS").arg(name).arg(m_store.notesIn(currentArea()).size()));
}

void Panel::addNote(Note::Type type) {
    auto *n = new Note;
    n->type = type;
    n->title = type == Note::Check    ? L("Nueva lista")
             : type == Note::Reminder ? L("Nuevo recordatorio")
             : type == Note::Voice    ? L("Nota de voz")
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
}

void Panel::removeNote(Note *n) {
    // If the note was ringing and was the only one, the tone kept going with
    // nothing on screen to explain or stop it. Asked before deleting, since
    // afterwards there is no one to ask.
    const bool wasRinging = n->ringing;
    m_store.remove(n);
    if (wasRinging && !anyRinging()) m_alarm->stop();
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
        if (!anyRinging()) m_alarm->stop();
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

void Panel::toggleSearch() {
    m_searchBar->setVisible(!m_searchBar->isVisible());
    if (m_searchBar->isVisible()) {
        showNotes();          // filtering with the planner in front would not be visible
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
    else m_footerText->setText(L("%1 DE %2").arg(shown).arg(cards().size()));
}

void Panel::bringToFront() {
    // An open normal window is left alone: expand() would restore the saved size
    // even if the user maximised it.
    if (!appMode() || m_stack->currentWidget() == m_badge) expand();
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

int Panel::shadowMargin() const { return appMode() ? 0 : kShadowMargin; }

void Panel::setAppMode(bool on) {
    if (on == appMode()) return;
    // The system frame replaces the shadow margin: the window shrinks or grows by
    // those margins so the panel inside keeps its size.
    const int delta = 2 * kShadowMargin * (on ? -1 : 1);
    m_store.prefs().appMode = on;
    applyAppModeChrome();
    applyWindowFlags();
    // The minimum first: with the old one in place, shrinking does not fit.
    syncShellMinimum();
    resize(size() + QSize(delta, delta));
    m_expandedSize += QSize(delta, delta);
    m_listSize += QSize(delta, delta);
    for (QSize &s : m_store.prefs().pageSizes) s += QSize(delta, delta);
    m_grownFrom = m_grownTo = m_narrowGeom = m_wideGeom = QRect();
    // Size per page is off in app mode, so the window may have moved since the
    // list's corner was recorded: back in the widget, it starts again from here.
    m_pageHome = m_pagePlaced = QRect();
    keepOnScreen();
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

    const char *tip = appMode() ? "Minimizar" : "Plegar a icono";
    m_minBtn->setProperty("tip", tip);
    m_minBtn->setToolTip(L(tip));
}

QList<Panel::Page> Panel::pages() const {
    return {{m_planner, m_calendarBtn, "Calendario"},
            {m_timerView, m_timersBtn, "Temporizadores"},
            {m_birthdays, m_birthdayBtn, "Cumpleaños"},
            {m_settings, m_settingsBtn, "Ajustes"}};
}

/// Opens a page, or returns to the notes if it was already open: the same
/// button enters and leaves.
void Panel::togglePage(QWidget *page) {
    if (m_body->currentWidget() == page) {
        showNotes();
        return;
    }
    m_searchBar->hide();
    if (page == m_planner) m_planner->refresh();
    else if (page == m_timerView) m_timerView->refresh();
    else if (page == m_birthdays) m_birthdays->refresh();
    else if (page == m_settings) m_settings->refresh();
    switchBodyPage(page);
    refreshPageButtons();
    refreshFooter();
    refreshTitle();
}

void Panel::showNotes() {
    if (m_body->currentWidget() == m_scroll) return;

    switchBodyPage(m_scroll);
    refreshPageButtons();
    refreshFooter();
    refreshTitle();
}

/// Widget only: an ordinary window keeps the size and place the user gives it,
/// whatever page is open. The preference is kept for when it goes back.
bool Panel::sizePerPage() const { return !appMode() && m_store.prefs().sizePerPage; }

/// Fixed names rather than those of pages(): those are Spanish for
/// translation, and these are stored in notes.json.
QString Panel::pageKey(const QWidget *page) const {
    if (page == m_planner) return "planner";
    if (page == m_timerView) return "timers";
    if (page == m_birthdays) return "birthdays";
    if (page == m_settings) return "settings";
    return {};
}

void Panel::rememberPageSize(QWidget *page) {
    if (!sizePerPage() || !m_stack || m_stack->currentWidget() != m_shell || !page) return;
    // Before mapping, the window has Qt's default size, which nobody chose: a
    // save during startup would record it.
    if (!m_posRestored) return;
    if (page == m_scroll) m_listSize = size();
    else if (const QString key = pageKey(page); !key.isEmpty()) m_store.prefs().pageSizes[key] = size();
}

/// A page with no stored size opens with the list's, not the previous page's,
/// so a first visit to the planner does not inherit a stretched settings
/// page, and the planner widens from there.
bool Panel::applyPageSize(QWidget *page) {
    const QSize stored = page == m_scroll ? m_listSize
                                          : m_store.prefs().pageSizes.value(pageKey(page));
    QSize target = stored.isValid() ? stored : m_listSize;
    if (target.isValid()) {
        // Never below what the page needs (see syncShellMinimum()), never more than
        // fits: a size recorded on another monitor.
        target = target.expandedTo(minimumSize());
        if (const QRect area = placementArea(); area.isValid()) target = target.boundedTo(area.size());
        // From the list, not from the previous page: see m_pageHome.
        const QRect from = m_pageHome.isValid() ? m_pageHome : geometry();
        const QRect want(anchoredTopLeft(from, target), target);
        if (want != geometry()) {
            setGeometry(want);
            keepOnScreen();
        }
    }
    m_grownFrom = m_grownTo = QRect();
    m_pagePlaced = geometry();
    return stored.isValid();
}

void Panel::syncPageHome(QWidget *from) {
    if (from == m_scroll || !m_pageHome.isValid() || !m_pagePlaced.isValid()) {
        // From the list, the list is what is there. With nothing recorded (the mode
        // was just switched on), the closest thing: the current corner.
        const QSize list = from != m_scroll && m_listSize.isValid() ? m_listSize : size();
        m_pageHome = QRect(pos(), list);
        return;
    }
    // However far the user dragged the page, the list goes too; untouched, the
    // offset is zero.
    m_pageHome.translate(pos() - m_pagePlaced.topLeft());
}

void Panel::switchBodyPage(QWidget *page) {
    QWidget *from = m_body->currentWidget();
    const bool perPage = sizePerPage() && m_stack && m_stack->currentWidget() == m_shell;
    if (perPage) {
        // How the page is left is its size; the widen and stretch notes belong to the
        // single-size mode and are not given back here.
        syncPageHome(from);
        rememberPageSize(from);
        m_grownFrom = m_grownTo = m_narrowGeom = m_wideGeom = QRect();
    } else if (from == m_planner) {
        // The width goes back first, or the next page would inherit the window
        // widened for the planner.
        leaveWide();
    }

    showBodyPage(page);

    if (perPage) {
        if (!applyPageSize(page) && page == m_planner) {
            enterWide();
            m_narrowGeom = m_wideGeom = QRect();
            m_pagePlaced = geometry();
        }
    } else if (page == m_planner) {
        enterWide();
    }
}

void Panel::setPageActive(QToolButton *button, bool on,
                          const QString &tipOn, const QString &tipOff) {
    button->setProperty("active", on);
    button->setToolTip(on ? tipOn : tipOff);
    button->setIcon(paintIcon(button->property("iconKind").toString(),
                              on ? m_theme.accent : QColor(Theme::muted())));
    // A dynamic property does not repaint by itself.
    button->style()->unpolish(button);
    button->style()->polish(button);
}

void Panel::refreshPageButtons() {
    QWidget *current = m_body ? m_body->currentWidget() : nullptr;
    for (const Page &p : pages())
        setPageActive(p.button, p.page == current, L("Ver notas"), L(p.name));
}

/// On the list the title is the app's name again.
void Panel::refreshTitle() {
    if (!m_titleLabel || !m_body) return;
    QString title = L("Tagoror");
    for (const Page &p : pages())
        if (p.page == m_body->currentWidget()) title = L(p.name);
    m_titleLabel->setText(title);
    m_titleLabel->update();   // ElidedLabel paints itself
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
        if (!anyRinging()) m_alarm->stop();
        refreshAlarmBar();
        applyBadgeAlert();
        refreshFooter();
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
            if (m_store.available()) m_drive->syncNow();
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

/// Widens towards where it fits (anchoredTopLeft(): leftwards against the
/// right edge) and records the result so it can be given back.
void Panel::enterWide() {
    if (!m_stack || m_stack->currentWidget() != m_shell) return;
    int target = PlannerView::kPreferredWidth + shadowMargin() * 2;
    if (const QRect area = placementArea(); area.isValid()) target = qMin(target, area.width());
    if (width() >= target) return;

    const QRect before = geometry();
    const QSize size(target, height());
    setGeometry(QRect(anchoredTopLeft(before, size), size));
    keepOnScreen();
    m_narrowGeom = before;
    m_wideGeom = geometry();
}

/// Only while the window is still what enterWide() left: if the user resized
/// it meanwhile, that size is theirs.
void Panel::leaveWide() {
    if (m_narrowGeom.isValid() && geometry() == m_wideGeom) {
        setGeometry(m_narrowGeom);
        keepOnScreen();
    }
    m_narrowGeom = QRect();
    m_wideGeom = QRect();
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
    // As with notes: deleting the ringing one must not leave the tone on.
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
    // The one that ends first is the interesting one.
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

/// The first thing ringing: a timer, or else an event. With several it says
/// how many, and Stop silences them all.
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
        if (e->ringingMs) {
            // With hours or a day of lead, "at 10:00" may be tomorrow: then say the day.
            const QDateTime at = QDateTime::fromMSecsSinceEpoch(e->ringingMs);
            const QString title = e->title.isEmpty() ? L("Sin título") : e->title;
            if (at.date() == QDate::currentDate())
                what << L("%1 · empieza a las %2").arg(title, at.toString("HH:mm"));
            else
                what << L("%1 · empieza el %2")
                            .arg(title, Lang::locale().toString(at, "ddd d, HH:mm"));
        }

    const bool show = !what.isEmpty();
    if (show) {
        QString text = what.first();
        if (what.size() > 1) text += " " + L("(y %1 más)").arg(what.size() - 1);
        m_alarmText->setText(text);
        m_alarmPlus->setVisible(timers);
    }
    if (show == !m_alarmBar->isHidden()) return;
    m_alarmBar->setVisible(show);
    syncShellMinimum();   // takes height inside the shell, like the other banners
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
    // Records which occurrence rang, not a bare "done": next week the class rings
    // again.
    e->firedMs = e->ringingMs;
    e->ringingMs = 0;
}

void Panel::revealNote(Note *n) {
    showNotes();
    if (m_store.areaOf(n) != currentArea()) switchArea(m_store.areaOf(n));
    for (NoteCard *card : cards()) {
        if (card->note() != n) continue;
        card->show();                       // a filter may have hidden it
        m_scroll->ensureWidgetVisible(card);
        card->focusTitle();
        return;
    }
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
    // addChoice() and not addChips(): the alarm time is a setting with state, and
    // the menu must show which one is set.
    menu->addChoice(labels, int(times.indexOf(b->remindAt)), [this, b, times](int i) {
        b->remindAt = times.at(i);
        // A new time re-arms this year's alarm: changing it right after silencing it
        // must do something.
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
    // It may be ringing when deleted: the tone would hang if nobody stopped it
    // before the only thing asking for it disappears.
    const bool wasRinging = b->ringing;
    m_store.removeBirthday(b);
    if (wasRinging && !anyRinging()) m_alarm->stop();
    m_birthdays->refresh();
    refreshFooter();
    applyBadgeAlert();
}

void Panel::toggleGreeted(Birthday *b) {
    b->greetedYear = b->greeted() ? 0 : QDate::currentDate().year();
    // Greeting is acknowledging: it makes no sense to keep ringing.
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
    // The year is recorded, not a bare "rang": the mark must expire on its own so
    // it rings again next year.
    b->firedYear = QDate::currentDate().year();
}

void Panel::dismissBirthday(Birthday *b) {
    silenceBirthday(b);
    if (!anyRinging()) m_alarm->stop();
    m_birthdays->refresh();
    applyBadgeAlert();
    save();
}

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
    // Birthdays with a time ring on the same heartbeat with the same tone: to
    // whoever hears it, it is the same alarm, and a second mechanism would only
    // be a second way for it to get stuck.
    const QDateTime nowAt = QDateTime::currentDateTime();
    for (Birthday *b : m_store.birthdays()) {
        if (b->alarmDue(nowAt) && !b->ringing) {
            b->ringing = true;
            started = true;
        }
    }
    // Planner events with an alert, the same tone again.
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

/// A normal reminder is marked as fired and does not come back; a repeating
/// one moves to its next turn, which is what makes it ring again.
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

/// Postpones a reminder that was ringing. The card already cleared its
/// @c ringing and repainted; here goes what lives outside it (the tone, the
/// red dock) and the planner, which still showed it ringing on its new day.
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
    refreshAreaTabs();   // each area's red dot comes from here too

    auto *badge = m_badge->findChild<QToolButton *>("badge");
    if (badge) {
        // Folded, the dock is the only thing visible: it changes icon and colour so
        // the waiting alarm shows.
        badge->setIcon(paintIcon(alert ? "bell" : "notes",
                                 QColor(alert ? "#ff7a6b" : Theme::fg()), 22));
        badge->setToolTip(alert ? L("Recordatorio vencido · clic para parar")
                                : L("Abrir Tagoror · arrastra para mover"));
    }
    if (m_tray) {
        // Hidden in the tray, the icon is the only sign that something is due.
        m_tray->setIcon(alert ? alertIcon() : qApp->windowIcon());
        m_tray->setToolTip(alert ? L("Recordatorio vencido") : L("Tagoror"));
    }
    if (m_badgeCount)
        m_badgeCount->setStyleSheet(
            QString("background:%1; color:#0d1014; border-radius:10px;"
                    "font-size:11px; font-weight:600;")
                .arg(alert ? QString("#ff7a6b") : m_theme.accent.name()));
}

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
void Panel::pollUpdates() {
    if (!m_store.prefs().updateCheck || m_updater->busy()) return;

    constexpr qint64 kDayMs = 24LL * 3600 * 1000;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 last = m_store.prefs().lastUpdateMs;
    // A date in the future (the clock moved back) would mean "never again": treat
    // it as none.
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
        // Only an answered check counts: after a failure it retries instead of
        // waiting a whole day.
        m_store.prefs().lastUpdateMs = QDateTime::currentMSecsSinceEpoch();
        save();
    }
    if (m_settings) m_settings->setUpdateState(false, error);
    refreshUpdateBanner();
}

void Panel::openLatestRelease() {
    // Without a stored URL (the version came from the file, not this session) the
    // releases page leads to the same place.
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

    // isHidden(), not isVisible(): the latter is also false while the panel is in
    // the tray, and the strip would be rebuilt every time.
    if (show == !m_updateBar->isHidden()) return;
    m_updateBar->setVisible(show);
    // Takes height inside the shell: at minimum size, appearing without this
    // would paint it over the first card.
    syncShellMinimum();
}

void Panel::pollDataDir() {
    if (m_store.available()) return;
    m_store.retryLoad();   // emits reloaded() the time it succeeds
}

void Panel::onStoreReloaded() {
    m_theme.accent = m_store.prefs().accent;
    m_theme.opacity = m_store.prefs().opacity;
    m_theme.textScale = m_store.prefs().textScale;
    VoiceRecorder::setPreferredInput(m_store.prefs().input);
    applyTheme();
    // Store::load() already set the language; retranslate() rewrites the window
    // with it and rebuilds cards, planner and tray in one go.
    retranslate();
    // Size and position are left alone on purpose: the window is where the user
    // has it now. The notes that were ringing no longer exist, so the tone stops
    // before checking whether anything in the new list should ring.
    if (!anyRinging()) m_alarm->stop();
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
    return !qobject_cast<QLineEdit *>(f) && !qobject_cast<QTextEdit *>(f);
}

void Panel::onStoreMerged() {
    // What was ringing here may have been silenced on the other machine: the
    // remote version carries the "fired" mark, so it is silenced here too.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const int year = QDate::currentDate().year();
    for (Note *n : m_store.notes())
        if (n->ringing && !n->isDue(now)) n->ringing = false;
    for (Birthday *b : m_store.birthdays())
        if (b->ringing && b->firedYear == year) b->ringing = false;
    for (Event *e : m_store.events())
        if (e->ringingMs && e->firedMs >= e->ringingMs) e->ringingMs = 0;
    if (!anyRinging()) m_alarm->stop();

    rebuildList();          // cards, footer, planner, birthdays and dock
    if (m_birthdays) m_birthdays->refresh();
    tickTimers();           // one arriving while running must start counting
    onTimersChanged();
    refreshAlarmBar();
    checkReminders();       // and one arriving overdue, ringing
}

void Panel::refreshDataWarning() {
    refreshSettings();   // the page shows the folder and whether it is available
    if (!m_dataWarn) return;
    const bool missing = !m_store.available();
    if (missing) {
        // The path goes in the tooltip, not the text: inside, the banner took three
        // lines, two of them a path already shown in settings.
        m_dataWarn->setText(
            L("La carpeta de notas no está disponible. Nada de lo que escribas se guardará."));
        m_dataWarn->setToolTip(appDataDir());
    }
    // isHidden(), not isVisible(): the latter is also false while the panel is in
    // the tray, and the banner would be rebuilt every time.
    if (missing == !m_dataWarn->isHidden()) return;
    m_dataWarn->setVisible(missing);
    // The banner takes height inside the shell: at minimum size, appearing without
    // this paints it over the first card (see *Window behavior*).
    syncShellMinimum();
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
        if (m_stack->currentWidget() == m_badge) expand();
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
    for (const Page &p : pages())
        connect(open->addAction(L(p.name)), &QAction::triggered, this, [this, page = p.page] {
            bringToFront();
            if (m_body->currentWidget() != page) togglePage(page);
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
        if (m_stack->currentWidget() == m_badge) expand();
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
                const bool folded = m_stack->currentWidget() == m_badge;
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
    if (!m_posRestored) {
        m_posRestored = true;
        restoreWindowPos();
    }
}

void Panel::applyWindowFlags() {
    // By default the widget stays on the desktop, below other windows; "always on
    // top" is optional. On Wayland these hints are only a request.
    Qt::WindowFlags flags = Qt::FramelessWindowHint | Qt::Tool;
    if (m_store.prefs().onTop) flags |= Qt::WindowStaysOnTopHint;
    else if (!m_lifted) flags |= Qt::WindowStaysOnBottomHint;
    // App mode: an ordinary window with its frame, in the taskbar and Alt+Tab.
    // Without "always on top" it stacks like any other, not below them.
    if (appMode()) {
        flags = Qt::Window;
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

void Panel::showPage(QWidget *page) {
    // Hidden pages of a QStackedWidget still count for the size hint; ignoring
    // them lets the window shrink to the dock.
    for (int i = 0; i < m_stack->count(); ++i) {
        QWidget *w = m_stack->widget(i);
        const auto policy = (w == page) ? QSizePolicy::Preferred : QSizePolicy::Ignored;
        w->setSizePolicy(policy, policy);
    }
    // The Ignored policy is not enough: an explicit minimumSize still adds to the
    // QStackedLayout's minimum, and the folded panel kept an invisible 300px
    // strip next to the dock that swallowed clicks.
    m_shell->setMinimumWidth(page == m_shell ? kShellMinWidth : 0);
    m_stack->setCurrentWidget(page);
}

/// Same idea one level in: a QStackedWidget takes the largest minimum of all
/// its pages, so the planner would impose its height on the note list.
void Panel::showBodyPage(QWidget *page) {
    // If the focus is on the page being left, park it on that page's header button
    // first. Hiding a focused widget makes Qt pass the focus down the chain, and
    // on the way back to the list that was the first card's title, which then
    // looked like it was being edited. No ring: it did not arrive by Tab.
    QWidget *leaving = m_body->currentWidget();
    QWidget *focus = QApplication::focusWidget();
    if (leaving && leaving != page && focus && leaving->isAncestorOf(focus)) {
        QToolButton *to = nullptr;
        for (const Page &p : pages())
            if (p.page == leaving || (!to && p.page == page)) to = p.button;
        if (to) to->setFocus(Qt::OtherFocusReason);
        else setFocus(Qt::OtherFocusReason);
    }

    for (int i = 0; i < m_body->count(); ++i) {
        QWidget *w = m_body->widget(i);
        const auto policy = (w == page) ? QSizePolicy::Preferred : QSizePolicy::Ignored;
        w->setSizePolicy(policy, policy);
    }
    m_body->setCurrentWidget(page);
    if (m_areaTabs) m_areaTabs->setVisible(page == m_scroll);
    syncShellMinimum();
}

int Panel::shellMinimumHeight() const {
    QLayout *l = m_shell ? m_shell->layout() : nullptr;
    if (!l) return kShellMinHeight;

    // Recompute inside out, by hand. Hiding a widget invalidates its parent's
    // layout through a *posted* LayoutRequest: without flushing it, minimumSize()
    // answers with the previous state. Invalidating only the outer layout is not
    // enough: it asks the inner one, which keeps its cached value until then.
    if (QWidget *page = m_body ? m_body->currentWidget() : nullptr) page->updateGeometry();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    l->invalidate();
    l->activate();
    return qMax(kShellMinHeight, l->minimumSize().height() + shadowMargin() * 2);
}

/// The window's minimum comes from the layout, never from a constant.
///
/// A setMinimumSize() below what the layout needs shrinks nothing: Qt hands
/// out the height it has and the geometries overlap. The other way round, a
/// page that needs more height grows the window downwards, and upwards only
/// as far as the work area forces it (the same rule as anchoredTopLeft()).
void Panel::syncShellMinimum() {
    if (!m_shell || !m_stack || m_stack->currentWidget() != m_shell) return;

    // setMinimumSize() already stretches the window when short, keeping the
    // top-left corner: it grows downwards, as wanted. What is left is to record
    // that the stretch is ours and pull it back into the work area.
    const QRect before = geometry();
    const int minH = shellMinimumHeight();
    setMinimumSize(kShellMinWidth + shadowMargin() * 2, minH);

    if (height() > before.height()) {
        // Only the first stretch is recorded: two growths in a row must give back the
        // size from before the first, not the one in between.
        if (!m_grownFrom.isValid()) m_grownFrom = before;
        if (const QRect area = placementArea(); area.isValid())
            if (const QPoint p = clampInto(pos(), size(), area); p != pos()) move(p);
        m_grownTo = geometry();
        return;
    }

    // It fits with room to spare. If the window is this big because we stretched
    // it, give it back, or it would grow a little every time. The note only holds
    // while the window is still what we left: once the user touches it, the size
    // is theirs.
    if (m_grownFrom.isValid() && geometry() == m_grownTo) {
        // Not everything can be given back yet (the page still needs more than the
        // starting size): leave it and keep the note, which is still true. Dropping
        // it here let an intermediate sync eat the return trip.
        if (m_grownFrom.height() < minH) return;
        setGeometry(m_grownFrom);
        keepOnScreen();
    }
    m_grownFrom = QRect();
    m_grownTo = QRect();
}

void Panel::collapse() {
    // The saved size is the normal one, not widened for the planner, or unfolding
    // would open wide for any page. With size per page the dock comes out of the
    // list, not the open page: if that opened leftwards or upwards, its corner is
    // not the panel's and the dock would land far from where it was left.
    QRect panel = geometry();
    if (sizePerPage()) {
        syncPageHome(m_body->currentWidget());
        rememberPageSize(m_body->currentWidget());
        panel = m_pageHome;
    } else if (m_body->currentWidget() == m_planner) {
        leaveWide();
        panel = geometry();
    }
    m_expandedSize = size();
    // The stretch note does not survive the dock: the geometry it compared
    // against is the open panel's, which no longer exists.
    m_grownFrom = QRect();
    m_grownTo = QRect();

    setMinimumSize(0, 0);
    showPage(m_badge);

    // The dock goes back to where the panel came out of, not to its top-left: if
    // it was at the bottom and the panel opened upwards, folding must return it
    // there. With m_dockOffset at zero (no unfold yet) that is the top-left.
    const QSize dock = m_badge->sizeHint() + QSize(shadowMargin() * 2, shadowMargin() * 2);
    const QPoint inside(qBound(0, m_dockOffset.x(), qMax(0, panel.width() - dock.width())),
                        qBound(0, m_dockOffset.y(), qMax(0, panel.height() - dock.height())));
    setGeometry(QRect(clampInto(panel.topLeft() + inside, dock, placementArea()), dock));
    keepOnScreen();
}

void Panel::expand() {
    // The dock's geometry before touching anything: setMinimumSize() below
    // already stretches the window, after which this corner is no longer the
    // dock's.
    const QRect dock = geometry();

    // Opening the panel counts as acknowledging: the alarm is silenced.
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
    setMinimumSize(kShellMinWidth + shadowMargin() * 2, shellMinimumHeight());

    // A saved size larger than what fits (another monitor or resolution, a desktop
    // panel cutting the work area) is clamped first; otherwise the window manager
    // clamps it and moves the window too.
    QSize target = m_expandedSize;
    // With size per page it opens with the size of the page it was folded on (or
    // the list's, if that has none).
    bool hadPageSize = false;
    if (sizePerPage()) {
        QWidget *cur = m_body->currentWidget();
        const QSize stored = cur == m_scroll ? m_listSize
                                             : m_store.prefs().pageSizes.value(pageKey(cur));
        hadPageSize = stored.isValid();
        if (hadPageSize) target = stored;
        else if (m_listSize.isValid()) target = m_listSize;
    }
    target = target.expandedTo(minimumSize());
    if (const QRect area = placementArea(); area.isValid())
        target = target.boundedTo(area.size());

    // The panel comes out of the dock's top-left corner towards the bottom right,
    // which is where it was before folding; only when that does not fit (a dock
    // dragged against the right or bottom edge) does it open the other way.
    //
    // With size per page, first decide where the list would come out and place the
    // open page from there, as on a page switch (see m_pageHome).
    QRect from = QRect(anchoredTopLeft(dock, target), target);
    if (sizePerPage()) {
        QSize list = m_listSize.isValid() ? m_listSize : target;
        if (const QRect area = placementArea(); area.isValid()) list = list.boundedTo(area.size());
        m_pageHome = QRect(anchoredTopLeft(dock, list), list);
        from = m_pageHome;
    }
    const QPoint at = anchoredTopLeft(from, target);
    setGeometry(QRect(at, target));

    // Which point of the panel the dock came out of, to fold back through it. It
    // is what keeps a bottom-left dock bottom-left across open and close.
    m_dockOffset = QPoint(qBound(0, dock.x() - from.x(), qMax(0, from.width() - dock.width())),
                          qBound(0, dock.y() - from.y(), qMax(0, from.height() - dock.height())));

    keepOnScreen();
    // Folded with the planner open: collapse() gave back the narrow width before
    // saving it, and here it widens again.
    if (m_body->currentWidget() == m_planner && !hadPageSize) enterWide();
    if (sizePerPage()) {
        m_grownFrom = m_grownTo = m_narrowGeom = m_wideGeom = QRect();
        if (m_body->currentWidget() == m_scroll) m_pageHome = geometry();
        m_pagePlaced = geometry();
    }
}

/// Where the window manager accepts the window: the available screen, grown by
/// the shadow margin (not visible frame, it may hang off the screen), and cut
/// to the WM's work area. That cut is what counts: a position outside it does
/// not fail, the WM corrects it and the window jumps.
QRect Panel::placementArea(const QScreen *sc) const {
    if (!sc) sc = screen();
    if (!sc) return {};

    QRect area = sc->availableGeometry().adjusted(-shadowMargin(), -shadowMargin(),
                                                  shadowMargin(), shadowMargin());
    if (const QRect wm = wmWorkArea(); wm.isValid()) area &= wm;
    return area;
}

/// Where the window grows or shrinks from. Not "towards the screen centre":
/// that sent the dock to the far side of the monitor as soon as the panel
/// passed the middle. What decides is whether it fits: the top-left corner
/// stays, and the opposite edge is anchored only when it would leave the area.
QPoint Panel::anchoredTopLeft(const QRect &before, const QSize &after) const {
    const QRect area = placementArea();
    if (!area.isValid()) return before.topLeft();

    // The shadow margin is the same on both sides, so aligning the window edges
    // aligns the visible frame too.
    return {anchorAxis(before.left(), before.width(), after.width(),
                       area.left(), area.right() + 1),
            anchorAxis(before.top(), before.height(), after.height(),
                       area.top(), area.bottom() + 1)};
}

/// Safety net: after folding or unfolding the window must not be left outside
/// the placement area. anchoredTopLeft() already accounts for it, but a
/// restored size or a screen change may leave it hanging out. On Wayland
/// move() may do nothing.
void Panel::keepOnScreen() {
    const QRect area = placementArea();
    if (!area.isValid()) return;

    // qMin before qMax inside clampInto(): if the window does not fit it stays
    // anchored top-left instead of going off the other side.
    if (const QPoint p = clampInto(pos(), size(), area); p != pos()) move(p);
}

/// Back to where the window was left last time. Without this every start
/// (and rebooting is a start) lets the window manager decide. On Wayland this
/// does nothing; on X11 it applies.
void Panel::restoreWindowPos() {
    if (!m_store.prefs().hasWindowPos) return;
    const QPoint saved = m_store.prefs().windowPos;

    // The screen under the saved window, not the primary: with two monitors,
    // correcting against the primary would drag a window from the other one.
    // Looked up by the centre, since the corner's shadow margin may hang off
    // screen. If that screen is gone, placementArea() uses the current one and
    // the clamp brings the window back into view.
    const QScreen *sc = QGuiApplication::screenAt(QRect(saved, size()).center());
    const QRect area = placementArea(sc);
    move(area.isValid() ? clampInto(saved, size(), area) : saved);
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

void Panel::moveEvent(QMoveEvent *e) {
    QWidget::moveEvent(e);
    // Record the position as it changes. The destructor saves, but on shutdown
    // the session kills the process first; the Store's timer also merges the
    // stream of moves from a drag.
    if (isVisible()) scheduleSave();
}

void Panel::syncPrefs() {
    // Folded, the size that counts is the expanded one.
    const bool folded = m_stack && m_stack->currentWidget() == m_badge;
    // Widened for the planner, what counts is the size before: the list opens at
    // startup, not the planner.
    if (sizePerPage()) {
        // The window size saved is the list's, which opens at startup; the open
        // page's goes to its own slot.
        if (!folded && m_body) rememberPageSize(m_body->currentWidget());
        m_store.prefs().windowSize = m_listSize.isValid() ? m_listSize : size();
    } else {
        const bool wide = m_narrowGeom.isValid() && geometry() == m_wideGeom;
        m_store.prefs().windowSize = folded ? m_expandedSize : wide ? m_narrowGeom.size() : size();
    }

    // The position is saved as it is, folded or not: at startup the panel opens
    // from that corner, which is exactly what unfolding the dock does.
    m_store.prefs().windowPos = pos();
    m_store.prefs().hasWindowPos = true;
}

void Panel::scheduleSave() { m_store.scheduleSave(); }

void Panel::save() { m_store.save(); }
