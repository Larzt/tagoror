#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QUuid>

/// A named countdown that can be paused, reset and started again. Idle ones
/// are kept as templates and never deleted on their own.
///
/// A running timer stores when it ends, not how much is left, so time keeps
/// passing while the app is closed or the machine sleeps.
struct Timer {
    enum State { Idle, Running, Paused, Done };

    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString name;
    qint64 totalMs = 0;
    State state = Idle;
    qint64 leftMs = 0;       ///< Idle and Paused: time left.
    qint64 endsAtMs = 0;     ///< Running: the instant it reaches zero.
    /// Last change, for the Drive sync (newest version of each element wins).
    /// Set by Store::stampChanges() on save, never by the editor.
    qint64 updatedMs = 0;

    /// Time left right now. A finished timer is at zero, never negative.
    qint64 remainingMs(qint64 now = QDateTime::currentMSecsSinceEpoch()) const {
        switch (state) {
            case Running: return qMax<qint64>(0, endsAtMs - now);
            case Done:    return 0;
            default:      return leftMs;
        }
    }

    /// From 1 to 0: what the ring draws.
    qreal fraction(qint64 now = QDateTime::currentMSecsSinceEpoch()) const {
        return totalMs > 0 ? qreal(remainingMs(now)) / totalMs : 0.0;
    }

    /// Whether it reached zero while running (what the panel's heartbeat asks).
    bool expired(qint64 now = QDateTime::currentMSecsSinceEpoch()) const {
        return state == Running && now >= endsAtMs;
    }

    bool ringing() const { return state == Done; }

    void start(qint64 now = QDateTime::currentMSecsSinceEpoch()) {
        // A finished or zeroed timer restarts from its full length: "start" on
        // 00:00 cannot mean "ring again now".
        const qint64 left = (state == Done || leftMs <= 0) ? totalMs : leftMs;
        endsAtMs = now + left;
        state = Running;
    }

    void pause(qint64 now = QDateTime::currentMSecsSinceEpoch()) {
        if (state != Running) return;
        leftMs = qMax<qint64>(0, endsAtMs - now);
        state = Paused;
    }

    /// Back to its full length and idle, i.e. back to being a template.
    void reset() {
        leftMs = totalMs;
        endsAtMs = 0;
        state = Idle;
    }

    /// "+1 min" on a ringing timer: silences it and restarts it with @p ms.
    void snooze(qint64 ms, qint64 now = QDateTime::currentMSecsSinceEpoch()) {
        endsAtMs = now + ms;
        state = Running;
    }

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
