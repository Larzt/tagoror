#pragma once

#include <QDate>
#include <QList>
#include <QRect>
#include <QSize>
#include <QWidget>

#include "core/store.hpp"
#include "ui/theme.hpp"

class QFrame;
class QGraphicsDropShadowEffect;
class QLabel;
class QLineEdit;
class QMenu;
class QPushButton;
class QScreen;
class QScrollArea;
class QStackedWidget;
class QSystemTrayIcon;
class QTimer;
class QToolButton;
class QVBoxLayout;
class Alarm;
class AreaTabs;
class DriveSync;
class Updater;
class BirthdayView;
class NoteCard;
class PlannerView;
class SettingsView;
class TimerView;

/// The single window: draws its own chrome and presents what the Store keeps.
/// An outer stack swaps the expanded panel for the folded dock; inside it,
/// another swaps between the note list, the planner, the timers, the
/// birthdays and the settings.
class Panel : public QWidget {
    Q_OBJECT

public:
    Panel();
    ~Panel() override;

    /// Called by a second instance through the socket: instead of opening another
    /// panel, this one is unfolded and raised.
    void bringToFront();

protected:
    /// Closing hides to the tray instead of quitting. Without a tray it quits, or
    /// there would be no way to get the window back.
    void closeEvent(QCloseEvent *e) override;
    /// Asks again on every map for the window to be kept out of the taskbar:
    /// changing flags destroys the native window and the property with it.
    void showEvent(QShowEvent *e) override;
    /// Saves the position as it changes: on shutdown the session kills the
    /// process and the destructor's save never happens.
    void moveEvent(QMoveEvent *e) override;
    /// Escape goes back to the notes from any other page.
    void keyPressEvent(QKeyEvent *e) override;

private:
    /// @name Building the interface
    /// @{
    void buildShell();
    QFrame *buildHeader();
    QWidget *buildBody();
    QFrame *buildFooter();
    QWidget *buildBadge();
    /// @}

    /// @name Visual state
    /// @{
    void applyTheme();
    /// Changes the interface language and rewrites the whole interface.
    void setLanguage(Lang::Code code);
    void retranslate();
    void rebuildList();
    void refreshFooter();
    void refreshFooterHint();
    QList<NoteCard *> cards() const;
    /// @}

    /// @name Notes
    /// @{
    void addNote(Note::Type type);
    void removeNote(Note *n);
    /// @}

    /// @name Workspace areas
    /// The list shows only the open area's notes; the tab strip switches between
    /// them. See area.hpp.
    /// @{
    QString currentArea() const;
    void switchArea(const QString &id);
    void refreshAreaTabs();
    void newArea();
    void openAreaMenu(const QString &id, const QPoint &globalPos);
    void openAreaOverflow(const QPoint &globalPos);
    /// Asks inside a menu what to do with its notes: by default they move to
    /// another area; deleting them is an explicit option.
    void confirmDeleteArea(const QString &id);
    void openMoveNoteMenu(Note *n, const QPoint &globalPos);
    void moveNoteToArea(Note *n, const QString &areaId);
    /// @}

    /// @name Reordering
    /// The screen holds the order and the Store copies it: dragging moves the
    /// card within the layout and the saved list comes from there, so there are
    /// never two ideas of the order that could disagree.
    /// @{
    void moveNote(Note *n, int steps);        ///< One step, from the menu.
    void beginCardDrag(NoteCard *card);
    void dragCardTo(NoteCard *card, const QPoint &globalPos);
    void endCardDrag(NoteCard *card);
    void commitOrder();
    /// @}

