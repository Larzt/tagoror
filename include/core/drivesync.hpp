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
class QNetworkAccessManager;
class QNetworkReply;
class QTcpServer;
class QTcpSocket;

// Sincronización de las notas entre equipos a través de Google Drive.
//
// Cada equipo guarda en local como siempre, y además comparte una carpeta
// "Tagoror" en la unidad del usuario. Sincronizar es: bajar lo que haya allí,
// mezclarlo con lo de aquí elemento a elemento (Store::mergeRemote: gana la
// versión más reciente de cada nota, evento, cumpleaños o temporizador; lo
// borrado se borra), guardar, y subir el resultado. Nunca se sustituye un
// fichero entero por otro, que es lo que haría que un equipo pisara al otro.
//
// Si dos equipos suben a la vez, el que queda pisado en Drive sigue teniendo lo
// suyo en local y lo vuelve a poner en la siguiente pasada: nada se pierde, a
// lo sumo tarda una vuelta más.
//
// Los adjuntos (audio/, images/) viajan con sus notas: se bajan los que faltan
// aquí o los de las notas cuya versión buena vino de fuera, y se suben los
// demás. Solo se transfiere lo que difiere (MD5 local contra el de Drive), y un
// JSON de fuera que no ha cambiado desde la última mezcla ni se baja.
//
// La autorización es la de las aplicaciones de escritorio de Google: se abre
// el navegador, Google redirige a un puerto local que escucha aquí (loopback)
// y el código se canjea con PKCE. El permiso pedido es drive.file, el más
// estrecho que hay: la aplicación solo ve los ficheros que ella misma crea, no
// el resto de la unidad.
//
// Necesita un ID de cliente OAuth registrado en Google Cloud, que se pone al
// compilar (TAGOROR_GOOGLE_CLIENT_ID y _SECRET en CMake). Sin él, configured()
// es falso y ajustes enseña la fila desactivada.
//
// El token de refresco se guarda en QSettings y no en notes.json: la cuenta es
// del equipo, no de la carpeta de datos, y así no viaja al pendrive ni entra en
// las copias de seguridad.
class DriveSync : public QObject {
    Q_OBJECT

public:
    enum State {
        Unavailable,    // compilado sin ID de cliente
        Disconnected,
        Authorizing,    // esperando al navegador
        Idle,           // conectado
        Syncing,
        Failed,         // conectado, pero la última subida falló
    };

    explicit DriveSync(Store *store, QObject *parent = nullptr);

    // Lo pregunta antes de mezclar: si el usuario está escribiendo, mezclar
    // rehace la lista y le quita el cursor. Con false, la pasada se deja para
    // la siguiente (deferred()).
    std::function<bool()> canApply;

    static bool configured();

    State state() const { return m_state; }
    bool connected() const { return !m_refresh.isEmpty(); }
    QString account() const { return m_account; }
    QDateTime lastSync() const { return m_lastSync; }
    QString lastError() const { return m_error; }

    void connectAccount();
    void cancel();
    void disconnectAccount();
    // Una pasada completa: bajar, mezclar, subir. Si ya hay una en marcha, se
    // apunta y se repite al terminar: lo guardado entre medias también cuenta.
    void syncNow();

    // Las direcciones de Google. Son configurables solo para poder probar la
    // subida contra un servidor falso; la aplicación usa las de serie.
    struct Endpoints {
        QString auth = "https://accounts.google.com/o/oauth2/v2/auth";
        QString token = "https://oauth2.googleapis.com/token";
        QString revoke = "https://oauth2.googleapis.com/revoke";
        QString api = "https://www.googleapis.com/drive/v3";
        QString upload = "https://www.googleapis.com/upload/drive/v3";
    };
    static Endpoints &endpoints();

signals:
    // El panel abre esto en el navegador: core no sabe de escritorio.
    void openUrl(const QUrl &url);
    void changed();
    // Han llegado adjuntos de otro equipo: las tarjetas que los enseñan se
    // construyeron sin ellos.
    void attachmentsArrived();
    // No se ha podido mezclar porque el usuario estaba escribiendo.
    void deferred();

private:
    struct Transfer {
        bool download = false;
        QString path;       // en disco
        QString name;       // en Drive
        QString folderId;
        QString fileId;     // al subir, vacío = crearlo
        QByteArray data;    // al subir: si no está vacío, esto y no el fichero
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

    // Pide (o renueva) el token de acceso y sigue.
    void withToken(std::function<void()> next);
    // Una petición a la API con el token puesto. Un 401 renueva el token y la
    // repite una vez: caducan cada hora.
    void api(const QByteArray &verb, const QUrl &url, const QByteArray &body,
             const QByteArray &contentType, Done done, bool retried = false);
    // Ok = respuesta 2xx; si no, fail() con el mensaje de Google y false.
    bool ok(QNetworkReply *reply);

    // Pasos de una pasada.
    void findFolder(const QString &name, const QString &parent,
                    std::function<void(const QString &)> next);
    void listFolder(const QString &folderId, const QString &pageToken,
                    QHash<QString, Remote> *into, std::function<void()> next);
    void fetchRemoteJson(int index);   // los tres JSON, uno detrás de otro
    void mergeAndPlan();
    void planAttachments(int index);   // audio/ e images/
    void transferNext();
    void finishSync();
    // El MD5 del JSON de fuera que ya se mezcló: si no ha cambiado, no se baja.
    QString seenMd5(const QString &name) const;
    void setSeenMd5(const QString &name, const QString &md5);

    QNetworkAccessManager *m_net = nullptr;
    State m_state = Disconnected;
    QString m_refresh;
    QString m_access;
    QDateTime m_accessUntil;
    QString m_account;
    QDateTime m_lastSync;
    QString m_error;

    // Autorización en curso.
    QTcpServer *m_server = nullptr;
    QString m_verifier;
    QString m_stateToken;
    QString m_redirect;
    bool m_authorized = false;   // el código ya llegó: lo demás es el navegador repitiendo
    int m_attempt = 0;   // para que un tiempo de espera viejo no cancele uno nuevo

    Store *m_store = nullptr;

    // Pasada en curso.
    QList<Transfer> m_queue;
    bool m_again = false;
    bool m_gotFiles = false;               // se bajó algún adjunto
    QString m_rootId;
    QHash<QString, Remote> m_rootFiles;    // lo que hay en la carpeta Tagoror
    QList<QJsonObject> m_remoteJson;       // notes, birthdays, events (vacío = nada nuevo)
    QSet<QString> m_pull;                  // adjuntos que hay que bajar sí o sí
};
