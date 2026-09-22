#pragma once

#include <QColor>
#include <QDate>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QTime>
#include <QUuid>

#include "core/lang.hpp"

// Una entrada del planificador: un evento (una clase, una reunión) o una tarea
// con hora (entregar una práctica). Tiene inicio y fin, a diferencia de un
// recordatorio, que es un instante; por eso no es una Note, igual que un
// cumpleaños tampoco lo es. Viven en su propio fichero, events.json.
struct Event {
    enum Kind { Meeting, Task };
    // Diaria, semanal y mensual son las que pide un horario: la clase de los
    // martes, el gimnasio a diario, el alquiler el día 1.
    enum Repeat { Once, Daily, Weekly, Monthly };

    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString title;
    Kind kind = Meeting;
    QDate date;                       // el primer día
    QTime start{10, 0};
    QTime end{11, 0};
    Repeat repeat = Once;
    QString category = "work";
    bool remind = false;              // suena kRemindBeforeMin antes de empezar
    QString description;
    // Una tarea repetida se hace muchas veces: se apunta qué días está hecha,
    // no un "hecha" suelto que marcaría todas las vueltas de golpe.
    QList<QDate> doneOn;
    // Inicio (en ms) de la última vuelta que ya avisó. Con un booleano, una
    // clase semanal sonaría una vez en la vida.
    qint64 firedMs = 0;
    qint64 ringingMs = 0;             // solo en memoria: la vuelta que suena ahora
    // Cuándo cambió por última vez, para la sincronización con Drive: entre
    // dos equipos gana la versión más reciente de cada elemento. No lo pone
    // quien edita sino Store al guardar (ver Store::stampChanges), así que
    // ninguna tarjeta tiene que acordarse de tocarlo.
    qint64 updatedMs = 0;

    static constexpr int kRemindBeforeMin = 10;

    // --- categorías ----------------------------------------------------------
    // Fijas por ahora: con cuatro se ordena un horario, y el acento es la de
    // trabajo porque es la que más se usa y la que tiene que parecer "la app".
    struct Category {
        QString id;
        QString label;    // en español; se traduce al pintar con L()
        QColor color;     // inválido = el acento
    };
    static const QList<Category> &categories() {
        static const QList<Category> list{
            {"work", "Trabajo", QColor()},
            {"personal", "Personal", QColor("#6fcf97")},
            {"study", "Estudios", QColor("#4ecdc4")},
            {"other", "Otros", QColor("#b98cff")},
        };
        return list;
    }
    static QColor categoryColor(const QString &id, const QColor &accent) {
        for (const Category &c : categories())
            if (c.id == id) return c.color.isValid() ? c.color : accent;
        return accent;
    }

    // --- cuándo cae ----------------------------------------------------------

    // ¿Cae en ese día? Como Note::occursOn, un día 31 mensual no se corre al 30
    // en los meses cortos: ese mes simplemente no toca.
    bool occursOn(const QDate &d) const {
        if (!date.isValid() || !d.isValid() || d < date) return false;
        switch (repeat) {
            case Daily:   return true;
            case Weekly:  return d.dayOfWeek() == date.dayOfWeek();
            case Monthly: return d.day() == date.day();
            default:      return d == date;
        }
    }

    // Un fin anterior al inicio (o inválido) se toma como una hora de duración:
    // es lo que se quería casi siempre, y un bloque de alto negativo no se
    // puede ni pintar ni pulsar.
    QTime effectiveEnd() const {
        if (end.isValid() && end > start) return end;
        const QTime plus = start.addSecs(3600);
        return plus > start ? plus : QTime(23, 59);
    }

    QDateTime startOn(const QDate &d) const { return QDateTime(d, start); }
    QDateTime endOn(const QDate &d) const { return QDateTime(d, effectiveEnd()); }

    // En horas decimales, para colocar el bloque en la rejilla.
    qreal startHours() const { return start.msecsSinceStartOfDay() / 3600000.0; }
    qreal endHours() const { return effectiveEnd().msecsSinceStartOfDay() / 3600000.0; }

    bool isDoneOn(const QDate &d) const { return kind == Task && doneOn.contains(d); }
    void setDoneOn(const QDate &d, bool on) {
        doneOn.removeAll(d);
        if (on) doneOn.append(d);
    }

    // ¿Tiene que sonar ahora? Mira la vuelta de hoy y la de mañana (una clase a
    // las 00:05 avisa a las 23:55 del día antes). No suena por una vuelta que
    // ya ha terminado: con la aplicación cerrada toda la mañana, al abrirla no
    // tiene sentido avisar de la clase de las nueve. Devuelve el inicio de la
    // vuelta en ms, o 0.
    qint64 alarmDue(const QDateTime &now = QDateTime::currentDateTime()) const {
        if (!remind) return 0;
        for (const QDate &d : {now.date(), now.date().addDays(1)}) {
            if (!occursOn(d) || isDoneOn(d)) continue;
            const QDateTime begins = startOn(d);
            const qint64 key = begins.toMSecsSinceEpoch();
            if (firedMs >= key) continue;
            if (now >= begins.addSecs(-60 * kRemindBeforeMin) && now < endOn(d)) return key;
        }
        return 0;
    }

    bool matches(const QString &query) const {
        if (query.isEmpty()) return true;
        return title.contains(query, Qt::CaseInsensitive) ||
               description.contains(query, Qt::CaseInsensitive);
    }

    // --- serialización -----------------------------------------------------

    QJsonObject toJson() const {
        QJsonObject o;
        o["id"] = id;
        o["title"] = title;
        o["kind"] = kind == Task ? "task" : "event";
        o["date"] = date.toString(Qt::ISODate);
        o["start"] = start.toString("HH:mm");
        o["end"] = end.toString("HH:mm");
        o["repeat"] = int(repeat);
        o["category"] = category;
        o["remind"] = remind;
        o["description"] = description;
        QJsonArray done;
        for (const QDate &d : doneOn) done.append(d.toString(Qt::ISODate));
        o["doneOn"] = done;
        o["fired"] = double(firedMs);
        o["updated"] = double(updatedMs);
        return o;
    }

    static Event *fromJson(const QJsonObject &o) {
        auto *e = new Event;
        e->id = o["id"].toString(e->id);
        e->title = o["title"].toString();
        e->kind = o["kind"].toString() == "task" ? Task : Meeting;
        e->date = QDate::fromString(o["date"].toString(), Qt::ISODate);
        if (const QTime t = QTime::fromString(o["start"].toString(), "HH:mm"); t.isValid())
            e->start = t;
        if (const QTime t = QTime::fromString(o["end"].toString(), "HH:mm"); t.isValid())
            e->end = t;
        e->repeat = Repeat(qBound(0, o["repeat"].toInt(), int(Monthly)));
        e->category = o["category"].toString("work");
        e->remind = o["remind"].toBool();
        e->description = o["description"].toString();
        for (const QJsonValue v : o["doneOn"].toArray())
            if (const QDate d = QDate::fromString(v.toString(), Qt::ISODate); d.isValid())
                e->doneOn.append(d);
        e->firedMs = qint64(o["fired"].toDouble());
        e->updatedMs = qint64(o["updated"].toDouble());
        return e;
    }
};
