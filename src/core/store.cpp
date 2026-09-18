#include "core/store.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

namespace {

constexpr int kSaveDelayMs = 600;

// Diez días (Store::kBackupsKept) es lo que hace falta para que la copia buena
// siga estando cuando alguien tarda una semana en darse cuenta de que le
// faltan notas; son ficheros de unos pocos KB.
constexpr auto kBackupPrefix = "notes-";
// Los cumpleaños se copian a la vez y con la misma marca de tiempo, para que
// una copia sea siempre las dos mitades del mismo instante y restaurar una
// pueda arrastrar la otra. Sin esto, separar el fichero habría dejado la
// agenda entera fuera de la historia.
constexpr auto kBirthdayBackupPrefix = "birthdays-";

// Nombres con los que se guardaron los datos en marcas anteriores, del más
// reciente al más antiguo. Renombrar la aplicación nunca debe dejar a nadie sin
// sus notas, así que se hereda tanto la carpeta como los ajustes. Cada cambio
// de nombre AÑADE a esta lista por delante; no sustituye lo que ya había, o el
// que se saltara una versión se quedaría con sus notas huérfanas.
const QStringList &legacyAppNames() {
    static const QStringList names{"Codex", "Abyss", "NotasWidget"};
    return names;
}

// Si los datos siguen bajo un nombre antiguo y todavía no hay nada en el
// nuevo, se mudan enteros al arrancar.
void migrateLegacyDataDir() {
    const QString current = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (QFile::exists(current + "/notes.json")) return;

    const QString parent = QFileInfo(current).path();
    for (const QString &name : legacyAppNames()) {
        const QString legacy = parent + "/" + name;
        if (!QFile::exists(legacy + "/notes.json")) continue;

        // rename() falla si el destino existe, y appDataDir() pudo crearlo vacío.
        if (QDir(current).exists() && QDir(current).isEmpty()) QDir().rmdir(current);
        QDir().rename(legacy, current);
        return;
    }
}

// Quita la barra final. La ruta guardada se compara con la que devuelve el
// selector de carpetas, que nunca la lleva, y "…/notas/" != "…/notas" hacía
// que reelegir la misma carpeta se tomara por una mudanza.
QString trimmedDir(const QString &in) {
    QString out = in;
    while (out.size() > 1 && out.endsWith('/')) out.chop(1);
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------

Store::Store(QObject *parent) : QObject(parent) {
    resolveDataDir();

    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(kSaveDelayMs);
    connect(m_saveTimer, &QTimer::timeout, this, &Store::save);
}

Store::~Store() {
    save();
    qDeleteAll(m_notes);
    qDeleteAll(m_birthdays);
}

void Store::resolveDataDir() {
    QSettings settings;
    QString dir = settings.value("dataDir").toString();

    if (dir.isEmpty()) {
        // La carpeta elegida a mano también se hereda de los nombres viejos.
        for (const QString &name : legacyAppNames()) {
            dir = QSettings("Stride", name).value("dataDir").toString();
            if (dir.isEmpty()) continue;
            settings.setValue("dataDir", dir);
            break;
        }
    }

    dataDirOverride() = trimmedDir(dir);
    if (dataDirOverride().isEmpty()) migrateLegacyDataDir();
}

// --- notas -----------------------------------------------------------------

void Store::add(Note *n) {
    m_notes.prepend(n);
    scheduleSave();
}

void Store::remove(Note *n) {
    // Los adjuntos mueren con la nota; si no, quedan ficheros huérfanos en la
    // carpeta de datos que ya nada sabe borrar.
    if (!n->audio.isEmpty()) QFile::remove(n->audioPath());
    for (const QString &image : n->images) QFile::remove(Note::imagePath(image));

    m_notes.removeOne(n);
    delete n;
    save();
}

// El orden llega hecho desde la pantalla, así que solo se comprueba que sean
// exactamente las mismas notas: cualquier discrepancia (una lista a medias,
// una nota que ya no existe) deja el orden anterior en vez de perder ninguna.
void Store::setOrder(const QList<Note *> &order) {
    if (order.size() != m_notes.size()) return;
    for (Note *n : order)
        if (!m_notes.contains(n)) return;

    m_notes = order;
    // Diferido a propósito: arrastrando una tarjeta esto se llama una vez por
    // cada tarjeta que cruza, y no hay que escribir el fichero en cada cruce.
    scheduleSave();
}

// --- cumpleaños --------------------------------------------------------------

// El orden en que se guardan da igual: la página los ordena por lo que falta
// para cada uno, que es lo único que se quiere mirar de una lista así.
void Store::addBirthday(Birthday *b) {
    m_birthdays.append(b);
    // Diferido como el de las notas: quien da uno de alta lo rellena justo
    // después, y ese guardado ya escribe los dos cambios de una vez.
    scheduleSave();
}

void Store::removeBirthday(Birthday *b) {
    m_birthdays.removeOne(b);
    delete b;
    save();
}

// --- persistencia ----------------------------------------------------------

QString Store::path() const { return appDataDir() + "/notes.json"; }

// Los cumpleaños son una agenda, no notas: viven en su propio fichero, al lado
// del de las notas y dentro de la misma carpeta de datos, así que siguen
// mudándose con ellas y entrando en las copias de seguridad.
QString Store::birthdaysPath() const { return appDataDir() + "/birthdays.json"; }

void Store::scheduleSave() { m_saveTimer->start(); }

void Store::save() {
    // Nunca se escribe un fichero que no se ha conseguido leer. Con la carpeta
    // ausente lo que hay en memoria no son las notas del usuario —son las que
    // no se pudieron cargar—, y volcarlas encima las borra en cuanto el
    // volumen vuelve a aparecer. Ver load().
    if (!m_available) return;

    if (beforeSave) beforeSave();

    // Antes de armar el JSON: si toca copia, se hace del fichero que todavía
    // está en disco —lo de antes de esta escritura— y la fecha que apunta
    // entra en este mismo guardado, sin tener que volver a escribir.
    backupIfDue();

    QJsonArray arr;
    for (Note *n : m_notes) arr.append(n->toJson());

    QJsonObject root;
    root["notes"] = arr;
    root["accent"] = m_prefs.accent.name();
    root["opacity"] = m_prefs.opacity;
    root["w"] = m_prefs.windowSize.width();
    root["h"] = m_prefs.windowSize.height();
    if (m_prefs.hasWindowPos) {
        root["x"] = m_prefs.windowPos.x();
        root["y"] = m_prefs.windowPos.y();
    }
    root["input"] = QString::fromLatin1(m_prefs.input);
    root["onTop"] = m_prefs.onTop;
    root["lang"] = Lang::toString(m_prefs.lang);
    root["birthdaysByMonth"] = m_prefs.birthdaysByMonth;
    root["backupEvery"] = m_prefs.backupEveryDays;
    root["backupAt"] = m_prefs.backupAt.toString("HH:mm");
    root["lastBackup"] = m_prefs.lastBackupMs;

    writeAtomic(path(), QJsonDocument(root).toJson(QJsonDocument::Indented));
    saveBirthdays();
}

// Aparte de save() para poder no escribirlo: un birthdays.json ilegible no se
// pisa, igual que no se pisa un notes.json que no se pudo leer.
void Store::saveBirthdays() {
    if (!m_available || !m_birthdaysReadable) return;

    QJsonArray arr;
    for (Birthday *b : m_birthdays) arr.append(b->toJson());

    QJsonObject root;
    root["birthdays"] = arr;
    writeAtomic(birthdaysPath(), QJsonDocument(root).toJson(QJsonDocument::Indented));
}

void Store::loadBirthdays() {
    m_birthdaysReadable = true;
    if (!m_available) return;

    QFile f(birthdaysPath());
    if (!f.exists()) {
        // No hay fichero. O no hay ningún cumpleaños todavía, o vienen de
        // cuando se guardaban dentro de notes.json: en ese caso readObject ya
        // los ha dejado en memoria y basta con pedir que se escriban donde
        // toca ahora. Lo de notes.json se queda ahí hasta el siguiente
        // guardado, que ya no vuelve a poner la clave.
        if (!m_birthdays.isEmpty()) scheduleSave();
        return;
    }

    if (!f.open(QIODevice::ReadOnly)) {
        m_birthdaysReadable = false;
        return;
    }
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    if (!root.contains("birthdays")) {
        // El fichero está pero no es lo que dice ser (truncado a medias, otra
        // cosa copiada ahí): no se toca, que es lo contrario de vaciarlo.
        m_birthdaysReadable = false;
        return;
    }

    // El fichero manda sobre lo que trajera notes.json: si existe, es el sitio
    // bueno y lo de dentro de las notas es un resto de la versión anterior.
    qDeleteAll(m_birthdays);
    m_birthdays.clear();
    for (const QJsonValue v : root["birthdays"].toArray())
        m_birthdays.append(Birthday::fromJson(v.toObject()));
}

bool Store::writeAtomic(const QString &file, const QByteArray &data) {
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly)) return false;
    if (f.write(data) != data.size()) {
        f.cancelWriting();
        return false;
    }
    return f.commit();   // aquí es donde el fichero pasa a ser el nuevo, de golpe
}

