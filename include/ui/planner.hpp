#pragma once

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QSet>
#include <QWidget>

#include "core/birthday.hpp"
#include "core/event.hpp"
#include "core/note.hpp"
#include "ui/theme.hpp"

class QCheckBox;
class QLabel;
class QLineEdit;
class QScrollArea;
class QStackedWidget;
class QTextEdit;
class QToolButton;
class QHBoxLayout;
class QVBoxLayout;

class TimeGrid;
class MonthBoard;
class MiniMonth;

// El planificador: la página del botón del calendario, que sustituye al
// calendario de antes. Tiene tres vistas -- día, semana y mes --, un lateral
// con el mes en pequeño, las tareas de hoy y el filtro de categorías, y un
// formulario para crear y editar dentro de la propia página.
//
// Enseña tres cosas distintas sobre la misma rejilla: los eventos y tareas
// (events.json, lo suyo), los recordatorios con instante (notas, que se
// quedan en la lista y aquí solo se pintan) y los cumpleaños (su propia
// página, aquí como aviso de día entero). Como las otras páginas, no guarda:
// pide a Panel cada cambio.
//
// Es ancha a propósito. En el panel estrecho de siempre (unos 300 px) las
// siete columnas de una semana no caben con nada legible dentro, así que al
// abrirla la ventana se ensancha (Panel::enterWide) y vuelve a su tamaño al
// salir. Si aun así se queda estrecha, el lateral se esconde y la barra de
// herramientas pasa a dos filas.
class PlannerView : public QWidget {
    Q_OBJECT

public:
    enum View { Day, Week, Month };

    // Ancho con el que se ve entera: lateral, más una semana legible.
    static constexpr int kPreferredWidth = 780;

    explicit PlannerView(const Theme &theme, QWidget *parent = nullptr);

    // Todo es de otros (Store): se guardan punteros a las listas y se leen en
    // cada repintado, igual que el calendario de antes.
    void setSources(const QList<Event *> *events, const QList<Note *> *notes,
                    const QList<Birthday *> *birthdays);
    void setTheme(const Theme &theme);
    void setView(View v);
    View view() const { return m_view; }
    void setHidden(const QStringList &categories);
    QStringList hidden() const { return QStringList(m_hidden.begin(), m_hidden.end()); }

    void refresh();
    void retranslate();
    void goTo(const QDate &day);
    // Cierra el formulario si estaba abierto. Devuelve si lo estaba: Escape lo
    // cierra primero, y solo la segunda pulsación sale de la página.
    bool closeEditor();

    // Categorías que no son de events.json pero se filtran igual.
    static constexpr auto kReminders = "reminders";
    static constexpr auto kBirthdays = "birthdays";

    // Lo que se pinta: una entrada de cualquiera de las tres fuentes, ya
    // colocada en un día concreto.
    struct Item {
        enum Source { FromEvent, FromReminder, FromBirthday };
        Source source = FromEvent;
        Event *event = nullptr;
        Note *note = nullptr;
        Birthday *birthday = nullptr;
        QDate day;
        qreal start = 0;      // horas decimales
        qreal end = 0;
        QString title;
        QColor color;
        bool task = false;
        bool done = false;
        bool alert = false;   // sonando, o un recordatorio suelto ya pasado
        bool allDay = false;
    };
    QList<Item> itemsOn(const QDate &day) const;

signals:
    void eventCreated(Event *e);           // la propiedad pasa a quien lo reciba
    void eventChanged(Event *e);
    void eventDeleted(Event *e);
    void reminderCreated(const QString &title, const QDateTime &when);
    void noteActivated(Note *n);
    void birthdayActivated(Birthday *b);
    void viewChanged(int view);
    void hiddenChanged(const QStringList &categories);

protected:
    void resizeEvent(QResizeEvent *e) override;

private:
    void build();
    void buildSide();
    void buildToolbar(QVBoxLayout *col);
    void buildEditor();
    void applyWidth();
    void refreshToolbar();
    void refreshSide();
    void shift(int direction);
    void activate(const Item &item);
    void toggleDone(const Item &item);
    // Formulario: con e nulo es uno nuevo, que empieza a esa hora de ese día.
    void openEditor(Event *e, const QDate &day, qreal hour);
    void saveEditor();
    void setEditorKind(int kind);
    void refreshEditorChoices();

    Theme m_theme;
    const QList<Event *> *m_events = nullptr;
    const QList<Note *> *m_notes = nullptr;
    const QList<Birthday *> *m_birthdays = nullptr;
    View m_view = Month;
    QDate m_cursor = QDate::currentDate();
    QSet<QString> m_hidden;
    bool m_wide = true;

    // --- lateral ---
    QWidget *m_side = nullptr;
    MiniMonth *m_mini = nullptr;
    QVBoxLayout *m_todayList = nullptr;
    QVBoxLayout *m_catList = nullptr;

    // --- barra ---
    QHBoxLayout *m_bar1 = nullptr;
    QHBoxLayout *m_bar2 = nullptr;
    QWidget *m_bar2Host = nullptr;
    QWidget *m_views = nullptr;          // Día / Semana / Mes
    QList<QToolButton *> m_viewButtons;
    QLabel *m_range = nullptr;
    QToolButton *m_todayBtn = nullptr;
    QToolButton *m_newBtn = nullptr;

    // --- vistas ---
    QStackedWidget *m_stack = nullptr;
    QWidget *m_gridPage = nullptr;
    QWidget *m_weekHead = nullptr;
    QScrollArea *m_gridScroll = nullptr;
    TimeGrid *m_grid = nullptr;
    MonthBoard *m_month = nullptr;
    bool m_scrolledOnce = false;

    // --- formulario ---
    QWidget *m_editor = nullptr;
    QLabel *m_editorTitle = nullptr;
    QLineEdit *m_fTitle = nullptr;
    QLineEdit *m_fDate = nullptr;
    QLineEdit *m_fStart = nullptr;
    QLineEdit *m_fEnd = nullptr;
    QWidget *m_fEndBox = nullptr;
    QTextEdit *m_fDesc = nullptr;
    QCheckBox *m_fRemind = nullptr;
    QLabel *m_fError = nullptr;
    QToolButton *m_fDelete = nullptr;
    QList<QToolButton *> m_kindButtons;
    QList<QToolButton *> m_repeatButtons;
    QList<QToolButton *> m_catButtons;
    QWidget *m_repeatBox = nullptr;
    QWidget *m_catBox = nullptr;
    Event *m_editing = nullptr;   // nulo = uno nuevo
    int m_fKind = 0;              // 0 evento, 1 tarea, 2 recordatorio
    int m_fRepeat = 0;
    QString m_fCategory = "work";
};
