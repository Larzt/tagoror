#pragma once

#include <QColor>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QPixmap>
#include <QRegularExpression>
#include <QString>

struct Theme {
    QColor accent{"#7c9cff"};
    int opacity = 96;   ///< 40..100
    /// Content text size in percent (Settings → Text size). Only what the user
    /// writes scales: the sheet marks those sizes as {fs:12} and sheet() converts
    /// them; the rest of the interface stays fixed.
    int textScale = 100;

    /// A content size already scaled, for what is painted by hand.
    qreal fs(qreal px) const { return px * textScale / 100.0; }

    /// Base surface colour, also exposed as a QColor so hand-painted popups use
    /// exactly the panel's opacity.
    QColor cardColor() const {
        return QColor(19, 22, 27, int(255 * opacity / 100.0));
    }
    /// The accent with transparency, for tinted backgrounds and borders.
    QString accentRgba(qreal alpha) const {
        return QString("rgba(%1,%2,%3,%4)")
            .arg(accent.red()).arg(accent.green()).arg(accent.blue())
            .arg(alpha, 0, 'f', 3);
    }
    QString card() const {
        return QString("rgba(19,22,27,%1)").arg(opacity / 100.0, 0, 'f', 3);
    }
    static QString fg()    { return "#e9eaee"; }
    static QString muted() { return "#8b909a"; }
    static QString line()  { return "rgba(255,255,255,0.09)"; }
    static QString sunk()  { return "rgba(255,255,255,0.03)"; }
    static QString hover() { return "rgba(255,255,255,0.07)"; }

    QString sheet() const;
};

