#include "ui/alertcenter.hpp"

#include "audio/alarm.hpp"
#include "core/store.hpp"

AlertCenter::AlertCenter(Store *store, QObject *parent)
    : QObject(parent), m_store(store), m_alarm(new Alarm(this)) {}

bool AlertCenter::anyRinging() const {
    for (Note *n : m_store->notes())
        if (n->ringing) return true;
    for (Birthday *b : m_store->birthdays())
        if (b->ringing) return true;
    for (Timer *t : m_store->timers())
        if (t->ringing()) return true;
    for (Event *e : m_store->events())
        if (e->ringingMs) return true;
    return false;
}

void AlertCenter::stopIfQuiet() {
    if (!anyRinging()) m_alarm->stop();
}

bool AlertCenter::check() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    bool started = false;
    for (Note *n : m_store->notes()) {
        if (n->isDue(now) && !n->ringing) {
            n->ringing = true;
            started = true;
        }
    }
    // Birthdays with a time ring on the same heartbeat with the same tone: to
    // whoever hears it, it is the same alarm, and a second mechanism would only
    // be a second way for it to get stuck.
    const QDateTime nowAt = QDateTime::currentDateTime();
    for (Birthday *b : m_store->birthdays()) {
        if (b->alarmDue(nowAt) && !b->ringing) {
            b->ringing = true;
            started = true;
        }
    }
    // Planner events with an alert, the same tone again.
    for (Event *e : m_store->events()) {
        const qint64 key = e->alarmDue(nowAt);
        if (key && e->ringingMs != key) {
            e->ringingMs = key;
            started = true;
        }
    }
    if (started) m_alarm->start();
    return started;
}

bool AlertCenter::tickTimers(bool *running) {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    bool fired = false;
    *running = false;
    for (Timer *t : m_store->timers()) {
        if (t->expired(now)) {
            t->state = Timer::Done;
            fired = true;
        }
        *running |= t->state == Timer::Running;
    }
    if (fired) m_alarm->start();
    return fired;
}

void AlertCenter::silence(Note *n) {
    n->ringing = false;
    if (!n->repeats()) {
        n->fired = true;
        return;
    }
    n->dueAtMs = n->nextOccurrenceAfter(QDateTime::currentMSecsSinceEpoch());
    n->fired = false;
}

void AlertCenter::silenceBirthday(Birthday *b) {
    b->ringing = false;
    // The year is recorded, not a bare "rang": the mark must expire on its own so
    // it rings again next year.
    b->firedYear = QDate::currentDate().year();
}

void AlertCenter::silenceEvent(Event *e) {
    // Records which occurrence rang, not a bare "done": next week the class rings
    // again.
    e->firedMs = e->ringingMs;
    e->ringingMs = 0;
}

bool AlertCenter::acknowledgeAll() {
    if (!anyRinging()) return false;
    for (Note *n : m_store->notes())
        if (n->ringing) silence(n);
    for (Birthday *b : m_store->birthdays())
        if (b->ringing) silenceBirthday(b);
    for (Timer *t : m_store->timers())
        if (t->ringing()) t->reset();
    for (Event *e : m_store->events())
        if (e->ringingMs) silenceEvent(e);
    m_alarm->stop();
    return true;
}

void AlertCenter::reconcileMerged() {
    // What was ringing here may have been silenced on the other machine: the
    // remote version carries the "fired" mark, so it is silenced here too.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const int year = QDate::currentDate().year();
    for (Note *n : m_store->notes())
        if (n->ringing && !n->isDue(now)) n->ringing = false;
    for (Birthday *b : m_store->birthdays())
        if (b->ringing && b->firedYear == year) b->ringing = false;
    for (Event *e : m_store->events())
        if (e->ringingMs && e->firedMs >= e->ringingMs) e->ringingMs = 0;
    stopIfQuiet();
}