// --- copias de seguridad ----------------------------------------------------

bool Store::copyToBackup(const QString &name) {
    const QString dir = backupDir();
    if (dir.isEmpty()) return false;     // carpeta ausente: ya no se escribe nada
    const QString src = path();
    if (!QFile::exists(src)) return false;   // no hay nada que apartar todavía

    const QString target = dir + "/" + name;
    if (QFile::exists(target)) return false;
    if (!QFile::copy(src, target)) return false;

    // La otra mitad, con el mismo nombre y otro prefijo. Puede no existir
    // todavía (nadie ha apuntado ningún cumpleaños) y eso no invalida la copia:
    // lo que manda es la de las notas.
    QString mate = name;
    mate.replace(0, int(qstrlen(kBackupPrefix)), kBirthdayBackupPrefix);
    if (QFile::exists(birthdaysPath())) QFile::copy(birthdaysPath(), dir + "/" + mate);

    pruneBackups();
    return true;
}

namespace {

// ¿Son el mismo contenido? Los ficheros son de unos pocos KB, así que leerlos
// enteros es más barato que cualquier cosa más lista.
bool sameFile(const QString &a, const QString &b) {
    QFile fa(a), fb(b);
    if (!fa.open(QIODevice::ReadOnly) || !fb.open(QIODevice::ReadOnly)) return false;
    return fa.readAll() == fb.readAll();
}

}  // namespace

