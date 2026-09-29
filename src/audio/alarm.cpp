#include "audio/alarm.hpp"
#include "core/note.hpp"
#include "audio/wave.hpp"

#include <QFile>
#include <QSoundEffect>
#include <QTimer>
#include <QUrl>
#include <QtMath>

namespace {

constexpr int kRate = 44100;
constexpr int kBeeps = 2;        // two beeps and a pause, then again
constexpr int kBeepMs = 180;
constexpr int kGapMs = 140;
constexpr int kTailMs = 900;

} // namespace

Alarm::Alarm(QObject *parent) : QObject(parent) {
    m_limit = new QTimer(this);
    m_limit->setSingleShot(true);
    m_limit->setInterval(kMaxRingMs);
    connect(m_limit, &QTimer::timeout, this, &Alarm::stop);
}

/// Two short 880 Hz beeps with a soft envelope (no clicks) and a silent tail.
QString Alarm::ensureToneFile() {
    const QString path = appDataDir() + "/alarm.wav";
    if (QFile::exists(path)) return path;

    QByteArray pcm;
    const auto sine = [&](int ms, bool sound) {
        const int frames = kRate * ms / 1000;
        for (int i = 0; i < frames; ++i) {
            qreal v = 0.0;
            if (sound) {
                // 6 ms ramp in and out.
                const int ramp = kRate * 6 / 1000;
                qreal env = 1.0;
                if (i < ramp) env = qreal(i) / ramp;
                else if (i > frames - ramp) env = qreal(frames - i) / ramp;
                v = qSin(2 * M_PI * 880.0 * i / kRate) * 0.35 * env;
            }
            const qint16 s = qint16(qBound(-1.0, v, 1.0) * 32767);
            pcm.append(char(s & 0xff));
            pcm.append(char((s >> 8) & 0xff));
        }
    };

    for (int i = 0; i < kBeeps; ++i) {
        sine(kBeepMs, true);
        sine(kGapMs, false);
    }
    sine(kTailMs, false);

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return QString();
    f.write(wavHeader(kRate, 1, 16, quint32(pcm.size())));
    f.write(pcm);
    f.close();
    return path;
}

void Alarm::start() {
    // A new alarm while already ringing restarts the minute: it is entitled to a
    // full one.
    m_limit->start();
    if (isRinging()) return;

    const QString tone = ensureToneFile();
    if (tone.isEmpty()) return;

    if (!m_effect) {
        m_effect = new QSoundEffect(this);
        m_effect->setSource(QUrl::fromLocalFile(tone));
        m_effect->setVolume(0.6);
    }
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