    /// A body page with its header button and its name. Opening, closing,
    /// lighting the button and setting the title all walk pages().
    struct Page {
        QWidget *page;
        QToolButton *button;
        const char *name;    ///< Spanish; translated when used.
    };
    QList<Page> pages() const;
    void togglePage(QWidget *page);
    /// Lights a page's button on or off. The icon says where it leads; the
    /// highlight says where you are.
    void setPageActive(QToolButton *button, bool on,
                       const QString &tipOn, const QString &tipOff);
    void refreshPageButtons();
    /// The header title names the page that is open.
    void refreshTitle();
    void showNotes();
    void createReminder(const QString &title, const QDateTime &when);
    void revealNote(Note *n);       ///< From the planner to its card in the list.
    void refreshPlanner();
    /// Deletes a planner category. For a Google calendar this is unfollowing it,
    /// which removes its events (and stops whatever of them was ringing).
    void removeCategory(Event::Category *c);
    /// Which Google calendar new Tagoror events go to.
    void openCalendarTarget(QWidget *anchor);

    /// The planner needs width: the window widens on entering and returns to its
    /// previous size on leaving, unless the user resized it meanwhile (the same
    /// rule as m_grownFrom for the height).
    void enterWide();
    void leaveWide();

    /// Size per page (Prefs::sizePerPage): leaving a page records the window
    /// size, entering one applies its own; the list keeps m_listSize. With this
    /// on, the m_grownFrom and m_narrowGeom notes are dropped.
    bool sizePerPage() const;
    QString pageKey(const QWidget *page) const;   ///< Empty for the list.
    void rememberPageSize(QWidget *page);
    /// @return false if the page has no stored size yet.
    bool applyPageSize(QWidget *page);
    /// Updates m_pageHome when leaving a page: if the user moved it, the list
    /// moves with it; otherwise it stays.
    void syncPageHome(QWidget *from);
    /// The whole page switch, with or without size per page.
    void switchBodyPage(QWidget *page);

    /// @name Timers
    /// @{
    void createTimer(const QString &name, qint64 ms);
    void toggleTimer(Timer *t);
    void resetTimer(Timer *t);
    void removeTimer(Timer *t);
    void onTimersChanged();         ///< Repaints everything that shows timers.
    void tickTimers();              ///< Every half second while something runs.
    void refreshFooterTimer();
    /// The red "time's up" / "starting now" strip: timers and events have no card
    /// with a stop button, so their alarm lives here. Stop silences all of it.
    void refreshAlarmBar();
    void stopBarAlarms();
    void silenceEvent(Event *e);
    /// @}

    /// @name Birthdays
    /// @{
    void refreshBirthdays();
    /// Adding and editing share one popup: a null @p b is a new one.
    void openBirthdayEditor(Birthday *b, QWidget *anchor);
    void askBirthdayReminder(Birthday *b, QWidget *anchor);
    void removeBirthday(Birthday *b);
    void toggleGreeted(Birthday *b);
    void dismissBirthday(Birthday *b);
    /// Silences a birthday alarm; the year is recorded so it does not ring again
    /// until the next.
    void silenceBirthday(Birthday *b);
    /// @}

    /// @name Reminders
    /// @{
    void checkReminders();          ///< Anything due? Then ring and show it.
    void dismissNote(Note *n);      ///< The user stops the alarm.
    void rescheduleNote(Note *n);   ///< Its date changed while it was ringing.
    /// Silences a reminder: repeating ones are not marked as fired, they move to
    /// their next turn. Shared by the stop button and opening the panel.
    void silence(Note *n);
    void refreshDueCards();         ///< Repaints the state without rebuilding the list.
    void applyBadgeAlert();         ///< The dock shows the alert even when folded.
    bool anyRinging() const;
    /// @}

    /// @name Menus and settings
    /// @{
    void openNewNoteMenu(QWidget *anchor);
    void refreshSettings();
    void openAccentEditor(QWidget *anchor);
    void chooseDataFolder();
    /// Picking a folder that already holds notes is ambiguous ("take mine there"
    /// or "open those"), so it asks: assuming the first overwrites the
    /// destination.
    void confirmDataFolder(const QString &to);
    void openBackups(QWidget *anchor);            ///< The backups menu.
    static QString backupPeriodLabel(int days);   ///< "Every day", "Every 3 days"…
    void pollBackup();                            ///< Is a scheduled backup due?
    /// @}

