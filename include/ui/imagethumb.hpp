#pragma once

#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QWidget>
#include <functional>

#include "ui/theme.hpp"

// Miniatura de una imagen adjunta: una vista previa pequeña, con el tamaño
// tomado de la proporción de la foto sobre un alto fijo.
//
// Ocupaba todo el ancho de la tarjeta y hasta 190 px de alto, que para dos o
// tres capturas es una tarjeta entera de imagen; ahora es una miniatura que se
// deja mirar de un vistazo y se abre con un clic si se quiere verla de verdad.
//
// No es un QLabel con un pixmap por lo mismo que el texto de las listas no vive
// dentro del QCheckBox: el sizeHint de un QLabel con imagen es el tamaño de la
// imagen, y como la lista no tiene barra horizontal, una captura de 1920 px
// ensancharía la tarjeta -- y con ella toda la lista -- muy por encima del
// panel. Aquí el ancho no pasa nunca de kMaxWidth.
class ImageThumb : public QWidget {
public:
    static constexpr int kHeight = 92;      // alto de la vista previa
    static constexpr int kMaxWidth = 156;   // tope de ancho: la tarjeta manda
    static constexpr int kMinWidth = 56;

    ImageThumb(const QString &path, int height = kHeight, QWidget *parent = nullptr)
        : QWidget(parent), m_h(height) {
        m_pixmap.load(path);
        setCursor(Qt::PointingHandCursor);
        setToolTip(path);
        // Fijar el tamaño no cambia la política, y una política Expanding se
        // propaga hacia arriba: la tarjeta pediría alto de más y lo repartiría
        // en el hijo flexible de abajo (ver autoGrowEditor en notecard.cpp).
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
            // El fichero se ha borrado o no se puede leer: se dice, en vez de
            // dejar un hueco vacío que parece un fallo de pintado.
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 255, 255, 10));
            p.drawPath(clip);
            p.setPen(QColor(Theme::muted()));
            p.drawText(rect(), Qt::AlignCenter, "?");
            return;
        }

        p.setClipPath(clip);
        // Se recorta al centro en vez de deformarse: una captura apaisada
        // dentro de una tarjeta estrecha se lee mejor recortada que estirada.
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
    // El ancho sale de la proporción de la foto, acotado por los dos extremos:
    // un panorama no puede ensanchar la tarjeta y un retrato no puede quedarse
    // en una tira de dos píxeles.
    QSize previewSize() const {
        if (m_pixmap.isNull() || m_pixmap.height() <= 0) return QSize(120, m_h);
        const int w = int(qreal(m_h) * m_pixmap.width() / m_pixmap.height());
        return QSize(qBound(kMinWidth, w, kMaxWidth), m_h);
    }

    QPixmap m_pixmap;
    int m_h;
};
