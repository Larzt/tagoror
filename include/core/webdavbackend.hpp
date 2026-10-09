#pragma once

#include <QUrl>

#include "core/syncbackend.hpp"

class QNetworkAccessManager;
class QNetworkReply;

/// WebDAV as a sync backend (5.0): Nextcloud, ownCloud, most NAS, a Raspberry
/// Pi. The shared copy is a "Tagoror" collection under the address given,
/// created on first use. Files are told apart by their ETag; WebDAV has no
/// content hash to compare without downloading, which SyncEngine handles.
///
/// The credentials are a user and, ideally, an app password (Nextcloud makes
/// one per device and can revoke it alone). They live in QSettings, like the
/// Google token, so they never travel with the data folder or its backups.
class WebDavBackend : public SyncBackend {
    Q_OBJECT

public:
    explicit WebDavBackend(QObject *parent = nullptr);

    QString id() const override { return QStringLiteral("webdav"); }
    bool ready() const override;

    QString server() const { return m_server; }
    QString user() const { return m_user; }
    bool hasPassword() const { return !m_password.isEmpty(); }
    /// Stores new credentials. A different server or user is another copy:
    /// what was merged from the old one is forgotten.
    void setCredentials(const QString &server, const QString &user, const QString &password);
    void forget();

    void list(const QString &dir, std::function<void(bool, Listing)> done) override;
    void download(const QString &dir, const QString &name, const Remote &remote,
                  std::function<void(bool, QByteArray)> done) override;
    void upload(const QString &dir, const QString &name, const Remote &existing,
                const QByteArray &data, std::function<void(bool, Remote)> done) override;

private:
    QUrl urlFor(const QString &dir, const QString &name = QString()) const;
    QNetworkReply *send(const QByteArray &verb, const QUrl &url, const QByteArray &body = {},
                        const QByteArray &depth = {});
    /// True on 2xx (or 207); otherwise sets error() in words.
    bool ok(QNetworkReply *r);
    /// Creates the Tagoror collection and @p dir under it, then continues.
    void ensure(const QString &dir, std::function<void(bool)> next);

    QNetworkAccessManager *m_net;
    QString m_server;
    QString m_user;
    QString m_password;
};
