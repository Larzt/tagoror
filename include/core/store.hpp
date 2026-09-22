#pragma once

#include <QByteArray>
#include <QColor>
#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QList>
#include <QObject>
#include <QPoint>
#include <QSize>
#include <QString>
#include <QStringList>
#include <functional>

#include "core/birthday.hpp"
#include "core/event.hpp"
#include "core/lang.hpp"
#include "core/note.hpp"
#include "core/timer.hpp"

class QJsonObject;
class QTimer;

// Todo lo que sobrevive al cierre de la aplicación: las notas y las
// preferencias que viajan con ellas. Está separado de Panel para que la
// ventana no tenga que saber de JSON, de QSettings ni de migraciones.
class Store : public QObject {
    Q_OBJECT

public:
    // Preferencias guardadas junto a las notas. El acento y la opacidad son
    // datos, no estilo: Panel construye su Theme a partir de ellos.
    struct Prefs {
        QColor accent{"#7c9cff"};
        int opacity = 96;              // 40..100
        QSize windowSize;              // tamaño del panel desplegado
        // Dónde dejó el usuario la ventana. Hace falta la bandera porque
        // (0,0) es una esquina perfectamente válida: un QPoint nulo no puede
        // significar "no hay nada guardado".
        QPoint windowPos;
        bool hasWindowPos = false;
        QByteArray input;              // micrófono elegido en ajustes
        bool onTop = false;            // por defecto vive en el escritorio
        Lang::Code lang = Lang::Es;    // idioma de la interfaz
        // Cómo se ordena la página de cumpleaños: por lo que falta para cada
        // uno (lo de serie) o por meses del año, de enero a diciembre. Vive
        // aquí y no en birthdays.json porque es una preferencia de la interfaz,
        // de la misma familia que el idioma, no un dato de la agenda.
        bool birthdaysByMonth = false;

        // Tamaño del texto de las notas, en tanto por ciento del de serie. Solo
        // escala el contenido (títulos, cuerpos, elementos, eventos); el resto
        // de la interfaz se queda como está, porque crecer las etiquetas de
        // anchura fija es lo que recorta la lista entera (ver *Card widths*).
        int textScale = 100;

        // Cómo se dejó el planificador: vista (0 día, 1 semana, 2 mes) y qué
        // categorías están escondidas. Son de la interfaz, como el orden de
        // los cumpleaños, y viajan con las notas.
        int plannerView = 2;
        QStringList plannerHidden;

        // Buscar si hay versión nueva una vez al día. Es lo único de la
        // aplicación que sale a la red, así que va como un ajuste a la vista y
        // no escondido; 'latestSeen' guarda la última versión que contestó el
        // servidor para poder enseñarla sin volver a preguntar.
        bool updateCheck = true;
        qint64 lastUpdateMs = 0;
        QString latestSeen;

        // Cada cuántos días se aparta una copia, y a qué hora. Cero significa
        // solo a mano. Viajan en notes.json, así que la pauta se muda con las
        // notas: el pendrive lleva sus copias y con qué frecuencia se hacen.
        int backupEveryDays = 1;
        QTime backupAt{3, 0};
        qint64 lastBackupMs = 0;       // cuándo se hizo la última
    };

    explicit Store(QObject *parent = nullptr);
    ~Store() override;

    // --- notas (propietario: se liberan en el destructor) ------------------
    const QList<Note *> &notes() const { return m_notes; }
    int count() const { return int(m_notes.size()); }
    void add(Note *n);                 // la más reciente, arriba
    void remove(Note *n);              // se lleva por delante sus adjuntos
    // Reordena las notas para que queden en el orden dado. Es lo que usa el
    // panel tras arrastrar una tarjeta: pasa el orden que ya tienen en
    // pantalla en vez de calcular índices por su cuenta.
    void setOrder(const QList<Note *> &order);

    // --- temporizadores -----------------------------------------------------
    // Van dentro de notes.json: son pocos, cambian de estado a cada rato y no
    // son una agenda que haya que poder restaurar por separado.
    const QList<Timer *> &timers() const { return m_timers; }
    void addTimer(Timer *t);           // el más reciente, arriba
    void removeTimer(Timer *t);

    // --- planificador ---------------------------------------------------------
    // Como los cumpleaños: su propio fichero (events.json), sus propias copias
    // de seguridad con la misma marca de tiempo, y el mismo candado si el
    // fichero está pero no se deja leer.
    const QList<Event *> &events() const { return m_events; }
    void addEvent(Event *e);
    void removeEvent(Event *e);