/// Icons drawn in code: no dependency on a system icon theme.
inline QIcon paintIcon(const QString &kind, const QColor &color, int px = 16) {
    const qreal dpr = 2.0;
    QPixmap pm(int(px * dpr), int(px * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    const qreal c = px / 2.0;

    if (kind == "search") {
        p.drawEllipse(QPointF(c - 1.5, c - 1.5), 4.2, 4.2);
        p.drawLine(QPointF(c + 1.8, c + 1.8), QPointF(c + 5.0, c + 5.0));
    } else if (kind == "plus") {
        p.drawLine(QPointF(c - 4.5, c), QPointF(c + 4.5, c));
        p.drawLine(QPointF(c, c - 4.5), QPointF(c, c + 4.5));
    } else if (kind == "gear") {
        p.drawEllipse(QPointF(c, c), 4.6, 4.6);
        p.setBrush(color);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(c, c), 1.6, 1.6);
    } else if (kind == "minus") {
        p.drawLine(QPointF(c - 4.5, c), QPointF(c + 4.5, c));
    } else if (kind == "notes") {
        pen.setWidthF(1.6);
        p.setPen(pen);
        p.drawRoundedRect(QRectF(c - 6, c - 6, 12, 12), 3, 3);
        p.drawLine(QPointF(c - 3, c - 2), QPointF(c + 3, c - 2));
        p.drawLine(QPointF(c - 3, c + 1.5), QPointF(c + 1, c + 1.5));

    // Note types (new-note selector)
    } else if (kind == "text") {
        p.drawLine(QPointF(c - 5, c - 3.5), QPointF(c + 5, c - 3.5));
        p.drawLine(QPointF(c - 5, c), QPointF(c + 5, c));
        p.drawLine(QPointF(c - 5, c + 3.5), QPointF(c + 1, c + 3.5));
    } else if (kind == "check") {
        p.drawRoundedRect(QRectF(c - 5.5, c - 5.5, 11, 11), 3, 3);
        pen.setWidthF(1.7);
        p.setPen(pen);
        p.drawPolyline(QPolygonF({QPointF(c - 2.6, c), QPointF(c - 0.6, c + 2.2),
                                  QPointF(c + 3.0, c - 2.4)}));
    } else if (kind == "reminder" || kind == "clock") {
        p.drawEllipse(QPointF(c, c), 5.2, 5.2);
        p.drawLine(QPointF(c, c - 2.6), QPointF(c, c));
        p.drawLine(QPointF(c, c), QPointF(c + 2.4, c + 1.2));
    } else if (kind == "calendar") {
        p.drawRoundedRect(QRectF(c - 5.6, c - 4.6, 11.2, 10.6), 2.4, 2.4);
        p.drawLine(QPointF(c - 5.6, c - 1.6), QPointF(c + 5.6, c - 1.6));
        p.drawLine(QPointF(c - 3.0, c - 6.2), QPointF(c - 3.0, c - 3.6));
        p.drawLine(QPointF(c + 3.0, c - 6.2), QPointF(c + 3.0, c - 3.6));
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawEllipse(QPointF(c - 2.4, c + 1.8), 1.0, 1.0);
        p.drawEllipse(QPointF(c + 1.0, c + 1.8), 1.0, 1.0);
    } else if (kind == "cake") {
        // Cake: base, one candle and the flame. At 16px a filled cake is a blur, so
        // it uses the same thin line as the other header icons.
        p.drawRoundedRect(QRectF(c - 5.6, c - 0.6, 11.2, 6.4), 1.8, 1.8);
        p.drawLine(QPointF(c - 5.6, c + 2.4), QPointF(c + 5.6, c + 2.4));
        p.drawLine(QPointF(c, c - 3.4), QPointF(c, c - 0.6));
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawEllipse(QPointF(c, c - 4.6), 1.2, 1.5);
    } else if (kind == "timer") {
        // Stopwatch: dial, top button and a hand. Unlike the reminders' clock, which
        // is a time, this is a countdown.
        p.drawEllipse(QPointF(c, c + 1.0), 5.0, 5.0);
        p.drawLine(QPointF(c - 1.6, c - 5.8), QPointF(c + 1.6, c - 5.8));
        p.drawLine(QPointF(c, c - 5.8), QPointF(c, c - 4.0));
        p.drawLine(QPointF(c + 3.8, c - 3.4), QPointF(c + 4.8, c - 4.4));
        p.drawLine(QPointF(c, c + 1.0), QPointF(c + 2.2, c - 1.4));
    } else if (kind == "chevronLeft") {
        p.drawPolyline(QPolygonF({QPointF(c + 2.0, c - 4.4), QPointF(c - 2.4, c),
                                  QPointF(c + 2.0, c + 4.4)}));
    } else if (kind == "chevronRight") {
        p.drawPolyline(QPolygonF({QPointF(c - 2.0, c - 4.4), QPointF(c + 2.4, c),
                                  QPointF(c - 2.0, c + 4.4)}));
    } else if (kind == "chevronDown") {
        p.drawPolyline(QPolygonF({QPointF(c - 4.4, c - 2.0), QPointF(c, c + 2.4),
                                  QPointF(c + 4.4, c - 2.0)}));
    } else if (kind == "chevronUp") {
        p.drawPolyline(QPolygonF({QPointF(c - 4.4, c + 2.0), QPointF(c, c - 2.4),
                                  QPointF(c + 4.4, c + 2.0)}));
    } else if (kind == "voice") {
        p.drawRoundedRect(QRectF(c - 2.1, c - 6, 4.2, 7.4), 2.1, 2.1);
        QPainterPath arc;
        arc.arcMoveTo(QRectF(c - 4.6, c - 4.6, 9.2, 9.2), 200);
        arc.arcTo(QRectF(c - 4.6, c - 4.6, 9.2, 9.2), 200, 140);
        p.drawPath(arc);
        p.drawLine(QPointF(c, c + 4.6), QPointF(c, c + 6.4));

    // Actions
    } else if (kind == "bell") {
        QPainterPath body;
        body.moveTo(c - 4.6, c + 2.2);
        body.cubicTo(c - 3.4, c + 1.4, c - 3.4, c - 1.2, c - 3.4, c - 2.0);
        body.cubicTo(c - 3.4, c - 5.0, c + 3.4, c - 5.0, c + 3.4, c - 2.0);
        body.cubicTo(c + 3.4, c - 1.2, c + 3.4, c + 1.4, c + 4.6, c + 2.2);
        body.closeSubpath();
        p.drawPath(body);
        p.drawLine(QPointF(c - 1.6, c + 4.0), QPointF(c + 1.6, c + 4.0));
    } else if (kind == "mic") {
        p.drawRoundedRect(QRectF(c - 2.1, c - 6, 4.2, 7.4), 2.1, 2.1);
        p.drawLine(QPointF(c, c + 4.6), QPointF(c, c + 6.4));
    } else if (kind == "record") {
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawEllipse(QPointF(c, c), 4.4, 4.4);
    } else if (kind == "play") {
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawPolygon(QPolygonF({QPointF(c - 3.2, c - 4.6), QPointF(c + 4.6, c),
                                 QPointF(c - 3.2, c + 4.6)}));
    } else if (kind == "pause") {
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawRoundedRect(QRectF(c - 3.8, c - 4.4, 2.8, 8.8), 1.2, 1.2);
        p.drawRoundedRect(QRectF(c + 1.0, c - 4.4, 2.8, 8.8), 1.2, 1.2);
    } else if (kind == "stop") {
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawRoundedRect(QRectF(c - 3.8, c - 3.8, 7.6, 7.6), 1.6, 1.6);
    } else if (kind == "link") {
        // Two tilted links overlapping in the middle.
        p.save();
        p.translate(c, c);
        p.rotate(-45);
        pen.setWidthF(1.4);
        p.setPen(pen);
        p.drawRoundedRect(QRectF(-6.0, -2.7, 6.8, 5.4), 2.7, 2.7);
        p.drawRoundedRect(QRectF(-0.8, -2.7, 6.8, 5.4), 2.7, 2.7);
        p.restore();
    } else if (kind == "repeat") {
        // An open cycle with an arrowhead: says "comes back" without text.
        QPainterPath arc;
        arc.arcMoveTo(QRectF(c - 4.8, c - 4.8, 9.6, 9.6), 60);
        arc.arcTo(QRectF(c - 4.8, c - 4.8, 9.6, 9.6), 60, 280);
        p.drawPath(arc);
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawPolygon(QPolygonF({QPointF(c + 1.2, c - 5.6), QPointF(c + 5.4, c - 4.0),
                                 QPointF(c + 1.6, c - 1.8)}));
    } else if (kind == "image") {
        p.drawRoundedRect(QRectF(c - 5.8, c - 4.8, 11.6, 9.6), 2.4, 2.4);
        // Sun and mountain: the silhouette read as "photo" at 13px.
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawEllipse(QPointF(c - 2.4, c - 2.0), 1.2, 1.2);
        p.drawPolygon(QPolygonF({QPointF(c - 4.6, c + 3.6), QPointF(c - 0.6, c - 0.8),
                                 QPointF(c + 2.0, c + 1.6), QPointF(c + 3.2, c + 0.6),
                                 QPointF(c + 4.6, c + 3.6)}));
    } else if (kind == "copy") {
        p.drawRoundedRect(QRectF(c - 5.4, c - 5.4, 8.0, 8.0), 2.0, 2.0);
        p.drawRoundedRect(QRectF(c - 2.6, c - 2.6, 8.0, 8.0), 2.0, 2.0);
    } else if (kind == "pencil") {
        p.drawLine(QPointF(c - 5.2, c + 5.2), QPointF(c - 4.4, c + 2.4));
        p.drawLine(QPointF(c - 4.4, c + 2.4), QPointF(c + 2.6, c - 4.6));
        p.drawLine(QPointF(c + 2.6, c - 4.6), QPointF(c + 5.2, c - 2.0));
        p.drawLine(QPointF(c + 5.2, c - 2.0), QPointF(c - 1.8, c + 5.0));
        p.drawLine(QPointF(c - 1.8, c + 5.0), QPointF(c - 5.2, c + 5.2));
    } else if (kind == "trash") {
        p.drawLine(QPointF(c - 5, c - 3.4), QPointF(c + 5, c - 3.4));
        p.drawRoundedRect(QRectF(c - 3.8, c - 3.4, 7.6, 9.2), 1.8, 1.8);
        p.drawLine(QPointF(c - 1.4, c - 5.4), QPointF(c + 1.4, c - 5.4));
    } else if (kind == "palette") {
        p.drawEllipse(QPointF(c, c), 5.4, 5.4);
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawEllipse(QPointF(c - 2.2, c - 1.4), 1.1, 1.1);
        p.drawEllipse(QPointF(c + 1.0, c - 2.6), 1.1, 1.1);
        p.drawEllipse(QPointF(c + 2.6, c + 0.8), 1.1, 1.1);
    } else if (kind == "opacity") {
        p.drawEllipse(QPointF(c, c), 5.2, 5.2);
        QPainterPath half;
        half.moveTo(c, c - 5.2);
        half.arcTo(QRectF(c - 5.2, c - 5.2, 10.4, 10.4), 90, -180);
        half.closeSubpath();
        p.fillPath(half, color);
    } else if (kind == "power") {
        QPainterPath arc;
        arc.arcMoveTo(QRectF(c - 4.8, c - 4.4, 9.6, 9.6), 70);
        arc.arcTo(QRectF(c - 4.8, c - 4.4, 9.6, 9.6), 70, 320);
        p.drawPath(arc);
        p.drawLine(QPointF(c, c - 6), QPointF(c, c - 1.4));

    // Settings sections
    } else if (kind == "globe") {
        p.drawEllipse(QPointF(c, c), 5.4, 5.4);
        p.drawEllipse(QRectF(c - 2.4, c - 5.4, 4.8, 10.8));
        p.drawLine(QPointF(c - 5.4, c), QPointF(c + 5.4, c));
    } else if (kind == "window") {
        p.drawRoundedRect(QRectF(c - 5.8, c - 4.8, 11.6, 9.6), 2.2, 2.2);
        p.drawLine(QPointF(c - 5.8, c - 1.8), QPointF(c + 5.8, c - 1.8));
    } else if (kind == "folder") {
        QPainterPath f;
        f.moveTo(c - 5.8, c + 4.4);
        f.lineTo(c - 5.8, c - 4.4);
        f.lineTo(c - 1.8, c - 4.4);
        f.lineTo(c - 0.4, c - 2.8);
        f.lineTo(c + 5.8, c - 2.8);
        f.lineTo(c + 5.8, c + 4.4);
        f.closeSubpath();
        p.drawPath(f);
    } else if (kind == "cloud") {
        QPainterPath cl;
        cl.moveTo(c - 3.4, c + 3.6);
        cl.arcTo(QRectF(c - 6.2, c - 1.6, 5.6, 5.2), 270, -180);
        cl.arcTo(QRectF(c - 3.6, c - 5.2, 7.4, 7.4), 160, -150);
        cl.arcTo(QRectF(c + 1.0, c - 1.4, 5.0, 5.0), 90, -180);
        cl.closeSubpath();
        p.drawPath(cl);
    } else if (kind == "download") {
        p.drawLine(QPointF(c, c - 5.6), QPointF(c, c + 1.8));
        p.drawPolyline(QPolygonF({QPointF(c - 3.2, c - 1.2), QPointF(c, c + 2.0),
                                  QPointF(c + 3.2, c - 1.2)}));
        p.drawLine(QPointF(c - 5.2, c + 5.0), QPointF(c + 5.2, c + 5.0));

    // Ornaments
    } else if (kind == "checkdots") {
        // Like QCheckBox::indicator but dotted: marks the add-item row as a checkbox
        // that does not exist yet.
        pen.setWidthF(1.4);
        pen.setStyle(Qt::CustomDashLine);
        pen.setDashPattern({0.9, 1.8});
        pen.setCapStyle(Qt::RoundCap);
        p.setPen(pen);
        p.drawRoundedRect(QRectF(c - 5.2, c - 5.2, 10.4, 10.4), 3.2, 3.2);
    } else if (kind == "grip") {
        // A 2x3 dot grid, the usual grip. The previous triangle of dots is a resize
        // corner, and with the vertical resize cursor it read as "make the card
        // taller" rather than "grab it".
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        for (int i = 0; i < 2; ++i)
            for (int j = 0; j < 3; ++j)
                p.drawEllipse(QPointF(c - 2.1 + i * 4.2, c - 3.6 + j * 3.6), 1.05, 1.05);
    } else if (kind.startsWith("glyph-")) {
        // Area glyphs (see area.hpp): simple solid shapes distinguishable at 8px
        // without colour. They scale with px because tabs draw them smaller.
        const QString g = kind.mid(6);
        const qreal r = px * 0.26;
        if (g == "none") {
            p.drawLine(QPointF(c - r, c + r), QPointF(c + r, c - r));
        } else if (g == "ring") {
            pen.setWidthF(qMax(1.2, px / 10.0));
            p.setPen(pen);
            p.drawEllipse(QPointF(c, c), r * 0.9, r * 0.9);
        } else {
            p.setPen(Qt::NoPen);
            p.setBrush(color);
            if (g == "dot") {
                p.drawEllipse(QPointF(c, c), r, r);
            } else if (g == "square") {
                p.drawRoundedRect(QRectF(c - r * 0.9, c - r * 0.9, r * 1.8, r * 1.8), 1.2, 1.2);
            } else if (g == "triangle") {
                p.drawPolygon(QPolygonF({QPointF(c, c - r * 1.05), QPointF(c + r * 1.1, c + r * 0.85),
                                         QPointF(c - r * 1.1, c + r * 0.85)}));
            } else if (g == "diamond") {
                p.drawPolygon(QPolygonF({QPointF(c, c - r * 1.15), QPointF(c + r * 1.15, c),
                                         QPointF(c, c + r * 1.15), QPointF(c - r * 1.15, c)}));
            } else if (g == "bar") {
                p.drawRoundedRect(QRectF(c - r * 1.1, c - r * 0.4, r * 2.2, r * 0.8), r * 0.4, r * 0.4);
            }
        }
    }
    p.end();
    return QIcon(pm);
}

inline QString Theme::sheet() const {
    const QString acc = accent.name();

    QString out = QString(R"(
QFrame#shell {
    background: %1;
    border: 1px solid %2;
    border-radius: 14px;
}
/* App mode: the frame is the system's, which has its own corners. */
QFrame#shell[app="true"] { border: none; border-radius: 0; }
QFrame#header { border: none; border-bottom: 1px solid %2; }
QFrame#footer { border: none; border-top: 1px solid %2; background: %6; }

QLabel          { color: %3; font-size: 12px; }
QLabel#title    { font-size: 12.5px; font-weight: 600; }
QLabel#cardTitle{ font-size: {fs:12}px;   font-weight: 600; }
QLabel#body     { color: %4; font-size: {fs:11.5}px; }
QLabel#meta     { color: %4; font-size: 9.5px; font-family: "IBM Plex Mono", monospace; }
QLabel#chip {
    color: #f2b757; font-size: 10.5px; font-weight: 600;
    background: rgba(242,183,87,0.12);
    border: 1px solid rgba(242,183,87,0.35);
    border-radius: 6px; padding: 2px 7px;
}
/* An overdue reminder is red whether or not it has rung: in amber it was
   confused with those still to come. */
QLabel#chip[state="overdue"], QLabel#chip[state="ringing"] {
    color: #ff7a6b;
    background: rgba(255,122,107,0.16);
    border: 1px solid rgba(255,122,107,0.55);
}
/* Missing data folder: the red of an overdue reminder, since it says the same
   thing (this needs attention now). */
