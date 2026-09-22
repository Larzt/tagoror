#include "core/drivesync.hpp"

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
constexpr auto kFolderMime = "application/vnd.google-apps.folder";
constexpr auto kRootFolder = "Tagoror";
constexpr int kTimeoutMs = 60000;
// Lo que se deja para autorizar en el navegador antes de dar el intento por
// perdido y soltar el puerto.
constexpr int kAuthorizeMs = 5 * 60 * 1000;

// Lo que se sincroniza, relativo a la carpeta de datos. Las copias de
// seguridad no: son de cada equipo, y subir diez versiones del mismo fichero es
// gastar la cuota del usuario en nada. El orden importa: es el de los objetos
// que recibe Store::mergeRemote.
const QStringList &jsonFiles() {
    static const QStringList files{"notes.json", "birthdays.json", "events.json"};
    return files;
}
const QStringList &subdirs() {
    static const QStringList dirs{"audio", "images"};
    return dirs;
}

QString randomToken(int length) {
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
    QString out;
    out.reserve(length);
    for (int i = 0; i < length; ++i)
        out += QChar(alphabet[QRandomGenerator::system()->bounded(int(sizeof(alphabet) - 1))]);
    return out;
}

QString fileMd5(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    QCryptographicHash h(QCryptographicHash::Md5);
    h.addData(&f);
    return QString::fromLatin1(h.result().toHex());
}

// Una cadena para una consulta de Drive: comillas y barras escapadas.
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

DriveSync::DriveSync(Store *store, QObject *parent) : QObject(parent), m_store(store) {
    m_net = new QNetworkAccessManager(this);
    m_net->setTransferTimeout(kTimeoutMs);

    QSettings s;
    s.beginGroup("drive");
    m_refresh = s.value("refreshToken").toString();
    m_account = s.value("account").toString();
    const qint64 last = s.value("lastSync").toLongLong();
    if (last > 0) m_lastSync = QDateTime::fromMSecsSinceEpoch(last);
    s.endGroup();

    if (!configured()) m_state = Unavailable;
    else m_state = connected() ? Idle : Disconnected;
}

void DriveSync::saveSettings() {
    QSettings s;
    s.beginGroup("drive");
    if (m_refresh.isEmpty()) {
        // Con la cuenta se olvida también qué se había mezclado de ella: otra
        // cuenta es otra carpeta, y hay que leerla entera.
        s.remove("");   // todo el grupo: desconectar no deja nada atrás
    } else {
        s.setValue("refreshToken", m_refresh);
        s.setValue("account", m_account);
        s.setValue("lastSync", m_lastSync.isValid() ? m_lastSync.toMSecsSinceEpoch() : 0);
    }
    s.endGroup();
}

void DriveSync::setState(State s) {
    m_state = s;
    emit changed();
}

void DriveSync::fail(const QString &why) {
    m_error = why;
    m_queue.clear();
    m_remoteJson.clear();
    m_again = false;
    setState(connected() ? Failed : Disconnected);
}

// --- autorización -------------------------------------------------------------