QDateTime Store::nextBackupDue() const {
    if (m_prefs.backupEveryDays <= 0) return {};   // solo a mano

    // Sin ninguna copia todavía, la primera toca hoy a esa hora: si ya ha
    // pasado, se hace ahora mismo, que es lo que se espera al encenderlo.
    if (m_prefs.lastBackupMs <= 0) return QDateTime(QDate::currentDate(), m_prefs.backupAt);

    const QDate last = QDateTime::fromMSecsSinceEpoch(m_prefs.lastBackupMs).date();
    return QDateTime(last.addDays(m_prefs.backupEveryDays), m_prefs.backupAt);
}

bool Store::backupIfDue() {
    const QDateTime due = nextBackupDue();
    if (!due.isValid() || QDateTime::currentDateTime() < due) return false;
    return makeBackup();
}

bool Store::makeBackup() {
    if (!m_available) return false;
    const QString dir = backupDir();
    if (dir.isEmpty()) return false;

    // El nombre lleva la hora hasta el milisegundo: con segundos, dos copias
    // seguidas —pulsar dos veces "crear copia ahora", o restaurar justo
    // después de guardar— caían en el mismo fichero, y la segunda no se hacía
    // ni apuntaba la fecha, lo que descolocaba la pauta entera.
    //
    // Y cuando aun así coinciden hay que mirar, no suponer: es tentador dar
    // por hecho que dos copias del mismo milisegundo son la misma porque el
    // origen no ha podido cambiar, pero sí puede — restaurar reescribe
    // notes.json y pide copia acto seguido. Con esa suposición se daba por
    // buena una copia del contenido anterior. Si lo que hay guardado ya es
    // igual, está hecho; si es distinto, se prueba el milisegundo siguiente.
    QDateTime stamp = QDateTime::currentDateTime();
    for (int i = 0; i < 100; ++i, stamp = stamp.addMSecs(1)) {
        const QString name = kBackupPrefix + stamp.toString("yyyy-MM-dd-HHmmsszzz") + ".json";
        if (copyToBackup(name)) break;

        const QString target = dir + "/" + name;
        if (!QFile::exists(target)) return false;      // el fallo fue de la copia
        if (!sameFile(target, path())) continue;       // ese hueco es de otra cosa
        break;                                         // ya estaba hecha, y es esta
    }

    m_prefs.lastBackupMs = QDateTime::currentMSecsSinceEpoch();
    return true;
}

void Store::pruneBackups() {
    const QString dir = backupDir();
    if (dir.isEmpty()) return;

    // El nombre lleva la fecha en ISO, así que ordenar por nombre es ordenar
    // por antigüedad y no hace falta preguntarle al sistema de ficheros. Las
    // dos mitades se podan por separado y con el mismo tope: comparten marca
    // de tiempo, así que caen a la vez.
    for (const char *prefix : {kBackupPrefix, kBirthdayBackupPrefix}) {
        QStringList files = QDir(dir).entryList({QString(prefix) + "*.json"}, QDir::Files);
        files.sort();
        while (files.size() > Store::kBackupsKept) QFile::remove(dir + "/" + files.takeFirst());
    }
}

