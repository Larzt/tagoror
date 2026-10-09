#include "core/webdavbackend.hpp"

#include <QCryptographicHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QXmlStreamReader>

#include "core/lang.hpp"

namespace {

constexpr auto kRoot = "Tagoror";
constexpr int kTimeoutMs = 60000;

const QByteArray kPropfind =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
    "<d:propfind xmlns:d=\"DAV:\"><d:prop><d:getetag/><d:resourcetype/></d:prop></d:propfind>";

}  // namespace

WebDavBackend::WebDavBackend(QObject *parent) : SyncBackend(parent) {
    m_net = new QNetworkAccessManager(this);
    m_net->setTransferTimeout(kTimeoutMs);
    QSettings s;
    s.beginGroup("sync/webdav");
    m_server = s.value("server").toString();
    m_user = s.value("user").toString();
    m_password = s.value("password").toString();
    s.endGroup();
}

bool WebDavBackend::ready() const {
    return !m_server.isEmpty() && !m_user.isEmpty() && !m_password.isEmpty();
}

void WebDavBackend::setCredentials(const QString &server, const QString &user,
                                   const QString &password) {
    QString base = server.trimmed();
    while (base.endsWith('/')) base.chop(1);
    if (!base.isEmpty() && !base.contains("://")) base.prepend("https://");
    QSettings s;
    if (base != m_server || user.trimmed() != m_user) {
        // Another server or account is another copy: nothing merged from the old
        // one says anything about this one.
        s.remove("sync/webdav");
    }
    m_server = base;
    m_user = user.trimmed();
    if (!password.isEmpty()) m_password = password;
    s.beginGroup("sync/webdav");
    s.setValue("server", m_server);
    s.setValue("user", m_user);
    s.setValue("password", m_password);
    s.endGroup();
    m_error.clear();
    emit changed();
}

void WebDavBackend::forget() {
    QSettings().remove("sync/webdav");
    m_server.clear();
    m_user.clear();
    m_password.clear();
    m_error.clear();
    emit changed();
}

QUrl WebDavBackend::urlFor(const QString &dir, const QString &name) const {
    QString path = m_server + "/" + kRoot + "/";
    if (!dir.isEmpty()) path += QUrl::toPercentEncoding(dir) + "/";
    if (!name.isEmpty()) path += QUrl::toPercentEncoding(name);
    return QUrl(path);
}

QNetworkReply *WebDavBackend::send(const QByteArray &verb, const QUrl &url,
                                   const QByteArray &body, const QByteArray &depth) {
    QNetworkRequest req(url);
    req.setRawHeader("Authorization",
                     "Basic " + (m_user + ":" + m_password).toUtf8().toBase64());
    if (!depth.isEmpty()) req.setRawHeader("Depth", depth);
    if (verb == "PROPFIND")
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/xml; charset=utf-8");
    return m_net->sendCustomRequest(req, verb, body);
}

bool WebDavBackend::ok(QNetworkReply *r) {
    const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (r->error() == QNetworkReply::NoError && status >= 200 && status < 300) return true;
    if (status == 401 || status == 403)
        m_error = L("El servidor no acepta el usuario o la contraseña.");
    else if (status == 0)
        m_error = L("Sin conexión con el servidor WebDAV.");
    else if (status == 507)
        m_error = L("No queda espacio en el servidor.");
    else
        m_error = L("El servidor WebDAV respondió con un error (%1).").arg(status);
    return false;
}

/// MKCOL on the root and then on @p dir. 405 means it already exists, which is
/// the usual case and not an error.
void WebDavBackend::ensure(const QString &dir, std::function<void(bool)> next) {
    QNetworkReply *r = send("MKCOL", urlFor(QString()));
    connect(r, &QNetworkReply::finished, this, [this, r, dir, next] {
        r->deleteLater();
        const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 405 && !ok(r)) {
            next(false);
            return;
        }
        if (dir.isEmpty()) {
            next(true);
            return;
        }
        QNetworkReply *r2 = send("MKCOL", urlFor(dir));
        connect(r2, &QNetworkReply::finished, this, [this, r2, next] {
            r2->deleteLater();
            const int st = r2->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            next(st == 405 || ok(r2));
        });
    });
}

void WebDavBackend::list(const QString &dir, std::function<void(bool, Listing)> done) {
    const QUrl url = urlFor(dir);
    QNetworkReply *r = send("PROPFIND", url, kPropfind, "1");
    connect(r, &QNetworkReply::finished, this, [this, r, dir, url, done] {
        r->deleteLater();
        const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 404) {
            // Not there yet (first sync, or the folder was deleted): create it.
            ensure(dir, [done](bool created) { done(created, {}); });
            return;
        }
        if (!ok(r)) {
            done(false, {});
            return;
        }
        // <d:multistatus><d:response><d:href>…</d:href>…<d:getetag>…
        Listing out;
        QXmlStreamReader xml(r->readAll());
        QString href, etag;
        bool collection = false;
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement()) {
                const auto name = xml.name();
                if (name == QLatin1String("response")) {
                    href.clear();
                    etag.clear();
                    collection = false;
                } else if (name == QLatin1String("href")) {
                    href = xml.readElementText();
                } else if (name == QLatin1String("getetag")) {
                    etag = xml.readElementText();
                } else if (name == QLatin1String("collection")) {
                    collection = true;
                }
            } else if (xml.isEndElement() && xml.name() == QLatin1String("response")) {
                if (collection || href.isEmpty()) continue;   // the folder itself, subfolders
                QString file = QUrl::fromPercentEncoding(href.toUtf8());
                while (file.endsWith('/')) file.chop(1);
                file = file.section('/', -1);
                if (!file.isEmpty()) out.insert(file, Remote{file, etag, QString()});
            }
        }
        if (xml.hasError()) {
            m_error = L("El servidor WebDAV respondió algo que no se entiende.");
            done(false, {});
            return;
        }
        done(true, out);
    });
}

void WebDavBackend::download(const QString &dir, const QString &name, const Remote &,
                             std::function<void(bool, QByteArray)> done) {
    QNetworkReply *r = send("GET", urlFor(dir, name));
    connect(r, &QNetworkReply::finished, this, [this, r, done] {
        r->deleteLater();
        if (!ok(r)) {
            done(false, {});
            return;
        }
        done(true, r->readAll());
    });
}

void WebDavBackend::upload(const QString &dir, const QString &name, const Remote &,
                           const QByteArray &data, std::function<void(bool, Remote)> done) {
    QNetworkReply *r = send("PUT", urlFor(dir, name), data);
    connect(r, &QNetworkReply::finished, this, [this, r, name, done] {
        r->deleteLater();
        if (!ok(r)) {
            done(false, {});
            return;
        }
        // Most servers answer a PUT with the new ETag (Nextcloud also as OC-ETag);
        // without it the next pass reads the file once more, which is harmless.
        QByteArray etag = r->rawHeader("ETag");
        if (etag.isEmpty()) etag = r->rawHeader("OC-ETag");
        done(true, Remote{name, QString::fromUtf8(etag), QString()});
    });
}