void DriveSync::connectAccount() {
    if (!configured() || m_state == Authorizing) return;

    delete m_server;
    m_server = new QTcpServer(this);
    // Solo la interfaz local: el código de autorización no tiene por qué
    // poder llegar desde fuera del equipo.
    if (!m_server->listen(QHostAddress::LocalHost, 0)) {
        fail(L("No se pudo abrir un puerto local para la autorización."));
        return;
    }
    connect(m_server, &QTcpServer::newConnection, this, &DriveSync::onRedirect);

    // PKCE: el código que devuelve Google solo sirve junto a este secreto de
    // un solo uso, que nunca pasa por el navegador.
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
    q.addQueryItem("scope", kScope);
    q.addQueryItem("code_challenge", QString::fromLatin1(challenge));
    q.addQueryItem("code_challenge_method", "S256");
    q.addQueryItem("state", m_stateToken);
    // offline + consent: sin ellos Google no siempre devuelve el token de
    // refresco, y sin él habría que volver a autorizar cada hora.
    q.addQueryItem("access_type", "offline");
    q.addQueryItem("prompt", "consent");
    url.setQuery(q);

    m_error.clear();
    const int attempt = ++m_attempt;
    QTimer::singleShot(kAuthorizeMs, this, [this, attempt] {
        if (m_state == Authorizing && attempt == m_attempt) {
            cancel();
            m_error = L("No se completó la autorización a tiempo.");
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
        // La petición puede llegar en varios trozos; basta con la primera línea.
        auto buffer = std::make_shared<QByteArray>();
        connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer] {
            buffer->append(socket->readAll());
            if (!buffer->contains("\r\n")) return;
            // Una sola respuesta por conexión, aunque lleguen más trozos.
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

    // El navegador pide también /favicon.ico y cosas así: no son la respuesta.
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
        m_error = L("La autorización se canceló en el navegador.");
        emit changed();
        return;
    }
    reply(200, L("Listo: Tagoror ya puede sincronizar con tu Drive. Puedes cerrar esta pestaña."));
    // El puerto no se cierra en seco: el navegador puede volver a pedir la
    // página (recargar, una segunda petición suya) y con el puerto cerrado
    // enseña "conexión rechazada", que parece un fallo cuando todo ha ido bien.
    // Un minuto más contestando lo mismo, y después se suelta.
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
            fail(L("Google no aceptó la autorización (%1).")
                     .arg(o["error_description"].toString(o["error"].toString(r->errorString()))));
            return;
        }
        m_verifier.clear();
        m_refresh = o["refresh_token"].toString();
        m_access = o["access_token"].toString();
        m_accessUntil = QDateTime::currentDateTime().addSecs(o["expires_in"].toInt(3600));
        saveSettings();
        fetchAccount();
    });
}

// A qué cuenta se ha conectado, para poder decirlo en ajustes. drive.file no
// da acceso al perfil, pero la API de Drive sí dice de quién es la unidad.
void DriveSync::fetchAccount() {
    QUrl url(endpoints().api + "/about");
    url.setQuery("fields=user(emailAddress,displayName)");
    api("GET", url, {}, {}, [this](QNetworkReply *r) {
        if (!ok(r)) return;
        const QJsonObject user = QJsonDocument::fromJson(r->readAll()).object()["user"].toObject();
        m_account = user["emailAddress"].toString(user["displayName"].toString());
        saveSettings();
        setState(Idle);
        syncNow();   // la primera copia, ya
    });
}

void DriveSync::disconnectAccount() {
    cancel();
    // Se le pide a Google que retire el permiso, pero no se espera: aunque
    // falle (sin red), aquí se olvida el token igual, y eso es lo que el
    // usuario ha pedido. Los ficheros ya subidos se quedan en su Drive.
    if (!m_refresh.isEmpty()) {
        QNetworkRequest req{QUrl(endpoints().revoke)};
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
        QNetworkReply *r = m_net->post(req, "token=" + QUrl::toPercentEncoding(m_refresh));
        connect(r, &QNetworkReply::finished, r, &QObject::deleteLater);
    }
    m_refresh.clear();
    m_access.clear();
    m_account.clear();
    m_lastSync = QDateTime();
    m_error.clear();
    m_queue.clear();
    m_rootId.clear();
    saveSettings();
    setState(configured() ? Disconnected : Unavailable);
}

// --- peticiones ---------------------------------------------------------------

void DriveSync::withToken(std::function<void()> next) {
    if (!m_access.isEmpty() && QDateTime::currentDateTime().secsTo(m_accessUntil) > 60) {
        next();
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
            // El usuario retiró el permiso desde su cuenta de Google, o el
            // token caducó: ya no sirve y hay que volver a conectar.
            m_refresh.clear();
            m_access.clear();
            saveSettings();
            fail(L("Google ha retirado el acceso. Vuelve a conectar la cuenta."));
            return;
        }
        if (r->error() != QNetworkReply::NoError || !o.contains("access_token")) {
            fail(L("Sin conexión con Google Drive."));
            return;
        }
        m_access = o["access_token"].toString();
        m_accessUntil = QDateTime::currentDateTime().addSecs(o["expires_in"].toInt(3600));
        next();
    });
}