QList<Store::Backup> Store::backups() const {
    const QString dir = backupDir();
    if (dir.isEmpty()) return {};

    QStringList files = QDir(dir).entryList({QString(kBackupPrefix) + "*.json"}, QDir::Files);
    files.sort();

    QList<Backup> out;
    for (auto it = files.crbegin(); it != files.crend(); ++it) {   // la más nueva primero
        Backup b;
        b.path = dir + "/" + *it;
        // Tres formas, de la más nueva a la más vieja: con milisegundos, con
        // segundos (copias de antes de que hicieran falta) y solo la fecha (de
        // cuando era una al día). Se prueban por orden, de más larga a más
        // corta; lo que no encaje en ninguna se enseña por su nombre en vez de
        // descartarse, que puede ser una copia puesta a mano ahí.
        const QString stamp = it->mid(int(qstrlen(kBackupPrefix)),
                                      it->size() - int(qstrlen(kBackupPrefix)) - 5);
        for (const char *fmt : {"yyyy-MM-dd-HHmmsszzz", "yyyy-MM-dd-HHmmss"}) {
            b.when = QDateTime::fromString(stamp, QString::fromLatin1(fmt));
            if (b.when.isValid()) break;
        }
        if (!b.when.isValid())
            b.when = QDateTime(QDate::fromString(stamp, "yyyy-MM-dd"), QTime(0, 0));
        if (!b.when.date().isValid()) b.when = QDateTime();

        QFile f(b.path);
        if (!f.open(QIODevice::ReadOnly)) continue;
        b.notes = int(QJsonDocument::fromJson(f.readAll()).object()["notes"].toArray().size());
        out.append(b);
    }
    return out;
}

bool Store::restoreBackup(const QString &file) {
    if (!m_available) return false;

    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    if (!root.contains("notes")) return false;   // no es un notes.json: no se toca nada
    f.close();

    // Lo que hay ahora pasa a ser una copia más: restaurar la copia equivocada
    // tiene que poder deshacerse.
    makeBackup();
    const qint64 justNow = m_prefs.lastBackupMs;

    qDeleteAll(m_notes);
    m_notes.clear();
    m_prefs = Prefs{};
    readObject(root);

    // La mitad de los cumpleaños de esa misma copia, si está. Si no está —una
    // copia de antes de que existieran— los de ahora se quedan como están: no
    // es lo mismo "esa copia no tenía ninguno" que "esa copia no sabía de
    // esto", y vaciar la agenda por lo segundo sería perderla entera.
    QString mate = QFileInfo(file).fileName();
    if (mate.startsWith(kBackupPrefix)) {
        mate.replace(0, int(qstrlen(kBackupPrefix)), kBirthdayBackupPrefix);
        const QString matePath = QFileInfo(file).path() + "/" + mate;
        if (QFile mf(matePath); mf.exists() && mf.open(QIODevice::ReadOnly)) {
            const QJsonObject broot = QJsonDocument::fromJson(mf.readAll()).object();
            if (broot.contains("birthdays")) {
                qDeleteAll(m_birthdays);
                m_birthdays.clear();
                for (const QJsonValue v : broot["birthdays"].toArray())
                    m_birthdays.append(Birthday::fromJson(v.toObject()));
            }
        }
    }
    // readObject trae el "última copia" que llevaba dentro la copia, que es
    // viejo: dejarlo pediría otra copia inmediatamente. La que vale es la que
    // se acaba de hacer.
    m_prefs.lastBackupMs = qMax(justNow, m_prefs.lastBackupMs);
    // Restaurar es justamente decir "quiero esto encima de lo que haya", así
    // que el candado de un birthdays.json ilegible se levanta aquí.
    m_birthdaysReadable = true;
    save();
    emit reloaded();
    return true;
}

void Store::seedDemoNotes() {
    auto *a = new Note;
    a->type = Note::Check;
    a->title = "Release 0.4.2";
    a->items = {{"Bump flatpak manifest", true}, {L("Escribir changelog"), true},
                {"Tag + push", false}, {L("Publicar en el foro"), false}};
    auto *b = new Note;
    b->title = L("Escalado en Wayland");
    b->body = L("El escalado fraccional emborrona el widget en el panel 4K.");
    m_notes = {a, b};
}

