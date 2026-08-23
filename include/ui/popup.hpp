#pragma once

#include <climits>

#include <QColor>
#include <QList>
#include <QStringList>
#include <QWidget>
#include <functional>

#include "ui/theme.hpp"

class QFrame;
class QVBoxLayout;

// Menú propio del widget: sustituye a QMenu en todos los selectores.
//
// QMenu es una ventana nativa que no hereda la translucidez del panel, así que
// con esquinas redondeadas y fondo semitransparente se veía opaco y con los
// bordes cuadrados. Popup se dibuja como el panel (marco translúcido + sombra)
// y toma la opacidad del Theme, de modo que todo el widget se ve igual.
class Popup : public QWidget {
    Q_OBJECT

public:
    // Los menús se dibujan opacos aunque el panel sea translúcido: un menú
    // transparente sobre las tarjetas se lee fatal. Translucent es opt-in.
    enum Surface { Opaque, Translucent };

    explicit Popup(const Theme &theme, QWidget *anchor = nullptr, Surface surface = Opaque);

    void addHeader(const QString &text);
    void addItem(const QString &iconKind, const QString &title,
                 const QString &subtitle = QString(),
                 std::function<void()> action = nullptr);
    void addSwatches(const QList<QColor> &colors, const QColor &current,
                     std::function<void(const QColor &)> action);
    void addSlider(int min, int max, int value, std::function<void(int)> live);
    void addEditor(const QString &placeholder, const QString &text,
                   std::function<void(const QString &)> commit);
    // Varios campos en un mismo popup, confirmados a la vez. Con addEditor no
    // se puede: cada campo cerraría el popup por su cuenta al pulsar Enter.
    void addFields(const QStringList &placeholders, const QStringList &values,
                   std::function<void(const QStringList &)> commit);
    // Botones pequeños en horizontal, dos o tres por fila. Una lista de horas
    // como filas de menú mide más que el calendario que la abre.
    void addChips(const QStringList &labels, const QList<bool> &muted,
                  const QString &mutedTip, std::function<void(int)> action);
    void addSeparator();

    // Sitúa el popup pegado a un widget, corrigiendo si se sale de la pantalla.
    void showUnder(QWidget *anchor);
    // Igual, pero con la promesa de no subirse nunca por encima del widget:
    // antes, un panel cerca del borde inferior hacía que la corrección contra
    // la pantalla empujara el menú hacia arriba, sobre el calendario.
    void showBelow(QWidget *anchor);
    void showAt(const QPoint &globalPos);

private:
    // La acción se ejecuta tras cerrar: elegir "eliminar" destruye la tarjeta
    // que abrió el popup, así que no puede correr con el popup todavía vivo.
    void run(const std::function<void()> &action);
    // minY corta la corrección hacia arriba: el popup se saldrá por abajo
    // antes que taparse el widget que lo abrió.
    void place(const QPoint &globalTopLeft, int minY = INT_MIN);

    Theme m_theme;
    QFrame *m_shell = nullptr;
    QVBoxLayout *m_col = nullptr;
};
