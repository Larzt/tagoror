#include "core/drivesync.hpp"

#include "core/calendarsync.hpp"

#include "core/lang.hpp"
#include "core/paths.hpp"
#include "core/store.hpp"

#include <memory>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrlQuery>

#ifndef TAGOROR_GOOGLE_CLIENT_ID
#define TAGOROR_GOOGLE_CLIENT_ID ""
#endif
#ifndef TAGOROR_GOOGLE_CLIENT_SECRET
#define TAGOROR_GOOGLE_CLIENT_SECRET ""
#endif

namespace {

constexpr auto kScope = "https://www.googleapis.com/auth/drive.file";
/// Google Calendar scopes, requested only when this machine enables it: they
/// are "sensitive" and pointless for someone who only wants Drive. The
/// calendar list needs a scope of its own; read-only is enough.
constexpr auto kCalendarScope = "https://www.googleapis.com/auth/calendar.events";
constexpr auto kCalendarListScope = "https://www.googleapis.com/auth/calendar.calendarlist.readonly";
constexpr auto kFolderMime = "application/vnd.google-apps.folder";
constexpr auto kRootFolder = "Tagoror";
constexpr int kTimeoutMs = 60000;
/// How long the browser authorisation may take before the port is released.
constexpr int kAuthorizeMs = 5 * 60 * 1000;

QString randomToken(int length) {
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
    QString out;
    out.reserve(length);
    for (int i = 0; i < length; ++i)
        out += QChar(alphabet[QRandomGenerator::system()->bounded(int(sizeof(alphabet) - 1))]);
    return out;
}

QString md5Of(const QByteArray &data) {
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Md5).toHex());
}

/// A string for a Drive query, with quotes and backslashes escaped.
QString quoted(const QString &s) {
    QString out = s;
    out.replace('\\', "\\\\").replace('\'', "\\'");
    return "'" + out + "'";
}

}  // namespace

DriveSync::Endpoints &DriveSync::endpoints() {
    static Endpoints e;
    return e;
}

bool DriveSync::configured() { return QByteArray(TAGOROR_GOOGLE_CLIENT_ID).size() > 0; }

DriveSync::DriveSync(Store *store, QObject *parent) : SyncBackend(parent), m_store(store) {
    m_net = new QNetworkAccessManager(this);
    m_net->setTransferTimeout(kTimeoutMs);

    QSettings s;
    s.beginGroup("drive");
    m_refresh = s.value("refreshToken").toString();
    m_scopes = s.value("scopes").toString();
    m_account = s.value("account").toString();
    s.endGroup();

    if (!configured()) m_state = Unavailable;
    else m_state = connected() ? Idle : Disconnected;

    m_calendar = new CalendarSync(this, store);
}

bool DriveSync::hasCalendarScope() const {
    const QStringList granted = m_scopes.split(' ', Qt::SkipEmptyParts);
    return granted.contains(kCalendarScope) && granted.contains(kCalendarListScope);
}

void DriveSync::saveSettings() {
    QSettings s;
    s.beginGroup("drive");
    if (m_refresh.isEmpty()) {
        // Forgetting the account also forgets what was merged from it: another account
        // is another folder and must be read in full.
        s.remove("");   // the whole group: disconnecting leaves nothing behind
    } else {
        s.setValue("refreshToken", m_refresh);
        s.setValue("scopes", m_scopes);
        s.setValue("account", m_account);
    }
    s.endGroup();
}

void DriveSync::setState(State s) {
    m_state = s;
    emit changed();
}

void DriveSync::failAccount(const QString &why) {
    m_accountError = why;
    m_error = why;
    setState(connected() ? Idle : Disconnected);
}