// Tres desenlaces, y hay que distinguirlos: no hay fichero (instalación
// nueva), lo hay y se lee, o debería haberlo y no se puede llegar a él.
// Confundir el tercero con el primero es lo que hacía que un pendrive sin
// montar se llevara por delante las notas de su dueño.
void Store::load() {
    m_available = dataDirAvailable();

    if (m_available && !QFile::exists(path())) {
        // Instalación nueva: aquí sí manda el idioma del sistema, porque no
        // hay ninguna elección anterior que respetar. Se fija antes de sembrar
        // las notas de ejemplo, que también van traducidas.
        m_prefs.lang = Lang::systemDefault();
        Lang::setCurrent(m_prefs.lang);
        seedDemoNotes();
        // Puede haber cumpleaños sin notas: quien borró todas las notas, o
        // quien llegó aquí con un birthdays.json copiado a mano.
        loadBirthdays();
        return;
    }

    if (m_available && readFile()) {
        // Después de readFile: si los cumpleaños siguen dentro de notes.json
        // (la versión anterior), ahí es donde acaban de leerse, y esto decide
        // si el fichero propio los sustituye.
        loadBirthdays();
        // Aquí y no solo tras copiar: un día que ya tiene su copia sale antes
        // de podar, así que colgada únicamente de eso la carpeta podía crecer
        // sin límite. Una vez por lanzamiento acota el número sin tener que
        // listar el directorio en cada pulsación de tecla.
        pruneBackups();
        // La app pudo estar cerrada a la hora programada: se pone al día aquí,
        // sobre el fichero recién leído y antes de que nada lo toque.
        if (backupIfDue()) scheduleSave();   // para dejar apuntada la fecha
        return;
    }

    // La carpeta configurada no está, o su notes.json está ahí pero no se deja
    // leer. En ninguno de los dos casos se siembra nada ni se escribe: el
    // panel abre vacío y en solo lectura, y retryLoad() vuelve a mirar por si
    // el volumen aparece.
    m_available = false;
    // Y como tampoco se ha podido leer qué idioma prefería, se sigue al del
    // sistema igual que en una instalación nueva: dejarlo en español hacía que
    // el aviso de "no se está guardando" —lo único que se ve— saliera en un
    // idioma que su dueño quizá no lee. Es provisional: cuando el volumen
    // aparece, retryLoad() trae la preferencia de verdad y retranslate() la
    // aplica.
    m_prefs.lang = Lang::systemDefault();
    Lang::setCurrent(m_prefs.lang);
}

bool Store::readFile() {
    QFile f(path());
    if (!f.open(QIODevice::ReadOnly)) return false;
    return readObject(QJsonDocument::fromJson(f.readAll()).object());
}

// Aparte del fichero, porque restaurar una copia lee exactamente lo mismo de
// otro sitio. Se lee tolerando ficheros de versiones anteriores: cada clave
// añadida con el tiempo se comprueba antes de usarla.
bool Store::readObject(const QJsonObject &root) {
    if (root.contains("accent")) m_prefs.accent = QColor(root["accent"].toString());
    if (root.contains("opacity")) m_prefs.opacity = root["opacity"].toInt(96);
    if (root.contains("input")) m_prefs.input = root["input"].toString().toLatin1();
    m_prefs.onTop = root["onTop"].toBool();
    // Un fichero de antes de que hubiera idioma se queda en español, que es
    // como lo venía viendo su dueño; el del sistema solo decide en un
    // arranque en blanco.
    m_prefs.lang = Lang::fromString(root["lang"].toString(), Lang::Es);
    Lang::setCurrent(m_prefs.lang);
    m_prefs.birthdaysByMonth = root["birthdaysByMonth"].toBool();
    if (root.contains("w") && root.contains("h"))
        m_prefs.windowSize = QSize(root["w"].toInt(), root["h"].toInt());
    if (root.contains("x") && root.contains("y")) {
        m_prefs.windowPos = QPoint(root["x"].toInt(), root["y"].toInt());
        m_prefs.hasWindowPos = true;
    }

    if (root.contains("backupEvery")) m_prefs.backupEveryDays = root["backupEvery"].toInt(1);
    if (root.contains("backupAt")) {
        const QTime t = QTime::fromString(root["backupAt"].toString(), "HH:mm");
        if (t.isValid()) m_prefs.backupAt = t;
    }
    m_prefs.lastBackupMs = qint64(root["lastBackup"].toDouble());

    for (const QJsonValue v : root["notes"].toArray())
        m_notes.append(Note::fromJson(v.toObject()));
    // Un fichero de antes de que existieran los cumpleaños no trae la clave, y
    // entonces toArray() devuelve una lista vacía: no hace falta guarda.
    for (const QJsonValue v : root["birthdays"].toArray())
        m_birthdays.append(Birthday::fromJson(v.toObject()));
    return true;
}

