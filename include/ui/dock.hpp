#pragma once

#include <QFontMetricsF>
#include <QVariantAnimation>

#include "ui/dragwidgets.hpp"
#include "ui/keynav.hpp"
#include "ui/theme.hpp"

/// The folded panel (5.0): a 56px tile with the tagoror's ring of stones,
/// in the accent. While something rings the stones turn red, a halo beats
/// around the tile (opacity and scale) and a badge counts the alarms — folded,
/// this is the only thing on screen, so it has to say it on its own. Click or
/// Enter unfolds; dragging moves it (DragButton).
///
/// The widget is a little larger than the tile so the halo has room: the
/// window around the dock is exactly this widget plus the shadow margin.
class DockButton : public DragButton {
public:
    static constexpr int kTile = 56;
    static constexpr int kHalo = 7;   ///< Room on each side for the halo.

    explicit DockButton(const Theme *theme, QWidget *parent = nullptr)
        : DragButton(parent), m_theme(theme) {
        setFixedSize(kTile + 2 * kHalo, kTile + 2 * kHalo);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::TabFocus);
        m_beat.setStartValue(0.0);
        m_beat.setEndValue(1.0);
        m_beat.setDuration(1400);
        m_beat.setLoopCount(-1);
        QObject::connect(&m_beat, &QVariantAnimation::valueChanged, this,
                         [this] { update(); });
    }

    /// @p alarms is how many things ring; 0 is the normal dock.
    void setAlarms(int alarms) {
        if (alarms == m_alarms) return;
        m_alarms = alarms;
        if (alarms > 0 && m_beat.state() != QAbstractAnimation::Running) m_beat.start();
        if (alarms == 0) m_beat.stop();
        update();
    }

    /// The countdown of the timer that ends first, in a pill over the tile's
    /// bottom edge; empty hides it.
    void setCountdown(const QString &text) {
        if (text == m_countdown) return;
        m_countdown = text;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF tile(kHalo + 0.5, kHalo + 0.5, kTile - 1.0, kTile - 1.0);
        const bool alarm = m_alarms > 0;
        const QColor red("#ff7a6b");

        if (alarm) {
            // Grows and fades, then again.
            const qreal t = m_beat.currentValue().toReal();
            QColor halo = red;
            halo.setAlphaF(0.75 * (1.0 - t));
            p.setPen(QPen(halo, 2));
            p.setBrush(Qt::NoBrush);
            const qreal grow = 1.5 + 5.0 * t;
            p.drawRoundedRect(tile.adjusted(-grow, -grow, grow, grow), 18 + grow, 18 + grow);
        }

        QColor fill = QColor(28, 31, 38);
        if (isDown()) fill = fill.lighter(115);
        else if (underMouse()) fill = fill.lighter(108);
        p.setPen(QPen(alarm ? QColor(255, 122, 107, 120) : QColor(255, 255, 255, 30), 1));
        p.setBrush(alarm ? QColor(52, 30, 30) : fill);
        p.drawRoundedRect(tile, 16, 16);
        if (keynav::showsFocus(this)) {
            p.setPen(QPen(m_theme->accent, 1.5));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(tile.adjusted(2, 2, -2, -2), 14, 14);
        }

        paintStoneRing(p, tile.center(), 13, 12, 2.3, alarm ? red : m_theme->accent);

        if (!m_countdown.isEmpty()) {
            // A timer running: its time left, centred on the tile's bottom edge.
            QFont f = font();
            f.setFamily("IBM Plex Mono");
            f.setStyleHint(QFont::Monospace);
            f.setPixelSize(10);
            f.setWeight(QFont::DemiBold);
            p.setFont(f);
            const qreal w = QFontMetricsF(f).horizontalAdvance(m_countdown) + 10;
            const QRectF pill(tile.center().x() - w / 2, tile.bottom() - 9, w, 15);
            p.setPen(QPen(QColor(19, 22, 27), 1.5));
            p.setBrush(m_theme->accent);
            p.drawRoundedRect(pill, 7.5, 7.5);
            p.setPen(QColor(19, 22, 27));
            p.drawText(pill, Qt::AlignCenter, m_countdown);
        }

        if (alarm) {
            // The count, top-right, over the tile's corner.
            const QRectF badge(tile.right() - 13, tile.top() - 6, 19, 19);
            p.setPen(QPen(QColor(19, 22, 27), 2));
            p.setBrush(red);
            p.drawEllipse(badge);
            QFont f = font();
            f.setPixelSize(11);
            f.setWeight(QFont::DemiBold);
            p.setFont(f);
            p.setPen(QColor("#2b0e0a"));
            p.drawText(badge, Qt::AlignCenter, m_alarms > 9 ? QStringLiteral("9+")
                                                           : QString::number(m_alarms));
        }
    }

    void enterEvent(QEnterEvent *e) override {
        DragButton::enterEvent(e);
        update();
    }
    void leaveEvent(QEvent *e) override {
        DragButton::leaveEvent(e);
        update();
    }

private:
    const Theme *m_theme;
    QVariantAnimation m_beat;
    int m_alarms = -1;
    QString m_countdown;
};
