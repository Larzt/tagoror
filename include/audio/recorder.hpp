#pragma once

#include <QAudioFormat>
#include <QFile>
#include <QList>
#include <QObject>
#include <QString>

class QAudioSource;
class QIODevice;

// Graba a WAV leyendo el micrófono a pelo con QAudioSource.
//
// Se usa en lugar de QMediaRecorder porque hace falta ver la señal *mientras*
// se graba: el nivel en vivo delata un micro mudo y los picos alimentan la
// onda sin tener que releer el fichero al terminar.
class VoiceRecorder : public QObject {
    Q_OBJECT

public:
    explicit VoiceRecorder(QObject *parent = nullptr);
    ~VoiceRecorder() override;

    // Micrófono elegido en ajustes; vacío = el que diga el sistema. Es
    // estático porque cada tarjeta crea su propio grabador.
    static void setPreferredInput(const QByteArray &id);
    static QByteArray preferredInput();

    bool start(const QString &path);
    void stop();
    bool isRecording() const { return m_source != nullptr; }

    qint64 durationMs() const;
    const QList<int> &peaks() const { return m_peaks; }   // 0..100, una cada 50 ms
    qreal loudest() const { return m_loudest; }
    QString errorText() const { return m_error; }

signals:
    void levelChanged(qreal peak);      // 0..1, pico de la última ventana
    void durationChanged(qint64 ms);
    void finished(bool ok);             // fichero cerrado y cabecera escrita

private:
    void drain();                       // vuelca lo que haya llegado del micro

    QAudioSource *m_source = nullptr;
    QIODevice *m_input = nullptr;       // propiedad de QAudioSource
    QFile m_file;
    QAudioFormat m_format;

    QList<int> m_peaks;
    int m_windowFrames = 0;             // frames por ventana de pico (~50 ms)
    int m_windowSeen = 0;
    int m_windowPeak = 0;
    qint64 m_frames = 0;
    qreal m_loudest = 0.0;
    QString m_error;
};
