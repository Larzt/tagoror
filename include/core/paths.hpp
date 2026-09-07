#pragma once

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QString>

// Dónde viven los datos. Está aparte del modelo (note.hpp) porque es lo único
// que sabe de disco: quien solo maneja notas no necesita arrastrar esto.

// Carpeta elegida a mano en ajustes. No puede guardarse en notes.json (que
// vive justamente ahí dentro), así que Store la lee de QSettings al arrancar.
inline QString &dataDirOverride() {
    static QString dir;
    return dir;
}

// Si la carpeta configurada se puede usar ahora mismo. Distinguirlo importa,
// y costó caro averiguarlo: una carpeta elegida a mano puede vivir en un
// volumen que todavía no está montado —un pendrive se monta cuando el usuario
// lo abre, mucho después del arranque de sesión— y entonces la ruta no existe.
inline bool dataDirAvailable() {
    if (dataDirOverride().isEmpty()) return true;   // la estándar siempre se crea
    return QFileInfo(dataDirOverride()).isDir();
}

// Raíz de datos de la app: notes.json y los adjuntos viven aquí. Todo el mundo
// la calcula desde aquí para que nadie discrepe de la ruta.
//
// La carpeta elegida a mano NO se crea nunca aquí, y esa es la diferencia
// entre esta versión y la que perdió notas: con `mkpath` a ciegas, un pendrive
// sin montar hacía que se fabricara la ruta entera sobre el punto de montaje
// vacío, la app arrancaba creyendo que su dueño no tenía ninguna nota, y al
// aparecer el volumen escribía ese vacío encima de las de verdad. La carpeta
// se creó al elegirla; si ahora no está, es que no está.
inline QString appDataDir() {
    if (!dataDirOverride().isEmpty()) return dataDirOverride();
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir;
}

// Los adjuntos, en una carpeta por tipo, para que mudar los datos siga siendo
// copiar dos directorios. Se crean solo si la raíz está de verdad ahí: `mkpath`
// crea la cadena entera, así que sin esta guarda volverían a fabricar la ruta
// fantasma que appDataDir() ya no fabrica.
inline QString audioDir() {
    const QString dir = appDataDir() + "/audio";
    if (dataDirAvailable()) QDir().mkpath(dir);
    return dir;
}

inline QString imageDir() {
    const QString dir = appDataDir() + "/images";
    if (dataDirAvailable()) QDir().mkpath(dir);
    return dir;
}

// Las copias de seguridad, dentro de la propia carpeta de datos: así viajan
// con las notas al pendrive, que es donde de verdad hacen falta. Devuelve
// vacío si la carpeta no está, para que quien copie sepa que no hay dónde.
inline QString backupDir() {
    if (!dataDirAvailable()) return QString();
    const QString dir = appDataDir() + "/backups";
    QDir().mkpath(dir);
    return dir;
}
