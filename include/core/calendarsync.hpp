#pragma once

#include <QColor>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <functional>
#include <memory>

#include "core/event.hpp"

class DriveSync;
class Store;
class QNetworkReply;

// El planificador con Google Calendar, en los dos sentidos.
//
// No es una conexión aparte: usa la cuenta de Google Drive (DriveSync), que
// pide además los permisos de calendario cuando este equipo lo activa, y corre
// como un paso de cada pasada de Drive — después de mezclar lo que llega de
// los otros equipos y antes de subir lo de aquí, así lo que traiga Google sale
// hacia los otros equipos en la misma pasada.
//
// Cada calendario de Google que se sigue es una categoría del planificador
// ("gcal:<id del calendario>", con su nombre y su color). Seguirlo o dejar de
// seguirlo es crearla o borrarla, y como las categorías viajan por Drive, todos
// los equipos siguen los mismos. Un evento en esa categoría vive en ese
// calendario; los de las categorías de Tagoror se suben al calendario de
// destino (target), que es de cada equipo.
//
// Qué cambió se decide por contenido, no por fechas: Event::gcalHash es la
// huella (fingerprint) de lo que los dos lados tenían la última vez que
// coincidieron. Si lo de aquí ya no da esa huella, se editó aquí; si lo de
// Google no la da, se editó allí; si las dos, gana el más reciente. Con fechas
// dos equipos que traen lo mismo de Google se lo devolverían el uno al otro
// sin fin, porque cada uno lo marca con su propia hora al guardarlo.
//
// Las repeticiones de Google que Tagoror sabe decir (diaria, semanal, mensual,
// anual, con o sin fin) son una serie; las demás (cada dos semanas, lunes y
// miércoles, el segundo martes) se traen vuelta a vuelta, un año por delante,
// y la serie queda escondida (Event::gcalExpanded). Una vuelta de una serie que
// se canceló o se movió en Google es un día saltado en la serie (Event::skip),
// y la que se movió, además, un evento suelto.
//
// Lo borrado aquí se borra en Google, pero solo lo que borró alguien: dejar de
// seguir un calendario quita sus eventos de Tagoror y en Google no los toca.
class CalendarSync : public QObject {
    Q_OBJECT

public:
    struct Calendar {
        QString id;
        QString name;
        QColor color;
        bool writable = false;
        bool primary = false;
        int defaultRemind = -1;   // aviso de serie en minutos, -1 ninguno
    };

    CalendarSync(DriveSync *drive, Store *store);

    // El interruptor de este equipo. Encenderlo sin el permiso de calendario
    // no sincroniza nada hasta que se vuelva a autorizar la cuenta.
    bool enabled() const { return m_enabled; }
    void setEnabled(bool on);

    // La lista de calendarios de la cuenta, tal como llegó en la última pasada.
    const QList<Calendar> &calendars() const { return m_calendars; }
    const Calendar *calendar(const QString &id) const;
    bool isFollowed(const QString &calId) const;
    // Crea su categoría (con el nombre y el color de Google). La de dejar de
    // seguirlo la borra Panel, que es quien sabe qué repintar después.
    void follow(const QString &calId);
    Event::Category *categoryFor(const QString &calId) const;

    // Adónde se suben los eventos de las categorías de Tagoror. Vacío = a
    // ninguno: se quedan aquí.
    QString target() const;
    void setTarget(const QString &calId);

    QString lastError() const { return m_error; }

    // Un paso de la pasada de Drive. Llama a done() siempre, falle lo que falle
    // de Calendar: eso no puede dejar a medias la copia de las notas.
    void run(std::function<void()> done);
    // Al desconectar la cuenta: se olvida todo lo de este equipo.
    void forget();

