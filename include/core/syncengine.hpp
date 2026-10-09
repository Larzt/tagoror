#pragma once

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <functional>

#include "core/syncbackend.hpp"

class Store;

/// A sync pass, whatever the backend (5.0). It used to be DriveSync's own
/// code; it moved here as it was, with the Drive calls behind SyncBackend.
///
/// A pass downloads the JSON files that changed since the last merge, merges
/// them element by element with the local data (Store::mergeRemote()), runs
/// the middle step (Google Calendar), uploads the shared payload of what now
/// differs, and moves the attachments the notes use. A whole file never
/// replaces another. If two machines upload at once, the overwritten one
/// still has its data and puts it back on the next pass.
class SyncEngine : public QObject {
    Q_OBJECT

public:
    enum State { Idle, Syncing, Failed };

    explicit SyncEngine(Store *store, QObject *parent = nullptr);

    /// Null: no file sync. Switching drops any pass in progress.
    void setBackend(SyncBackend *backend);
    SyncBackend *backend() const { return m_backend; }

    /// Asked before merging: merging rebuilds the list and would steal the
    /// cursor from a user who is typing. When false the pass is dropped and
    /// deferred() is emitted.
    std::function<bool()> canApply;
    /// Runs between the merge and the upload, so what it brings reaches the
    /// other machines in the same pass (Google Calendar). Must call its
    /// argument when done, failed or not.
    std::function<void(std::function<void()>)> middleStep;

    State state() const { return m_state; }
    QString lastError() const { return m_error; }
    QDateTime lastSync() const { return m_lastSync; }

    /// A full pass. A request during a pass is remembered and runs when it ends.
    void syncNow();

signals:
    void changed();
    /// Attachments arrived from another machine: cards built without them must
    /// be rebuilt.
    void attachmentsArrived();
    /// The merge was skipped because the user was typing.
    void deferred();

private:
    struct Transfer {
        bool download = false;
        QString dir;          ///< "" for the root.
        QString name;
        QString path;         ///< On disk.
        SyncBackend::Remote remote;
        QByteArray data;      ///< Upload: when not empty, sent instead of the file.
    };

    void setState(State s);
    void fail(const QString &why);
    void fetchRemoteJson(int index);
    void mergeAndPlan();
    void planUploads();
    void planAttachments(int index);
    void transferNext();
    void finishSync();

    /// @name What was merged, per backend (QSettings, backend's group)
    /// @{
    QString seen(const QString &key) const;
    void setSeen(const QString &key, const QString &value);
    /// @}

    Store *m_store;
    SyncBackend *m_backend = nullptr;
    State m_state = Idle;
    QString m_error;
    QDateTime m_lastSync;
    int m_pass = 0;                        ///< Replies from an older pass are dropped.

    QList<Transfer> m_queue;
    bool m_again = false;
    bool m_gotFiles = false;
    SyncBackend::Listing m_root;
    QList<QJsonObject> m_remoteJson;       ///< notes, birthdays, events (empty = nothing new).
    QHash<QString, QString> m_remoteMd5;   ///< Content MD5 of each JSON there, when known.
    QSet<QString> m_pull;                  ///< Attachments that must be downloaded regardless.
};
