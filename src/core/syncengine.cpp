#include "core/syncengine.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSettings>

#include "core/lang.hpp"
#include "core/paths.hpp"
#include "core/store.hpp"

namespace {

/// What is synced, relative to the data folder. Backups are not: they belong
/// to each machine. The order matters: it is the order of the objects passed
/// to Store::mergeRemote().
const QStringList &jsonFiles() {
    static const QStringList files{"notes.json", "birthdays.json", "events.json"};
    return files;
}
const QStringList &subdirs() {
    static const QStringList dirs{"audio", "images"};
    return dirs;
}

QString md5Of(const QByteArray &data) {
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Md5).toHex());
}

QString fileMd5(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    QCryptographicHash h(QCryptographicHash::Md5);
    h.addData(&f);
    return QString::fromLatin1(h.result().toHex());
}

}  // namespace

SyncEngine::SyncEngine(Store *store, QObject *parent) : QObject(parent), m_store(store) {}

void SyncEngine::setBackend(SyncBackend *backend) {
    if (backend == m_backend) return;
    m_backend = backend;
    ++m_pass;   // whatever was in flight belongs to the other one
    m_queue.clear();
    m_remoteJson.clear();
    m_again = false;
    m_error.clear();
    m_lastSync = QDateTime();
    if (m_backend) {
        const qint64 last = seen("lastSync").toLongLong();
        if (last > 0) m_lastSync = QDateTime::fromMSecsSinceEpoch(last);
    }
    setState(Idle);
}

QString SyncEngine::seen(const QString &key) const {
    return m_backend ? QSettings().value(m_backend->settingsGroup() + "/" + key).toString()
                     : QString();
}

void SyncEngine::setSeen(const QString &key, const QString &value) {
    if (m_backend) QSettings().setValue(m_backend->settingsGroup() + "/" + key, value);
}

void SyncEngine::setState(State s) {
    m_state = s;
    emit changed();
}

void SyncEngine::fail(const QString &why) {
    ++m_pass;
    m_error = why.isEmpty() ? L("La sincronización falló.") : why;
    m_queue.clear();
    m_remoteJson.clear();
    m_again = false;
    setState(Failed);
}

void SyncEngine::syncNow() {
    if (!m_backend || !m_backend->ready() || !m_store) return;
    if (m_state == Syncing) {
        m_again = true;
        return;
    }
    // A missing data folder is never synced: what is in memory then is not the
    // notes, and merging or uploading it would spread emptiness to every machine.
    if (!m_store->available() || !dataDirAvailable()) return;

    m_error.clear();
    m_queue.clear();
    m_pull.clear();
    m_remoteJson.clear();
    m_remoteMd5.clear();
    m_root.clear();
    m_gotFiles = false;
    const int pass = ++m_pass;
    setState(Syncing);

    m_backend->list(QString(), [this, pass](bool ok, SyncBackend::Listing listing) {
        if (pass != m_pass) return;
        if (!ok) return fail(m_backend->error());
        m_root = listing;
        fetchRemoteJson(0);
    });
}

/// Downloads the remote JSON files that changed since the last merge. An
/// unchanged one brings nothing new: it stays empty and the merge skips it.
void SyncEngine::fetchRemoteJson(int index) {
    if (index >= jsonFiles().size()) {
        mergeAndPlan();
        return;
    }
    const QString name = jsonFiles().at(index);
    const SyncBackend::Remote there = m_root.value(name);
    if (there.id.isEmpty() || there.hash == seen("seen/" + name)) {
        // Unchanged since the last merge: its content is what was recorded then.
        if (!there.id.isEmpty())
            m_remoteMd5.insert(name, there.md5.isEmpty() ? seen("content/" + name) : there.md5);
        m_remoteJson.append(QJsonObject());
        fetchRemoteJson(index + 1);
        return;
    }
    const int pass = m_pass;
    m_backend->download(QString(), name, there, [this, pass, index, name](bool ok, QByteArray data) {
        if (pass != m_pass) return;
        if (!ok) return fail(m_backend->error());
        m_remoteMd5.insert(name, md5Of(data));
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
        // Unparseable: treated as empty, i.e. not merged.
        m_remoteJson.append(err.error == QJsonParseError::NoError ? doc.object() : QJsonObject());
        fetchRemoteJson(index + 1);
    });
}

void SyncEngine::mergeAndPlan() {
    bool anything = false;
    for (const QJsonObject &o : m_remoteJson) anything |= !o.isEmpty();

    if (anything) {
        // Merging rebuilds the list: wait while the user is typing.
        if (canApply && !canApply()) {
            m_remoteJson.clear();
            ++m_pass;
            setState(Idle);
            emit deferred();
            return;
        }
        if (!m_store->available()) {   // the drive went away mid-pass
            fail(L("La carpeta de notas no está disponible."));
            return;
        }
        const Store::MergeResult merged =
            m_store->mergeRemote(m_remoteJson.value(0), m_remoteJson.value(1), m_remoteJson.value(2));
        m_pull = merged.pull;
        // Merged: next time it is not downloaded again unless it changed.
        for (int i = 0; i < jsonFiles().size(); ++i) {
            const QString name = jsonFiles().at(i);
            if (m_remoteJson.value(i).isEmpty()) continue;
            setSeen("seen/" + name, m_root.value(name).hash);
            setSeen("content/" + name, m_remoteMd5.value(name));
        }
    }

    // The middle step (Calendar) runs between the merge and the upload, so what
    // it brings reaches the other machines in this same pass.
    const int pass = m_pass;
    if (middleStep) {
        middleStep([this, pass] {
            if (pass == m_pass && m_state == Syncing) planUploads();
        });
    } else {
        planUploads();
    }
}