    /// @name Updates
    /// @{
    void pollUpdates();               ///< Due for a check? Once a day.
    void checkUpdatesNow();           ///< The settings button.
    void onUpdateChecked(const QString &version, const QString &url, const QString &error);
    void openLatestRelease();
    /// Shows or hides the "new version available" strip.
    void refreshUpdateBanner();
    bool updateAvailable() const;
    void confirmRestore(const QString &file);
    /// @}

    /// @name Data folder
    /// @{

    /// Checks again whether the configured folder has appeared: a USB stick is
    /// mounted when its owner opens it, long after login.
    void pollDataDir();
    /// The lists and preferences were replaced after recovering the folder.
    void onStoreReloaded();
    /// Changes arrived from another machine (Drive). Existing objects were
    /// updated in place; what changes is the list.
    void onStoreMerged();
    /// Whether the list may be rebuilt now: not while the user types in one of
    /// the panel's fields, or they would lose the cursor mid-sentence.
    bool userIdle() const;
    /// Shows or hides the "not saving" banner.
    void refreshDataWarning();
    /// @}

    /// @name System tray
    /// The widget lives there instead of in the taskbar: the icon is what is left
    /// when the window is hidden.
    /// @{
    void buildTray();
    void buildTrayMenu();           ///< Rebuilt wholesale on a language change.
    void toggleFromTray();
    /// Above every other window until sent back: the widget leaves the desktop
    /// layer, which a plain raise() cannot get it out of.
    void liftToFront();
    void sendToBack();
    /// @}

    /// @name Window
    /// @{
    void applyWindowFlags();        ///< Always on top or on the desktop.
    void setOnTop(bool on);         ///< Prefs::onTop, from settings or the tray.
    /// Frameless widget or ordinary desktop window (Prefs::appMode). In app mode
    /// there is no shadow margin and no dock: "–" minimises.
    void setAppMode(bool on);
    void applyAppModeChrome();      ///< Margins, shadow, frame and the "–" button.
    bool appMode() const { return m_store.prefs().appMode; }
    int shadowMargin() const;
    /// From the tray or a second instance: also when minimised.
    void showRestored();
    void keepOnScreen();            ///< Keeps folding/unfolding from pushing it off screen.
    /// Puts the window back where it was saved. Without this it reappears
    /// wherever the window manager puts it.
    void restoreWindowPos();
    /// Where the window manager lets the window be, which is not the whole screen
    /// (see the definition). Without @p sc the window's own screen is used.
    QRect placementArea(const QScreen *sc = nullptr) const;
    /// The corner the window grows or shrinks from: it keeps the top-left unless
    /// growing from there would leave the placement area.
    QPoint anchoredTopLeft(const QRect &before, const QSize &after) const;
    /// @}

    /// @name Search and folding
    /// @{
    void toggleSearch();
    void applyFilter(const QString &q);
    void collapse();
    void expand();
    void showPage(QWidget *page);   ///< Sizes the window to the visible page.
    /// Same for the body pages: the hidden one must not impose its minimum on
    /// the visible one.
    void showBodyPage(QWidget *page);
    /// The minimum height the visible page's layout needs, and applying it. Not a
    /// constant: the planner needs more than the list.
    int shellMinimumHeight() const;
    void syncShellMinimum();
    /// @}

    /// Copies what only the panel knows (the window geometry) into the
    /// preferences; runs right before every save.
    void syncPrefs();
    void scheduleSave();
    void save();

    Store m_store;

    QStackedWidget *m_stack = nullptr;   ///< Expanded panel / folded dock.
    QFrame *m_shell = nullptr;
    QWidget *m_badge = nullptr;
    QLabel *m_badgeCount = nullptr;
    /// Red banner under the header: the notes folder is missing and nothing
    /// typed will be saved. Without it the panel opened empty without saying why.
    QLabel *m_dataWarn = nullptr;
    /// Strip under the header when a new version exists. It goes below the
    /// missing-folder warning, which outranks it.
    QWidget *m_updateBar = nullptr;
    QLabel *m_updateText = nullptr;

