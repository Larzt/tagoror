#pragma once

#include <QColor>
#include <QEnterEvent>
#include <QFontMetrics>
#include <QLabel>
#include <QPainter>

// Etiqueta que recorta el texto con puntos suspensivos en vez de exigir su
// ancho completo. Es lo que evita que un texto largo ensanche su tarjeta por
// encima del panel: la lista no tiene barra horizontal, así que el mínimo de
// un hijo se convierte en el mínimo de toda la lista (ver CLAUDE.md).
class ElidedLabel : public QLabel {
public:
    ElidedLabel(const QString &text, const QColor &color, QWidget *parent = nullptr)
        : QLabel(text, parent), m_color(color) {
        setAttribute(Qt::WA_Hover);
    }

    // Subraya al pasar por encima; para lo que se puede pulsar.
    void setUnderlineOnHover(bool on) { m_underline = on; }

    QSize minimumSizeHint() const override { return QSize(24, sizeHint().height()); }

protected:
    void enterEvent(QEnterEvent *) override { m_hover = true;  update(); }
    void leaveEvent(QEvent *) override      { m_hover = false; update(); }

    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        QFont f = font();
        f.setUnderline(m_underline && m_hover);
        p.setFont(f);
        p.setPen(m_color);
        p.drawText(rect(), Qt::AlignVCenter | Qt::AlignLeft,
                   QFontMetrics(f).elidedText(text(), Qt::ElideRight, width()));
    }

private:
    QColor m_color;
    bool m_hover = false;
    bool m_underline = false;
};
