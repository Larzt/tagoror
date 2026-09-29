#pragma once

#include <QApplication>
#include <QFrame>
#include <QMouseEvent>
#include <QPainter>
#include <QToolButton>
#include <QWidget>
#include <QWindow>

/// @file
/// Everything that moves or resizes the window delegates to the compositor:
/// Wayland does not let a client position its own windows. Any new handle
/// should follow these three.

/// Draggable header strip: startSystemMove() on press.
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

/// A button that can also be dragged (the folded dock). It still answers a
/// click when the pointer did not move; once dragging, startSystemMove() takes
/// the rest of the gesture and the click never fires.
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
                setDown(false);          // otherwise it stays pressed after the drag
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

/// Resize corner, delegated to the compositor through startSystemResize().
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
