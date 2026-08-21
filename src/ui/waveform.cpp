#include "ui/waveform.hpp"
#include "ui/theme.hpp"

#include <QMouseEvent>
#include <QPainter>

namespace {
constexpr int kBar = 3;
constexpr int kGap = 2;
}

Waveform::Waveform(QWidget *parent) : QWidget(parent) {
    setMinimumHeight(26);
    setCursor(Qt::PointingHandCursor);
}

void Waveform::setPeaks(const QList<int> &peaks) {
    m_peaks = peaks;
    update();
}

void Waveform::setProgress(qreal fraction) {
    const qreal clamped = qBound(0.0, fraction, 1.0);
    if (qFuzzyCompare(clamped + 1.0, m_progress + 1.0)) return;
    m_progress = clamped;
    update();
}

void Waveform::setAccent(const QColor &c) {
    m_accent = c;
    update();
}

void Waveform::setLive(bool live) {
    m_live = live;
    update();
}

void Waveform::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);

    const int barCount = qMax(1, (width() + kGap) / (kBar + kGap));
    const qreal mid = height() / 2.0;

    // Sin datos todavía: una línea base tenue en lugar de un hueco vacío.
    if (m_peaks.isEmpty()) {
        p.setBrush(QColor(139, 144, 154, 90));
        for (int i = 0; i < barCount; ++i)
            p.drawRoundedRect(QRectF(i * (kBar + kGap), mid - 1, kBar, 2), 1, 1);
        return;
    }

    // Al grabar interesa la cola (lo que se acaba de decir); al reproducir, la
    // toma entera comprimida en el ancho disponible.
    const int from = m_live ? qMax(0, int(m_peaks.size()) - barCount) : 0;
    const int count = m_live ? qMin(barCount, int(m_peaks.size())) : barCount;

    for (int i = 0; i < count; ++i) {
        int value;
        if (m_live) {
            value = m_peaks.at(from + i);
        } else {
            // Repartir todos los picos entre las barras visibles.
            const int a = int(qint64(i) * m_peaks.size() / count);
            const int b = qMax(a + 1, int(qint64(i + 1) * m_peaks.size() / count));
            value = 0;
            for (int k = a; k < b && k < m_peaks.size(); ++k)
                value = qMax(value, m_peaks.at(k));
        }

        const qreal h = qMax(2.0, value / 100.0 * (height() - 4));
        const bool played = !m_live && count > 0 && (qreal(i) / count) < m_progress;
        p.setBrush(played ? m_accent
                          : (m_live ? m_accent : QColor(139, 144, 154, 140)));
        p.drawRoundedRect(QRectF(i * (kBar + kGap), mid - h / 2, kBar, h), 1.5, 1.5);
    }
}

void Waveform::mousePressEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton || m_peaks.isEmpty() || m_live) {
        QWidget::mousePressEvent(e);
        return;
    }
    emit seeked(qBound(0.0, e->position().x() / qMax(1, width()), 1.0));
}
