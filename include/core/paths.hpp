#pragma once

#include <QDir>
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

// Raíz de datos de la app: notes.json y los adjuntos de audio viven aquí.
// Todo el mundo la calcula desde aquí para que nadie discrepe de la ruta.
inline QString appDataDir() {
    const QString dir = dataDirOverride().isEmpty()
                            ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                            : dataDirOverride();
    QDir().mkpath(dir);
    return dir;
}

inline QString audioDir() {
    const QString dir = appDataDir() + "/audio";
    QDir().mkpath(dir);
    return dir;
}

// Las imágenes adjuntas, al lado del audio: una carpeta por tipo de adjunto
// para que mover la carpeta de datos siga siendo copiar dos directorios.
inline QString imageDir() {
    const QString dir = appDataDir() + "/images";
    QDir().mkpath(dir);
    return dir;
}
