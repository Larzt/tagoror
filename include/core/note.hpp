#pragma once

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QUuid>

#include "core/lang.hpp"
#include "core/paths.hpp"

struct CheckItem {
    QString text;
    bool done = false;
};

// Enlace adjunto a una nota. 'label' es opcional: sin él se muestra la propia
// dirección recortada. Cualquier tipo de nota puede llevarlos.
struct Link {
    QString url;
    QString label;
};

struct Note {
    enum Type { Text, Check, Reminder, Voice };

    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    Type type = Text;
    QString title;
    QString body;
    QString due;              // solo Reminder
    QList<CheckItem> items;   // solo Check
    QList<Link> links;        // adjuntos de cualquier tipo
    QString audio;            // solo Voice: nombre de fichero dentro de audioDir()
    qint64 durationMs = 0;    // solo Voice
    QList<int> peaks;         // solo Voice: onda ya calculada, 0..100
    // Pico absoluto de la toma (0..100, -1 = desconocido). Los peaks están
    // normalizados y no sirven para esto: hace falta saber si la grabación
    // salió muda para poder avisar, también después de reiniciar.
    int level = -1;

    // Recordatorio: 'due' es la etiqueta que se ve y dueAtMs el instante real.
    // Si dueAtMs es 0 la fecha es solo texto libre y no dispara alarma.
    qint64 dueAtMs = 0;
    bool fired = false;       // ya avisó (y se descartó): no vuelve a sonar
    bool ringing = false;     // solo en memoria: está sonando ahora mismo

    // Un pico por debajo del 2% del fondo de escala es inaudible: casi siempre
    // es micrófono mudo o entrada equivocada.
    bool isSilentTake() const { return type == Voice && level >= 0 && level < 2; }

    bool isDue(qint64 nowMs) const {
        return type == Reminder && dueAtMs > 0 && !fired && nowMs >= dueAtMs;
    }

    // Un recordatorio solo cae en un día del calendario si lleva instante
    // real; los de fecha libre ('due' suelto) son etiqueta y no se colocan.
    bool isScheduled() const { return type == Reminder && dueAtMs > 0; }

    QDateTime dueAt() const { return QDateTime::fromMSecsSinceEpoch(dueAtMs); }

    // Lo que se enseña como fecha. Con instante real se vuelve a escribir en
    // el idioma de ahora, porque 'due' guarda la etiqueta tal como se generó
    // -- cambiar de idioma dejaría "vie 21 ago 18:00" en un panel en inglés.
    // Sin instante, 'due' es texto libre del usuario y se respeta.
    QString dueLabel() const {
        return isScheduled() ? Lang::locale().toString(dueAt(), "ddd d MMM HH:mm") : due;
    }
    QDate dueDate() const { return isScheduled() ? dueAt().date() : QDate(); }

    // Ruta absoluta del adjunto; vacía si la nota no tiene audio grabado.
    QString audioPath() const {
        return audio.isEmpty() ? QString() : audioDir() + "/" + audio;
    }

    // --- serialización -----------------------------------------------------

    QJsonObject toJson() const {
        QJsonObject o;
        o["id"] = id;
        o["type"] = typeToString(type);
        o["title"] = title;
        o["body"] = body;
        o["due"] = due;
        o["audio"] = audio;
        o["durationMs"] = durationMs;
        o["dueAtMs"] = dueAtMs;
        o["fired"] = fired;

        QJsonArray peakArr;
        for (int v : peaks) peakArr.append(v);
        o["peaks"] = peakArr;
        o["level"] = level;

        QJsonArray arr;
        for (const CheckItem &it : items)
            arr.append(QJsonObject{{"text", it.text}, {"done", it.done}});
        o["items"] = arr;

        QJsonArray linkArr;
        for (const Link &l : links)
            linkArr.append(QJsonObject{{"url", l.url}, {"label", l.label}});
        o["links"] = linkArr;
        return o;
    }

    static Note *fromJson(const QJsonObject &o) {
        auto *n = new Note;
        n->id = o["id"].toString(QUuid::createUuid().toString(QUuid::WithoutBraces));
        n->type = typeFromString(o["type"].toString());
        n->title = o["title"].toString();
        n->body = o["body"].toString();
        n->due = o["due"].toString();
        n->audio = o["audio"].toString();
        n->durationMs = qint64(o["durationMs"].toDouble());
        n->dueAtMs = qint64(o["dueAtMs"].toDouble());
        n->fired = o["fired"].toBool();
        for (const QJsonValue v : o["peaks"].toArray())
            n->peaks.append(v.toInt());
        n->level = o.contains("level") ? o["level"].toInt(-1) : -1;

        for (const QJsonValue v : o["items"].toArray()) {
            const QJsonObject io = v.toObject();
            n->items.append(CheckItem{io["text"].toString(), io["done"].toBool()});
        }
        for (const QJsonValue v : o["links"].toArray()) {
            const QJsonObject lo = v.toObject();
            n->links.append(Link{lo["url"].toString(), lo["label"].toString()});
        }
        return n;
    }

    // --- helpers -----------------------------------------------------------

    int doneCount() const {
        int c = 0;
        for (const CheckItem &it : items)
            if (it.done) ++c;
        return c;
    }

    bool matches(const QString &query) const {
        if (query.isEmpty()) return true;
        if (title.contains(query, Qt::CaseInsensitive)) return true;
        if (body.contains(query, Qt::CaseInsensitive)) return true;
        for (const CheckItem &it : items)
            if (it.text.contains(query, Qt::CaseInsensitive)) return true;
        for (const Link &l : links)
            if (l.label.contains(query, Qt::CaseInsensitive) ||
                l.url.contains(query, Qt::CaseInsensitive))
                return true;
        return false;
    }

    static QString typeToString(Type t) {
        switch (t) {
            case Check:    return "check";
            case Reminder: return "reminder";
            case Voice:    return "voice";
            default:       return "text";
        }
    }

    static Type typeFromString(const QString &s) {
        if (s == "check")    return Check;
        if (s == "reminder") return Reminder;
        if (s == "voice")    return Voice;
        return Text;
    }
};
