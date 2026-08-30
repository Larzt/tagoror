#pragma once

#include <QDate>
#include <QList>
#include <QRect>
#include <QSize>
#include <QWidget>

#include "core/store.hpp"
#include "ui/theme.hpp"

class QFrame;
class QLabel;
class QLineEdit;
class QMenu;
class QPushButton;
class QScreen;
class QScrollArea;
class QStackedWidget;
class QSystemTrayIcon;
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

protected:
    // Cerrar esconde en la bandeja en vez de terminar: es lo que se espera de
    // algo que vive ahí. Sin bandeja disponible sí se sale, o no habría manera
    // de recuperar la ventana.
    void closeEvent(QCloseEvent *e) override;
    // Cada vez que se mapea la ventana hay que volver a pedir que la barra de
    // tareas la ignore: cambiar de flags destruye la ventana nativa y con ella
    // la propiedad.
    void showEvent(QShowEvent *e) override;
    // El sitio de la ventana es del usuario: se apunta en cuanto cambia, sin
    // esperar al destructor. Al apagar el equipo la sesión mata el proceso y
    // ese último guardado no llega nunca.
    void moveEvent(QMoveEvent *e) override;

private:
    // --- construcción de la interfaz ---
    void buildShell();
    QFrame *buildHeader();
    QWidget *buildBody();
    QFrame *buildFooter();
    QWidget *buildBadge();

    // --- estado visual ---
    void applyTheme();
    // Cambia el idioma de la interfaz y la vuelve a escribir entera.
    void setLanguage(Lang::Code code);
    void retranslate();
    void rebuildList();
    void refreshFooter();
    QList<NoteCard *> cards() const;

    // --- notas ---
    void addNote(Note::Type type);
    void removeNote(Note *n);

    // --- reordenar ---
    // El orden lo lleva la pantalla y el Store lo copia: arrastrar mueve la
    // tarjeta dentro del layout y de ahí sale la lista que se guarda, así no
    // hay dos ideas del orden que puedan discrepar.
    void moveNote(Note *n, int steps);        // un paso, desde el menú
    void beginCardDrag(NoteCard *card);
    void dragCardTo(NoteCard *card, const QPoint &globalPos);
    void endCardDrag(NoteCard *card);
    void commitOrder();

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
    void rescheduleNote(Note *n);   // le cambió la fecha mientras sonaba
    // Calla un aviso: los que se repiten no quedan como avisados, saltan a su
    // siguiente vuelta. Lo comparten el botón de parar y abrir el panel.
    void silence(Note *n);
    void refreshDueCards();         // repinta el estado sin rehacer la lista
    void applyBadgeAlert();         // el dock avisa aunque esté plegado
    bool anyRinging() const;

    // --- selectores ---
    void openNewNoteMenu(QWidget *anchor);
    void openSettings(QWidget *anchor);
    void openAccentEditor(QWidget *anchor);
    void chooseDataFolder();

    // --- bandeja del sistema ---
    // El widget vive ahí en vez de en la barra de tareas: el icono es lo que
    // queda cuando la ventana se esconde, igual que en Discord o Telegram.
    void buildTray();
    void buildTrayMenu();           // se rehace entero al cambiar de idioma
    void toggleFromTray();

    // --- ventana ---
    void applyWindowFlags();        // encima de todo o pegado al escritorio
    void keepOnScreen();            // que plegar/desplegar no la saque de la pantalla
    // Devuelve la ventana al sitio guardado en el arranque. Sin esto reaparece
    // donde la ponga el gestor, que no es donde la dejó su dueño.
    void restoreWindowPos();
    // Dónde deja el gestor de ventanas poner la ventana, que no es toda la
    // pantalla: ver placementArea() en el .cpp. Sin pantalla se toma la de la
    // propia ventana; se pasa una cuando se coloca en un monitor que todavía
    // no es el suyo.
    QRect placementArea(const QScreen *sc = nullptr) const;
    // Esquina por la que crece o encoge la ventana: el panel se abre hacia el
    // centro de la pantalla, no siempre hacia abajo y a la derecha.
    QPoint anchoredTopLeft(const QRect &before, const QSize &after) const;

    // --- búsqueda y plegado ---
    void toggleSearch();
    void applyFilter(const QString &q);
    void collapse();
    void expand();
    void showPage(QWidget *page);   // ajusta el tamaño a la página visible
    // Lo mismo para las dos páginas de dentro (lista y calendario): la que no
    // se ve no puede imponer su mínimo a la que sí.
    void showBodyPage(QWidget *page);
    // El alto mínimo que pide el layout de la página visible, y aplicarlo.
    // No es una constante: el calendario necesita más que la lista, y con la
    // lista del día abierta necesita más todavía.
    int shellMinimumHeight() const;
    void syncShellMinimum();

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

    QLabel *m_titleLabel = nullptr;
    QWidget *m_empty = nullptr;            // cartel de "no hay notas"
    QLabel *m_emptyText = nullptr;
    QPushButton *m_emptyBtn = nullptr;
    QLabel *m_footerHint = nullptr;
    QWidget *m_searchBar = nullptr;
    QLineEdit *m_search = nullptr;
    QLabel *m_footerText = nullptr;
    QToolButton *m_calendarBtn = nullptr;
    QList<QToolButton *> m_headerButtons;

    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_trayMenu = nullptr;

    NoteCard *m_dragCard = nullptr;        // tarjeta que se está arrastrando
    QTimer *m_dueTimer = nullptr;          // vigilancia de recordatorios
    Alarm *m_alarm = nullptr;
    Theme m_theme;
    QSize m_expandedSize;                  // se restaura al desplegar (y se guarda)
    QPoint m_dockOffset;                   // por qué punto del panel entra y sale el dock
    bool m_posRestored = false;            // la posición guardada solo se repone al mapear
    // Lo que la ventana medía antes de estirarse para que cupiera la lista del
    // día, y cómo se quedó al estirarla. Plegar la lista devuelve la primera,
    // pero solo si la segunda sigue siendo la geometría actual: si el usuario
    // ha tocado la ventana desde entonces, el tamaño es suyo y no se toca.
    QRect m_grownFrom;
    QRect m_grownTo;
};