void DriveSync::connectAccount() {
    if (!configured() || m_state == Authorizing) return;

    delete m_server;
    m_server = new QTcpServer(this);
    // Loopback only: the authorisation code has no reason to be reachable from
    // outside the machine.
    if (!m_server->listen(QHostAddress::LocalHost, 0)) {
        failAccount(L("No se pudo abrir un puerto local para la autorización."));
        return;
    }
    connect(m_server, &QTcpServer::newConnection, this, &DriveSync::onRedirect);

    // PKCE: Google's code is only valid together with this one-time secret, which
    // never goes through the browser.
    m_authorized = false;
    m_verifier = randomToken(64);
    m_stateToken = randomToken(24);
    m_redirect = QString("http://127.0.0.1:%1").arg(m_server->serverPort());
    const QByteArray challenge =
        QCryptographicHash::hash(m_verifier.toLatin1(), QCryptographicHash::Sha256)
            .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);

    QUrl url(endpoints().auth);
    QUrlQuery q;
    q.addQueryItem("client_id", TAGOROR_GOOGLE_CLIENT_ID);
    q.addQueryItem("redirect_uri", m_redirect);
    q.addQueryItem("response_type", "code");
    QString scope = kScope;
    if (m_calendar->enabled()) scope += QString(" %1 %2").arg(kCalendarScope, kCalendarListScope);
    q.addQueryItem("scope", scope);
    // Re-authorising to add Calendar keeps what was already granted.
    q.addQueryItem("include_granted_scopes", "true");
    q.addQueryItem("code_challenge", QString::fromLatin1(challenge));
    q.addQueryItem("code_challenge_method", "S256");
    q.addQueryItem("state", m_stateToken);
    // offline + consent: without them Google does not always return a refresh
    // token, and authorisation would be needed every hour.
    q.addQueryItem("access_type", "offline");
    q.addQueryItem("prompt", "consent");
    url.setQuery(q);

    m_accountError.clear();
    const int attempt = ++m_attempt;
    QTimer::singleShot(kAuthorizeMs, this, [this, attempt] {
        if (m_state == Authorizing && attempt == m_attempt) {
            cancel();
            m_accountError = L("No se completó la autorización a tiempo.");
            emit changed();
        }
    });
    setState(Authorizing);
    emit openUrl(url);
}

void DriveSync::cancel() {
    if (m_server) {
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
    }
    if (m_state == Authorizing) setState(connected() ? Idle : Disconnected);
}

void DriveSync::onRedirect() {
    while (m_server && m_server->hasPendingConnections()) {
        QTcpSocket *socket = m_server->nextPendingConnection();
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        // The request may arrive in several chunks; the first line is enough.
        auto buffer = std::make_shared<QByteArray>();
        connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer] {
            buffer->append(socket->readAll());
            if (!buffer->contains("\r\n")) return;
            // One answer per connection, whatever else arrives.
            QObject::disconnect(socket, &QTcpSocket::readyRead, this, nullptr);
            answerBrowser(socket, *buffer);
        });
    }
}

void DriveSync::answerBrowser(QTcpSocket *socket, const QByteArray &request) {
    // "GET /?code=...&state=... HTTP/1.1"
    const QList<QByteArray> parts = request.left(request.indexOf("\r\n")).split(' ');
    const QUrl target(QString::fromLatin1(parts.value(1)));
    auto reply = [socket](int status, const QString &text) {
        const QByteArray html =
            "<!doctype html><meta charset=utf-8><title>Tagoror</title>"
            "<body style=\"font-family:sans-serif;background:#13161b;color:#e9eaee;"
            "display:grid;place-items:center;height:90vh\"><p>" +
            text.toHtmlEscaped().toUtf8() + "</p></body>";
        socket->write("HTTP/1.1 " + QByteArray::number(status) +
                      (status == 200 ? " OK" : " Not Found") +
                      "\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: " +
                      QByteArray::number(html.size()) + "\r\nConnection: close\r\n\r\n" + html);
        socket->disconnectFromHost();
    };

    // The browser also asks for /favicon.ico and the like.
    if (target.path() != "/") {
        reply(404, QString());
        return;
    }
    if (m_authorized) {
        reply(200, L("Listo: Tagoror ya puede sincronizar con tu Drive. Puedes cerrar esta pestaña."));
        return;
    }
    const QUrlQuery q(target);
    if (q.queryItemValue("state") != m_stateToken) {
        reply(404, L("Respuesta inesperada. Vuelve a Tagoror e inténtalo otra vez."));
        return;
    }

    const QString code = q.queryItemValue("code");
    if (code.isEmpty()) {
        reply(200, L("No se ha dado acceso. Puedes cerrar esta pestaña."));
        cancel();
        m_accountError = L("La autorización se canceló en el navegador.");
        emit changed();
        return;
    }
    reply(200, L("Listo: Tagoror ya puede sincronizar con tu Drive. Puedes cerrar esta pestaña."));
    // The port keeps answering for a minute: the browser may request the page
    // again, and a closed port shows "connection refused", which looks like a
    // failure after a successful login.
    m_stateToken.clear();
    m_authorized = true;
    if (QTcpServer *server = m_server) {
        m_server = nullptr;
        QTimer::singleShot(60 * 1000, server, [server] {
            server->close();
            server->deleteLater();
        });
        disconnect(server, &QTcpServer::newConnection, this, &DriveSync::onRedirect);
        connect(server, &QTcpServer::newConnection, this, [this, server] {
            while (server->hasPendingConnections()) {
                QTcpSocket *socket = server->nextPendingConnection();
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                    QObject::disconnect(socket, &QTcpSocket::readyRead, this, nullptr);
                    answerBrowser(socket, socket->readAll());
                });
            }
        });
    }
    exchangeCode(code);
}

