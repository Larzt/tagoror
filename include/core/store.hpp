#pragma once

#include <QByteArray>
#include <QColor>
#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QList>
#include <QObject>
#include <QPoint>
#include <QSize>
#include <QString>
#include <QStringList>
#include <functional>

#include "core/area.hpp"
#include "core/birthday.hpp"
#include "core/event.hpp"
#include "core/lang.hpp"
#include "core/note.hpp"
#include "core/timer.hpp"

class QJsonObject;
class QTimer;

/// Everything that outlives the process: notes, birthdays, events, timers,
/// areas and the preferences that travel with them. Kept apart from Panel so
/// the window knows nothing about JSON, QSettings or migrations.
class Store : public QObject {
    Q_OBJECT

public:
    /// Preferences stored in notes.json. Accent and opacity are data, not a
    /// Theme: `core` must not depend on `ui`.
    struct Prefs {
        QColor accent{"#7c9cff"};
        int opacity = 96;              ///< 40..100
        QSize windowSize;              ///< Size of the expanded panel (the note list's, with per-page sizes).
        /// Where the window was left. The flag is needed because (0,0) is a valid
        /// corner, so a null QPoint cannot mean "nothing stored".
        QPoint windowPos;
        bool hasWindowPos = false;
        QByteArray input;              ///< Microphone chosen in settings.
        bool onTop = false;            ///< Off by default: the widget lives on the desktop.
        /// App mode: an ordinary decorated window in the taskbar instead of the
        /// frameless desktop widget. Per machine.
        bool appMode = false;
        /// Each page remembers its own window size and the list keeps windowSize.
        /// Off, the window has a single size. Per machine, never synced.
        bool sizePerPage = true;
        QHash<QString, QSize> pageSizes;   ///< Keyed "planner", "timers", "birthdays", "settings".
        /// The open area. Per machine, never synced.
        QString activeArea;
        Lang::Code lang = Lang::Es;    ///< Interface language.
        /// Birthdays page order: by time left (default) or January to December.
        /// An interface preference, so it lives here and not in birthdays.json.
        bool birthdaysByMonth = false;

        /// Note text size in percent. Only content scales: growing fixed-width
        /// chrome labels would clip the whole list (see *Card widths* in CLAUDE.md).
        int textScale = 100;

        /// Planner view (0 day, 1 week, 2 month) and hidden categories.
        int plannerView = 2;
        QStringList plannerHidden;

        /// Daily update check. `latestSeen` is the last version the server reported,
        /// shown without asking again.
        bool updateCheck = true;
        qint64 lastUpdateMs = 0;
        QString latestSeen;

        /// Backup schedule: every N days (0 = manual only) at a given time. Stored
        /// in notes.json so it moves with the notes.
        int backupEveryDays = 1;
        QTime backupAt{3, 0};
        qint64 lastBackupMs = 0;       ///< When the last backup was taken.
    };

    explicit Store(QObject *parent = nullptr);
    ~Store() override;

    /// @name Notes
    /// Owned: freed in the destructor and in remove().
    /// @{
    const QList<Note *> &notes() const { return m_notes; }
    int count() const { return int(m_notes.size()); }
    void add(Note *n);                 ///< Adds at the top.
    void remove(Note *n);              ///< Deletes its attachments too.
    /// Reorders the notes to @p order, which must be a permutation of exactly the
    /// current notes. The panel passes the on-screen order after a drag.
    void setOrder(const QList<Note *> &order);
    /// @}

    /// @name Workspace areas
    /// There is always at least one: the default area is recreated when none is
    /// left. Sorted by Area::pos.
    /// @{
    const QList<Area *> &areas() const { return m_areas; }
    Area *area(const QString &id) const;
    /// The area a note is shown in. A note with no area, or whose area no longer
    /// exists (deleted on another machine), falls in the first one instead of
    /// becoming invisible.
    QString areaOf(const Note *n) const;
    QList<Note *> notesIn(const QString &areaId) const;
    Area *addArea(const QString &name);           ///< Appended at the end of the strip.
    /// Deletes the area. With @p moveTo its notes go there; empty, they are
    /// deleted with it (attachments included). The last area cannot be deleted.
    void removeArea(Area *a, const QString &moveTo);
    void moveArea(Area *a, int steps);            ///< One place left or right.
    void setNoteArea(Note *n, const QString &areaId);
    /// @}