QLabel#warnBanner {
    color: #ff7a6b; font-size: 10.5px; font-weight: 600;
    background: rgba(255,122,107,0.14);
    border-bottom: 1px solid rgba(255,122,107,0.35);
    padding: 7px 10px;
}

QLabel#chip:hover {
    background: rgba(242,183,87,0.22);
    border: 1px solid rgba(242,183,87,0.65);
}

QFrame#card {
    background: %6;
    border: 1px solid %2;
    border-radius: 10px;
}
QFrame#card:hover { border: 1px solid %5; }

QToolButton {
    border: none; border-radius: 8px; padding: 4px;
    background: transparent;
}
QToolButton:hover { background: %7; }
/* Active page button: it stays lit instead of changing icon. */
QToolButton[active="true"] {
    background: %8;
    border: 1px solid %9;
}
QToolButton[active="true"]:hover { background: %10; }

QPushButton {
    color: #0d1014; background: %5;
    border: none; border-radius: 8px;
    padding: 6px 12px; font-size: 11.5px; font-weight: 600;
}
QPushButton:hover { background: %5; }

QLineEdit {
    color: %3; background: %6;
    border: 1px solid %2; border-radius: 8px;
    padding: 6px 9px; font-size: 11.5px;
    selection-background-color: %5;
}
QLineEdit:focus { border: 1px solid %5; }
QLineEdit#cardTitleEdit {
    background: transparent; border: none; padding: 0;
    font-size: {fs:12}px; font-weight: 600; color: %3;
}
/* Add-item row: borderless, so it reads as one more task. */
QLineEdit#newItemEdit {
    background: transparent; border: none; padding: 0;
    font-size: {fs:11.5}px; color: %3;
}
QLineEdit#newItemEdit:focus { border: none; }
/* Renaming an item: the same borderless field, at the size of the text it
   replaces so the row does not jump. */
