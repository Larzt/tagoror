#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QUuid>

// Un temporizador: una cuenta atrás con nombre que se puede pausar, reiniciar
// y volver a lanzar. Los que no están en marcha no se borran solos: se quedan
// como plantillas ("Pomodoro", "Pausa corta"), que es para lo que se guardan.
//
// Mientras corre no se apunta lo que queda sino cuándo acaba. Así el tiempo
// sigue pasando con la aplicación cerrada o el equipo suspendido, igual que en
// un temporizador de cocina, y al volver lo que queda es la resta y no lo que
// hubiera en memoria la última vez que alguien miró.
struct Timer {
    enum State { Idle, Running, Paused, Done };

    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString name;
    qint64 totalMs = 0;
    State state = Idle;
    qint64 leftMs = 0;       // Idle y Paused: lo que queda
    qint64 endsAtMs = 0;     // Running: el instante en que llega a cero
    // Cuándo cambió por última vez, para la sincronización con Drive: entre
    // dos equipos gana la versión más reciente de cada elemento. No lo pone
    // quien edita sino Store al guardar (ver Store::stampChanges), así que
    // ninguna tarjeta tiene que acordarse de tocarlo.
    qint64 updatedMs = 0;

    // Lo que queda ahora mismo. Terminado es cero, no negativo: el contador
    // no sigue bajando mientras suena.
    qint64 remainingMs(qint64 now = QDateTime::currentMSecsSinceEpoch()) const {
        switch (state) {
            case Running: return qMax<qint64>(0, endsAtMs - now);
            case Done:    return 0;
            default:      return leftMs;
        }
    }

    // De 1 a 0: lo que se pinta en el anillo.
    qreal fraction(qint64 now = QDateTime::currentMSecsSinceEpoch()) const {
        return totalMs > 0 ? qreal(remainingMs(now)) / totalMs : 0.0;
    }

    // ¿Ha llegado a cero mientras corría? Es lo que el latido del panel
    // pregunta para hacerlo sonar.
    bool expired(qint64 now = QDateTime::currentMSecsSinceEpoch()) const {
        return state == Running && now >= endsAtMs;
    }

    bool ringing() const { return state == Done; }

    void start(qint64 now = QDateTime::currentMSecsSinceEpoch()) {
        // Uno terminado o a cero vuelve a empezar entero: darle a "iniciar"
        // sobre un 00:00 no puede querer decir "suena otra vez ya".
        const qint64 left = (state == Done || leftMs <= 0) ? totalMs : leftMs;
        endsAtMs = now + left;
        state = Running;
    }

    void pause(qint64 now = QDateTime::currentMSecsSinceEpoch()) {
        if (state != Running) return;
        leftMs = qMax<qint64>(0, endsAtMs - now);
        state = Paused;
    }

    // Vuelve a su duración y se queda quieto: de vuelta a plantilla.
    void reset() {
        leftMs = totalMs;
        endsAtMs = 0;
        state = Idle;
    }

    // "+1 min" sobre uno que suena: lo calla y lo relanza con un minuto.
    void snooze(qint64 ms, qint64 now = QDateTime::currentMSecsSinceEpoch()) {
        endsAtMs = now + ms;
        state = Running;
    }

    // --- serialización -----------------------------------------------------

    QJsonObject toJson() const {
        QJsonObject o;
        o["id"] = id;
        o["name"] = name;
        o["total"] = double(totalMs);
        o["state"] = int(state);
        o["left"] = double(leftMs);
        o["endsAt"] = double(endsAtMs);
        o["updated"] = double(updatedMs);
        return o;
    }

    static Timer *fromJson(const QJsonObject &o) {
        auto *t = new Timer;
        t->id = o["id"].toString(t->id);
        t->name = o["name"].toString();
        t->totalMs = qint64(o["total"].toDouble());
        t->state = State(qBound(0, o["state"].toInt(), int(Done)));
        t->leftMs = qint64(o["left"].toDouble(double(t->totalMs)));
        t->endsAtMs = qint64(o["endsAt"].toDouble());
        t->updatedMs = qint64(o["updated"].toDouble());
        return t;
    }
};
