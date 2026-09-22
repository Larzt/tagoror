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
class DriveSync;
class Updater;
class BirthdayView;
class NoteCard;
class PlannerView;
class SettingsView;
class TimerView;

// Ventana única del widget: dibuja su propio chrome (sin decoración del WM) y
// presenta lo que guarda el Store, que es quien sabe de disco. El
// QStackedWidget exterior alterna entre el panel expandido y el icono plegado;
// dentro, otro alterna entre la lista de notas, el planificador, los
// temporizadores, los cumpleaños y los ajustes.
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
    // Escape vuelve a las notas desde cualquier otra página, que es lo que
    // anuncia el pie mientras se está en una.
    void keyPressEvent(QKeyEvent *e) override;

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
    void refreshFooterHint();
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

    // --- páginas del cuerpo ---
    // Cada página con su botón de la cabecera y su nombre: abrir, cerrar,
    // encender el botón y poner el título salen de esta lista y no de una
    // función por página.
    struct Page {
        QWidget *page;
        QToolButton *button;
        const char *name;    // en español; se traduce al usarlo
    };
    QList<Page> pages() const;
    void togglePage(QWidget *page);
    // Enciende o apaga el botón de una página. El icono no cambia -- dice
    // adónde lleva --, lo que cambia es el realce, que dice dónde estás.
    void setPageActive(QToolButton *button, bool on,
                       const QString &tipOn, const QString &tipOff);
    void refreshPageButtons();
    // El rótulo de la cabecera dice en qué página estás, como en el diseño.
    void refreshTitle();
    void showNotes();
    void createReminder(const QString &title, const QDateTime &when);
    void revealNote(Note *n);       // del planificador a su tarjeta en la lista
    void refreshPlanner();

    // El planificador necesita ancho: la ventana se ensancha al abrirlo y
    // vuelve a lo que medía al salir, si el usuario no la ha tocado entre
    // medias (la misma regla que m_grownFrom con el alto).
    void enterWide();
    void leaveWide();

    // --- temporizadores ---
    void createTimer(const QString &name, qint64 ms);
    void toggleTimer(Timer *t);
    void resetTimer(Timer *t);
    void removeTimer(Timer *t);
    void onTimersChanged();         // repinta todo lo que enseña temporizadores
    void tickTimers();              // cada medio segundo mientras algo cuenta
    void refreshFooterTimer();
    // La tira roja de "tiempo cumplido" / "empieza ya": los temporizadores y
    // los eventos no tienen tarjeta en la lista con un botón de parar, así que
    // su aviso vive aquí. Detener calla todo lo que enseña.
    void refreshAlarmBar();
    void stopBarAlarms();
    void silenceEvent(Event *e);

    // --- cumpleaños ---
    void refreshBirthdays();
    // Alta y edición comparten popup: con b nulo es uno nuevo. Así el formato
    // de la fecha y lo que se pide se escriben una sola vez.
    void openBirthdayEditor(Birthday *b, QWidget *anchor);
    void askBirthdayReminder(Birthday *b, QWidget *anchor);
    void removeBirthday(Birthday *b);
    void toggleGreeted(Birthday *b);
    void dismissBirthday(Birthday *b);
    // Calla el aviso de un cumpleaños: queda apuntado el año para que no
    // vuelva a sonar hasta el que viene.
    void silenceBirthday(Birthday *b);

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
    void refreshSettings();
    void openAccentEditor(QWidget *anchor);
    void chooseDataFolder();
    // Apuntar a una carpeta que ya tiene notas es ambiguo: puede querer decir
    // "llévame las mías allí" o "abre las que hay". Se pregunta en vez de
    // suponer, porque suponer lo primero borra las del destino.
    void confirmDataFolder(const QString &to);
    void openBackups(QWidget *anchor);            // el menú propio de las copias
    static QString backupPeriodLabel(int days);   // "Cada día", "Cada 3 días"…
    void pollBackup();                            // ¿toca copia programada?

    // --- actualizaciones ---
    void pollUpdates();               // ¿toca mirar? una vez al día
    void checkUpdatesNow();           // el botón de ajustes
    void onUpdateChecked(const QString &version, const QString &url, const QString &error);
    void openLatestRelease();
    // Enseña u oculta la tira de "hay una versión nueva".
    void refreshUpdateBanner();
    bool updateAvailable() const;
    void confirmRestore(const QString &file);

    // --- carpeta de datos ---
    // Vuelve a mirar si la carpeta configurada ha aparecido: un pendrive se
    // monta cuando su dueño lo abre, mucho después del arranque de sesión.
    void pollDataDir();
    // La lista y las preferencias son otras tras recuperar la carpeta.
    void onStoreReloaded();
    // Han llegado cambios de otro equipo (Drive). Los objetos que ya existían
    // son los mismos, actualizados en su sitio; lo que cambia es la lista.
    void onStoreMerged();
    // ¿Se puede rehacer la lista ahora? No mientras el usuario escribe en un
    // campo del panel: se quedaría sin cursor a media frase.
    bool userIdle() const;
    // Enseña u oculta el cartel de "no se está guardando".
    void refreshDataWarning();

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
    // Lo mismo para las páginas de dentro (lista, calendario, cumpleaños y
    // ajustes): la que no se ve no puede imponer su mínimo a la que sí.
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
    // Cartel rojo bajo la cabecera: la carpeta de notas no está y nada de lo
    // que se escriba se va a guardar. Sin él, el panel abría vacío sin decir
    // por qué, que es como se pierden las notas sin enterarse.
    QLabel *m_dataWarn = nullptr;
    // Tira bajo la cabecera cuando hay versión nueva. Va debajo del aviso de
    // carpeta ausente: ese dice que ahora mismo no se guarda nada, y manda.
    QWidget *m_updateBar = nullptr;
    QLabel *m_updateText = nullptr;

    // Aviso de temporizador o evento sonando (ver refreshAlarmBar).
    QWidget *m_alarmBar = nullptr;
    QLabel *m_alarmText = nullptr;
    QToolButton *m_alarmPlus = nullptr;

    QStackedWidget *m_body = nullptr;    // lista de notas / páginas
    QScrollArea *m_scroll = nullptr;
    QWidget *m_listHost = nullptr;
    QVBoxLayout *m_listLayout = nullptr;   // tarjetas + stretch final
    PlannerView *m_planner = nullptr;
    TimerView *m_timerView = nullptr;
    BirthdayView *m_birthdays = nullptr;
    SettingsView *m_settings = nullptr;

    QLabel *m_titleLabel = nullptr;
    QWidget *m_empty = nullptr;            // cartel de "no hay notas"
    QLabel *m_emptyText = nullptr;
    QPushButton *m_emptyBtn = nullptr;
    QLabel *m_footerHint = nullptr;
    QWidget *m_searchBar = nullptr;
    QLineEdit *m_search = nullptr;
    QLabel *m_footerText = nullptr;
    QToolButton *m_calendarBtn = nullptr;
    QToolButton *m_timersBtn = nullptr;
    QToolButton *m_footerTimer = nullptr;   // cuenta atrás en el pie
    QToolButton *m_birthdayBtn = nullptr;
    QToolButton *m_settingsBtn = nullptr;
    QList<QToolButton *> m_headerButtons;

    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_trayMenu = nullptr;

    NoteCard *m_dragCard = nullptr;        // tarjeta que se está arrastrando
    QTimer *m_dueTimer = nullptr;          // vigilancia de recordatorios
    QTimer *m_tick = nullptr;              // temporizadores en marcha
    Alarm *m_alarm = nullptr;
    Updater *m_updater = nullptr;
    // Copia en Google Drive. Sube un rato después de cada guardado, para que
    // una ráfaga de tecleo sea una subida y no cincuenta.
    DriveSync *m_drive = nullptr;
    QTimer *m_driveTimer = nullptr;
    // Cada cuánto se mira si otro equipo ha cambiado algo. Mirar es listar una
    // carpeta; bajar solo se baja lo que ha cambiado.
    QTimer *m_drivePoll = nullptr;
    QString m_latestUrl;              // la página de la última publicada
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
    // Lo mismo con el ancho del planificador (ver enterWide).
    QRect m_narrowGeom;
    QRect m_wideGeom;
};
