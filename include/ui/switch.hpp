#pragma once

#include <QMouseEvent>
#include <QPainter>
#include <QWidget>
#include <functional>

#include "ui/keynav.hpp"
#include "ui/theme.hpp"

/// A sliding switch, painted by hand. A checkbox says "tick this one from a
/// list"; a switch says "this is on or off", which is what these settings are.
class Switch : public QWidget {
public:
    Switch(bool on, const Theme *theme, std::function<void(bool)> changed,
           QWidget *parent = nullptr)
        : QWidget(parent), m_on(on), m_theme(theme), m_changed(std::move(changed)) {
        setFixedSize(38, 21);
        setCursor(Qt::PointingHandCursor);
        keynav::activatable(this, [this] { flip(); });
    }

    bool isOn() const { return m_on; }

    /// As a click: the callback is told. The whole row can call it.
    void flip() {
        m_on = !m_on;
        update();
        if (m_changed) m_changed(m_on);
    }

protected:
    void mouseReleaseEvent(QMouseEvent *e) override {
        if (e->button() != Qt::LeftButton || !rect().contains(e->position().toPoint())) return;
        flip();
    }

    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        const QColor accent = m_theme ? m_theme->accent : QColor("#7c9cff");
        p.setPen(keynav::showsFocus(this) ? QPen(QColor(Theme::fg()), 1.5) : QPen(Qt::NoPen));
        p.setBrush(m_on ? accent : QColor(255, 255, 255, 28));
        const QRectF track = QRectF(rect()).adjusted(0.75, 0.75, -0.75, -0.75);
        p.drawRoundedRect(track, track.height() / 2.0, track.height() / 2.0);
        p.setPen(Qt::NoPen);

        const qreal r = height() / 2.0 - 3.0;
        const qreal x = m_on ? width() - height() / 2.0 : height() / 2.0;
        p.setBrush(m_on ? QColor("#0d1014") : QColor(Theme::muted()));
        p.drawEllipse(QPointF(x, height() / 2.0), r, r);
    }

private:
    bool m_on;
    const Theme *m_theme;
    std::function<void(bool)> m_changed;
};
