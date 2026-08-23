#pragma once

#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QWidget>
#include <functional>

#include "ui/theme.hpp"

// Miniatura de una imagen adjunta: se ajusta al ancho que le den y calcula su
// alto a partir de la proporción de la foto.
//
// No es un QLabel con un pixmap por lo mismo que el texto de las listas no vive
// dentro del QCheckBox: el sizeHint de un QLabel con imagen es el tamaño de la
// imagen, y como la lista no tiene barra horizontal, una captura de 1920 px
// ensancharía la tarjeta -- y con ella toda la lista -- muy por encima del
// panel. Aquí el mínimo es pequeño y el alto se recalcula al cambiar el ancho.
class ImageThumb : public QWidget {
public:
    ImageThumb(const QString &path, int maxHeight = 190, QWidget *parent = nullptr)
        : QWidget(parent), m_max(maxHeight) {
        m_pixmap.load(path);
        setCursor(Qt::PointingHandCursor);
        setToolTip(path);
        // Fijar el alto no cambia la política, y una política Expanding se
        // propaga hacia arriba: la tarjeta pediría alto de más y lo repartiría
        // en el hijo flexible de abajo (ver autoGrowEditor en notecard.cpp).
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        updateHeight();
    }

    bool isValid() const { return !m_pixmap.isNull(); }

    std::function<void()> activate;
    std::function<void(const QPoint &)> menu;

    QSize minimumSizeHint() const override { return QSize(48, m_lastHeight); }

protected:
    void resizeEvent(QResizeEvent *e) override {
        QWidget::resizeEvent(e);
        updateHeight();
    }

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
    void updateHeight() {
        const int w = qMax(1, width());
        int h = m_max;
        if (!m_pixmap.isNull() && m_pixmap.width() > 0)
            h = qBound(40, int(qreal(w) * m_pixmap.height() / m_pixmap.width()), m_max);
        else if (m_pixmap.isNull())
            h = 46;
        if (h == m_lastHeight) return;
        m_lastHeight = h;
        setFixedHeight(h);
    }

    QPixmap m_pixmap;
    int m_max;
    int m_lastHeight = 0;
};