    // --- cumpleaños (mismo dueño y mismo fichero que las notas) ------------
    // Viven aquí y no en la lista de notas porque son otra cosa (ver
    // birthday.hpp), pero se guardan en el mismo notes.json: así viajan con
    // las notas al cambiar de carpeta y entran en las copias de seguridad sin
    // tener que duplicar nada de todo eso.
    const QList<Birthday *> &birthdays() const { return m_birthdays; }
    int birthdayCount() const { return int(m_birthdays.size()); }
    void addBirthday(Birthday *b);
    void removeBirthday(Birthday *b);

    Prefs &prefs() { return m_prefs; }
    const Prefs &prefs() const { return m_prefs; }

    // Gancho para que el dueño refresque lo que solo él sabe (el tamaño de la
    // ventana) justo antes de escribir. Debe soltarse antes de destruirlo: el
    // Store guarda una última vez al morir, y para entonces ya no existe.
    std::function<void()> beforeSave;

    // Una copia de seguridad ya hecha, tal como se le ofrece al usuario.
    struct Backup {
        QString path;
        QDateTime when;   // inválido si el nombre no lleva fecha reconocible
        int notes = 0;
    };

    // --- persistencia -------------------------------------------------------
    QString path() const;              // notes.json
    QString birthdaysPath() const;     // birthdays.json, al lado del anterior
    QString eventsPath() const;        // events.json, lo mismo
    void load();                       // siembra dos notas si no hay fichero
    void save();

    // Días de historia que se conservan. Es público porque los ajustes lo
    // dicen y no puede haber dos números que discrepen.
    static constexpr int kBackupsKept = 10;

    // Cuándo toca la siguiente según la pauta, o inválido si es solo a mano.
    // El menú lo enseña: una programación que no dice cuándo va a actuar no
    // se puede comprobar.
    QDateTime nextBackupDue() const;
    // Hace una si ya tocaba. La llaman el arranque y el latido del panel, así
    // que una copia programada a las 3:00 con la app cerrada se hace al abrir.
    bool backupIfDue();
    // Una ahora, pase lo que pase: el botón de "crear copia ahora", y también
    // el paso previo a restaurar.
    bool makeBackup();

    // Las copias que hay ahora mismo, de la más reciente a la más antigua.
    QList<Backup> backups() const;
    // Vuelve a una de ellas. Lo que había pasa antes a ser copia: restaurar
    // por equivocación no puede ser el error del que ya no se vuelve.
    bool restoreBackup(const QString &file);
    void scheduleSave();               // agrupa ráfagas de tecleo (600 ms)

    // Si la carpeta configurada estaba ahí cuando se leyó. En falso el Store
    // entra en modo de solo lectura y no escribe nada en absoluto: ver save().
    bool available() const { return m_available; }

    // Vuelve a intentar la carga si la carpeta ha aparecido (el pendrive que
    // se monta al abrirlo, el disco de red que tarda). Devuelve true la vez
    // que lo consigue; lo que el usuario escribiera mientras tanto se conserva.
    bool retryLoad();

    // Mueve la carpeta de datos copiando los adjuntos; deja los originales en
    // su sitio, así un fallo a mitad no destruye nada. Sobrescribe el
    // notes.json que hubiera en el destino: es "llevarme mis notas allí".
    void changeDataDir(const QString &to);

    // La otra mitad: apuntar a una carpeta que ya tiene notas y quedarse con
    // las suyas, sin escribir en ella hasta haberla leído. Quien elige carpeta
    // tiene que poder decir cuál de las dos cosas quiere.
    void adoptDataDir(const QString &to);

