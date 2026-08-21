#pragma once

#include <QApplication>
#include <QFrame>
#include <QMouseEvent>
#include <QPainter>
#include <QToolButton>
#include <QWidget>
#include <QWindow>

// Todo lo que mueve o redimensiona la ventana se delega en el compositor:
// Wayland no permite que un cliente coloque sus propias ventanas. Cualquier
// asidero nuevo debe seguir a estos tres.

// Franja superior arrastrable: delega el movimiento al compositor, así
// funciona igual en X11 y en Wayland (donde move() no está permitido).
class DragBar : public QFrame {
public:
    using QFrame::QFrame;

protected:
    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton) {
            if (QWindow *w = window()->windowHandle()) {
                w->startSystemMove();
                return;
            }
        }
        QFrame::mousePressEvent(e);
    }
};

// Botón que además se puede arrastrar: el dock plegado se mueve como el panel,
// pero sigue respondiendo al clic si no ha habido desplazamiento. startSystemMove
// se queda con el resto del gesto, así que el clic no llega a dispararse.
class DragButton : public QToolButton {
public:
    using QToolButton::QToolButton;

protected:
    void mousePressEvent(QMouseEvent *e) override {
        m_press = e->globalPosition().toPoint();
        m_dragging = false;
        QToolButton::mousePressEvent(e);
    }

    void mouseMoveEvent(QMouseEvent *e) override {
        if (!m_dragging && (e->buttons() & Qt::LeftButton) &&
            (e->globalPosition().toPoint() - m_press).manhattanLength() >=
                QApplication::startDragDistance()) {
            if (QWindow *w = window()->windowHandle()) {
                m_dragging = true;
                setDown(false);          // si no, se queda hundido tras el arrastre
                w->startSystemMove();
                return;
            }
        }
        QToolButton::mouseMoveEvent(e);
    }

    void mouseReleaseEvent(QMouseEvent *e) override {
        if (m_dragging) { m_dragging = false; return; }
        QToolButton::mouseReleaseEvent(e);
    }

private:
    QPoint m_press;
    bool m_dragging = false;
};

// Esquina de redimensión. Igual que el arrastre, se delega en el compositor
// (startSystemResize) para que funcione en Wayland.
class GripCorner : public QWidget {
public:
    explicit GripCorner(QWidget *parent = nullptr) : QWidget(parent) {
        setFixedSize(16, 16);
        setCursor(Qt::SizeFDiagCursor);
        setToolTip("Redimensionar");
    }

protected:
    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton) {
            if (QWindow *w = window()->windowHandle()) {
                w->startSystemResize(Qt::BottomEdge | Qt::RightEdge);
                return;
            }
        }
        QWidget::mousePressEvent(e);
    }

    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(139, 144, 154, 150));
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j <= i; ++j)
                p.drawEllipse(QPointF(12 - i * 4.0, 12 - j * 4.0), 1.05, 1.05);
    }
};
