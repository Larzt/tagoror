#pragma once

#include <QDateTime>
#include <QObject>

class Alarm;
class Store;
struct Birthday;
struct Event;
struct Note;

/// Everything that rings: reminders, birthdays, planner events and timers,
/// and the one tone they share. Split out of Panel, which kept this logic next
/// to the window code. It decides what rings and silences it; repainting the
/// cards, the planner and the dock stays with Panel, which listens.
///
/// The rule that matters (see *Reminders and the alarm* in CLAUDE.md): the
/// tone starts only when something *newly* rings, never on every heartbeat,
/// and anything that stops the last ringing thing stops the tone.
class AlertCenter : public QObject {
    Q_OBJECT

public:
    AlertCenter(Store *store, QObject *parent = nullptr);

    Alarm *alarm() const { return m_alarm; }

    bool anyRinging() const;
    /// Stops the tone if nothing rings any more. Everything that removes or
    /// silences a ringing thing ends here.
    void stopIfQuiet();

    /// Reminders, birthdays and events that came due start ringing, and the
    /// tone with them. @return whether anything new started.
    bool check();
    /// Timers that ran out become Done (ringing). @p running says whether any
    /// still counts. @return whether any ran out now; the tone is started.
    bool tickTimers(bool *running);

    /// A normal reminder is marked as fired; a repeating one moves to its next
    /// turn, which is what makes it ring again.
    void silence(Note *n);
    /// The year is recorded so it rings again next year.
    void silenceBirthday(Birthday *b);
    /// Records which occurrence rang, so next week's rings again.
    void silenceEvent(Event *e);
    /// Opening the panel counts as acknowledging everything. @return whether
    /// anything was ringing.
    bool acknowledgeAll();
    /// After a merge: what was silenced on another machine is silenced here.
    void reconcileMerged();

private:
    Store *m_store;
    Alarm *m_alarm;
};
