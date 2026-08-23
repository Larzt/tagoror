#pragma once

#include <QDate>
#include <QWidget>

#include "core/note.hpp"
#include "ui/theme.hpp"

class QLabel;
class QScrollArea;
class QToolButton;
class QVBoxLayout;
class MonthGrid;

// Vista de mes: dibuja los recordatorios con fecha real sobre una rejilla y
// lista los del día elegido debajo, con una fila para crear uno nuevo.
//
// Solo entran los recordatorios con instante (Note::isScheduled): los de fecha
// libre son una etiqueta y no se pueden colocar en ningún día.
class CalendarView : public QWidget {
    Q_OBJECT

public:
    explicit CalendarView(const Theme &theme, QWidget *parent = nullptr);

    // La lista es propiedad del Store y vive más que esta vista; se guarda el
    // puntero para no copiar las notas en cada repintado.
    void setSource(const QList<Note *> *notes);
    void setTheme(const Theme &theme);

    // Vuelve a leer las notas: puntos del mes y lista del día seleccionado.
    void refresh();

    // Reescribe los textos fijos tras un cambio de idioma.
    void retranslate();

    // Lleva la vista al día indicado (lo selecciona y salta a su mes).
    void goTo(const QDate &date);

    QDate selectedDate() const { return m_selected; }

protected:
    // La lista del día se pliega sola cuando el panel se queda sin alto: con
    // la rejilla y la lista peleándose por 200 px no se veía bien ninguna.
    void resizeEvent(QResizeEvent *e) override;

signals:
    void createRequested(const QDate &day, QWidget *anchor);   // "nuevo recordatorio"
    void noteActivated(Note *n);                               // abrir la nota en la lista
    void dismissRequested(Note *n);                            // callar un aviso que suena

private:
    void buildHeader(QVBoxLayout *col);
    void buildDayHeader(QVBoxLayout *col);
    void refreshMonthLabel();
    void refreshDayList();
    void toggleDayList();      // lo pulsa el usuario: manda sobre el plegado automático
    void applyDayCollapsed();
    void showMonth(const QDate &anyDayOfMonth);
    QList<Note *> notesOn(const QDate &day) const;

    Theme m_theme;
    const QList<Note *> *m_notes = nullptr;

    QDate m_month;        // día 1 del mes visible
    QDate m_selected;

    MonthGrid *m_grid = nullptr;
    QLabel *m_monthLabel = nullptr;
    QToolButton *m_prevBtn = nullptr;
    QToolButton *m_nextBtn = nullptr;
    QToolButton *m_todayBtn = nullptr;
    QWidget *m_dayHeader = nullptr;
    QToolButton *m_dayToggle = nullptr;
    bool m_dayCollapsed = false;
    // Plegada por falta de sitio, no por decisión de nadie: al recuperar alto
    // se vuelve a abrir sola. Si el usuario toca el plegado, deja de estarlo y
    // manda su elección.
    bool m_autoCollapsed = false;
    bool m_tight = false;
    QLabel *m_dayLabel = nullptr;
    QLabel *m_dayCount = nullptr;
    QScrollArea *m_dayScroll = nullptr;
    QVBoxLayout *m_dayLayout = nullptr;
};
