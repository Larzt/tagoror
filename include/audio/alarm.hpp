#pragma once

#include <QObject>
#include <QString>

class QSoundEffect;
class QTimer;

/// The alarm tone for reminders, timers, events and birthdays.
///
/// The tone is synthesised into alarm.wav on first use, so there is no bundled
/// asset and no dependency on a system sound theme. It loops until stopped or
/// until kMaxRingMs passes; that only silences the sound, whatever rang stays
/// red until someone acknowledges it.
class Alarm : public QObject {
    Q_OBJECT

public:
    static constexpr int kMaxRingMs = 60 * 1000;

    explicit Alarm(QObject *parent = nullptr);

    void start();
    void stop();
    bool isRinging() const;

private:
    QString ensureToneFile();   ///< Generates alarm.wav if missing.

    QSoundEffect *m_effect = nullptr;
    QTimer *m_limit = nullptr;   ///< Cuts the loop after kMaxRingMs.
};