    /// @name Timers
    /// Stored inside notes.json: few, constantly changing, not an agenda that
    /// needs restoring separately.
    /// @{
    const QList<Timer *> &timers() const { return m_timers; }
    void addTimer(Timer *t);           ///< Adds at the top.
    void removeTimer(Timer *t);
    /// @}

    /// @name Planner
    /// Own file (events.json), own backups with the same timestamp, and the same
    /// lock when the file exists but cannot be read.
    /// @{
    const QList<Event *> &events() const { return m_events; }
    /// With @p save false nothing is written yet: Google Calendar brings hundreds
    /// at once on the first pass and saves once at the end.
    void addEvent(Event *e, bool save = true);
    void removeEvent(Event *e, bool save = true);
    /// The user's categories (built-in ones are never stored); synced like
    /// events.
    const QList<Event::Category *> &categories() const { return m_categories; }
    void addCategory(Event::Category *c);
    /// Its events move to Event::kFallbackCategory, except those of a Google
    /// calendar ("gcal:…"), which are removed; they stay in Google.
    void removeCategory(Event::Category *c);
    /// Whether events.json was read; without it nothing syncs events.
    bool eventsReadable() const { return m_eventsReadable; }
    /// When that element ("e:<id>") was deleted, or 0.
    qint64 deletedAt(const QString &key) const { return m_deleted.value(key); }
    /// @}

    /// @name Birthdays
    /// Kept in birthdays.json, next to notes.json (see birthday.hpp).
    /// @{
    const QList<Birthday *> &birthdays() const { return m_birthdays; }
    int birthdayCount() const { return int(m_birthdays.size()); }
    void addBirthday(Birthday *b);
    void removeBirthday(Birthday *b);

    Prefs &prefs() { return m_prefs; }
    const Prefs &prefs() const { return m_prefs; }
    /// @}

    /// Hook to refresh what only the owner knows (the window size) right before
    /// writing. Must be cleared before the owner dies: Store saves once more in
    /// its destructor.
    std::function<void()> beforeSave;

    /// A backup on disk, as offered to the user.
    struct Backup {
        QString path;
        QDateTime when;   ///< Invalid if the file name carries no recognisable date.
        int notes = 0;
    };

    QString path() const;              ///< notes.json
    QString birthdaysPath() const;     ///< birthdays.json, next to it.
    QString eventsPath() const;        ///< events.json, next to it.
    void load();                       ///< Seeds two demo notes when there is no file.
    void save();

    /// Backups kept. Public because settings prints it.
    static constexpr int kBackupsKept = 10;

    /// When the next scheduled backup is due; invalid if manual only.
    QDateTime nextBackupDue() const;
    /// Takes a backup if one is due. Called at startup and on the panel's
    /// heartbeat, so a 03:00 backup missed while closed happens on launch.
    bool backupIfDue();
    /// Takes one now, unconditionally (the button, and the step before a
    /// restore).
    bool makeBackup();

    /// Existing backups, newest first.
    QList<Backup> backups() const;
    /// Restores one. The current state is backed up first, so restoring the
    /// wrong copy is never the mistake there is no coming back from.
    bool restoreBackup(const QString &file);
    void scheduleSave();               ///< Debounces bursts of typing (600 ms).

    /// Whether the configured folder was there when read. When false the Store
    /// is read-only and writes nothing at all (see save()).
    bool available() const { return m_available; }

    /// Retries the load if the folder has appeared (a drive mounted later).
    /// @return true the time it succeeds; what was typed meanwhile is kept.
    bool retryLoad();

    /// Moves the data folder, copying the attachments and leaving the originals
    /// in place, so a failure halfway destroys nothing. Overwrites any notes.json
    /// at the destination: "take my notes there".
    void changeDataDir(const QString &to);