void SyncEngine::planUploads() {
    // The merged local data is uploaded if it differs from what is there. What
    // goes up is the shared payload (Store::syncPayload()), not the local file.
    for (const QString &name : m_store->syncableFiles()) {
        const QByteArray payload = m_store->syncPayload(name);
        const SyncBackend::Remote there = m_root.value(name);
        if (!there.id.isEmpty() && m_remoteMd5.value(name) == md5Of(payload)) continue;
        Transfer t;
        t.dir = QString();
        t.name = name;
        t.path = appDataDir() + "/" + name;
        t.remote = there;
        t.data = payload;
        m_queue.append(t);
    }
    planAttachments(0);
}

/// Plans the attachments the notes use, folder by folder. Files no note uses
/// are left alone on both sides.
///
/// When the backend knows the MD5 there (Drive, the folder) both sides are
/// compared directly. When it only has an ETag (WebDAV), what each side was
/// at the last transfer is remembered: only the side that changed since then
/// moves, and a file first seen on both sides is taken as already in step.
void SyncEngine::planAttachments(int index) {
    if (index >= subdirs().size()) {
        transferNext();
        return;
    }
    const QString dir = subdirs().at(index);
    QStringList wanted;
    for (const QString &ref : m_store->attachments())
        if (ref.startsWith(dir + "/")) wanted << ref.mid(dir.size() + 1);
    if (wanted.isEmpty()) {   // no attachments of this kind: do not even create the folder
        planAttachments(index + 1);
        return;
    }

    const int pass = m_pass;
    m_backend->list(dir, [this, pass, index, dir, wanted](bool ok, SyncBackend::Listing remote) {
        if (pass != m_pass) return;
        if (!ok) return fail(m_backend->error());
        const QString base = appDataDir() + "/" + dir;
        for (const QString &name : wanted) {
            const QString path = base + "/" + name;
            const SyncBackend::Remote there = remote.value(name);
            const bool here = QFileInfo::exists(path);
            const QString ref = dir + "/" + name;
            Transfer t;
            t.dir = dir;
            t.name = name;
            t.path = path;
            t.remote = there;
            if (!here && there.id.isEmpty()) continue;   // nowhere
            if (!here) {
                t.download = true;
                m_queue.append(t);
                continue;
            }
            if (there.id.isEmpty()) {
                m_queue.append(t);
                continue;
            }
            const QString local = fileMd5(path);
            if (!there.md5.isEmpty()) {
                if (there.md5 == local) continue;
                // Both exist and differ: the side the note's winning version came from wins.
                t.download = m_pull.contains(ref);
                m_queue.append(t);
                continue;
            }
            const QStringList last = seen("att/" + ref).split('|');
            if (last.size() != 2) {
                // Never transferred by this machine: in step unless the merge says
                // the other side's version won.
                if (m_pull.contains(ref)) {
                    t.download = true;
                    m_queue.append(t);
                } else {
                    setSeen("att/" + ref, there.hash + "|" + local);
                }
                continue;
            }
            const bool remoteChanged = there.hash != last.at(0);
            const bool localChanged = local != last.at(1);
            if (!remoteChanged && !localChanged) continue;
            t.download = m_pull.contains(ref) || (remoteChanged && !localChanged);
            m_queue.append(t);
        }
        planAttachments(index + 1);
    });
}

/// Runs the transfers one after another.
void SyncEngine::transferNext() {
    if (m_queue.isEmpty()) {
        finishSync();
        return;
    }
    const Transfer t = m_queue.first();
    const int pass = m_pass;

    if (t.download) {
        m_backend->download(t.dir, t.name, t.remote, [this, pass, t](bool ok, QByteArray data) {
            if (pass != m_pass) return;
            if (!ok) return fail(m_backend->error());
            // Creates the subfolder and writes all or nothing.
            QDir().mkpath(QFileInfo(t.path).path());
            QSaveFile f(t.path);
            if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit()) {
                fail(L("No se pudo escribir %1.").arg(t.name));
                return;
            }
            if (!t.dir.isEmpty()) setSeen("att/" + t.dir + "/" + t.name, t.remote.hash + "|" + md5Of(data));
            m_gotFiles = true;
            if (!m_queue.isEmpty()) m_queue.removeFirst();
            transferNext();
        });
        return;
    }

    QByteArray content = t.data;
    if (content.isEmpty()) {
        QFile f(t.path);
        if (!f.open(QIODevice::ReadOnly)) {   // deleted meanwhile: not an error
            m_queue.removeFirst();
            transferNext();
            return;
        }
        content = f.readAll();
    }
    m_backend->upload(t.dir, t.name, t.remote, content,
                      [this, pass, t, content](bool ok, SyncBackend::Remote now) {
        if (pass != m_pass) return;
        if (!ok) return fail(m_backend->error());
        const QString md5 = md5Of(content);
        if (t.dir.isEmpty()) {
            // What was just uploaded is already merged: do not download it next pass
            // just because it changed there. A backend that cannot tell its new hash
            // leaves it empty, and the next pass reads it once more.
            setSeen("seen/" + t.name, now.hash);
            setSeen("content/" + t.name, md5);
        } else {
            setSeen("att/" + t.dir + "/" + t.name, now.hash + "|" + md5);
        }
        if (!m_queue.isEmpty()) m_queue.removeFirst();
        transferNext();
    });
}

void SyncEngine::finishSync() {
    m_lastSync = QDateTime::currentDateTime();
    setSeen("lastSync", QString::number(m_lastSync.toMSecsSinceEpoch()));
    m_remoteJson.clear();
    setState(Idle);
    if (m_gotFiles) emit attachmentsArrived();
    if (m_again) {
        m_again = false;
        syncNow();
    }
}