QLineEdit#checkTextEdit {
    background: transparent; border: none; padding: 0;
    font-size: {fs:11.5}px; color: %3;
}
QLineEdit#checkTextEdit:focus { border: none; }
QTextEdit {
    color: %3; background: %6;
    border: 1px solid %2; border-radius: 8px;
    padding: 6px; font-size: {fs:11.5}px;
    selection-background-color: %5;
}

QCheckBox { color: %3; font-size: {fs:11.5}px; spacing: 8px; }
/* An item's text has its own label so it can wrap; see addCheckRow(). */
QLabel#checkText { color: %3; font-size: {fs:11.5}px; }
QCheckBox::indicator {
    width: 13px; height: 13px; border-radius: 4px;
    border: 1.4px solid %4; background: transparent;
}
QCheckBox::indicator:checked { background: %5; border: 1.4px solid %5; }

QProgressBar {
    background: %7; border: none; border-radius: 2px;
    max-height: 4px; min-height: 4px; text-align: center;
}
QProgressBar::chunk { background: %5; border-radius: 2px; }

QSlider::groove:horizontal {
    background: %7; height: 4px; border-radius: 2px;
}
QSlider::sub-page:horizontal { background: %5; border-radius: 2px; }
QSlider::handle:horizontal {
    background: %3; width: 12px; height: 12px;
    margin: -4px 0; border-radius: 6px;
}

