#pragma once

#include <QByteArray>
#include <QDataStream>
#include <QFile>
#include <QList>
#include <QString>
#include <QtEndian>

// Lectura mínima de WAV PCM para dibujar la onda. Solo se necesita entender
// los ficheros que graba la propia app (PCM 16 bits), así que no se enlaza
// ningún decodificador: se recorre el RIFF a mano.
// Cabecera RIFF/WAVE de 44 bytes para PCM entero.
inline QByteArray wavHeader(int sampleRate, int channels, int bits, quint32 dataBytes) {
    const quint32 byteRate = quint32(sampleRate) * channels * bits / 8;
    const quint16 blockAlign = quint16(channels * bits / 8);

    QByteArray h;
    QDataStream s(&h, QIODevice::WriteOnly);
    s.setByteOrder(QDataStream::LittleEndian);

    s.writeRawData("RIFF", 4);
    s << quint32(36 + dataBytes);
    s.writeRawData("WAVE", 4);

    s.writeRawData("fmt ", 4);
    s << quint32(16) << quint16(1) << quint16(channels)
      << quint32(sampleRate) << byteRate << blockAlign << quint16(bits);

    s.writeRawData("data", 4);
    s << quint32(dataBytes);
    return h;
}

struct WaveScan {
    QList<int> peaks;      // 0..100, normalizados al pico del propio fichero
    qreal loudest = 0.0;   // pico absoluto 0..1, para detectar tomas mudas
    bool ok = false;
};

inline WaveScan scanWave(const QString &path, int buckets = 56) {
    WaveScan out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return out;

    const QByteArray raw = f.readAll();
    if (raw.size() < 44 || raw.left(4) != "RIFF" || raw.mid(8, 4) != "WAVE") return out;

    int channels = 0, bits = 0;
    qint64 dataAt = -1, dataLen = 0;

    // Recorrido de chunks: fmt y data pueden venir en cualquier orden y con
    // otros chunks (LIST, fact…) intercalados.
    qint64 pos = 12;
    while (pos + 8 <= raw.size()) {
        const QByteArray id = raw.mid(pos, 4);
        const quint32 len = qFromLittleEndian<quint32>(
            reinterpret_cast<const uchar *>(raw.constData()) + pos + 4);
        const qint64 body = pos + 8;

        if (id == "fmt " && body + 16 <= raw.size()) {
            const auto *p = reinterpret_cast<const uchar *>(raw.constData()) + body;
            channels = qFromLittleEndian<quint16>(p + 2);
            bits = qFromLittleEndian<quint16>(p + 14);
        } else if (id == "data") {
            dataAt = body;
            dataLen = qMin<qint64>(len, raw.size() - body);
        }
        pos = body + len + (len & 1);   // los chunks van alineados a 2 bytes
    }

    if (dataAt < 0 || bits != 16 || channels <= 0) return out;

    const auto *samples = reinterpret_cast<const qint16 *>(raw.constData() + dataAt);
    const qint64 frames = dataLen / 2 / channels;
    if (frames <= 0) return out;

    QList<qreal> rough;
    rough.reserve(buckets);
    for (int b = 0; b < buckets; ++b) {
        const qint64 from = frames * b / buckets;
        const qint64 to = qMax(from + 1, frames * (b + 1) / buckets);

        int peak = 0;
        for (qint64 i = from; i < to; ++i)
            for (int c = 0; c < channels; ++c)
                peak = qMax(peak, qAbs(int(samples[i * channels + c])));
        const qreal v = peak / 32768.0;
        out.loudest = qMax(out.loudest, v);
        rough.append(v);
    }

    // Se normaliza al pico propio (como WhatsApp): una toma floja sigue
    // teniéndose que ver. Si es prácticamente muda no se amplifica el ruido.
    const qreal scale = out.loudest > 0.02 ? 1.0 / out.loudest : 1.0;
    for (qreal v : rough)
        out.peaks.append(int(qBound(0.0, v * scale, 1.0) * 100));

    out.ok = true;
    return out;
}
