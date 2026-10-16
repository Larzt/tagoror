#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class QSoundEffect;
class QTimer;

/// The alarm tone for reminders, timers, events and birthdays.
///
/// The built-in tones are synthesised into alarm*.wav on first use, so there
/// is no bundled asset and no dependency on a system sound theme. A custom
/// tone is a WAV the user picked, copied into the data folder. It loops until
/// stopped or until kMaxRingMs passes; that only silences the sound, whatever
/// rang stays red until someone acknowledges it.
class Alarm : public QObject {
    Q_OBJECT

public:
    static constexpr int kMaxRingMs = 60 * 1000;

    /// The tone ids as stored in the prefs, in the order Settings lists them.
    /// "custom" is last and only plays when customPath() exists.
    static QStringList tones();
    static QString customPath();
    /// Copies @p wav to customPath(). @return An error to show, or empty.
    static QString importCustom(const QString &wav);

    explicit Alarm(QObject *parent = nullptr);

    /// An unknown id, or "custom" without its file, plays the default.
    void setTone(const QString &id);
    QString tone() const { return m_tone; }
    void setVolume(int percent);   ///< 0..100

    void start();
    void stop();
    bool isRinging() const;
    /// The current tone once, at the current volume, for Settings.
    void preview();

private:
    QString toneFile();   ///< Generates the built-in tone's file if missing.

    QString m_tone = QStringLiteral("beeps");
    qreal m_volume = 0.6;
    QSoundEffect *m_effect = nullptr;
    QSoundEffect *m_preview = nullptr;
    QTimer *m_limit = nullptr;   ///< Cuts the loop after kMaxRingMs.
};