QScrollArea { border: none; background: transparent; }
QWidget#listHost { background: transparent; }
QWidget#scrollViewport { background: transparent; }
QScrollBar:vertical {
    background: transparent; width: 6px; margin: 0;
}
QScrollBar::handle:vertical {
    background: rgba(140,140,150,0.35); border-radius: 3px; min-height: 24px;
}
QScrollBar::add-line, QScrollBar::sub-line { height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

/* Attached links */
/* Needs WA_StyledBackground. */
QWidget#linkRow { background: transparent; border-radius: 7px; }
QWidget#linkRow:hover { background: %7; }
QLabel#linkText { font-size: {fs:11.5}px; }
)" R"(
QWidget#calendar { background: transparent; }
QFrame#calSeparator { background: %2; border: none; }
QToolButton#calNav {
    background: %6; border: 1px solid %2;
    border-radius: 7px; padding: 3px;
}
QToolButton#calNav:hover { background: %7; }
QLabel#calMonth { color: %3; font-size: 13px; font-weight: 700; }
QToolButton#todayBtn {
    color: %5; font-size: 10px; font-weight: 700;
    background: %8; border: 1px solid %9;
    border-radius: 8px; padding: 4px 9px;
}
QToolButton#todayBtn:hover { background: %10; }

QLabel#dayTime {
    color: %4; font-size: 10px;
    font-family: "IBM Plex Mono", monospace;
}
QLabel#dayChip {
    color: #ff7a6b; font-size: 8.5px; font-weight: 700;
    background: rgba(255,122,107,0.14);
    border: 1px solid rgba(255,122,107,0.40);
    border-radius: 5px; padding: 1px 5px;
}
QWidget#dayRow { background: transparent; border-radius: 8px; }
QWidget#dayRow:hover { background: %7; }
QLabel#dayRowTitle { color: %3; font-size: 11.5px; }
QLabel#dayRowTitleAlert { color: #ff7a6b; font-size: 11.5px; font-weight: 600; }

/* The dragged card: same fill, accent border, so it shows what is moving. */
QFrame#card[dragging="true"] {
    border: 1px solid %5;
    background: %7;
}
/* The reorder grip is only tinted on hover: at rest it hides behind the title. */
QToolButton#dragHandle { background: transparent; border: none; padding: 0; }
QToolButton#dragHandle:hover { background: %7; }

