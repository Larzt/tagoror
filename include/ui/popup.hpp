#pragma once

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
    void addSeparator();

    // Sitúa el popup pegado a un widget, corrigiendo si se sale de la pantalla.
    void showUnder(QWidget *anchor);
    void showAt(const QPoint &globalPos);

private:
    // La acción se ejecuta tras cerrar: elegir "eliminar" destruye la tarjeta
    // que abrió el popup, así que no puede correr con el popup todavía vivo.
    void run(const std::function<void()> &action);
    void place(const QPoint &globalTopLeft);

    Theme m_theme;
    QFrame *m_shell = nullptr;
    QVBoxLayout *m_col = nullptr;
};
