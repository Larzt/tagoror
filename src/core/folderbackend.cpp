#include "core/folderbackend.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>
#include <QTimer>

#include "core/lang.hpp"
#include "core/paths.hpp"

namespace {

QString md5Of(const QByteArray &data) {
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Md5).toHex());
}

QString clean(const QString &path) {
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

/// Whether one path is the other or contains it.
bool overlaps(const QString &a, const QString &b) {
    const QString x = clean(a) + "/", y = clean(b) + "/";
    return x.startsWith(y) || y.startsWith(x);
}

}  // namespace

FolderBackend::FolderBackend(QObject *parent) : SyncBackend(parent) {
    m_path = QSettings().value("sync/folder/path").toString();
}

bool FolderBackend::ready() const { return !m_path.isEmpty() && QFileInfo(m_path).isDir(); }

bool FolderBackend::setPath(const QString &path) {
    if (path.isEmpty()) return false;
    if (overlaps(path, appDataDir())) {
        m_error = L("Tiene que ser otra carpeta, no la de las notas ni una que la contenga.");
        emit changed();
        return false;
    }
    if (clean(path) != clean(m_path)) QSettings().remove("sync/folder");   // another copy
    m_path = clean(path);
    QSettings().setValue("sync/folder/path", m_path);
    m_error.clear();
    emit changed();
    return true;
}

void FolderBackend::forget() {
    QSettings().remove("sync/folder");
    m_path.clear();
    m_error.clear();
    emit changed();
}

QString FolderBackend::dirPath(const QString &dir) const {
    return dir.isEmpty() ? m_path : m_path + "/" + dir;
}

// Local and quick, but answered on the next turn of the loop like the network
// backends: the engine must not run its next step inside this call.
void FolderBackend::list(const QString &dir, std::function<void(bool, Listing)> done) {
    QTimer::singleShot(0, this, [this, dir, done] {
        if (!ready()) {
            m_error = L("La carpeta de sincronización no está disponible.");
            done(false, {});
            return;
        }
        QDir d(dirPath(dir));
        if (!d.exists() && !QDir().mkpath(d.path())) {
            m_error = L("No se pudo crear %1.").arg(d.path());
            done(false, {});
            return;
        }
        Listing out;
        for (const QFileInfo &fi : d.entryInfoList(QDir::Files | QDir::NoDotAndDotDot)) {
            // Half-written copies of the clients that sync the folder.
            if (fi.fileName().startsWith('.') || fi.fileName().endsWith(".tmp")) continue;
            QFile f(fi.absoluteFilePath());
            if (!f.open(QIODevice::ReadOnly)) continue;
            const QString md5 = md5Of(f.readAll());
            out.insert(fi.fileName(), Remote{fi.fileName(), md5, md5});
        }
        done(true, out);
    });
}

void FolderBackend::download(const QString &dir, const QString &name, const Remote &,
                             std::function<void(bool, QByteArray)> done) {
    QTimer::singleShot(0, this, [this, dir, name, done] {
        QFile f(dirPath(dir) + "/" + name);
        if (!f.open(QIODevice::ReadOnly)) {
            m_error = L("No se pudo leer %1.").arg(f.fileName());
            done(false, {});
            return;
        }
        done(true, f.readAll());
    });
}

void FolderBackend::upload(const QString &dir, const QString &name, const Remote &,
                           const QByteArray &data, std::function<void(bool, Remote)> done) {
    QTimer::singleShot(0, this, [this, dir, name, data, done] {
        QDir().mkpath(dirPath(dir));
        // All or nothing: the client syncing the folder must never pick up half a file.
        QSaveFile f(dirPath(dir) + "/" + name);
        if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit()) {
            m_error = L("No se pudo escribir %1.").arg(name);
            done(false, {});
            return;
        }
        const QString md5 = md5Of(data);
        done(true, Remote{name, md5, md5});
    });
}