/* Finished list: the button only appears when nothing is left to tick, so it
   can afford red. */
QToolButton#listDone {
    color: #ff7a6b; background: transparent;
    border: 1px solid rgba(255,122,107,0.35); border-radius: 8px;
    padding: 3px 9px; font-size: 10.5px; font-weight: 600;
}
QToolButton#listDone:hover { background: rgba(255,122,107,0.14); }

/* "Add details": the only way into an empty reminder, so it must look
   clickable. */
QLabel#addDetails { color: %4; font-size: {fs:10.5}px; }
QLabel#addDetails:hover { color: %5; }

/* Attached images */
QWidget#imgHeader { background: transparent; border-radius: 7px; }
QWidget#imgHeader:hover { background: %7; }
QLabel#imgTitle {
    color: %4; font-size: 9.5px; font-weight: 700;
    font-family: "IBM Plex Mono", monospace;
}

QWidget#dayHeader { background: transparent; border-radius: 7px; }
QWidget#dayHeader:hover { background: %7; }

/* Birthdays */
QWidget#birthdays { background: transparent; }
/* The highlighted birthday card: tinted frame, since it is the one row with
   something to do. */
QFrame#bdayToday {
    background: %6;
    border: 1px solid %9;
    border-radius: 11px;
}
QLabel#bdayName { color: %3; font-size: {fs:13}px; font-weight: 700; }
QLabel#bdayTodayChip {
    color: %5; font-size: 8.5px; font-weight: 700;
    font-family: "IBM Plex Mono", monospace;
    background: %10; border: 1px solid %9;
    border-radius: 5px; padding: 1px 5px;
}
/* Already greeted: the same chip in green, since it says the opposite. */
QLabel#bdayDoneChip {
    color: #6fcf97; font-size: 8.5px; font-weight: 700;
    font-family: "IBM Plex Mono", monospace;
    background: rgba(111,207,151,0.14);
    border: 1px solid rgba(111,207,151,0.40);
    border-radius: 5px; padding: 1px 5px;
}
/* Secondary button of the highlighted card: the greet button already has the
   accent fill, and two fills side by side do not say which is which. */
QToolButton#bdayGhost {
    color: %4; background: %6;
    border: 1px solid %2; border-radius: 8px;
    padding: 5px 10px; font-size: 11px; font-weight: 600;
}
QToolButton#bdayGhost:hover { color: %5; border: 1px solid %5; background: %8; }
/* While ringing, the main button silences it, in the red of everything due. */
QPushButton#bdayStop { background: #ff7a6b; }
QPushButton#bdayStop:hover { background: #ff8f82; }
/* The two list orders as small tabs. Same language as the popup chips, with
   their own object name so the two places can change independently. */
QToolButton#bdayTab {
    color: %4; background: transparent;
    border: 1px solid transparent; border-radius: 7px;
    padding: 2px 7px; font-size: 9px; font-weight: 700;
    font-family: "IBM Plex Mono", monospace;
}
QToolButton#bdayTab:hover { color: %3; background: %7; }
QToolButton#bdayTab[chosen="true"] {
    color: %5; background: %8; border: 1px solid %9;
}

/* Month separator in the accent: in #meta's grey it got lost between the
   rows, and it exists to make the month visible at a glance. */
QLabel#bdayMonth {
    color: %5; font-size: 9px; font-weight: 700;
    font-family: "IBM Plex Mono", monospace;
}
QFrame#bdayMonthRule { background: %2; border: none; }

/* Plain QWidget rows: they need WA_StyledBackground (birthdays.cpp). */
QWidget#bdayRow { background: transparent; border-radius: 9px; }
QWidget#bdayRow:hover { background: %7; }
QLabel#bdayRowName { color: %3; font-size: {fs:12}px; font-weight: 600; }
QLabel#bdayDate {
    color: %4; font-size: 10px;
    font-family: "IBM Plex Mono", monospace;
}
QLabel#bdayWhen { color: %5; font-size: 9.5px; font-weight: 600; }
/* Today and tomorrow read first in the accent; the rest is context. */
QLabel#bdayWhen[soon="false"] { color: %4; }

/* New version: in the accent, not red. It is news, not a problem; red belongs
   to the "not saving" warning, which sits right above when both show. */
