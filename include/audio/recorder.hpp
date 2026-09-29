#pragma once

#include <QAudioFormat>
#include <QFile>
#include <QList>
#include <QObject>
#include <QString>

class QAudioSource;
class QIODevice;

/// Records to WAV by reading the microphone directly through QAudioSource.
///
/// Used instead of QMediaRecorder because the signal must be visible while
/// recording: the live level reveals a muted microphone, and the peaks feed
/// the waveform without re-reading the file.
class VoiceRecorder : public QObject {
    Q_OBJECT

public:
    explicit VoiceRecorder(QObject *parent = nullptr);
    ~VoiceRecorder() override;

    /// Input chosen in settings; empty means the system default. Static because
    /// every card builds its own recorder.
    static void setPreferredInput(const QByteArray &id);
    static QByteArray preferredInput();

    bool start(const QString &path);
    void stop();
    bool isRecording() const { return m_source != nullptr; }

    qint64 durationMs() const;
    const QList<int> &peaks() const { return m_peaks; }   ///< 0..100, one every 50 ms.
    qreal loudest() const { return m_loudest; }
    QString errorText() const { return m_error; }

signals:
    void levelChanged(qreal peak);      ///< 0..1, peak of the last window.
    void durationChanged(qint64 ms);
    void finished(bool ok);             ///< File closed and header written.

private:
    void drain();                       ///< Writes out whatever the microphone delivered.

    QAudioSource *m_source = nullptr;
    QIODevice *m_input = nullptr;       ///< Owned by QAudioSource.
    QFile m_file;
    QAudioFormat m_format;

    QList<int> m_peaks;
    int m_windowFrames = 0;             ///< Frames per peak window (~50 ms).
    int m_windowSeen = 0;
    int m_windowPeak = 0;
    qint64 m_frames = 0;
    qreal m_loudest = 0.0;
    QString m_error;
};