    /// Alarm of a ringing timer or event (see refreshAlarmBar()).
    QWidget *m_alarmBar = nullptr;
    QLabel *m_alarmText = nullptr;
    QToolButton *m_alarmPlus = nullptr;

    QStackedWidget *m_body = nullptr;    ///< Note list / pages.
    QScrollArea *m_scroll = nullptr;
    QWidget *m_listHost = nullptr;
    QVBoxLayout *m_listLayout = nullptr;   ///< Cards plus the trailing stretch.
    PlannerView *m_planner = nullptr;
    TimerView *m_timerView = nullptr;
    BirthdayView *m_birthdays = nullptr;
    SettingsView *m_settings = nullptr;

    QLabel *m_titleLabel = nullptr;
    QWidget *m_empty = nullptr;            ///< "No notes" placeholder.
    QLabel *m_emptyText = nullptr;
    QPushButton *m_emptyBtn = nullptr;
    QLabel *m_footerHint = nullptr;
    QWidget *m_searchBar = nullptr;
    QLineEdit *m_search = nullptr;
    QLabel *m_footerText = nullptr;
    QToolButton *m_calendarBtn = nullptr;
    QToolButton *m_timersBtn = nullptr;
    QToolButton *m_footerTimer = nullptr;   ///< Countdown in the footer.
    QToolButton *m_birthdayBtn = nullptr;
    QToolButton *m_settingsBtn = nullptr;
    QList<QToolButton *> m_headerButtons;
    QToolButton *m_minBtn = nullptr;
    QVBoxLayout *m_outer = nullptr;
    QGraphicsDropShadowEffect *m_shellShadow = nullptr;

    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_trayMenu = nullptr;

    NoteCard *m_dragCard = nullptr;        ///< Card being dragged.
    /// Area whose tab the dragged card is over: on release the note moves there
    /// instead of being reordered.
    QString m_dropArea;
    AreaTabs *m_areaTabs = nullptr;
    QTimer *m_dueTimer = nullptr;          ///< Reminder heartbeat.
    QTimer *m_tick = nullptr;              ///< Running timers.
    Alarm *m_alarm = nullptr;
    Updater *m_updater = nullptr;
    /// Google Drive sync. Runs a while after each save, so a burst of typing is
    /// one upload and not fifty.
    DriveSync *m_drive = nullptr;
    QTimer *m_driveTimer = nullptr;
    /// How often to check whether another machine changed something. Checking
    /// lists a folder; only what changed is downloaded.
    QTimer *m_drivePoll = nullptr;
    QString m_latestUrl;              ///< Page of the latest release.
    Theme m_theme;
    QSize m_expandedSize;                  ///< Restored when unfolding (and saved).
    QSize m_listSize;                      ///< The list's, with size per page.
    QPoint m_dockOffset;                   ///< The point of the panel the dock goes in and out through.
    bool m_posRestored = false;            ///< The saved position is restored only on map.
    /// Brought to front from the tray: the widget is out of the desktop layer
    /// until sent back. Not saved, it does not survive a restart.
    bool m_lifted = false;
    /// The geometry before the window was stretched to fit a page's minimum, and
    /// the one it was left at. Leaving restores the first, but only if the second
    /// is still the current geometry: once the user resizes, the size is theirs.
    QRect m_grownFrom;
    QRect m_grownTo;
    /// The same for the planner's width (see enterWide()).
    QRect m_narrowGeom;
    QRect m_wideGeom;
    /// With size per page, every page is placed from the list's geometry
    /// (m_pageHome), never from the page being left: otherwise a large page that
    /// opened leftwards or upwards dragged the corner along. m_pagePlaced is
    /// where the open page was placed, to tell whether the user moved it.
    QRect m_pageHome;
    QRect m_pagePlaced;
};