QWidget#updateBanner {
    background: %8;
    border-bottom: 1px solid %9;
}
QWidget#updateBanner:hover { background: %10; }
QLabel#updateBannerText { color: %5; font-size: 10.5px; font-weight: 600; }
)" R"(
/* Timers */
QWidget#timers { background: transparent; }
/* Plain QWidget rows: they need WA_StyledBackground (timers.cpp). */
QWidget#timerRow {
    background: %6; border: 1px solid %2; border-radius: 10px;
}
QWidget#timerRow:hover { background: %7; }
QWidget#timerRow[chosen="true"] { border: 1px solid %9; }
QWidget#timerRow[done="true"] { border: 1px solid rgba(255,122,107,0.55); }
QLabel#timerName { color: %3; font-size: {fs:12}px; font-weight: 600; }
QLabel#timerTime, QLabel#timerTimeLive {
    color: %3; font-size: {fs:13}px;
    font-family: "IBM Plex Mono", monospace;
}
QLabel#timerTimeLive { color: %5; }
QLabel#timerFocusName { color: %3; font-size: {fs:12.5}px; font-weight: 600; }
QLineEdit#timerField {
    font-size: 14px; font-family: "IBM Plex Mono", monospace;
    padding: 6px 2px;
}
/* Footer countdown of the running timer. */
QToolButton#footerTimer {
    color: %5; background: %8; border: 1px solid %9; border-radius: 7px;
    padding: 1px 7px; font-size: 10px; font-family: "IBM Plex Mono", monospace;
}

/* Time's up / event starting: the red of everything due, with its buttons. */
QWidget#alarmBar {
    background: rgba(255,122,107,0.14);
    border-bottom: 1px solid rgba(255,122,107,0.35);
}
QLabel#alarmBarText { color: #ff7a6b; font-size: 11px; font-weight: 600; }
QToolButton#alarmBtn {
    color: #ff7a6b; background: transparent;
    border: 1px solid rgba(255,122,107,0.45); border-radius: 7px;
    padding: 3px 8px; font-size: 10.5px; font-weight: 600;
}
QToolButton#alarmBtn:hover { background: rgba(255,122,107,0.18); }

/* Planner */
QWidget#planner { background: transparent; }
QWidget#plannerSide { background: rgba(255,255,255,0.015); }
QLabel#plannerSideText { font-size: {fs:11.5}px; }
QCheckBox#plannerCat { color: %3; font-size: 11.5px; }
QLineEdit#plannerTitleEdit {
    font-size: {fs:13.5}px; font-weight: 600; padding: 8px 10px;
}
QLineEdit#plannerField { font-family: "IBM Plex Mono", monospace; }
QLabel#plannerError { color: #ff7a6b; font-size: 10.5px; }

/* Settings page */
QWidget#settings { background: transparent; }
QLabel#setSection {
    color: %4; font-size: 9px; font-weight: 700;
    font-family: "IBM Plex Mono", monospace;
    padding: 6px 2px 1px 2px;
}
QLabel#setValue {
    color: %3; font-size: 10px; font-weight: 700;
    font-family: "IBM Plex Mono", monospace;
}
QLabel#setRowText { color: %3; font-size: 11.5px; }
/* Section header: the name in the text colour with the accent icon before it. */
QLabel#setHead {
    color: %3; font-size: 10px; font-weight: 700; letter-spacing: 0.6px;
}
/* A section's group; rows are separated by a thin line. */
QFrame#setGroup {
    background: %6;
    border: 1px solid %2;
    border-radius: 11px;
}
QFrame#setDivider { background: %2; border: none; }
/* Segmented control: a sunken track with the chosen option in the accent. */
QFrame#segTrack {
    background: rgba(0,0,0,0.22);
    border: 1px solid %2;
    border-radius: 9px;
}
QToolButton#segOption {
    color: %4; background: transparent;
    border: 1px solid transparent; border-radius: 7px;
    padding: 5px 6px; font-size: 11px; font-weight: 600;
}
QToolButton#segOption:hover { color: %3; background: %7; }
QToolButton#segOption[chosen="true"] {
    color: %5; background: %8; border: 1px solid %9;
}
/* Plain QWidget rows: they need WA_StyledBackground (settings.cpp). */
QWidget#setRow { background: transparent; border-radius: 8px; }
QWidget#setRow:hover { background: %7; }
/* Action buttons, and single-choice groups in the planner and timers. The
   chosen one is tinted with the accent. */
QToolButton#segButton {
    color: %4; background: %6;
    border: 1px solid %2; border-radius: 8px;
    padding: 6px 10px; font-size: 11px; font-weight: 600;
}
QToolButton#segButton:hover { color: %3; background: %7; }
QToolButton#segButton[chosen="true"] {
    color: %5; background: %8; border: 1px solid %9;
}
QToolButton#segButton:disabled { color: rgba(139,144,154,0.45); background: transparent; }
QFrame#setCard {
    background: %6;
    border: 1px solid %2;
    border-radius: 10px;
}
/* Quit is red and outlined, not filled: it does not compete with the accent,
   but is not clicked by accident either. */
QToolButton#quitBtn {
    color: #ff7a6b; background: transparent;
    border: 1px solid rgba(255,122,107,0.35); border-radius: 8px;
    padding: 6px 11px; font-size: 11px; font-weight: 600;
}
QToolButton#quitBtn:hover { background: rgba(255,122,107,0.14); }

/* Popups */
QFrame#popupShell {
    background: %1;
    border: 1px solid %2;
    border-radius: 12px;
}
QLabel#popupHeader {
    color: %4; font-size: 9px; font-weight: 700;
    font-family: "IBM Plex Mono", monospace;
    padding: 2px 8px;
}
QLabel#popupText { color: %4; font-size: 11px; }
/* Renaming an area in its own tab, with the tab's font so the name does not
   jump. */
