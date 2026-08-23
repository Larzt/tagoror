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
    // Cada cuánto vuelve un recordatorio. Once es lo de siempre: suena una vez
    // y se acabó. Weekly y Yearly existen para lo que se repite en el
    // calendario sin tener que volver a escribirlo -- la basura del jueves, un
    // cumpleaños -- y hacen que la nota aparezca en TODAS sus fechas, no solo
    // en la próxima.
    enum Repeat { Once, Weekly, Yearly };

    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    Type type = Text;
    QString title;
    QString body;
    QString due;              // solo Reminder
    QList<CheckItem> items;   // solo Check
    QList<Link> links;        // adjuntos de cualquier tipo
    QList<QString> images;    // adjuntos: nombres de fichero dentro de imageDir()
    // Las imágenes se pliegan por nota, no por panel: una tarjeta con capturas
    // se lee mejor cerrada, y esa elección es de quien la escribió.
    bool imagesHidden = false;
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
    Repeat repeat = Once;     // solo Reminder, y solo con dueAtMs
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

    // Se repite de verdad: sin instante real no hay nada que repetir.
    bool repeats() const { return isScheduled() && repeat != Once; }

    QDateTime dueAt() const { return QDateTime::fromMSecsSinceEpoch(dueAtMs); }

    // ¿Cae este recordatorio en ese día? Para uno normal es su fecha y ya;
    // uno repetido cae en todas las que encajan con el patrón, también las
    // anteriores a dueAtMs -- así un cumpleaños sigue estando en el año pasado
    // cuando se retrocede por el calendario, en vez de aparecer y desaparecer
    // según cuándo sonó por última vez.
    //
    // Un 29 de febrero solo encaja en los años que lo tienen: adelantarlo al
    // 28 sería inventarse una fecha que el usuario no escribió.
    bool occursOn(const QDate &day) const {
        if (!isScheduled() || !day.isValid()) return false;
        const QDate base = dueAt().date();
        switch (repeat) {
            case Weekly: return day.dayOfWeek() == base.dayOfWeek();
            case Yearly: return day.day() == base.day() && day.month() == base.month();
            default:     return day == base;
        }
    }

    // El instante que le toca a un día concreto: la misma hora, otra fecha.
    QDateTime occurrenceOn(const QDate &day) const {
        return QDateTime(day, dueAt().time());
    }

    // El siguiente aviso posterior a 'fromMs'. Cada vuelta se cuenta desde la
    // fecha original y no desde la anterior: encadenar addYears() sobre un 29
    // de febrero lo deja en el 28 al pasar por un año normal, y a partir de ahí
    // el recordatorio se habría mudado de día él solo.
    //
    // Se salta de golpe a la vuelta que toca -- la aplicación puede haber
    // estado cerrada años -- y se afina en el bucle, que además exige que la
    // fecha encaje de verdad con el patrón: por eso un aviso del 29 de febrero
    // vuelve cada cuatro años, que es cuando existe ese día.
    qint64 nextOccurrenceAfter(qint64 fromMs) const {
        if (!repeats()) return dueAtMs;

        const QDateTime base = dueAt();
        const qint64 baseMs = base.toMSecsSinceEpoch();
        int step = 0;
        if (fromMs > baseMs) {
            constexpr qint64 week = 7LL * 24 * 3600 * 1000;
            // La estimación se queda corta a propósito (división entera, año
            // contra año): pasarse saltaría una vuelta, quedarse corto solo
            // cuesta un par de iteraciones.
            step = repeat == Weekly
                       ? int((fromMs - baseMs) / week)
                       : QDateTime::fromMSecsSinceEpoch(fromMs).date().year() - base.date().year();
        }

        // El tope cubre de sobra lo que falte por afinar, incluidos los cuatro
        // años de espera de un 29 de febrero.
        for (int i = 0; i < 16; ++i, ++step) {
            const QDateTime when = repeat == Weekly ? base.addDays(7LL * step)
                                                    : base.addYears(step);
            if (when.toMSecsSinceEpoch() > fromMs && occursOn(when.date()))
                return when.toMSecsSinceEpoch();
        }
        return dueAtMs;
    }

    QString repeatLabel() const {
        switch (repeat) {
            case Weekly: return L("Cada semana");
            case Yearly: return L("Cada año");
            default:     return QString();
        }
    }

    static QString repeatToString(Repeat r) {
        switch (r) {
            case Weekly: return "weekly";
            case Yearly: return "yearly";
            default:     return "once";
        }
    }

    static Repeat repeatFromString(const QString &s) {
        if (s == "weekly") return Weekly;
        if (s == "yearly") return Yearly;
        return Once;
    }

    // Ruta absoluta de una imagen adjunta.
    static QString imagePath(const QString &name) { return imageDir() + "/" + name; }

    // Lo que se enseña como fecha. Con instante real se vuelve a escribir en
    // el idioma de ahora, porque 'due' guarda la etiqueta tal como se generó
    // -- cambiar de idioma dejaría "vie 21 ago 18:00" en un panel en inglés.
    // Sin instante, 'due' es texto libre del usuario y se respeta.
    QString dueLabel() const {
        if (!isScheduled()) return due;
        // El anual se enseña sin año: la gracia de un cumpleaños es el día, y
        // el año que llevara escrito sería siempre el de la próxima vez.
        return Lang::locale().toString(dueAt(), repeat == Yearly ? "d MMM HH:mm"
                                                                : "ddd d MMM HH:mm");
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
        o["repeat"] = repeatToString(repeat);
        o["fired"] = fired;
        o["imagesHidden"] = imagesHidden;

        QJsonArray imgArr;
        for (const QString &name : images) imgArr.append(name);
        o["images"] = imgArr;

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
        n->repeat = repeatFromString(o["repeat"].toString());
        n->fired = o["fired"].toBool();
        n->imagesHidden = o["imagesHidden"].toBool();
        for (const QJsonValue v : o["images"].toArray())
            n->images.append(v.toString());
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
