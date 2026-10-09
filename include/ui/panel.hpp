#pragma once

#include <QDate>
#include <QFont>
#include <QList>
#include <QRect>
#include <QSize>
#include <QWidget>

#include "core/store.hpp"
#include "ui/theme.hpp"

class QAction;
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
class AlertCenter;
class AlertZone;
class CommandPalette;
class MarkdownPage;
class AreaTabs;
class NavBar;
class QHBoxLayout;
class DriveSync;
class SyncController;
class Updater;
class BirthdayView;
class Navigator;
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
    /// Resizing from any edge: the cursor changes over the band around the
    /// visible frame, and a press there hands the gesture to the compositor.
    void mouseMoveEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *e) override;
    /// Catches presses on the band's inner side, which children cover.
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    /// @name Building the interface
    /// @{
    void buildShell();
    /// Which edges of the visible frame @p pos (in panel coordinates) is close
    /// enough to grab; none when folded, in app mode (the system frame does it)
    /// or maximised.
    Qt::Edges resizeEdgesAt(const QPoint &pos) const;
    QFrame *buildHeader();
    QWidget *buildTools();      ///< Area tabs, search and new note.
    QWidget *buildBody();
    QWidget *buildBadge();
    void installShortcuts();
    /// Bar along the bottom, or rail on the left from NavBar::kRailFrom.
    void updateNavLayout();
    /// @}

    /// @name Visual state
    /// @{
    void applyTheme();
    /// Changes the interface language and rewrites the whole interface.
    void setLanguage(Lang::Code code);
    void retranslate();
    void rebuildList();
    /// The navigation's active page, timer countdown and red dot.
    void refreshNav();
    QList<NoteCard *> cards() const;
    /// @}

    /// @name Notes
    /// @{
    void addNote(Note::Type type);
    void removeNote(Note *n);
    /// A Markdown note opens on its own page.
    void openMarkdown(Note *n);
    void dropMissingMarkdown();
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

    /// Opens a page. The pages and the window sizes live in Navigator.
    void openPage(QWidget *page);
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

    /// @name Timers
    /// @{
    void createTimer(const QString &name, qint64 ms);
    void toggleTimer(Timer *t);
    void resetTimer(Timer *t);
    void removeTimer(Timer *t);
    void onTimersChanged();         ///< Repaints everything that shows timers.
    void tickTimers();              ///< Every half second while something runs.
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
    /// @}

    /// @name Reminders
    /// @{
    void checkReminders();          ///< Anything due? Then ring and show it.
    void dismissNote(Note *n);      ///< The user stops the alarm.
    void snoozeNote(Note *n, int minutes);
    void rescheduleNote(Note *n);   ///< Its date changed while it was ringing.
    void refreshDueCards();         ///< Repaints the state without rebuilding the list.
    void applyBadgeAlert();         ///< The dock shows the alert even when folded.
    /// @}

    /// @name Menus and settings
    /// @{
    void openNewNoteMenu(QWidget *anchor);
    void openMoreMenu(QWidget *anchor);   ///< "⋯" in the header.
    void openPalette();                   ///< Ctrl+K; again closes it.
    void openAbout(QWidget *anchor);
    void refreshSettings();
    void openAccentEditor(QWidget *anchor);
    /// IBM Plex Sans, the system's or an installed one typed by name. @p notFound is
    /// the name just typed that matched nothing, to say so.
    void openFontEditor(QWidget *anchor, const QString &notFound = QString());
    /// Sets the chosen typeface on the whole application.
    void applyFont();
    void pickFont(const QString &pref);
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
    /// The alert zone: what rings, a missing folder, a failed sync, an update.
    void refreshNotices();
    void placeAlertZone();
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
    /// @}

    /// @name Search and folding
    /// @{
    void focusSearch();
    void closeSearch();
    void applyFilter(const QString &q);
    void collapse();
    /// Unfolding acknowledges whatever rings, then Navigator opens the panel.
    void expand();
    /// @}

    void scheduleSave();
    void save();

    Store m_store;
    Navigator *m_nav = nullptr;   ///< Pages and window geometry.

    QStackedWidget *m_stack = nullptr;   ///< Expanded panel / folded dock.
    QFrame *m_shell = nullptr;
    QWidget *m_badge = nullptr;
    QLabel *m_badgeCount = nullptr;
    QHBoxLayout *m_shellRow = nullptr;   ///< Rail, then the column.
    QVBoxLayout *m_mainCol = nullptr;    ///< Header, tools, body, bar.
    NavBar *m_navBar = nullptr;
    QWidget *m_bodyHost = nullptr;       ///< The pages, with the notices over them.
    AlertZone *m_alertZone = nullptr;
    CommandPalette *m_palette = nullptr;
    MarkdownPage *m_mdPage = nullptr;

    QStackedWidget *m_body = nullptr;    ///< Note list / pages.
    QScrollArea *m_scroll = nullptr;
    QWidget *m_listHost = nullptr;
    QVBoxLayout *m_listLayout = nullptr;   ///< Cards plus the trailing stretch.
    PlannerView *m_planner = nullptr;
    TimerView *m_timerView = nullptr;
    BirthdayView *m_birthdays = nullptr;
    SettingsView *m_settings = nullptr;

    QFrame *m_header = nullptr;
    QLabel *m_titleLabel = nullptr;
    QToolButton *m_backBtn = nullptr;      ///< Only on settings.
    QToolButton *m_moreBtn = nullptr;      ///< "⋯"
    QWidget *m_empty = nullptr;            ///< "No notes" placeholder.
    QLabel *m_emptyText = nullptr;
    QPushButton *m_emptyBtn = nullptr;
    QWidget *m_tools = nullptr;            ///< Tabs, search and new note.
    QLineEdit *m_search = nullptr;
    QToolButton *m_searchBtn = nullptr;
    QToolButton *m_addBtn = nullptr;
    QFont m_systemFont;   ///< The application font before any choice was applied.
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
    AlertCenter *m_alerts = nullptr;       ///< What rings, and the tone.
    Updater *m_updater = nullptr;
    SyncController *m_sync = nullptr;   ///< When to sync.
    DriveSync *m_drive = nullptr;       ///< m_sync's, for the settings and the calendar.
    QString m_latestUrl;              ///< Page of the latest release.
    Theme m_theme;
    /// Brought to front from the tray: the widget is out of the desktop layer
    /// until sent back. Not saved, it does not survive a restart.
    bool m_lifted = false;
};
