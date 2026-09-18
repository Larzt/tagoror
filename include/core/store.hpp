#pragma once

#include <QByteArray>
#include <QColor>
#include <QDateTime>
#include <QList>
#include <QObject>
#include <QPoint>
#include <QSize>
#include <QString>
#include <functional>

#include "core/birthday.hpp"
#include "core/lang.hpp"
#include "core/note.hpp"

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

signals:
    // La lista y las preferencias son otras: quien las muestre tiene que
    // rehacerse entero (lo emiten retryLoad y adoptDataDir).
    void reloaded();

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
    void seedDemoNotes();

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
    Prefs m_prefs;
    QTimer *m_saveTimer = nullptr;
    bool m_available = true;
    // Había un birthdays.json y no se pudo leer: entonces no se escribe encima,
    // por la misma razón que con las notas -- lo que hay en memoria no son los
    // cumpleaños de su dueño, son los que no se pudieron cargar.
    bool m_birthdaysReadable = true;
};
