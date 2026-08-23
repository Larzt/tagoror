#include "core/store.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

namespace {

constexpr int kSaveDelayMs = 600;

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

    dataDirOverride() = dir;
    if (dir.isEmpty()) migrateLegacyDataDir();
}

// --- notas -----------------------------------------------------------------

void Store::add(Note *n) {
    m_notes.prepend(n);
    scheduleSave();
}

void Store::remove(Note *n) {
    // El adjunto de audio muere con la nota; si no, quedan wav huérfanos.
    if (!n->audio.isEmpty()) QFile::remove(n->audioPath());

    m_notes.removeOne(n);
    delete n;
    save();
}

// --- persistencia ----------------------------------------------------------

QString Store::path() const { return appDataDir() + "/notes.json"; }

void Store::scheduleSave() { m_saveTimer->start(); }

void Store::save() {
    if (beforeSave) beforeSave();

    QJsonArray arr;
    for (Note *n : m_notes) arr.append(n->toJson());

    QJsonObject root;
    root["notes"] = arr;
    root["accent"] = m_prefs.accent.name();
    root["opacity"] = m_prefs.opacity;
    root["w"] = m_prefs.windowSize.width();
    root["h"] = m_prefs.windowSize.height();
    root["input"] = QString::fromLatin1(m_prefs.input);
    root["onTop"] = m_prefs.onTop;
    root["lang"] = Lang::toString(m_prefs.lang);

    QFile f(path());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

void Store::load() {
    QFile f(path());
    if (!f.open(QIODevice::ReadOnly)) {
        // Instalación nueva: aquí sí manda el idioma del sistema, porque no
        // hay ninguna elección anterior que respetar. Se fija antes de sembrar
        // las notas de ejemplo, que también van traducidas.
        m_prefs.lang = Lang::systemDefault();
        Lang::setCurrent(m_prefs.lang);

        // semilla inicial
        auto *a = new Note;
        a->type = Note::Check;
        a->title = "Release 0.4.2";
        a->items = {{"Bump flatpak manifest", true}, {L("Escribir changelog"), true},
                    {"Tag + push", false}, {L("Publicar en el foro"), false}};
        auto *b = new Note;
        b->title = L("Escalado en Wayland");
        b->body = L("El escalado fraccional emborrona el widget en el panel 4K.");
        m_notes = {a, b};
        return;
    }

    // Se lee tolerando ficheros de versiones anteriores: cada clave añadida
    // con el tiempo se comprueba antes de usarla.
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    if (root.contains("accent")) m_prefs.accent = QColor(root["accent"].toString());
    if (root.contains("opacity")) m_prefs.opacity = root["opacity"].toInt(96);
    if (root.contains("input")) m_prefs.input = root["input"].toString().toLatin1();
    m_prefs.onTop = root["onTop"].toBool();
    // Un fichero de antes de que hubiera idioma se queda en español, que es
    // como lo venía viendo su dueño; el del sistema solo decide en un
    // arranque en blanco.
    m_prefs.lang = Lang::fromString(root["lang"].toString(), Lang::Es);
    Lang::setCurrent(m_prefs.lang);
    if (root.contains("w") && root.contains("h"))
        m_prefs.windowSize = QSize(root["w"].toInt(), root["h"].toInt());

    for (const QJsonValue v : root["notes"].toArray())
        m_notes.append(Note::fromJson(v.toObject()));
}

void Store::changeDataDir(const QString &to) {
    const QString from = appDataDir();
    if (to.isEmpty() || to == from) return;

    // Las notas se llevan consigo sus adjuntos; si no, las notas de voz
    // apuntarían a ficheros que se quedaron en la carpeta anterior.
    QDir().mkpath(to + "/audio");
    for (Note *n : m_notes) {
        if (n->audio.isEmpty()) continue;
        const QString target = to + "/audio/" + n->audio;
        if (QFile::exists(target)) QFile::remove(target);
        QFile::copy(from + "/audio/" + n->audio, target);
    }

    dataDirOverride() = to;
    QSettings().setValue("dataDir", to);
    save();
}