QLineEdit#areaRename {
    background: %6; border: 1px solid %5; border-radius: 6px;
    padding: 1px 5px; font-size: 12px; font-weight: 600; color: %3;
}
QLineEdit#popupEdit {
    background: %6; border: 1px solid %2; border-radius: 8px;
    padding: 6px 9px; font-size: 11.5px; color: %3;
}
QLineEdit#popupEdit:focus { border: 1px solid %5; }
/* Popup chips: small buttons in rows, so a list of options stays shorter than
   the widget that opens it. */
QToolButton#popupChip {
    color: %3; background: %6;
    border: 1px solid %2; border-radius: 8px;
    padding: 4px 5px; font-size: 11px; font-weight: 600;
    font-family: "IBM Plex Mono", monospace;
}
QToolButton#popupChip:hover { color: %5; border: 1px solid %5; background: %8; }
QToolButton#popupChip[past="true"] { color: %4; }
/* The chosen chip of a group: tinted with the accent, so a row of chips says
   both what can be picked and what is set. */
QToolButton#popupChip[chosen="true"] {
    color: %5; background: %8; border: 1px solid %9;
}
/* Keyboard focus. Tab-reachable widgets get a border when focused *by the
   keyboard* ([kbfocus], set by ui/keynav.hpp), like the web's :focus-visible;
   without that condition the ring also showed when Qt moved the focus itself
   on hiding a page. Last in the sheet so it wins over per-object borders. */
QToolButton[kbfocus="true"]:focus,
QToolButton#segButton[kbfocus="true"]:focus, QToolButton#segOption[kbfocus="true"]:focus,
QToolButton#popupChip[kbfocus="true"]:focus,
QToolButton#calNav[kbfocus="true"]:focus, QToolButton#todayBtn[kbfocus="true"]:focus,
QToolButton#bdayTab[kbfocus="true"]:focus, QToolButton#bdayGhost[kbfocus="true"]:focus,
QToolButton#listDone[kbfocus="true"]:focus, QToolButton#quitBtn[kbfocus="true"]:focus,
QToolButton#alarmBtn[kbfocus="true"]:focus, QToolButton#footerTimer[kbfocus="true"]:focus,
QToolButton#dragHandle[kbfocus="true"]:focus {
    border: 1px solid %5;
}
/* Widgets already in the accent when chosen need another focus colour: the
   text colour. */
QToolButton#segButton[chosen="true"][kbfocus="true"]:focus,
QToolButton#segOption[chosen="true"][kbfocus="true"]:focus,
QToolButton#popupChip[chosen="true"][kbfocus="true"]:focus,
QToolButton#bdayTab[chosen="true"][kbfocus="true"]:focus,
QToolButton[active="true"][kbfocus="true"]:focus {
    border: 1px solid %3;
}
QPushButton[kbfocus="true"]:focus { border: 2px solid %3; padding: 4px 10px; }
QCheckBox[kbfocus="true"]::indicator:focus { border: 2px solid %3; }
QTextEdit:focus { border: 1px solid %5; }
QLineEdit#cardTitleEdit:focus, QLineEdit#newItemEdit:focus, QLineEdit#checkTextEdit:focus {
    border: none; border-bottom: 1px solid %5;
}
QSlider[kbfocus="true"]:focus { border: 1px solid %9; border-radius: 6px; }
QWidget#setRow[kbfocus="true"]:focus, QWidget#linkRow[kbfocus="true"]:focus,
QWidget#imgHeader[kbfocus="true"]:focus, QWidget#bdayRow[kbfocus="true"]:focus,
#bdayToday[kbfocus="true"]:focus, QWidget#timerRow[kbfocus="true"]:focus,
QWidget#updateBanner[kbfocus="true"]:focus, QLabel#addDetails[kbfocus="true"]:focus {
    border: 1px solid %5;
}
QLabel#chip[kbfocus="true"]:focus { border: 1px solid %3; }
)")
        .arg(card())      // %1
        .arg(line())      // %2
        .arg(fg())        // %3
        .arg(muted())     // %4
        .arg(acc)         // %5
        .arg(sunk())      // %6
        .arg(hover())     // %7
        .arg(accentRgba(0.14))    // %8  tinted fill
        .arg(accentRgba(0.38))    // %9  tinted border
        .arg(accentRgba(0.24));   // %10 hover fill

    // Content sizes are marked and scaled here, back to front so each replacement
    // does not shift the ones still pending.
    static const QRegularExpression mark(R"(\{fs:([\d.]+)\})");
    QList<QRegularExpressionMatch> found;
    for (auto it = mark.globalMatch(out); it.hasNext();) found.append(it.next());
    for (auto it = found.crbegin(); it != found.crend(); ++it)
        out.replace(it->capturedStart(), it->capturedLength(),
                    QString::number(fs(it->captured(1).toDouble()), 'f', 1));
    return out;
}
