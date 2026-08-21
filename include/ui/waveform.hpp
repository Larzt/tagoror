#pragma once

#include <QColor>
#include <QList>
#include <QWidget>

// Onda estilo mensajería: barras de amplitud, la parte ya reproducida en el
// color de acento y el resto apagado. Se puede pinchar para buscar.
class Waveform : public QWidget {
    Q_OBJECT

public:
    explicit Waveform(QWidget *parent = nullptr);

    void setPeaks(const QList<int> &peaks);   // valores 0..100
    void setProgress(qreal fraction);         // 0..1
    void setAccent(const QColor &c);
    void setLive(bool live);                  // durante la grabación

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
