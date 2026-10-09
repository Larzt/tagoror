#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <functional>

/// Where the shared copy lives: a folder (called Tagoror by convention) with
/// notes.json, birthdays.json, events.json and the audio/ and images/
/// subfolders. The backend only lists, downloads and uploads files there;
/// what to transfer and how to merge is SyncEngine's, the same for all of
/// them (see *Sync* in CLAUDE.md).
///
/// Everything is asynchronous, even where it need not be (the synced folder),
/// so the engine has one shape. On failure a callback gets ok = false and
/// error() says why, in the interface language.
class SyncBackend : public QObject {
    Q_OBJECT

public:
    /// One file there. @c hash changes whenever the content does (an MD5, an
    /// ETag); @c md5 is the content's MD5 when the backend knows it, which
    /// lets the engine compare without downloading.
    struct Remote {
        QString id;
        QString hash;
        QString md5;
    };
    using Listing = QHash<QString, Remote>;   ///< By file name.

    using QObject::QObject;

    /// Stable name, used in QSettings: "drive", "webdav", "folder".
    virtual QString id() const = 0;
    /// Configured and reachable enough to try a pass.
    virtual bool ready() const = 0;
    QString error() const { return m_error; }

    /// @p dir is "" for the root, or "audio" / "images". A missing folder is
    /// created and listed as empty.
    virtual void list(const QString &dir, std::function<void(bool, Listing)> done) = 0;
    virtual void download(const QString &dir, const QString &name, const Remote &remote,
                          std::function<void(bool, QByteArray)> done) = 0;
    /// @p existing is what the listing said is there (empty id: create it).
    /// Answers with the file as it now is; its hash may be empty if the
    /// backend cannot tell, and the next pass then reads it once more.
    virtual void upload(const QString &dir, const QString &name, const Remote &existing,
                        const QByteArray &data, std::function<void(bool, Remote)> done) = 0;

    /// QSettings group for what the engine remembers per backend (what was
    /// merged, when). Drive keeps the one it had before backends existed, so
    /// its users carry on without a full re-read.
    virtual QString settingsGroup() const { return "sync/" + id(); }

signals:
    /// Connection state changed (connected, credentials, folder): repaint.
    void changed();

protected:
    QString m_error;
};
