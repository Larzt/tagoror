#pragma once

#include <QList>
#include <QWidget>

#include "core/birthday.hpp"
#include "ui/theme.hpp"

class QVBoxLayout;

// Página de cumpleaños: la tercera de m_body, hermana del calendario.
//
// Arriba, el más inminente, destacado en una tarjeta con lo que se puede hacer
// con él: darlo por felicitado —solo el día que toca— y ponerle o quitarle un
// aviso. Es el siguiente que llega, sea hoy o dentro de tres meses: destacar
// solo los de hoy dejaba la página sin nada resaltado 364 días al año.
//
// Debajo, el resto, partidos por meses, que es como se lee una lista de
// cumpleaños y como estaba escrita la que estrenó la página. En qué orden van
// esos meses lo elige el usuario con los dos botones de la cabecera: por lo que
// falta para cada uno (empezando por el mes de hoy y dando la vuelta al año) o
// por el calendario de siempre, de enero a diciembre.
//
// Todo va dentro de una misma zona de desplazamiento, tarjeta de hoy incluida.
// Dejarla fuera se veía mejor, pero entonces el alto mínimo de la página
// dependía de cuánta gente cumple hoy: tres el mismo día lo subían varios
// cientos de píxeles y la ventana crecía con ellos. Ver *Window behavior* en
// CLAUDE.md: el mínimo sale del layout, así que un layout que no está acotado
// es una ventana que tampoco lo está.
//
// Como CalendarView, no toca disco ni guarda nada: sostiene un puntero a la
// lista del Store y avisa hacia arriba de lo que el usuario pide.
class BirthdayView : public QWidget {
    Q_OBJECT

public:
    explicit BirthdayView(const Theme &theme, QWidget *parent = nullptr);

    // La lista es propiedad del Store y vive más que esta vista.
    void setSource(const QList<Birthday *> *list);
    void setTheme(const Theme &theme);
    // El orden de la lista. Lo guarda el Store en sus preferencias, así que la
    // vista lo recibe al construirse y avisa cuando el usuario lo cambia.
    void setByMonth(bool on);
    bool byMonth() const { return m_byMonth; }

    // Vuelve a leer la lista: tarjeta de hoy y filas de los próximos.
    void refresh();

    // Reescribe los textos tras un cambio de idioma. Aquí es refresh() a secas
    // porque la página se rehace entera en cada repintado: no hay ningún texto
    // fijo que sobreviva de una vez a la siguiente.
    void retranslate() { refresh(); }

signals:
    void addRequested(QWidget *anchor);                    // "añadir cumpleaños"
    void editRequested(Birthday *b, QWidget *anchor);      // menú de una fila
    void remindRequested(Birthday *b, QWidget *anchor);    // elegir hora de aviso
    void greetToggled(Birthday *b);                        // felicitado / sin felicitar
    void dismissRequested(Birthday *b);                    // callar un aviso que suena
    void orderChanged(bool byMonth);                       // para que se guarde

private:
    QWidget *buildHighlightCard(Birthday *b);
    QWidget *buildHeader(int listed);
    QWidget *buildMonthHeader(int month);
    QWidget *buildRow(Birthday *b);
    QWidget *buildAddRow();
    // Todos los que valen, ordenados por lo que falta para cada uno. Los
    // primeros —los que empatan en ser los más cercanos— son los destacados.
    QList<Birthday *> sorted() const;
    QList<Birthday *> byCalendar() const;

    Theme m_theme;
    const QList<Birthday *> *m_list = nullptr;
    bool m_byMonth = false;
    QVBoxLayout *m_layout = nullptr;   // contenido + stretch final
};