    /// The other half: point at a folder that already has notes and keep those,
    /// writing nothing there until it has been read.
    void adoptDataDir(const QString &to);

    /// @name Drive sync
    /// @{

    /// Result of mergeRemote().
    struct MergeResult {
        bool changed = false;
        /// Attachments ("audio/x.wav", "images/y.png") of notes whose winning
        /// version came from outside: downloaded even if a local file exists.
        QSet<QString> pull;
    };
    MergeResult mergeRemote(const QJsonObject &notesRoot, const QJsonObject &birthdaysRoot,
                            const QJsonObject &eventsRoot);
    /// Attachments used by the notes, as "audio/<name>" or "images/<name>".
    QStringList attachments() const;
    /// JSON files that may be uploaded. One that existed but could not be read is
    /// excluded: uploading it would spread the damage.
    QStringList syncableFiles() const;
    /// What is uploaded under @p name: only shared data (elements, tombstones,
    /// order), without this machine's preferences, always serialised the same way.
    /// Uploading the local notes.json made two idle machines overwrite each other
    /// forever, since each stores its own window geometry.
    /// @}
    QByteArray syncPayload(const QString &name) const;

signals:
    /// The lists and preferences were replaced wholesale (retryLoad,
    /// adoptDataDir): views must rebuild.
    void reloaded();
    /// mergeRemote() brought changes. Existing objects were updated in place,
    /// so pointers held by views stay valid.
    void merged();
    /// notes.json (and its siblings) were just written. The Drive sync hangs off
    /// this.
    void saved();

private:
    /// Resolves the data folder before any disk access: the override wins, and
    /// settings from legacy names are inherited.
    void resolveDataDir();
    /// Reads the file into memory.
    /// @return false if it could not be opened, touching nothing: the file that
    ///         cannot be read is exactly the one that must not be overwritten.
    bool readFile();
    bool readObject(const QJsonObject &root);
    /// Birthdays live in their own file. A missing file is normal (none yet, or
    /// data from when they lived in notes.json); only a file that exists and
    /// cannot be read blocks writing (see m_birthdaysReadable).
    void loadBirthdays();
    void saveBirthdays();
    /// Same for the planner, with its own lock.
    void loadEvents();
    void saveEvents();
    void seedDemoNotes();
    /// Guarantees at least one area and keeps the list sorted by pos.
    void ensureAreas();
    void sortAreas();

    /// Stamps updatedMs on whatever changed since the last snapshot and records
    /// as deleted whatever disappeared. Runs at the start of every save().
    void stampChanges();
    /// Takes the current state as the baseline without stamping anything.
    void resetSnapshots();

    /// Writes the whole file or leaves it untouched (QSaveFile: temp file, then
    /// rename). A truncating write interrupted halfway leaves a JSON that does
    /// not parse, which is every note, not the last few.
    bool writeAtomic(const QString &file, const QByteArray &data);
    /// Sets the file on disk aside as a backup. The schedule decides when
    /// (backupIfDue), not save(), which runs every 600 ms while typing.
    bool copyToBackup(const QString &name);
    void pruneBackups();

    QList<Note *> m_notes;
    QList<Area *> m_areas;
    QList<Birthday *> m_birthdays;
    QList<Timer *> m_timers;
    QList<Event *> m_events;
    QList<Event::Category *> m_categories;
    Prefs m_prefs;
    QTimer *m_saveTimer = nullptr;
    bool m_available = true;
    /// A birthdays.json existed and could not be read: it is never overwritten,
    /// since what is in memory is not the user's data.
    bool m_birthdaysReadable = true;
    bool m_eventsReadable = true;

    /// Each element's JSON (without its timestamp) as last seen, keyed
    /// "<type>:<id>", to detect changes nobody reported.
    QHash<QString, QByteArray> m_snap;
    QStringList m_orderSnap;
    qint64 m_orderUpdatedMs = 0;   ///< The note order is synced too.
    /// Tombstones: "<type>:<id>" of deleted elements -> when. Without them a
    /// deletion would come back from the other machine. Stored in notes.json.
    QHash<QString, qint64> m_deleted;
};