    // --- sincronización con Drive -------------------------------------------
    //
    // Mezcla elemento a elemento lo que viene de otro equipo con lo de aquí:
    // de cada nota, cumpleaños, evento o temporizador se queda la versión más
    // reciente (updatedMs), lo que solo está en un lado se añade, y lo borrado
    // en cualquiera de los dos se borra si el borrado es posterior a la última
    // edición. Nunca se sustituye un fichero entero por otro: eso es lo que
    // haría que un equipo pisara lo que el otro escribió.
    //
    // Un objeto vacío para una de las tres partes quiere decir "de eso no hay
    // nada nuevo" y esa parte no se toca. Guarda al terminar y emite merged()
    // si ha cambiado algo de aquí.
    struct MergeResult {
        bool changed = false;
        // Adjuntos ("audio/x.wav", "images/y.png") de notas cuya versión buena
        // vino de fuera: esos se bajan aunque aquí exista uno con ese nombre.
        QSet<QString> pull;
    };
    MergeResult mergeRemote(const QJsonObject &notesRoot, const QJsonObject &birthdaysRoot,
                            const QJsonObject &eventsRoot);
    // Los adjuntos que usan las notas, como "audio/<nombre>" o "images/<nombre>".
    QStringList attachments() const;
    // Los JSON que se pueden subir. Uno que estaba pero no se dejó leer no: en
    // memoria no está lo que tiene, y subirlo sería repartir el destrozo.
    QStringList syncableFiles() const;
    // Lo que se sube con ese nombre: solo lo compartido (elementos, lápidas y
    // orden), sin las preferencias de este equipo, y siempre escrito igual.
    // Subir el notes.json local haría que dos equipos sin cambios se pisaran
    // sin fin, porque cada uno guarda su tamaño y su posición de ventana.
    QByteArray syncPayload(const QString &name) const;

signals:
    // La lista y las preferencias son otras: quien las muestre tiene que
    // rehacerse entero (lo emiten retryLoad y adoptDataDir).
    void reloaded();
    // mergeRemote() ha traído cambios de otro equipo: la lista es otra, pero
    // los objetos que ya existían siguen siendo los mismos (se actualizan en
    // su sitio), así que los punteros que tengan las vistas siguen valiendo.
    void merged();
    // Acaba de escribirse notes.json (y sus hermanos). La copia en Drive se
    // cuelga de aquí para subir lo que ha cambiado.
    void saved();

private:
    // Resuelve la carpeta antes de tocar disco: la override manda sobre la
    // ruta estándar, y de las marcas anteriores se hereda todo lo que haya.
    void resolveDataDir();
    // Lee el fichero en memoria. Devuelve false si no se pudo abrir, sin
    // tocar nada: el que no se pueda leer es justo el que no hay que pisar.
    bool readFile();
    bool readObject(const QJsonObject &root);
    // Los cumpleaños viven en su propio fichero. No hay un tercer desenlace
    // como en las notas: que no exista es lo normal (una instalación que
    // todavía no tiene ninguno, o una que viene de cuando iban dentro de
    // notes.json), y solo el fichero que está pero no se deja leer manda callar
    // la escritura -- ver m_birthdaysReadable.
    void loadBirthdays();
    void saveBirthdays();
    // Lo mismo para el planificador, con su propio candado.
    void loadEvents();
    void saveEvents();
    void seedDemoNotes();

    // Pone la fecha de cambio a lo que ha cambiado desde la última vez y apunta
    // como borrado lo que ya no está. Corre al principio de cada save(): así
    // ninguna tarjeta tiene que acordarse de marcar nada al editar.
    void stampChanges();
    // Toma lo que hay ahora como punto de partida, sin marcar nada.
    void resetSnapshots();

    // Escribe el fichero entero o no lo toca. QSaveFile escribe a un temporal
    // y renombra encima, que es atómico: sin esto, retirar el pendrive a
    // mitad de la escritura deja un notes.json truncado, y un JSON a medias
    // no se lee — son todas las notas, no las últimas.
    bool writeAtomic(const QString &file, const QByteArray &data);
    // Aparta el fichero que hay tal como está en disco. Quién decide cuándo es
    // la pauta (backupIfDue), no esto: guardar se llama cada 600 ms mientras
    // se teclea, y copiar en cada pulsación dejaría diez copias del último
    // minuto en vez de diez días de historia.
    bool copyToBackup(const QString &name);
    void pruneBackups();

    QList<Note *> m_notes;
    QList<Birthday *> m_birthdays;
    QList<Timer *> m_timers;
    QList<Event *> m_events;
    Prefs m_prefs;
    QTimer *m_saveTimer = nullptr;
    bool m_available = true;
    // Había un birthdays.json y no se pudo leer: entonces no se escribe encima,
    // por la misma razón que con las notas -- lo que hay en memoria no son los
    // cumpleaños de su dueño, son los que no se pudieron cargar.
    bool m_birthdaysReadable = true;
    bool m_eventsReadable = true;

    // Cómo era cada elemento la última vez que se miró (su JSON sin la fecha
    // de cambio), para saber qué ha cambiado sin que nadie lo avise.
    QHash<QString, QByteArray> m_snap;
    QStringList m_orderSnap;
    qint64 m_orderUpdatedMs = 0;   // el orden de las notas también se sincroniza
    // Lápidas: "<tipo>:<id>" de lo borrado -> cuándo. Sin ellas, lo borrado aquí volvería
    // desde el otro equipo, que todavía lo tiene. Viven en notes.json.
    QHash<QString, qint64> m_deleted;
};
