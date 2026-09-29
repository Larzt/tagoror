#pragma once

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QString>

/// Data folder picked by hand in settings. It cannot live in notes.json (which
/// lives inside it), so Store reads it from QSettings at startup.
inline QString &dataDirOverride() {
    static QString dir;
    return dir;
}

/// Whether the configured data folder can be used right now. A hand-picked
/// folder may sit on a volume that is not mounted yet (a USB stick is mounted
/// when its owner opens it, long after login), and then the path is missing.
inline bool dataDirAvailable() {
    if (dataDirOverride().isEmpty()) return true;   // the default folder is always created
    return QFileInfo(dataDirOverride()).isDir();
}

/// Root of the app data: notes.json and the attachments live here.
///
/// A hand-picked folder is never created here. A blind `mkpath` on an
/// unmounted drive built the whole path on the empty mount point, the app
/// took it for a fresh install, and later wrote that emptiness over the real
/// notes. The folder was created when it was picked; if it is missing now,
/// it is missing.
inline QString appDataDir() {
    if (!dataDirOverride().isEmpty()) return dataDirOverride();
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir;
}

/// Attachment folders, one per kind. Created only while the root is really
/// there: `mkpath` creates the whole chain and would rebuild the phantom path
/// appDataDir() no longer builds.
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

/// Backups, inside the data folder so they travel with the notes.
/// @return Empty when the data folder is unavailable.
inline QString backupDir() {
    if (!dataDirAvailable()) return QString();
    const QString dir = appDataDir() + "/backups";
    QDir().mkpath(dir);
    return dir;
}