void DriveSync::exchangeCode(const QString &code) {
    QUrlQuery form;
    form.addQueryItem("code", code);
    form.addQueryItem("client_id", TAGOROR_GOOGLE_CLIENT_ID);
    form.addQueryItem("client_secret", TAGOROR_GOOGLE_CLIENT_SECRET);
    form.addQueryItem("redirect_uri", m_redirect);
    form.addQueryItem("grant_type", "authorization_code");
    form.addQueryItem("code_verifier", m_verifier);

    QNetworkRequest req{QUrl(endpoints().token)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    QNetworkReply *r = m_net->post(req, form.toString(QUrl::FullyEncoded).toLatin1());
    connect(r, &QNetworkReply::finished, this, [this, r] {
        r->deleteLater();
        const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
        if (r->error() != QNetworkReply::NoError || !o.contains("refresh_token")) {
            m_verifier.clear();
            failAccount(L("Google no aceptó la autorización (%1).")
                     .arg(o["error_description"].toString(o["error"].toString(r->errorString()))));
            return;
        }
        m_verifier.clear();
        m_refresh = o["refresh_token"].toString();
        m_scopes = o["scope"].toString();
        m_access = o["access_token"].toString();
        m_accessUntil = QDateTime::currentDateTime().addSecs(o["expires_in"].toInt(3600));
        saveSettings();
        fetchAccount();
    });
}

/// Which account was connected, to show it in settings. drive.file gives no
/// profile access, but the Drive API says who owns the drive.
void DriveSync::fetchAccount() {
    QUrl url(endpoints().api + "/about");
    url.setQuery("fields=user(emailAddress,displayName)");
    api("GET", url, {}, {}, [this](QNetworkReply *r) {
        if (!ok(r)) {
            failAccount(m_error);
            return;
        }
        const QJsonObject user = QJsonDocument::fromJson(r->readAll()).object()["user"].toObject();
        m_account = user["emailAddress"].toString(user["displayName"].toString());
        saveSettings();
        setState(Idle);
        emit connectedNow();   // the first sync, right away
    });
}

void DriveSync::disconnectAccount() {
    cancel();
    // Google is asked to revoke the grant, without waiting: even if that fails
    // (no network) the token is forgotten here, which is what the user asked for.
    // Files already uploaded stay in their Drive.
    if (!m_refresh.isEmpty()) {
        QNetworkRequest req{QUrl(endpoints().revoke)};
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
        QNetworkReply *r = m_net->post(req, "token=" + QUrl::toPercentEncoding(m_refresh));
        connect(r, &QNetworkReply::finished, r, &QObject::deleteLater);
    }
    m_refresh.clear();
    m_scopes.clear();
    m_access.clear();
    m_account.clear();
    m_calendar->forget();
    m_error.clear();
    m_accountError.clear();
    m_folders.clear();
    saveSettings();
    setState(configured() ? Disconnected : Unavailable);
}

void DriveSync::withToken(std::function<void(bool)> next) {
    if (!m_access.isEmpty() && QDateTime::currentDateTime().secsTo(m_accessUntil) > 60) {
        next(true);
        return;
    }
    if (m_refresh.isEmpty()) {
        m_error = L("La cuenta de Google no está conectada.");
        next(false);
        return;
    }
    QUrlQuery form;
    form.addQueryItem("client_id", TAGOROR_GOOGLE_CLIENT_ID);
    form.addQueryItem("client_secret", TAGOROR_GOOGLE_CLIENT_SECRET);
    form.addQueryItem("refresh_token", m_refresh);
    form.addQueryItem("grant_type", "refresh_token");

    QNetworkRequest req{QUrl(endpoints().token)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    QNetworkReply *r = m_net->post(req, form.toString(QUrl::FullyEncoded).toLatin1());
    connect(r, &QNetworkReply::finished, this, [this, r, next] {
        r->deleteLater();
        const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
        if (o["error"].toString() == "invalid_grant") {
            // The grant was revoked from the Google account, or the token expired: it is
            // useless now and the account must be reconnected.
            m_refresh.clear();
            m_access.clear();
            saveSettings();
            failAccount(L("Google ha retirado el acceso. Vuelve a conectar la cuenta."));
            next(false);
            return;
        }
        if (r->error() != QNetworkReply::NoError || !o.contains("access_token")) {
            m_error = L("Sin conexión con Google.");
            next(false);
            return;
        }
        m_access = o["access_token"].toString();
        m_accessUntil = QDateTime::currentDateTime().addSecs(o["expires_in"].toInt(3600));
        // Google reports the granted scopes on every refresh, which also covers
        // accounts connected before they were stored.
        if (const QString scopes = o["scope"].toString(); !scopes.isEmpty() && scopes != m_scopes) {
            m_scopes = scopes;
            saveSettings();
        }
        next(true);
    });
}

void DriveSync::api(const QByteArray &verb, const QUrl &url, const QByteArray &body,
                    const QByteArray &contentType, Done done, bool retried) {
    withToken([=, this](bool haveToken) {
        // No token: the caller hears it as a null reply, which ok() refuses.
        if (!haveToken) {
            done(nullptr);
            return;
        }
        QNetworkRequest req(url);
        req.setRawHeader("Authorization", "Bearer " + m_access.toLatin1());
        if (!contentType.isEmpty()) req.setHeader(QNetworkRequest::ContentTypeHeader, contentType);
        QNetworkReply *r = m_net->sendCustomRequest(req, verb, body);
        connect(r, &QNetworkReply::finished, this, [=, this] {
            r->deleteLater();
            const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status == 401 && !retried) {
                m_access.clear();   // expired early: refresh and retry
                api(verb, url, body, contentType, done, true);
                return;
            }
            done(r);
        });
    });
}

bool DriveSync::ok(QNetworkReply *r) {
    if (!r) return false;   // no token: m_error already says why
    const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (r->error() == QNetworkReply::NoError && status >= 200 && status < 300) return true;
    if (status == 0) {
        m_error = L("Sin conexión con Google Drive.");
        return false;
    }
    const QJsonObject err = QJsonDocument::fromJson(r->readAll()).object()["error"].toObject();
    const QString reason = err["errors"].toArray().first().toObject()["reason"].toString();
    if (reason == "storageQuotaExceeded")
        m_error = L("No queda espacio en tu Google Drive.");
    else
        m_error = L("Google Drive respondió con un error (%1).")
                      .arg(err["message"].toString(QString::number(status)));
    return false;
}

void DriveSync::list(const QString &dir, std::function<void(bool, Listing)> done) {
    // A pass starts by listing the root: the folders are looked up again, in case
    // one was deleted or trashed in Drive meanwhile.
    if (dir.isEmpty()) m_folders.clear();
    folderFor(dir, [this, done](const QString &folderId) {
        if (folderId.isEmpty()) {
            done(false, {});
            return;
        }
        auto *into = new Listing;
        listFolder(folderId, QString(), into, [into, done](bool ok) {
            const Listing out = *into;
            delete into;
            done(ok, out);
        });
    });
}

void DriveSync::download(const QString &, const QString &, const Remote &remote,
                         std::function<void(bool, QByteArray)> done) {
    QUrl url(endpoints().api + "/files/" + remote.id);
    url.setQuery("alt=media");
    api("GET", url, {}, {}, [this, done](QNetworkReply *r) {
        if (!ok(r)) {
            done(false, {});
            return;
        }
        done(true, r->readAll());
    });
}

/// Uploads are always resumable (multipart stops at 5 MB and a long voice note
/// exceeds it): one request opens the session, another sends the content.
void DriveSync::upload(const QString &dir, const QString &name, const Remote &existing,
                       const QByteArray &content, std::function<void(bool, Remote)> done) {
    folderFor(dir, [=, this](const QString &folderId) {
        if (folderId.isEmpty()) {
            done(false, {});
            return;
        }
        const QString mime = QMimeDatabase().mimeTypeForFile(name, QMimeDatabase::MatchExtension).name();
        QJsonObject meta{{"name", name}};
        if (existing.id.isEmpty()) meta["parents"] = QJsonArray{folderId};
        QUrl url(endpoints().upload + "/files" + (existing.id.isEmpty() ? "" : "/" + existing.id));
        url.setQuery("uploadType=resumable&fields=id");

        withToken([=, this](bool haveToken) {
            if (!haveToken) {
                done(false, {});
                return;
            }
            QNetworkRequest req(url);
            req.setRawHeader("Authorization", "Bearer " + m_access.toLatin1());
            req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json; charset=UTF-8");
            req.setRawHeader("X-Upload-Content-Type", mime.toLatin1());
            req.setRawHeader("X-Upload-Content-Length", QByteArray::number(content.size()));
            QNetworkReply *r = m_net->sendCustomRequest(req, existing.id.isEmpty() ? "POST" : "PATCH",
                                                        QJsonDocument(meta).toJson(QJsonDocument::Compact));
            connect(r, &QNetworkReply::finished, this, [=, this] {
                r->deleteLater();
                if (!ok(r)) {
                    done(false, {});
                    return;
                }
                const QUrl session(QString::fromLatin1(r->rawHeader("Location")));
                QNetworkRequest put(session);
                put.setHeader(QNetworkRequest::ContentTypeHeader, mime);
                QNetworkReply *pr = m_net->put(put, content);
                connect(pr, &QNetworkReply::finished, this, [this, pr, existing, content, done] {
                    pr->deleteLater();
                    if (!ok(pr)) {
                        done(false, {});
                        return;
                    }
                    const QString id = QJsonDocument::fromJson(pr->readAll()).object()["id"].toString(existing.id);
                    // Drive's md5Checksum is the content's MD5: known without asking.
                    const QString md5 = md5Of(content);
                    done(true, Remote{id, md5, md5});
                });
            });
        });
    });
}

void DriveSync::folderFor(const QString &dir, std::function<void(const QString &)> next) {
    if (m_folders.contains(dir)) {
        next(m_folders.value(dir));
        return;
    }
    const auto remember = [this, dir, next](const QString &id) {
        if (!id.isEmpty()) m_folders.insert(dir, id);
        next(id);
    };
    if (dir.isEmpty()) {
        findFolder(kRootFolder, "root", remember);
        return;
    }
    folderFor(QString(), [this, dir, remember](const QString &rootId) {
        if (rootId.isEmpty()) {
            remember(QString());
            return;
        }
        findFolder(dir, rootId, remember);
    });
}

/// Finds the folder, creating it if missing. With drive.file only folders the
/// app created are visible, so a hand-made "Tagoror" is never confused with
/// ours; and since the grant is per app, every machine on the account sees
/// the same one. An empty id means it failed (error() says why).
void DriveSync::findFolder(const QString &name, const QString &parent,
                           std::function<void(const QString &)> next) {
    QUrl url(endpoints().api + "/files");
    QUrlQuery q;
    q.addQueryItem("q", QString("name = %1 and mimeType = '%2' and %3 in parents and trashed = false")
                            .arg(quoted(name), kFolderMime, quoted(parent)));
    q.addQueryItem("fields", "files(id)");
    q.addQueryItem("spaces", "drive");
    url.setQuery(q);

    api("GET", url, {}, {}, [=, this](QNetworkReply *r) {
        if (!ok(r)) {
            next(QString());
            return;
        }
        const QJsonArray files = QJsonDocument::fromJson(r->readAll()).object()["files"].toArray();
        if (!files.isEmpty()) {
            next(files.first().toObject()["id"].toString());
            return;
        }
        QJsonObject meta{{"name", name}, {"mimeType", kFolderMime}};
        if (parent != "root") meta["parents"] = QJsonArray{parent};
        QUrl create(endpoints().api + "/files");
        create.setQuery("fields=id");
        api("POST", create, QJsonDocument(meta).toJson(QJsonDocument::Compact),
            "application/json; charset=UTF-8", [=, this](QNetworkReply *cr) {
                if (!ok(cr)) {
                    next(QString());
                    return;
                }
                next(QJsonDocument::fromJson(cr->readAll()).object()["id"].toString());
            });
    });
}

void DriveSync::listFolder(const QString &folderId, const QString &pageToken, Listing *into,
                           std::function<void(bool)> next) {
    QUrl url(endpoints().api + "/files");
    QUrlQuery q;
    q.addQueryItem("q", QString("%1 in parents and trashed = false").arg(quoted(folderId)));
    q.addQueryItem("fields", "nextPageToken,files(id,name,md5Checksum)");
    q.addQueryItem("pageSize", "1000");
    if (!pageToken.isEmpty()) q.addQueryItem("pageToken", pageToken);
    url.setQuery(q);

    api("GET", url, {}, {}, [=, this](QNetworkReply *r) {
        if (!ok(r)) {
            next(false);
            return;
        }
        const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
        for (const QJsonValue v : o["files"].toArray()) {
            const QJsonObject f = v.toObject();
            const QString md5 = f["md5Checksum"].toString();
            into->insert(f["name"].toString(), {f["id"].toString(), md5, md5});
        }
        const QString nextToken = o["nextPageToken"].toString();
        if (!nextToken.isEmpty()) listFolder(folderId, nextToken, into, next);
        else next(true);
    });
}
