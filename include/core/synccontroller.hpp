#pragma once

#include <QObject>
#include <QString>
#include <functional>

class DriveSync;
class FolderBackend;
class QTimer;
class Store;
class SyncBackend;
class SyncEngine;
class WebDavBackend;

/// Which backend syncs the notes, and when (5.0). The pass is SyncEngine's;
/// the backends are Google Drive, WebDAV and a synced folder; this picks one,
/// runs passes on the chosen rhythm and keeps Google Calendar going.
///
/// Calendar is a connection of its own, independent of where the notes go:
/// it rides the Google account, and runs as the middle step of every pass
/// whatever the backend — or alone, on the same rhythm, with none. Never
/// with the data folder missing: what is in memory then is not the notes.
class SyncController : public QObject {
    Q_OBJECT

public:
    /// When passes run besides the button: a while after each save (and every
    /// 90 s, to pick up other machines' changes), every five minutes, or only
    /// by hand.
    enum When { OnSave, Every5, Manual };

    SyncController(Store *store, QObject *parent = nullptr);

    DriveSync *drive() const { return m_drive; }
    WebDavBackend *webdav() const { return m_webdav; }
    FolderBackend *folder() const { return m_folder; }
    SyncEngine *engine() const { return m_engine; }

    /// "", "drive", "webdav" or "folder".
    QString provider() const { return m_provider; }
    void setProvider(const QString &id);
    When when() const { return m_when; }
    void setWhen(When when);

    /// Whether a merge may happen now; false while the user types.
    void setCanApply(std::function<bool()> canApply);

    /// A pass in a little while: after startup, after a change.
    void syncSoon();
    /// A pass right away (the *Sincronizar* button), the delayed one dropped.
    void syncNow();
    /// Whether a pass would do anything: a backend chosen and ready, or
    /// Calendar on its own.
    bool active() const;

signals:
    /// Anything shown in settings changed: provider, state, a backend.
    void changed();

private:
    SyncBackend *backendFor(const QString &id) const;
    bool calendarOn() const;
    /// Calendar with no file backend: on the same rhythm, alone.
    void runCalendarAlone();
    void applyWhen();

    Store *m_store;
    DriveSync *m_drive;
    WebDavBackend *m_webdav;
    FolderBackend *m_folder;
    SyncEngine *m_engine;
    QString m_provider;
    When m_when = OnSave;
    bool m_calendarRunning = false;
    QTimer *m_soon;   ///< 20 s single shot, restarted by every save.
    QTimer *m_poll;   ///< 90 s or 5 min: listing a folder is cheap, only changes are downloaded.
};