    static QString categoryId(const QString &calId) { return "gcal:" + calId; }
    // Lo que se comparte de un evento, resumido. Ver Event::gcalHash.
    static QString fingerprint(const Event &e);
    // El evento de Google, como evento de Tagoror. 'base' da lo que Google no
    // sabe (id, avisos ya dados, días saltados); lo demás viene de allí.
    struct Mapped {
        Event event;
        bool representable = true;   // su repetición cabe en Event::Repeat
        QString tagororId;           // lo puso Tagoror al subirlo
        qint64 updatedMs = 0;
    };
    static Mapped fromGoogle(const QJsonObject &item, const Event *base,
                             const QString &defaultCategory, int defaultRemind,
                             const QSet<QString> &knownCategories);
    // Y al revés. Una vuelta de una serie no lleva repetición ni id propio.
    static QJsonObject toGoogle(const Event &e);

signals:
    // Han cambiado eventos: el planificador y las alarmas tienen que mirarlo.
    void eventsChanged();
    // Ha cambiado lo que enseñan los ajustes (lista, destino, error).
    void changed();

private:
    using Done = std::function<void(QNetworkReply *)>;
    void request(const QByteArray &verb, const QString &path, const QString &query,
                 const QJsonObject &body, Done done);
    // Respuesta 2xx. Si no, apunta el error (el de Google, si lo dice).
    bool ok(QNetworkReply *r, const QString &what);
    static int status(QNetworkReply *r);

    void fetchCalendars(const QString &pageToken);
    void listEvents(int index, const QString &pageToken);
    void apply();
    void handleItem(const QString &cal, const QJsonObject &item);
    // Deja el evento de aquí como diga el de allí, salvo que lo de aquí sea
    // una edición más reciente (que se sube después). Devuelve el de aquí.
    Event *upsert(Event *local, const Mapped &m, const QString &cal, const QString &gid);
    void removeLocal(Event *e);
    void expandNext();
    // Por id y no por puntero: entre petición y respuesta el usuario ha podido
    // borrar eventos, y un puntero de antes podría no valer ya.
    void fetchInstances(const QString &cal, const QString &masterGid, const QString &pageToken,
                        std::shared_ptr<QSet<QString>> seen);
    void planPushes();
    void pushNext();
    void planDeletes();
    void deleteNext();
    void finish();

    QString activeTarget() const;
    QString desiredCalendar(const Event *e) const;
    bool active(const QString &calId) const;
    Event *byGoogle(const QString &cal, const QString &gid) const;
    Event *byId(const QString &id) const;
    static QString derivedId(const QString &cal, const QString &gid);
    QSet<QString> knownCategories() const;
    void load();
    void saveState();

    DriveSync *m_drive = nullptr;
    Store *m_store = nullptr;
    bool m_enabled = false;
    bool m_seeded = false;   // la primera vez se sigue el principal
    QString m_target;
    QList<Calendar> m_calendars;
    QHash<QString, QString> m_tokens;     // calendario -> syncToken
    // id de evento enlazado -> "calendario\nid de Google", de la última
    // pasada: así se sabe qué hay que borrar en Google cuando desaparece aquí.
    QHash<QString, QString> m_links;
    QHash<QString, QString> m_expandedDay;   // serie -> día de la última expansión
    QString m_error;

    // Pasada en curso. m_pass descarta las respuestas de una pasada anterior
    // que ya no espera nadie (Drive falló a medias y empezó otra).
    std::function<void()> m_done;
    int m_pass = 0;
    bool m_running = false;
    bool m_applied = false;                // se llegó a mezclar
    QList<Calendar> m_listing;             // la lista de calendarios, a medio leer
    QStringList m_active;
    QHash<QString, QList<QJsonObject>> m_fetched;
    QHash<QString, QString> m_newTokens;
    QSet<QString> m_full;                  // calendarios leídos enteros
    QSet<QString> m_failed;                // calendarios que no se pudieron leer
    QList<QPair<QString, QString>> m_expandQueue;   // calendario, serie
    QSet<QString> m_touched;               // series expandidas que cambiaron ahora
    struct Push {
        enum Kind { Insert, Patch, Move } kind;
        QString eventId;
        QString cal;       // Insert: dónde; Move: adónde
    };
    QList<Push> m_pushes;
    struct Delete {
        QString id;    // de Tagoror; vacío en una copia duplicada
        QString cal;
        QString gid;
    };
    QList<Delete> m_deletes;
    bool m_changed = false;
};