void DriveSync::api(const QByteArray &verb, const QUrl &url, const QByteArray &body,
                    const QByteArray &contentType, Done done, bool retried) {
    withToken([=, this] {
        QNetworkRequest req(url);
        req.setRawHeader("Authorization", "Bearer " + m_access.toLatin1());
        if (!contentType.isEmpty()) req.setHeader(QNetworkRequest::ContentTypeHeader, contentType);
        QNetworkReply *r = m_net->sendCustomRequest(req, verb, body);
        connect(r, &QNetworkReply::finished, this, [=, this] {
            r->deleteLater();
            const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status == 401 && !retried) {
                m_access.clear();   // caducado antes de tiempo: se renueva y se repite
                api(verb, url, body, contentType, done, true);
                return;
            }
            done(r);
        });
    });
}

bool DriveSync::ok(QNetworkReply *r) {
    const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (r->error() == QNetworkReply::NoError && status >= 200 && status < 300) return true;
    if (status == 0) {
        fail(L("Sin conexión con Google Drive."));
        return false;
    }
    const QJsonObject err = QJsonDocument::fromJson(r->readAll()).object()["error"].toObject();
    const QString reason = err["errors"].toArray().first().toObject()["reason"].toString();
    if (reason == "storageQuotaExceeded")
        fail(L("No queda espacio en tu Google Drive."));
    else
        fail(L("Google Drive respondió con un error (%1).")
                 .arg(err["message"].toString(QString::number(status))));
    return false;
}

// --- sincronización -------------------------------------------------------------

QString DriveSync::seenMd5(const QString &name) const {
    return QSettings().value("drive/seen/" + name).toString();
}

void DriveSync::setSeenMd5(const QString &name, const QString &md5) {
    QSettings().setValue("drive/seen/" + name, md5);
}

void DriveSync::syncNow() {
    if (!connected() || !m_store || m_state == Authorizing) return;
    if (m_state == Syncing) {
        m_again = true;
        return;
    }
    // Una carpeta de datos ausente (el pendrive sin montar) no se sincroniza:
    // lo que hay en memoria entonces no son las notas, y mezclar con ello o
    // subirlo sería repartir un vacío a todos los equipos.
    if (!m_store->available() || !dataDirAvailable()) return;

    m_error.clear();
    m_queue.clear();
    m_pull.clear();
    m_remoteJson.clear();
    m_rootFiles.clear();
    m_gotFiles = false;
    setState(Syncing);

    findFolder(kRootFolder, "root", [this](const QString &rootId) {
        m_rootId = rootId;
        listFolder(rootId, QString(), &m_rootFiles, [this] { fetchRemoteJson(0); });
    });
}

// Baja los JSON de fuera que hayan cambiado desde la última mezcla. Uno que
// sigue igual no trae nada nuevo: se deja vacío y la mezcla no lo toca.
void DriveSync::fetchRemoteJson(int index) {
    if (m_state != Syncing) return;
    if (index >= jsonFiles().size()) {
        mergeAndPlan();
        return;
    }
    const QString name = jsonFiles().at(index);
    const Remote there = m_rootFiles.value(name);
    if (there.id.isEmpty() || there.md5 == seenMd5(name)) {
        m_remoteJson.append(QJsonObject());
        fetchRemoteJson(index + 1);
        return;
    }
    QUrl url(endpoints().api + "/files/" + there.id);
    url.setQuery("alt=media");
    api("GET", url, {}, {}, [this, index](QNetworkReply *r) {
        if (!ok(r)) return;
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(r->readAll(), &err);
        // Uno que no se entiende no se mezcla: vacío, como si no hubiera nada.
        m_remoteJson.append(err.error == QJsonParseError::NoError ? doc.object() : QJsonObject());
        fetchRemoteJson(index + 1);
    });
}