bool Store::retryLoad() {
    if (m_available || !dataDirAvailable()) return false;

    if (QFile::exists(path())) {
        // Lo que se haya escrito mientras la carpeta no estaba se conserva, y
        // queda arriba como cualquier nota recién creada: recuperar el fichero
        // no puede costarle una nota a nadie.
        const QList<Note *> pending = m_notes;
        const QList<Birthday *> pendingBdays = m_birthdays;
        m_notes.clear();
        m_birthdays.clear();
        if (!readFile()) {
            m_notes = pending;
            m_birthdays = pendingBdays;
            return false;
        }
        for (int i = int(pending.size()) - 1; i >= 0; --i) m_notes.prepend(pending[i]);

        // loadBirthdays mira si hay fichero propio y, de haberlo, se queda con
        // lo que diga; por eso lo que se escribió mientras la carpeta no estaba
        // se vuelve a poner después. No tienen orden que respetar (la página
        // los ordena por fecha), así que van detrás y ya está.
        m_available = true;      // lo que consulta loadBirthdays
        loadBirthdays();
        m_birthdays += pendingBdays;
    }
    // Si la carpeta apareció sin fichero, se queda lo que hubiera en memoria:
    // sembrar los ejemplos ahora sería ponerlos encima de lo que el usuario
    // acaba de escribir.

    m_available = true;
    save();
    emit reloaded();
    return true;
}

void Store::changeDataDir(const QString &raw) {
    const QString to = trimmedDir(raw);
    const QString from = appDataDir();
    if (to.isEmpty() || to == from) return;

    // Las notas se llevan consigo sus adjuntos; si no, las notas de voz y las
    // imágenes apuntarían a ficheros que se quedaron en la carpeta anterior.
    QDir().mkpath(to + "/audio");
    QDir().mkpath(to + "/images");
    auto copyAttachment = [&](const QString &sub, const QString &name) {
        const QString target = to + "/" + sub + "/" + name;
        if (QFile::exists(target)) QFile::remove(target);
        QFile::copy(from + "/" + sub + "/" + name, target);
    };
    for (Note *n : m_notes) {
        if (!n->audio.isEmpty()) copyAttachment("audio", n->audio);
        for (const QString &image : n->images) copyAttachment("images", image);
    }

    // La historia se muda con las notas: son unos pocos KB y sin ella el
    // primer día en la carpeta nueva es un día sin nada a lo que volver.
    QDir().mkpath(to + "/backups");
    for (const char *prefix : {kBackupPrefix, kBirthdayBackupPrefix})
        for (const QString &name : QDir(from + "/backups")
                                       .entryList({QString(prefix) + "*.json"}, QDir::Files))
            QFile::copy(from + "/backups/" + name, to + "/backups/" + name);

    dataDirOverride() = to;
    QSettings().setValue("dataDir", to);
    // La carpeta nueva sí está —viene del selector—, así que se puede volver a
    // escribir aunque la anterior hubiera desaparecido. Lo mismo vale para el
    // candado de los cumpleaños: el fichero ilegible era el de la carpeta que
    // se deja atrás. El save() de abajo escribe notes.json y birthdays.json en
    // el destino, así que ninguno de los dos hay que copiarlo a mano.
    m_available = true;
    m_birthdaysReadable = true;
    save();
}

// La otra manera de cambiar de carpeta: quedarse con las notas que ya viven
// allí en lugar de llevarle las de aquí. Sin esto, elegir una carpeta era
// siempre "sobrescribe lo que haya", que es exactamente lo que no se quiere
// cuando se apunta a un pendrive que ya tiene las notas de uno.
void Store::adoptDataDir(const QString &raw) {
    const QString to = trimmedDir(raw);
    if (to.isEmpty() || to == appDataDir()) return;

    save();   // lo que había en memoria se queda guardado donde estaba
    qDeleteAll(m_notes);
    m_notes.clear();
    qDeleteAll(m_birthdays);
    m_birthdays.clear();
    m_prefs = Prefs{};

    dataDirOverride() = to;
    QSettings().setValue("dataDir", to);
    load();
    emit reloaded();
}
