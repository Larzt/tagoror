#include "audio/recorder.hpp"
#include "audio/wave.hpp"

#include <QAudioDevice>
#include <QAudioSource>
#include <QMediaDevices>

namespace {
constexpr int kWindowMs = 50;    // resolución de la onda mientras se graba
constexpr int kBits = 16;
}

namespace {
QByteArray g_preferredInput;   // id del dispositivo elegido en ajustes
}

void VoiceRecorder::setPreferredInput(const QByteArray &id) { g_preferredInput = id; }
QByteArray VoiceRecorder::preferredInput() { return g_preferredInput; }

VoiceRecorder::VoiceRecorder(QObject *parent) : QObject(parent) {}

VoiceRecorder::~VoiceRecorder() { stop(); }

bool VoiceRecorder::start(const QString &path) {
    stop();
    m_peaks.clear();
    m_frames = 0;
    m_loudest = 0.0;
    m_windowSeen = 0;
    m_windowPeak = 0;
    m_error.clear();

    // Si el dispositivo guardado ya no está (auriculares desconectados), se
    // vuelve al del sistema en lugar de fallar.
    QAudioDevice dev = QMediaDevices::defaultAudioInput();
    if (!g_preferredInput.isEmpty()) {
        for (const QAudioDevice &d : QMediaDevices::audioInputs())
            if (d.id() == g_preferredInput) { dev = d; break; }
    }
    if (dev.isNull()) {
        m_error = "No hay micrófono disponible";
        emit finished(false);
        return false;
    }

    // Se fuerza Int16 (la onda y el WAV asumen enteros de 16 bits); si el
    // dispositivo no lo acepta tal cual, se prueba con 48 kHz mono.
    m_format = dev.preferredFormat();
    m_format.setSampleFormat(QAudioFormat::Int16);
    if (!dev.isFormatSupported(m_format)) {
        m_format.setSampleRate(48000);
        m_format.setChannelCount(1);
    }
    if (!dev.isFormatSupported(m_format)) {
        m_error = "El micrófono no admite PCM 16 bits";
        emit finished(false);
        return false;
    }

    m_file.setFileName(path);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_error = "No se pudo escribir " + path;
        emit finished(false);
        return false;
    }
    // Cabecera provisional: los tamaños se reescriben al parar.
    m_file.write(wavHeader(m_format.sampleRate(), m_format.channelCount(), kBits, 0));

    m_windowFrames = qMax(1, m_format.sampleRate() * kWindowMs / 1000);
    m_source = new QAudioSource(dev, m_format, this);
    m_input = m_source->start();
    if (!m_input) {
        m_error = "No se pudo abrir el micrófono";
        delete m_source;
        m_source = nullptr;
        m_file.close();
        emit finished(false);
        return false;
    }
    connect(m_input, &QIODevice::readyRead, this, &VoiceRecorder::drain);
    return true;
}

void VoiceRecorder::drain() {
    if (!m_input || !m_source) return;

    const QByteArray chunk = m_input->readAll();
    if (chunk.isEmpty()) return;
    m_file.write(chunk);

    const int channels = m_format.channelCount();
    const auto *samples = reinterpret_cast<const qint16 *>(chunk.constData());
    const qint64 frames = chunk.size() / 2 / channels;

    for (qint64 i = 0; i < frames; ++i) {
        int peak = 0;
        for (int c = 0; c < channels; ++c)
            peak = qMax(peak, qAbs(int(samples[i * channels + c])));
        m_windowPeak = qMax(m_windowPeak, peak);

        if (++m_windowSeen >= m_windowFrames) {
            const qreal v = m_windowPeak / 32768.0;
            m_loudest = qMax(m_loudest, v);
            m_peaks.append(int(qBound(0.0, v, 1.0) * 100));
            emit levelChanged(v);
            m_windowSeen = 0;
            m_windowPeak = 0;
        }
    }

    m_frames += frames;
    emit durationChanged(durationMs());
}

void VoiceRecorder::stop() {
    if (!m_source) return;

    disconnect(m_input, nullptr, this, nullptr);
    m_source->stop();
    drain();                     // lo que quedara en el buffer
    m_input = nullptr;
    m_source->deleteLater();
    m_source = nullptr;

    const quint32 dataBytes = quint32(m_frames * m_format.channelCount() * kBits / 8);
    m_file.seek(0);
    m_file.write(wavHeader(m_format.sampleRate(), m_format.channelCount(), kBits, dataBytes));
    m_file.close();

    // La onda se normaliza al pico propio, igual que scanWave().
    if (m_loudest > 0.02) {
        for (int &v : m_peaks)
            v = int(qBound(0.0, v / 100.0 / m_loudest, 1.0) * 100);
    }
    emit finished(true);
}

qint64 VoiceRecorder::durationMs() const {
    const int rate = m_format.sampleRate();
    return rate > 0 ? m_frames * 1000 / rate : 0;
}