void DriveSync::mergeAndPlan() {
    bool anything = false;
    for (const QJsonObject &o : m_remoteJson) anything |= !o.isEmpty();

    if (anything) {
        // Mezclar rehace la lista: con el usuario escribiendo, se espera.
        if (canApply && !canApply()) {
            m_remoteJson.clear();
            setState(Idle);
            emit deferred();
            return;
        }
        if (!m_store->available()) {   // el pendrive se fue a mitad de pasada
            fail(L("La carpeta de notas no está disponible."));
            return;
        }
        const Store::MergeResult merged =
            m_store->mergeRemote(m_remoteJson.value(0), m_remoteJson.value(1), m_remoteJson.value(2));
        m_pull = merged.pull;
        // Lo de fuera ya está dentro: la próxima vez, si no ha cambiado, no se
        // vuelve a bajar.
        for (int i = 0; i < jsonFiles().size(); ++i)
            if (!m_remoteJson.value(i).isEmpty())
                setSeenMd5(jsonFiles().at(i), m_rootFiles.value(jsonFiles().at(i)).md5);
    }

    // Y lo de aquí, ya mezclado, sube si difiere de lo que hay allí. Lo que
    // sube es lo compartido (Store::syncPayload), no el fichero local.
    for (const QString &name : m_store->syncableFiles()) {
        const QByteArray payload = m_store->syncPayload(name);
        const QString md5 =
            QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Md5).toHex());
        const Remote there = m_rootFiles.value(name);
        if (!there.id.isEmpty() && there.md5 == md5) continue;
        m_queue.append({false, appDataDir() + "/" + name, name, m_rootId, there.id, payload});
    }
    planAttachments(0);
}

// Los adjuntos que usan las notas, carpeta por carpeta. Los que no usa
// ninguna nota no se tocan en ningún lado: allí se quedan como estaban.
void DriveSync::planAttachments(int index) {
    if (m_state != Syncing) return;
    if (index >= subdirs().size()) {
        transferNext();
        return;
    }
    const QString dir = subdirs().at(index);
    QStringList wanted;
    for (const QString &ref : m_store->attachments())
        if (ref.startsWith(dir + "/")) wanted << ref.mid(dir.size() + 1);
    if (wanted.isEmpty()) {   // sin adjuntos de ese tipo, ni se crea la carpeta
        planAttachments(index + 1);
        return;
    }

    findFolder(dir, m_rootId, [this, index, dir, wanted](const QString &folderId) {
        auto *remote = new QHash<QString, Remote>;
        listFolder(folderId, QString(), remote, [=, this] {
            const QString base = appDataDir() + "/" + dir;
            for (const QString &name : wanted) {
                const QString path = base + "/" + name;
                const Remote there = remote->value(name);
                const bool here = QFileInfo::exists(path);
                if (!here && there.id.isEmpty()) continue;   // no está en ningún sitio
                if (!here) {
                    m_queue.append({true, path, name, folderId, there.id});
                    continue;
                }
                if (!there.id.isEmpty() && there.md5 == fileMd5(path)) continue;
                // Distintos: manda de dónde vino la versión buena de su nota.
                if (!there.id.isEmpty() && m_pull.contains(dir + "/" + name))
                    m_queue.append({true, path, name, folderId, there.id});
                else
                    m_queue.append({false, path, name, folderId, there.id});
            }
            delete remote;
            planAttachments(index + 1);
        });
    });
}

