#include "ui/splash.hpp"

#include <QCoreApplication>
#include <QEvent>
#include <QPainter>
#include <QTimer>
#include <QVariantAnimation>

#include "core/lang.hpp"

namespace {
constexpr int kStones = 12;
}

Splash::Splash(const Theme *theme, QWidget *host) : QWidget(host), m_theme(theme) {
    setObjectName("splash");
    setGeometry(host->rect());
    host->installEventFilter(this);

    // One turn of the ring every 1.2 s: each stone in turn at full opacity, the
    // ones behind it fading. Opacity only, no movement.
    m_spin = new QVariantAnimation(this);
    m_spin->setStartValue(0.0);
    m_spin->setEndValue(double(kStones));
    m_spin->setDuration(1200);
    m_spin->setLoopCount(-1);
    connect(m_spin, &QVariantAnimation::valueChanged, this, [this] { update(); });
    m_spin->start();
    raise();
    show();
}

void Splash::run(const QList<Step> &steps) {
    m_steps = steps;
    setStep(0);
    auto *timer = new QTimer(this);
    timer->setInterval(40);
    connect(timer, &QTimer::timeout, this, &Splash::poll);
    timer->start();
}

void Splash::setStep(int index) {
    m_current = index;
    m_clock.restart();
    m_step = m_steps.value(index).text;
    m_count = QString("%1 · %2/%3")
                  .arg(QCoreApplication::applicationVersion())
                  .arg(index + 1)
                  .arg(m_steps.size());
    update();
}

/// Each step stays at least a moment, so the line can be read rather than
/// flicker; it moves on as soon as its work is done or its time is up.
void Splash::poll() {
    if (m_finishing || m_current < 0) return;
    const Step &step = m_steps.at(m_current);
    const qint64 elapsed = m_clock.elapsed();
    const bool done = !step.done || step.done();
    if ((done && elapsed >= 220) || elapsed >= step.timeoutMs) {
        if (m_current + 1 < m_steps.size()) setStep(m_current + 1);
        else finish();
    }
}

void Splash::finish() {
    if (m_finishing) return;
    m_finishing = true;
    auto *fade = new QVariantAnimation(this);
    fade->setStartValue(1.0);
    fade->setEndValue(0.0);
    fade->setDuration(160);
    connect(fade, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        m_fade = v.toReal();
        update();
    });
    connect(fade, &QVariantAnimation::finished, this, [this] {
        hide();
        deleteLater();
    });
    fade->start();
}

bool Splash::eventFilter(QObject *watched, QEvent *event) {
    if (watched == parentWidget() && event->type() == QEvent::Resize)
        setGeometry(parentWidget()->rect());
    return QWidget::eventFilter(watched, event);
}

void Splash::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setOpacity(m_fade);

    // The shell's own colour, opaque: nothing of the half-built page shows through.
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(19, 22, 27));
    p.drawRoundedRect(QRectF(rect()), 14, 14);

    const QPointF centre(width() / 2.0, height() / 2.0 - 30);
    const qreal turn = m_spin->currentValue().toReal();
    QList<qreal> alpha;
    for (int i = 0; i < kStones; ++i) {
        // Distance behind the lit stone, around the ring.
        qreal behind = turn - i;
        while (behind < 0) behind += kStones;
        alpha << qMax(0.22, 1.0 - behind / 7.0);
    }
    paintStoneRing(p, centre, 26, kStones, 4.2, m_theme->accent, alpha);

    QFont f = font();
    f.setPixelSize(15);
    f.setWeight(QFont::DemiBold);
    p.setFont(f);
    p.setPen(QColor(Theme::fg()));
    p.drawText(QRectF(0, centre.y() + 44, width(), 22), Qt::AlignCenter, QStringLiteral("Tagoror"));

    f.setPixelSize(12);
    f.setWeight(QFont::Normal);
    p.setFont(f);
    p.setPen(QColor(Theme::muted()));
    p.drawText(QRectF(12, centre.y() + 68, width() - 24, 18), Qt::AlignCenter,
               QFontMetrics(f).elidedText(m_step, Qt::ElideRight, width() - 24));

    QFont mono = font();
    mono.setFamily("IBM Plex Mono");
    mono.setStyleHint(QFont::Monospace);
    mono.setPixelSize(10);
    p.setFont(mono);
    p.setPen(QColor("#636872"));
    p.drawText(QRectF(0, height() - 34, width(), 16), Qt::AlignCenter, m_count);
}
