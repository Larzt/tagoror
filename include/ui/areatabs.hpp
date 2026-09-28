#pragma once

#include <QList>
#include <QRect>
#include <QString>
#include <QWidget>
#include <functional>

#include "ui/theme.hpp"

class QLineEdit;

// La tira de pestañas de las áreas de trabajo, entre la cabecera y la lista.
//
// Es un QWidget pintado a mano y no un QTabBar: tiene que pintar el glifo de
// cada área, el punto rojo de las que tienen algo sonando y un «+N» con las que
// no caben. Y no se desplaza en horizontal: un scroll lateral esconde pestañas
// sin dar pistas y se lleva mal con la rueda. Se enseñan las que caben (dos o
// tres a 300 px), la activa siempre, y el resto va a «+N», que hereda el punto
// rojo de las que esconde.
//
// Como las piezas del planificador, habla por std::function y no por señales:
// no hay Q_OBJECT ni moc que tocar para añadir una.
class AreaTabs : public QWidget {
public:
    struct Tab {
        QString id;
        QString name;
        QString glyph;
        bool ringing = false;
    };

    // El Theme es un puntero al del panel, igual que en ajustes: así un cambio
    // de acento se ve con un update() y sin rehacer nada.
    explicit AreaTabs(const Theme *theme, QWidget *parent = nullptr);

    void setTabs(const QList<Tab> &tabs, const QString &active);
    QString active() const { return m_active; }

    // Deja el nombre de esa pestaña en edición, sobre la propia pestaña.
    // Intro confirma, Escape cancela y perder el foco también confirma.
    void beginRename(const QString &id);

    // Para soltar una tarjeta encima: el área bajo ese punto de la pantalla,
    // o vacío. setDropTarget() la resalta mientras dura el arrastre.
    QString areaAt(const QPoint &globalPos) const;
    void setDropTarget(const QString &id);

    // Dónde abrir el menú de una pestaña o el de «+N», en coordenadas de
    // pantalla (la esquina de abajo a la izquierda).
    QPoint menuPoint(const QString &id) const;

    std::function<void(const QString &)> activated;
    std::function<void(const QString &, const QPoint &)> menuRequested;
    std::function<void(const QPoint &)> overflowRequested;
    std::function<void()> addRequested;
    std::function<void(const QString &, const QString &)> renamed;
    std::function<void(const QString &)> deleteRequested;
    std::function<void(const QString &, int)> moveRequested;

    QSize sizeHint() const override;
    // Pequeño a propósito: la tira no puede exigir el ancho de sus nombres o
    // ensancharía el panel entero (ver *Card widths*). Lo que no cabe va a «+N».
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void focusInEvent(QFocusEvent *e) override;
    void focusOutEvent(QFocusEvent *e) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // Qué hay en cada sitio de la tira, recalculado al cambiar el ancho o las
    // pestañas. index es la posición en m_tabs.
    struct Slot {
        int index;
        QRect rect;
        QString text;   // el nombre, ya recortado si hacía falta
    };
    void relayout();
    int tabWidth(const Tab &t, QString *elided) const;
    int indexOf(const QString &id) const;
    // -2 el «+», -3 el «+N», -1 nada; si no, el índice de la pestaña.
    int hitTest(const QPoint &pos) const;
    void activate(int index);
    void finishRename(bool commit);

    const Theme *m_theme;
    QList<Tab> m_tabs;
    QString m_active;
    QList<Slot> m_slots;
    QRect m_overflowRect;
    QRect m_addRect;
    int m_hidden = 0;
    bool m_hiddenRinging = false;
    int m_hover = -1;
    QString m_drop;
    QLineEdit *m_editor = nullptr;
    QString m_editing;
};
