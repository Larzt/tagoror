#pragma once

#include "ui/keynav.hpp"

#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QWidget>
#include <functional>

#include "ui/theme.hpp"

/// Thumbnail of an attached image: a fixed height, the width taken from the
/// photo's aspect ratio and clamped.
///
/// Not a QLabel with a pixmap: a QLabel's size hint is its image's size, and
/// since the list has no horizontal scrollbar, one 1920px screenshot would
/// widen every card. Here the width never exceeds kMaxWidth.
class ImageThumb : public QWidget {
public:
    static constexpr int kHeight = 92;      ///< Preview height.
    static constexpr int kMaxWidth = 156;   ///< Width cap: the card decides.
    static constexpr int kMinWidth = 56;

    ImageThumb(const QString &path, int height = kHeight, QWidget *parent = nullptr)
        : QWidget(parent), m_h(height) {
        m_pixmap.load(path);
        setCursor(Qt::PointingHandCursor);
        setToolTip(path);
        keynav::activatable(this, [this] { if (activate) activate(); });
        // Fixing the size leaves the policy alone, and an Expanding policy propagates
        // upwards (see autoGrow() in notecard.cpp).
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setFixedSize(previewSize());
    }

    bool isValid() const { return !m_pixmap.isNull(); }

    std::function<void()> activate;
    std::function<void(const QPoint &)> menu;

    QSize minimumSizeHint() const override { return previewSize(); }
    QSize sizeHint() const override { return previewSize(); }

protected:
    void mouseReleaseEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton && rect().contains(e->position().toPoint()) && activate)
            activate();
    }

    void contextMenuEvent(QContextMenuEvent *e) override {
        if (!menu) return;
        menu(e->globalPos());
        e->accept();
    }

    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);

        QPainterPath clip;
        clip.addRoundedRect(QRectF(rect()), 8, 8);

        if (m_pixmap.isNull()) {
            // Missing or unreadable file: say so instead of leaving a gap that looks like
            // a painting bug.
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 255, 255, 10));
            p.drawPath(clip);
            p.setPen(QColor(Theme::muted()));
            p.drawText(rect(), Qt::AlignCenter, "?");
            return;
        }

        p.setClipPath(clip);
        // Cropped to the centre rather than distorted.
        const QPixmap scaled = m_pixmap.scaled(size() * devicePixelRatioF(),
                                               Qt::KeepAspectRatioByExpanding,
                                               Qt::SmoothTransformation);
        const QPoint at((width() - int(scaled.width() / devicePixelRatioF())) / 2,
                        (height() - int(scaled.height() / devicePixelRatioF())) / 2);
        p.drawPixmap(QRect(at, scaled.size() / devicePixelRatioF()), scaled);

        p.setClipping(false);
        p.setPen(QPen(QColor(255, 255, 255, 26), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
    }

private:
    /// Width from the aspect ratio, clamped at both ends: a panorama cannot widen
    /// the card and a portrait cannot shrink to a sliver.
    QSize previewSize() const {
        if (m_pixmap.isNull() || m_pixmap.height() <= 0) return QSize(120, m_h);
        const int w = int(qreal(m_h) * m_pixmap.width() / m_pixmap.height());
        return QSize(qBound(kMinWidth, w, kMaxWidth), m_h);
    }

    QPixmap m_pixmap;
    int m_h;
};
