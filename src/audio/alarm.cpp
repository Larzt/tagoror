#include "audio/alarm.hpp"
#include "core/lang.hpp"
#include "core/note.hpp"
#include "audio/wave.hpp"

#include <QFile>
#include <QFileInfo>
#include <QSoundEffect>
#include <QTimer>
#include <QUrl>
#include <QtMath>

#include <cmath>

namespace {

constexpr int kRate = 44100;
constexpr qint64 kMaxCustomBytes = 8 * 1024 * 1024;   // QSoundEffect holds it all in memory

/// 16-bit mono PCM, built up one stretch at a time.
class Synth {
public:
    /// @p voice gives the sample at time t (seconds since the stretch began),
    /// before the 6 ms ramps in and out that keep it from clicking.
    template <typename F>
    void tone(int ms, F voice) {
        const int frames = kRate * ms / 1000;
        const int ramp = kRate * 6 / 1000;
        for (int i = 0; i < frames; ++i) {
            qreal env = 1.0;
            if (i < ramp) env = qreal(i) / ramp;
            else if (i > frames - ramp) env = qreal(frames - i) / ramp;
            push(voice(qreal(i) / kRate) * env);
        }
    }
    void sine(int ms, qreal hz) {
        tone(ms, [hz](qreal t) { return qSin(2 * M_PI * hz * t) * 0.35; });
    }
    void silence(int ms) {
        for (int i = 0; i < kRate * ms / 1000; ++i) push(0.0);
    }
    bool write(const QString &path) const {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        f.write(wavHeader(kRate, 1, 16, quint32(m_pcm.size())));
        f.write(m_pcm);
        return true;
    }

private:
    void push(qreal v) {
        const qint16 s = qint16(qBound(-1.0, v, 1.0) * 32767);
        m_pcm.append(char(s & 0xff));
        m_pcm.append(char((s >> 8) & 0xff));
    }
    QByteArray m_pcm;
};

/// Each built-in tone is one loop of the alarm, silent tail included.
void synthesise(const QString &id, Synth &s) {
    if (id == "bell") {
        // One strike with its overtones, dying away.
        s.tone(1500, [](qreal t) {
            const qreal decay = qExp(-t * 3.2);
            return (qSin(2 * M_PI * 660 * t) + 0.45 * qSin(2 * M_PI * 1320 * t) +
                    0.2 * qSin(2 * M_PI * 1980 * t)) * 0.24 * decay;
        });
        s.silence(500);
    } else if (id == "digital") {
        // Four quick beeps, a softened square: the bedside clock.
        for (int i = 0; i < 4; ++i) {
            s.tone(70, [](qreal t) { return std::tanh(3 * qSin(2 * M_PI * 1200 * t)) * 0.22; });
            s.silence(70);
        }
        s.silence(600);
    } else if (id == "rise") {
        // Three notes going up.
        for (const qreal hz : {660.0, 880.0, 1100.0}) {
            s.sine(160, hz);
            s.silence(40);
        }
        s.silence(800);
    } else {
        // "beeps", the only tone before 5.0.4: two short 880 Hz beeps.
        for (int i = 0; i < 2; ++i) {
            s.sine(180, 880.0);
            s.silence(140);
        }
        s.silence(900);
    }
}

} // namespace

QStringList Alarm::tones() {
    return {"beeps", "bell", "digital", "rise", "custom"};
}

QString Alarm::customPath() {
    return appDataDir() + "/alarm-custom.wav";
}

QString Alarm::importCustom(const QString &wav) {
    const QFileInfo info(wav);
    if (info.size() > kMaxCustomBytes) return L("El archivo es demasiado grande (máximo 8 MB).");
    // The same reader as voice notes: if it cannot walk the file, QSoundEffect
    // would fail silently and the alarm would ring with nothing.
    if (!scanWave(wav, 4).ok) return L("Solo se admiten archivos WAV PCM de 16 bits.");
    const QString to = customPath();
    if (QFileInfo(to) == info) return QString();   // picked the copy itself
    QFile::remove(to);
    if (!QFile::copy(wav, to)) return L("No se pudo copiar el archivo.");
    return QString();
}

Alarm::Alarm(QObject *parent) : QObject(parent) {
    m_limit = new QTimer(this);
    m_limit->setSingleShot(true);
    m_limit->setInterval(kMaxRingMs);
    connect(m_limit, &QTimer::timeout, this, &Alarm::stop);
}

void Alarm::setTone(const QString &id) {
    m_tone = tones().contains(id) ? id : QStringLiteral("beeps");
    // Rebuilt on next use: the same URL would not reload a replaced custom file.
    delete m_preview;
    m_preview = nullptr;
    if (!isRinging()) {
        delete m_effect;
        m_effect = nullptr;
    }
}

void Alarm::setVolume(int percent) {
    m_volume = qBound(0, percent, 100) / 100.0;
    if (m_effect) m_effect->setVolume(m_volume);
    if (m_preview) m_preview->setVolume(m_volume);
}

QString Alarm::toneFile() {
    if (m_tone == "custom") {
        if (QFile::exists(customPath())) return customPath();
        // Its file went with an old data folder: the default rather than silence.
    }
    const QString id = m_tone == "custom" ? QStringLiteral("beeps") : m_tone;
    // The default keeps the name it always had.
    const QString path = appDataDir() + (id == "beeps" ? "/alarm.wav" : "/alarm-" + id + ".wav");
    if (QFile::exists(path)) return path;

    Synth s;
    synthesise(id, s);
    return s.write(path) ? path : QString();
}

void Alarm::start() {
    // A new alarm while already ringing restarts the minute: it is entitled to a
    // full one.
    m_limit->start();
    if (isRinging()) return;

    const QString tone = toneFile();
    if (tone.isEmpty()) return;

    if (!m_effect || m_effect->source() != QUrl::fromLocalFile(tone)) {
        delete m_effect;
        m_effect = new QSoundEffect(this);
        m_effect->setSource(QUrl::fromLocalFile(tone));
    }
    m_effect->setVolume(m_volume);
    m_effect->setLoopCount(QSoundEffect::Infinite);
    m_effect->play();
}

void Alarm::stop() {
    m_limit->stop();
    if (m_effect) m_effect->stop();
}

bool Alarm::isRinging() const {
    return m_effect && m_effect->isPlaying();
}

void Alarm::preview() {
    // While a real alarm rings, that is the preview.
    if (isRinging()) return;
    const QString tone = toneFile();
    if (tone.isEmpty()) return;
    if (!m_preview) {
        m_preview = new QSoundEffect(this);
        m_preview->setSource(QUrl::fromLocalFile(tone));
    }
    m_preview->setVolume(m_volume);
    m_preview->setLoopCount(1);
    m_preview->stop();
    m_preview->play();
}
