#pragma once

#include <QAbstractButton>
#include <QAbstractScrollArea>
#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPushButton>
#include <QStyle>
#include <QWidget>
#include <functional>

// Navegación con el teclado.
//
// Los botones de Qt ya reciben el foco con Tab; lo que no lo recibía era todo
// lo que en esta aplicación está hecho a mano para poder pintarse como en el
// diseño: las filas de los menús, las de ajustes, los enlaces de una tarjeta,
// el chip de la fecha, los puntos del acento, los interruptores. Eran solo de
// ratón. Esto les da foco por Tab (no por clic: un anillo que aparece al pulsar
// con el ratón es ruido) y los activa con Intro o con la barra espaciadora, que
// es lo que hace un botón de verdad.
namespace keynav {

class Activator : public QObject {
public:
    Activator(QWidget *target, std::function<void()> activate)
        : QObject(target), m_activate(std::move(activate)) {}

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        auto *w = static_cast<QWidget *>(watched);
        switch (event->type()) {
            case QEvent::KeyPress: {
                const int key = static_cast<QKeyEvent *>(event)->key();
                if ((key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Space) &&
                    m_activate) {
                    m_activate();
                    return true;
                }
                break;
            }
            // Los que se pintan a mano no se repintan solos al ganar o perder el
            // foco, y el anillo se quedaría puesto o no llegaría a salir.
            case QEvent::FocusIn:
            case QEvent::FocusOut:
                w->update();
                break;
            default:
                break;
        }
        return false;
    }

private:
    std::function<void()> m_activate;
};

// Hace que un widget se pueda alcanzar con Tab y pulsar con Intro/Espacio.
inline void activatable(QWidget *w, std::function<void()> activate) {
    w->setFocusPolicy(Qt::TabFocus);
    w->installEventFilter(new Activator(w, std::move(activate)));
}

// ¿Hay que pintar el anillo del foco? Solo si llegó con el teclado. Es el
// :focus-visible de la web: cuando se esconde el widget que tenía el foco, Qt
// se lo pasa por su cuenta al siguiente de la cadena -- el primer botón de la
// cabecera --, y un anillo ahí que nadie ha pedido parece un botón atascado.
inline bool showsFocus(const QWidget *w) {
    return w->hasFocus() && w->property("kbfocus").toBool();
}

// Filtro de toda la aplicación, con dos trabajos:
//  - apuntar en cada widget si el foco le llegó por Tab (la propiedad
//    "kbfocus", que es lo que miran las reglas de foco de la hoja de estilos
//    y showsFocus());
//  - que Intro pulse el botón que tiene el foco. QAbstractButton solo
//    responde a la barra espaciadora, y quien navega con el teclado espera
//    que Intro también valga; QPushButton ya lo hace y se deja en paz.
class ButtonEnter : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        // Los campos de texto no: su borde de foco se enseña siempre, porque
        // dice dónde va a caer lo que se escriba. Y repulirlos a cada foco
        // rehace su letra, que al editor de Markdown le cuesta un formateo.
        if (event->type() == QEvent::FocusIn && watched->isWidgetType() &&
            !qobject_cast<QLineEdit *>(watched) && !qobject_cast<QAbstractScrollArea *>(watched)) {
            auto *w = static_cast<QWidget *>(watched);
            const Qt::FocusReason reason = static_cast<QFocusEvent *>(event)->reason();
            // Las flechas de un menú y un atajo también son teclado.
            const bool keyboard = reason == Qt::TabFocusReason ||
                                  reason == Qt::BacktabFocusReason ||
                                  reason == Qt::ShortcutFocusReason;
            if (w->property("kbfocus").toBool() != keyboard) {
                w->setProperty("kbfocus", keyboard);
                // Una propiedad dinámica no repinta sola.
                w->style()->unpolish(w);
                w->style()->polish(w);
                w->update();
            }
            return false;
        }
        if (event->type() != QEvent::KeyPress) return false;
        const int key = static_cast<QKeyEvent *>(event)->key();
        if (key != Qt::Key_Return && key != Qt::Key_Enter) return false;
        auto *button = qobject_cast<QAbstractButton *>(watched);
        if (!button || qobject_cast<QPushButton *>(watched) || !button->hasFocus() ||
            !button->isEnabled())
            return false;
        button->animateClick();
        return true;
    }
};

inline void installButtonEnter() {
    static ButtonEnter *filter = nullptr;
    if (filter) return;
    filter = new ButtonEnter(qApp);
    qApp->installEventFilter(filter);
}

}  // namespace keynav
