#pragma once

#include <QObject>
#include <functional>

class DriveSync;
class QTimer;
class Store;

/// When to sync, as opposed to how (DriveSync). Split out of Panel, which
/// owned the timers: a pass a while after each save (so a burst of typing is
/// one upload and not fifty), a poll for what other machines changed, and a
/// retry when a pass had to wait for the user to stop typing. Never with the
/// data folder missing: what is in memory then is not the notes.
class SyncController : public QObject {
    Q_OBJECT

public:
    SyncController(Store *store, QObject *parent = nullptr);

    DriveSync *drive() const { return m_drive; }

    /// Whether a merge may happen now; false while the user types. See
    /// DriveSync::canApply.
    void setCanApply(std::function<bool()> canApply);

    /// A pass in a little while: after startup, after a change.
    void syncSoon();
    /// A pass right away (the *Sincronizar* button), the delayed one dropped.
    void syncNow();

private:
    Store *m_store;
    DriveSync *m_drive;
    QTimer *m_soon;   ///< 20 s single shot, restarted by every save.
    QTimer *m_poll;   ///< 90 s: listing a folder is cheap, only changes are downloaded.
};
