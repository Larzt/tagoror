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

#include "core/syncbackend.hpp"

class Store;
class CalendarSync;
class QNetworkAccessManager;
class QNetworkReply;
class QTcpServer;
class QTcpSocket;

/// The Google account, and Google Drive as a sync backend.
///
/// Since 5.0 the pass itself is SyncEngine's; this keeps what is Google's:
/// the authorisation (Google's desktop flow: loopback redirect plus PKCE,
/// with the narrow `drive.file` scope), the token, and the Drive calls that
/// list, download and upload in a "Tagoror" folder of the user's Drive.
/// Google Calendar uses the same account and token (api()), whether or not
/// Drive is the file backend. The OAuth client comes from CMake
/// (TAGOROR_GOOGLE_CLIENT_ID/_SECRET); without it configured() is false. The
/// refresh token is kept in QSettings, not in notes.json, so it never travels
/// with the data folder or into backups.
class DriveSync : public SyncBackend {
    Q_OBJECT

public:
    /// The account's state; a pass's is SyncEngine's.
    enum State {
        Unavailable,    ///< Built without a client ID.
        Disconnected,
        Authorizing,    ///< Waiting for the browser.
        Idle,           ///< Connected.
    };

    explicit DriveSync(Store *store, QObject *parent = nullptr);

    /// Asked by Google Calendar before it applies what it fetched: it would
    /// change events under a user who is typing.
    std::function<bool()> canApply;

    static bool configured();

    State state() const { return m_state; }
    bool connected() const { return !m_refresh.isEmpty(); }
    QString account() const { return m_account; }
    /// The account's last error (authorisation, revoked access).
    QString lastError() const { return m_accountError; }

    /// Connects (or reconnects) the account. If Google Calendar is enabled on
    /// this machine, its scopes are requested too.
    void connectAccount();
    void cancel();
    void disconnectAccount();
    /// @name SyncBackend
    /// @{
    QString id() const override { return QStringLiteral("drive"); }
    bool ready() const override { return connected(); }
    QString settingsGroup() const override { return QStringLiteral("drive"); }
    void list(const QString &dir, std::function<void(bool, Listing)> done) override;
    void download(const QString &dir, const QString &name, const Remote &remote,
                  std::function<void(bool, QByteArray)> done) override;
    void upload(const QString &dir, const QString &name, const Remote &existing,
                const QByteArray &data, std::function<void(bool, Remote)> done) override;
    /// @}

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
    /// The account was just connected: a first pass is due.
    void connectedNow();

private:
    friend class CalendarSync;   // uses api() and this account's token

    using Done = std::function<void(QNetworkReply *)>;

    void setState(State s);
    /// An account-level failure (authorisation): shown in settings.
    void failAccount(const QString &why);
    void onRedirect();
    void answerBrowser(QTcpSocket *socket, const QByteArray &request);
    void exchangeCode(const QString &code);
    void fetchAccount();
    void saveSettings();

    /// Obtains (or refreshes) the access token, then continues.
    /// @p next gets false when no token could be had (error() says why).
    void withToken(std::function<void(bool)> next);
    /// An API request with the token. A 401 refreshes the token and retries once.
    /// Without a token at all, @p done gets a null reply.
    void api(const QByteArray &verb, const QUrl &url, const QByteArray &body,
             const QByteArray &contentType, Done done, bool retried = false);
    /// True on a 2xx reply; otherwise sets error() from Google's message.
    bool ok(QNetworkReply *reply);

    /// Finds the folder, creating it if missing.
    void findFolder(const QString &name, const QString &parent,
                    std::function<void(const QString &)> next);
    /// The folder id for @p dir ("" is the Tagoror folder), found once per run.
    void folderFor(const QString &dir, std::function<void(const QString &)> next);
    void listFolder(const QString &folderId, const QString &pageToken, Listing *into,
                    std::function<void(bool)> next);

    QNetworkAccessManager *m_net = nullptr;
    State m_state = Disconnected;
    QString m_refresh;
    QString m_scopes;   ///< Granted scopes, space-separated.
    QString m_access;
    QDateTime m_accessUntil;
    QString m_account;
    QString m_accountError;

    // Authorisation in progress.
    QTcpServer *m_server = nullptr;
    QString m_verifier;
    QString m_stateToken;
    QString m_redirect;
    bool m_authorized = false;   ///< The code arrived: the rest is the browser re-requesting.
    int m_attempt = 0;   ///< So a stale timeout does not cancel a newer attempt.

    Store *m_store = nullptr;
    CalendarSync *m_calendar = nullptr;

    /// Folder ids by dir ("" is the Tagoror folder), found once per run.
    QHash<QString, QString> m_folders;
};
