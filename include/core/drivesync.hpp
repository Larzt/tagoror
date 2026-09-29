#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>
#include <functional>

class Store;
class CalendarSync;
class QNetworkAccessManager;
class QNetworkReply;
class QTcpServer;
class QTcpSocket;

/// Syncs the data between machines through a "Tagoror" folder in the user's
/// Google Drive.
///
/// A pass downloads what is there, merges it element by element with the
/// local data (Store::mergeRemote()), saves and uploads the result. A whole
/// file never replaces another, which is how one machine would overwrite the
/// other. If two machines upload at once, the overwritten one still has its
/// data locally and puts it back on the next pass.
///
/// Attachments follow their notes, and only what differs (by MD5) is
/// transferred. Authorisation is Google's desktop flow: loopback redirect plus
/// PKCE, with the narrow `drive.file` scope. The OAuth client comes from CMake
/// (TAGOROR_GOOGLE_CLIENT_ID/_SECRET); without it configured() is false. The
/// refresh token is kept in QSettings, not in notes.json, so it never travels
/// with the data folder or into backups.
class DriveSync : public QObject {
    Q_OBJECT

public:
    enum State {
        Unavailable,    ///< Built without a client ID.
        Disconnected,
        Authorizing,    ///< Waiting for the browser.
        Idle,           ///< Connected.
        Syncing,
        Failed,         ///< Connected, but the last pass failed.
    };

    explicit DriveSync(Store *store, QObject *parent = nullptr);

    /// Asked before merging: merging rebuilds the list and would steal the
    /// cursor from a user who is typing. When false the pass is dropped and
    /// deferred() is emitted.
    std::function<bool()> canApply;

    static bool configured();

    State state() const { return m_state; }
    bool connected() const { return !m_refresh.isEmpty(); }
    QString account() const { return m_account; }
    QDateTime lastSync() const { return m_lastSync; }
    QString lastError() const { return m_error; }

    /// Connects (or reconnects) the account. If Google Calendar is enabled on
    /// this machine, its scopes are requested too.
    void connectAccount();
    void cancel();
    void disconnectAccount();
    /// A full pass: download, merge, upload. A request during a pass is
    /// remembered and runs when it ends.
    void syncNow();

    /// Google Calendar: one more step of every pass, with this same account.
    CalendarSync *calendar() { return m_calendar; }
    const CalendarSync *calendar() const { return m_calendar; }
    /// Whether Google granted the calendar scopes. They can be unticked on the
    /// consent screen, and accounts connected before this lack them.
    bool hasCalendarScope() const;

    /// Google endpoints. Configurable only to test against a local mock.
    struct Endpoints {
        QString auth = "https://accounts.google.com/o/oauth2/v2/auth";
        QString token = "https://oauth2.googleapis.com/token";
        QString revoke = "https://oauth2.googleapis.com/revoke";
        QString api = "https://www.googleapis.com/drive/v3";
        QString upload = "https://www.googleapis.com/upload/drive/v3";
        QString calendar = "https://www.googleapis.com/calendar/v3";
    };
    static Endpoints &endpoints();

signals:
    /// Asks the panel to open @p url in the browser (core knows no desktop).
    void openUrl(const QUrl &url);
    void changed();
    /// Attachments arrived from another machine: cards built without them must
    /// be rebuilt.
    void attachmentsArrived();
    /// The merge was skipped because the user was typing.
    void deferred();

private:
    friend class CalendarSync;   // uses api() and this account's token

    struct Transfer {
        bool download = false;
        QString path;       ///< On disk.
        QString name;       ///< In Drive.
        QString folderId;
        QString fileId;     ///< Upload: empty means create it.
        QByteArray data;    ///< Upload: when not empty, sent instead of the file.
    };
    struct Remote {
        QString id;
        QString md5;
    };
    using Done = std::function<void(QNetworkReply *)>;

    void setState(State s);
    void fail(const QString &why);
    void onRedirect();
    void answerBrowser(QTcpSocket *socket, const QByteArray &request);
    void exchangeCode(const QString &code);
    void fetchAccount();
    void saveSettings();

    /// Obtains (or refreshes) the access token, then continues.
    void withToken(std::function<void()> next);
    /// An API request with the token. A 401 refreshes the token and retries once.
    void api(const QByteArray &verb, const QUrl &url, const QByteArray &body,
             const QByteArray &contentType, Done done, bool retried = false);
    /// True on a 2xx reply; otherwise calls fail() with Google's message.
    bool ok(QNetworkReply *reply);

    /// @name Steps of a pass
    /// @{
    void findFolder(const QString &name, const QString &parent,
                    std::function<void(const QString &)> next);
    void listFolder(const QString &folderId, const QString &pageToken,
                    QHash<QString, Remote> *into, std::function<void()> next);
    void fetchRemoteJson(int index);   ///< The three JSON files, one after another.
    void mergeAndPlan();
    /// After Calendar: what to upload, including what it brought.
    void planUploads();
    void planAttachments(int index);   ///< audio/ and images/
    void transferNext();
    void finishSync();
    /// @}

    /// MD5 of the remote JSON last merged: unchanged ones are not downloaded.
    QString seenMd5(const QString &name) const;
    void setSeenMd5(const QString &name, const QString &md5);

    QNetworkAccessManager *m_net = nullptr;
    State m_state = Disconnected;
    QString m_refresh;
    QString m_scopes;   ///< Granted scopes, space-separated.
    QString m_access;
    QDateTime m_accessUntil;
    QString m_account;
    QDateTime m_lastSync;
    QString m_error;

    // Authorisation in progress.
    QTcpServer *m_server = nullptr;
    QString m_verifier;
    QString m_stateToken;
    QString m_redirect;
    bool m_authorized = false;   ///< The code arrived: the rest is the browser re-requesting.
    int m_attempt = 0;   ///< So a stale timeout does not cancel a newer attempt.

    Store *m_store = nullptr;
    CalendarSync *m_calendar = nullptr;

    // Pass in progress.
    QList<Transfer> m_queue;
    bool m_again = false;
    bool m_gotFiles = false;               ///< Some attachment was downloaded.
    QString m_rootId;
    QHash<QString, Remote> m_rootFiles;    ///< Contents of the Tagoror folder.
    QList<QJsonObject> m_remoteJson;       ///< notes, birthdays, events (empty = nothing new).
    QSet<QString> m_pull;                  ///< Attachments that must be downloaded regardless.
};