// Una transferencia detrás de otra. Las subidas son siempre reanudables: la
// sencilla (multipart) se queda en 5 MB, y una nota de voz larga los pasa.
// Son dos peticiones: una abre la sesión y la otra manda el contenido.
void DriveSync::transferNext() {
    if (m_state != Syncing) return;   // falló por el camino
    if (m_queue.isEmpty()) {
        finishSync();
        return;
    }
    const Transfer t = m_queue.first();

    if (t.download) {
        QUrl url(endpoints().api + "/files/" + t.fileId);
        url.setQuery("alt=media");
        api("GET", url, {}, {}, [this, t](QNetworkReply *r) {
            if (!ok(r)) return;
            // Crea la subcarpeta (y solo esa: la raíz ya existe, si no no se
            // habría llegado aquí) y escribe entero o nada.
            QDir().mkpath(QFileInfo(t.path).path());
            QSaveFile f(t.path);
            const QByteArray data = r->readAll();
            if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit()) {
                fail(L("No se pudo escribir %1.").arg(t.name));
                return;
            }
            m_gotFiles = true;
            if (!m_queue.isEmpty()) m_queue.removeFirst();
            transferNext();
        });
        return;
    }

    QByteArray content = t.data;
    if (content.isEmpty()) {
        QFile f(t.path);
        if (!f.open(QIODevice::ReadOnly)) {   // se borró entre medias: no es un error
            m_queue.removeFirst();
            transferNext();
            return;
        }
        content = f.readAll();
    }
    const QString mime = QMimeDatabase().mimeTypeForFile(t.path).name();

    QJsonObject meta{{"name", t.name}};
    if (t.fileId.isEmpty()) meta["parents"] = QJsonArray{t.folderId};
    QUrl url(endpoints().upload + "/files" + (t.fileId.isEmpty() ? "" : "/" + t.fileId));
    url.setQuery("uploadType=resumable&fields=id");

    withToken([=, this] {
        QNetworkRequest req(url);
        req.setRawHeader("Authorization", "Bearer " + m_access.toLatin1());
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json; charset=UTF-8");
        req.setRawHeader("X-Upload-Content-Type", mime.toLatin1());
        req.setRawHeader("X-Upload-Content-Length", QByteArray::number(content.size()));
        QNetworkReply *r = m_net->sendCustomRequest(req, t.fileId.isEmpty() ? "POST" : "PATCH",
                                                    QJsonDocument(meta).toJson(QJsonDocument::Compact));
        connect(r, &QNetworkReply::finished, this, [=, this] {
            r->deleteLater();
            if (!ok(r)) return;
            const QUrl session(QString::fromLatin1(r->rawHeader("Location")));
            QNetworkRequest put(session);
            put.setHeader(QNetworkRequest::ContentTypeHeader, mime);
            QNetworkReply *pr = m_net->put(put, content);
            connect(pr, &QNetworkReply::finished, this, [this, pr, t, content] {
                pr->deleteLater();
                if (!ok(pr)) return;
                // Lo que se acaba de subir ya está mezclado: no hay que bajarlo
                // en la próxima pasada solo porque haya cambiado su MD5 allí.
                if (jsonFiles().contains(t.name) && t.folderId == m_rootId)
                    setSeenMd5(t.name, QString::fromLatin1(
                                           QCryptographicHash::hash(content, QCryptographicHash::Md5)
                                               .toHex()));
                if (!m_queue.isEmpty()) m_queue.removeFirst();
                transferNext();
            });
        });
    });
}

// Busca la carpeta y la crea si no está. Con drive.file solo se ven las que
// ha creado la propia aplicación, así que una "Tagoror" hecha a mano por el
// usuario no se confunde con la nuestra; y como el permiso es de la aplicación
// y no del equipo, todos los equipos de la misma cuenta ven la misma.
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
        if (!ok(r)) return;
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
                if (!ok(cr)) return;
                next(QJsonDocument::fromJson(cr->readAll()).object()["id"].toString());
            });
    });
}

void DriveSync::listFolder(const QString &folderId, const QString &pageToken,
                           QHash<QString, Remote> *into, std::function<void()> next) {
    QUrl url(endpoints().api + "/files");
    QUrlQuery q;
    q.addQueryItem("q", QString("%1 in parents and trashed = false").arg(quoted(folderId)));
    q.addQueryItem("fields", "nextPageToken,files(id,name,md5Checksum)");
    q.addQueryItem("pageSize", "1000");
    if (!pageToken.isEmpty()) q.addQueryItem("pageToken", pageToken);
    url.setQuery(q);

    api("GET", url, {}, {}, [=, this](QNetworkReply *r) {
        if (!ok(r)) {
            if (into != &m_rootFiles) delete into;
            return;
        }
        const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
        for (const QJsonValue v : o["files"].toArray()) {
            const QJsonObject f = v.toObject();
            into->insert(f["name"].toString(), {f["id"].toString(), f["md5Checksum"].toString()});
        }
        const QString nextToken = o["nextPageToken"].toString();
        if (!nextToken.isEmpty()) listFolder(folderId, nextToken, into, next);
        else next();
    });
}

void DriveSync::finishSync() {
    m_lastSync = QDateTime::currentDateTime();
    m_remoteJson.clear();
    saveSettings();
    setState(Idle);
    if (m_gotFiles) emit attachmentsArrived();
    if (m_again) {
        m_again = false;
        syncNow();
    }
}
