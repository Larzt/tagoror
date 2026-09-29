#pragma once

#include <QColor>
#include <QList>
#include <QWidget>

/// Messaging-style waveform: amplitude bars, the played part in the accent
/// and the rest dimmed. Clicking seeks.
class Waveform : public QWidget {
    Q_OBJECT

public:
    explicit Waveform(QWidget *parent = nullptr);

    void setPeaks(const QList<int> &peaks);   ///< Values 0..100.
    void setProgress(qreal fraction);         ///< 0..1
    void setAccent(const QColor &c);
    void setLive(bool live);                  ///< While recording.

    QSize sizeHint() const override { return QSize(120, 26); }

signals:
    void seeked(qreal fraction);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;

private:
    QList<int> m_peaks;
    qreal m_progress = 0.0;
    QColor m_accent{"#7c9cff"};
    bool m_live = false;
};
