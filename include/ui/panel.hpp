#pragma once

#include <QDate>
#include <QList>
#include <QSize>
#include <QWidget>

#include "core/store.hpp"
#include "ui/theme.hpp"

class QFrame;
class QLabel;
class QLineEdit;
class QScrollArea;
class QStackedWidget;
class QTimer;
class QToolButton;
class QVBoxLayout;
class Alarm;
class CalendarView;
class NoteCard;

// Ventana única del widget: dibuja su propio chrome (sin decoración del WM) y
// presenta lo que guarda el Store, que es quien sabe de disco. El
// QStackedWidget exterior alterna entre el panel expandido y el icono plegado;
// dentro, otro alterna entre la lista de notas y el calendario.
class Panel : public QWidget {
    Q_OBJECT

public:
    Panel();
    ~Panel() override;

    // La llama la segunda instancia a través del socket: en vez de abrir otro
    // panel, se despliega y se trae al frente el que ya estaba.
    void bringToFront();

private:
    // --- construcción de la interfaz ---
    void buildShell();
    QFrame *buildHeader();
    QWidget *buildBody();
    QFrame *buildFooter();
    QWidget *buildBadge();

    // --- estado visual ---
    void applyTheme();
    void rebuildList();
    void refreshFooter();
    QList<NoteCard *> cards() const;

    // --- notas ---
    void addNote(Note::Type type);
    void removeNote(Note *n);

    // --- calendario ---
    void toggleCalendar();
    void setCalendarActive(bool on);
    void showNotes();
    void askReminderTime(const QDate &day, QWidget *anchor);
    void createReminder(const QDateTime &when);
    void revealNote(Note *n);       // del calendario a su tarjeta en la lista
    void refreshCalendar();

    // --- recordatorios ---
    void checkReminders();          // ¿alguno ha vencido? → suena y avisa
    void dismissNote(Note *n);      // el usuario para el aviso
    void refreshDueCards();         // repinta el estado sin rehacer la lista
    void applyBadgeAlert();         // el dock avisa aunque esté plegado
    bool anyRinging() const;

    // --- selectores ---
    void openNewNoteMenu(QWidget *anchor);
    void openSettings(QWidget *anchor);
    void openAccentEditor(QWidget *anchor);
    void chooseDataFolder();

    // --- ventana ---
    void applyWindowFlags();        // encima de todo o pegado al escritorio
    void keepOnScreen();            // que plegar/desplegar no la saque de la pantalla
    // Esquina por la que crece o encoge la ventana: el panel se abre hacia el
    // centro de la pantalla, no siempre hacia abajo y a la derecha.
    QPoint anchoredTopLeft(const QRect &before, const QSize &after) const;

    // --- búsqueda y plegado ---
    void toggleSearch();
    void applyFilter(const QString &q);
    void collapse();
    void expand();
    void showPage(QWidget *page);   // ajusta el tamaño a la página visible

    // --- persistencia ---
    // El tamaño de la ventana solo lo sabe el panel, así que se vuelca en las
    // preferencias justo antes de cada guardado.
    void syncPrefs();
    void scheduleSave();
    void save();

    Store m_store;

    QStackedWidget *m_stack = nullptr;   // panel expandido / icono plegado
    QFrame *m_shell = nullptr;
    QWidget *m_badge = nullptr;
    QLabel *m_badgeCount = nullptr;

    QStackedWidget *m_body = nullptr;    // lista de notas / calendario
    QScrollArea *m_scroll = nullptr;
    QWidget *m_listHost = nullptr;
    QVBoxLayout *m_listLayout = nullptr;   // tarjetas + stretch final
    CalendarView *m_calendar = nullptr;

    QWidget *m_empty = nullptr;            // cartel de "no hay notas"
    QWidget *m_searchBar = nullptr;
    QLineEdit *m_search = nullptr;
    QLabel *m_footerText = nullptr;
    QToolButton *m_calendarBtn = nullptr;
    QList<QToolButton *> m_headerButtons;

    QTimer *m_dueTimer = nullptr;          // vigilancia de recordatorios
    Alarm *m_alarm = nullptr;
    Theme m_theme;
    QSize m_expandedSize;                  // se restaura al desplegar (y se guarda)
    QRect m_expandedGeom;                  // sitio exacto del panel antes de plegar
    QRect m_dockGeom;                      // dónde quedó el dock al plegar
};
