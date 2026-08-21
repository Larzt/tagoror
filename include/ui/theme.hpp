#pragma once

#include <QColor>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QPixmap>
#include <QString>

struct Theme {
    QColor accent{"#7c9cff"};
    int opacity = 96;   // 40..100

    // Color base de las superficies. Se expone también como QColor para que
    // los popups (que se pintan a mano) usen exactamente la misma opacidad
    // que el panel en lugar de quedarse opacos.
    QColor cardColor() const {
        return QColor(19, 22, 27, int(255 * opacity / 100.0));
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

// Iconos dibujados a mano: evita depender de un tema de iconos del sistema.
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

    // --- tipos de nota (selector de "nueva nota") --------------------------
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
    } else if (kind == "chevronLeft") {
        p.drawPolyline(QPolygonF({QPointF(c + 2.0, c - 4.4), QPointF(c - 2.4, c),
                                  QPointF(c + 2.0, c + 4.4)}));
    } else if (kind == "chevronRight") {
        p.drawPolyline(QPolygonF({QPointF(c - 2.0, c - 4.4), QPointF(c + 2.4, c),
                                  QPointF(c - 2.0, c + 4.4)}));
    } else if (kind == "voice") {
        p.drawRoundedRect(QRectF(c - 2.1, c - 6, 4.2, 7.4), 2.1, 2.1);
        QPainterPath arc;
        arc.arcMoveTo(QRectF(c - 4.6, c - 4.6, 9.2, 9.2), 200);
        arc.arcTo(QRectF(c - 4.6, c - 4.6, 9.2, 9.2), 200, 140);
        p.drawPath(arc);
        p.drawLine(QPointF(c, c + 4.6), QPointF(c, c + 6.4));

    // --- acciones -----------------------------------------------------------
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

    // --- adornos ------------------------------------------------------------
    } else if (kind == "checkdots") {
        // Igual que QCheckBox::indicator pero con el borde punteado: marca la
        // fila de "añadir elemento" como una casilla todavía sin existir.
        pen.setWidthF(1.4);
        pen.setStyle(Qt::CustomDashLine);
        pen.setDashPattern({0.9, 1.8});
        pen.setCapStyle(Qt::RoundCap);
        p.setPen(pen);
        p.drawRoundedRect(QRectF(c - 5.2, c - 5.2, 10.4, 10.4), 3.2, 3.2);
    } else if (kind == "grip") {
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j <= i; ++j)
                p.drawEllipse(QPointF(c + 4 - i * 3.4, c + 4 - j * 3.4), 1.05, 1.05);
    }
    p.end();
    return QIcon(pm);
}

inline QString Theme::sheet() const {
    const QString acc = accent.name();

    return QString(R"(
QFrame#shell {
    background: %1;
    border: 1px solid %2;
    border-radius: 14px;
}
QFrame#header { border: none; border-bottom: 1px solid %2; }
QFrame#footer { border: none; border-top: 1px solid %2; background: %6; }

QLabel          { color: %3; font-size: 12px; }
QLabel#title    { font-size: 12.5px; font-weight: 600; }
QLabel#cardTitle{ font-size: 12px;   font-weight: 600; }
QLabel#body     { color: %4; font-size: 11.5px; }
QLabel#meta     { color: %4; font-size: 9.5px; font-family: "IBM Plex Mono", monospace; }
QLabel#chip {
    color: #f2b757; font-size: 10.5px; font-weight: 600;
    background: rgba(242,183,87,0.12);
    border: 1px solid rgba(242,183,87,0.35);
    border-radius: 6px; padding: 2px 7px;
}
/* Un recordatorio pasado de hora va en rojo, haya sonado ya o no: en ámbar
   se confundía con los que aún están por llegar. */
QLabel#chip[state="overdue"], QLabel#chip[state="ringing"] {
    color: #ff7a6b;
    background: rgba(255,122,107,0.16);
    border: 1px solid rgba(255,122,107,0.55);
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
    font-size: 12px; font-weight: 600; color: %3;
}
/* Fila "añadir elemento": sin caja, para que se lea como una tarea más. */
QLineEdit#newItemEdit {
    background: transparent; border: none; padding: 0;
    font-size: 11.5px; color: %3;
}
QLineEdit#newItemEdit:focus { border: none; }
QTextEdit {
    color: %3; background: %6;
    border: 1px solid %2; border-radius: 8px;
    padding: 6px; font-size: 11.5px;
    selection-background-color: %5;
}

QCheckBox { color: %3; font-size: 11.5px; spacing: 8px; }
/* El texto de un elemento vive en su propia etiqueta para poder partirse en
   varias líneas; ver addCheckRow(). */
QLabel#checkText { color: %3; font-size: 11.5px; }
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

/* --- calendario ---------------------------------------------------------- */
QWidget#calendar { background: transparent; }
QFrame#calSeparator { background: %2; border: none; }
QToolButton#todayBtn {
    color: %4; font-size: 10px; font-weight: 600;
    padding: 4px 8px; border-radius: 8px;
}
QToolButton#todayBtn:hover { color: %3; background: %7; }
/* Las filas del día son QWidget lisos: sin WA_StyledBackground no pintarían
   este fondo (se pone en calendar.cpp). */
QWidget#dayRow { background: transparent; border-radius: 8px; }
QWidget#dayRow:hover { background: %7; }
QLabel#dayRowTitle { color: %3; font-size: 11.5px; }
QLabel#dayRowTitleAlert { color: #ff7a6b; font-size: 11.5px; font-weight: 600; }

/* --- popups propios (nueva nota, ajustes, menú de tarjeta, fecha) --------- */
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
QLineEdit#popupEdit {
    background: %6; border: 1px solid %2; border-radius: 8px;
    padding: 6px 9px; font-size: 11.5px; color: %3;
}
QLineEdit#popupEdit:focus { border: 1px solid %5; }
)")
        .arg(card())      // %1
        .arg(line())      // %2
        .arg(fg())        // %3
        .arg(muted())     // %4
        .arg(acc)         // %5
        .arg(sunk())      // %6
        .arg(hover());    // %7
}
