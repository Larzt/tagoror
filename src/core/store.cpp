#include "core/store.hpp"

#include <algorithm>

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

/// Ten days of history (Store::kBackupsKept): enough for someone who notices
/// a week later that notes are missing.
constexpr auto kBackupPrefix = "notes-";
/// Birthdays are backed up at the same time with the same timestamp, so a
/// backup is always both halves of one instant and restoring one brings the
/// other.
constexpr auto kBirthdayBackupPrefix = "birthdays-";
/// The planner too: the third part of the same snapshot.
constexpr auto kEventBackupPrefix = "events-";

/// Names the data was stored under by earlier releases, newest first. A
/// rename must never strand anyone's notes, so both the folder and the
/// settings are inherited. Each rename ADDS to the front of this list;
/// replacing entries would orphan the notes of whoever skipped a version.
const QStringList &legacyAppNames() {
    static const QStringList names{"Codex", "Abyss", "NotasWidget"};
    return names;
}

/// Moves the data wholesale if it still sits under an old name and the new
/// location is still empty.
void migrateLegacyDataDir() {
    const QString current = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (QFile::exists(current + "/notes.json")) return;

    const QString parent = QFileInfo(current).path();
    for (const QString &name : legacyAppNames()) {
        const QString legacy = parent + "/" + name;
        if (!QFile::exists(legacy + "/notes.json")) continue;

        // rename() fails if the destination exists, and appDataDir() may have created
        // it empty.
        if (QDir(current).exists() && QDir(current).isEmpty()) QDir().rmdir(current);
        QDir().rename(legacy, current);
        return;
    }
}

/// Drops the trailing slash. The folder picker never returns one, and
/// ".../notes/" != ".../notes" made re-picking the same folder look like a
/// move.
QString trimmedDir(const QString &in) {
    QString out = in;
    while (out.size() > 1 && out.endsWith('/')) out.chop(1);
    return out;
}

/// Tombstones are kept for half a year: plenty for every machine to connect
/// once, without the file growing forever.
constexpr qint64 kTombstoneMs = 180LL * 24 * 3600 * 1000;

/// Key of an element in snapshots and tombstones: its type before its id.
/// With bare ids, a note and a birthday sharing one (old or hand-edited files)
/// overwrote each other's snapshot and both looked changed on every save.
inline QString keyOf(const Note *n) { return "n:" + n->id; }
inline QString keyOf(const Birthday *b) { return "b:" + b->id; }
inline QString keyOf(const Event *e) { return "e:" + e->id; }
inline QString keyOf(const Timer *t) { return "t:" + t->id; }
inline QString keyOf(const Event::Category *c) { return "c:" + c->id; }
inline QString keyOf(const Area *a) { return "a:" + a->id; }

/// What an element says without its timestamp: if this does not change, the
/// element did not change.
template <typename T>
QByteArray snapshotOf(const T *item) {
    QJsonObject o = item->toJson();
    o.remove("updated");
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

/// Adopting the remote version keeps runtime-only state: ringing belongs to
/// this machine, not the other.
void adopt(Note *local, const Note *remote) {
    const bool ringing = local->ringing;
    *local = *remote;
    local->ringing = ringing;
}
void adopt(Birthday *local, const Birthday *remote) {
    const bool ringing = local->ringing;
    *local = *remote;
    local->ringing = ringing;
}
void adopt(Event *local, const Event *remote) {
    const qint64 ringing = local->ringingMs;
    *local = *remote;
    local->ringingMs = ringing;
}
void adopt(Timer *local, const Timer *remote) { *local = *remote; }
void adopt(Event::Category *local, const Event::Category *remote) { *local = *remote; }
void adopt(Area *local, const Area *remote) { *local = *remote; }

/// Custom categories of an events.json (or of its backup).
QList<Event::Category *> readCategories(const QJsonObject &root) {
    QList<Event::Category *> out;
    for (const QJsonValue v : root["categories"].toArray()) {
        Event::Category *c = Event::Category::fromJson(v.toObject());
        // A custom category cannot pose as a built-in one.
        if (Event::isBuiltinCategory(c->id)) delete c;
        else out.append(c);
    }
    return out;
}

}  // namespace

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
    qDeleteAll(m_areas);
    qDeleteAll(m_birthdays);
    qDeleteAll(m_timers);
    qDeleteAll(m_events);
    qDeleteAll(m_categories);
}

void Store::resolveDataDir() {
    QSettings settings;
    QString dir = settings.value("dataDir").toString();

    if (dir.isEmpty()) {
        // The hand-picked folder is inherited from old names too.
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

void Store::add(Note *n) {
    m_notes.prepend(n);
    scheduleSave();
}

void Store::remove(Note *n) {
    // Attachments die with the note, or nothing would ever delete them.
    if (!n->audio.isEmpty()) QFile::remove(n->audioPath());
    for (const QString &image : n->images) QFile::remove(Note::imagePath(image));

    m_notes.removeOne(n);
    delete n;
    save();
}

// The order comes from the screen, so only check that it holds exactly the
// same notes: any mismatch keeps the previous order rather than losing one.
void Store::setOrder(const QList<Note *> &order) {
    if (order.size() != m_notes.size()) return;
    for (Note *n : order)
        if (!m_notes.contains(n)) return;

    m_notes = order;
    // Deferred on purpose: a card drag calls this once per card crossed.
    scheduleSave();
}

Area *Store::area(const QString &id) const {
    for (Area *a : m_areas)
        if (a->id == id) return a;
    return nullptr;
}

QString Store::areaOf(const Note *n) const {
    const QString id = n->area.isEmpty() ? QString(Area::kDefaultId) : n->area;
    if (area(id)) return id;
    return m_areas.isEmpty() ? QString(Area::kDefaultId) : m_areas.first()->id;
}

QList<Note *> Store::notesIn(const QString &areaId) const {
    QList<Note *> out;
    for (Note *n : m_notes)
        if (areaOf(n) == areaId) out.append(n);
    return out;
}

Area *Store::addArea(const QString &name) {
    auto *a = new Area;
    a->name = name;
    a->pos = m_areas.isEmpty() ? 0 : m_areas.last()->pos + 1;
    m_areas.append(a);
    save();
    return a;
}

void Store::removeArea(Area *a, const QString &moveTo) {
    if (!m_areas.contains(a) || m_areas.size() < 2) return;
    const QString id = a->id;
    // Decided while the area is still in the list: without it, areaOf() would
    // already send its notes to the first area.
    const QList<Note *> own = notesIn(id);
    for (Note *n : own) {
        if (!moveTo.isEmpty() && moveTo != id && area(moveTo)) {
            setNoteArea(n, moveTo);
            continue;
        }
        if (!n->audio.isEmpty()) QFile::remove(n->audioPath());
        for (const QString &image : n->images) QFile::remove(Note::imagePath(image));
        m_notes.removeOne(n);
        delete n;
    }
    m_areas.removeOne(a);
    delete a;
    if (m_prefs.activeArea == id) m_prefs.activeArea.clear();
    save();
}

void Store::moveArea(Area *a, int steps) {
    const int from = int(m_areas.indexOf(a));
    const int to = from + steps;
    if (from < 0 || to < 0 || to >= m_areas.size()) return;
    m_areas.move(from, to);
    // Renumber all of them: two areas with the same pos (created at once on two
    // machines) would otherwise end up in merge-dependent order.
    for (int i = 0; i < m_areas.size(); ++i) m_areas[i]->pos = i;
    save();
}

void Store::setNoteArea(Note *n, const QString &areaId) {
    n->area = areaId == Area::kDefaultId ? QString() : areaId;
}

void Store::sortAreas() {
    std::stable_sort(m_areas.begin(), m_areas.end(), [](const Area *a, const Area *b) {
        // Equal pos: by id, so every machine sees the same order.
        return a->pos != b->pos ? a->pos < b->pos : a->id < b->id;
    });
}

void Store::ensureAreas() {
    sortAreas();
    if (!m_areas.isEmpty()) return;
    auto *a = new Area;
    a->id = Area::kDefaultId;
    a->name = L("Personal");
    m_areas.append(a);
}

void Store::addTimer(Timer *t) {
    m_timers.prepend(t);
    save();
}

void Store::removeTimer(Timer *t) {
    m_timers.removeOne(t);
    delete t;
    save();
}

void Store::addEvent(Event *e, bool save) {
    m_events.append(e);
    if (save) this->save();
}

void Store::removeEvent(Event *e, bool save) {
    m_events.removeOne(e);
    delete e;
    if (save) this->save();
}

void Store::addCategory(Event::Category *c) {
    m_categories.append(c);
    save();
}

void Store::removeCategory(Event::Category *c) {
    const bool google = c->id.startsWith("gcal:");
    for (int i = int(m_events.size()) - 1; i >= 0; --i) {
        Event *e = m_events.at(i);
        if (e->category != c->id) continue;
        if (google) {
            m_events.removeAt(i);
            delete e;
        } else {
            e->category = Event::kFallbackCategory;
        }
    }
    m_categories.removeOne(c);
    delete c;
    save();
}

void Store::addBirthday(Birthday *b) {
    m_birthdays.append(b);
    // Deferred like notes: whoever adds one fills it in right after, and that
    // save writes both changes at once.
    scheduleSave();
}

void Store::removeBirthday(Birthday *b) {
    m_birthdays.removeOne(b);
    delete b;
    save();
}

QString Store::path() const { return appDataDir() + "/notes.json"; }

QString Store::birthdaysPath() const { return appDataDir() + "/birthdays.json"; }
QString Store::eventsPath() const { return appDataDir() + "/events.json"; }

void Store::scheduleSave() { m_saveTimer->start(); }

void Store::save() {
    // Never write a file that could not be read. With the folder missing, what is
    // in memory is not the user's notes but the ones that failed to load, and
    // writing them would wipe the real file once the volume reappears.
    if (!m_available) return;

    if (beforeSave) beforeSave();
    stampChanges();

    // Before building the JSON: a due backup copies the file still on disk, and
    // its new lastBackupMs goes into this very write.
    backupIfDue();

    QJsonArray arr;
    for (Note *n : m_notes) arr.append(n->toJson());
    QJsonArray areas;
    for (Area *a : m_areas) areas.append(a->toJson());

    QJsonObject root;
    root["notes"] = arr;
    root["areas"] = areas;
    root["activeArea"] = m_prefs.activeArea;
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
    root["appMode"] = m_prefs.appMode;
    root["sizePerPage"] = m_prefs.sizePerPage;
    QJsonObject pageSizes;
    for (auto it = m_prefs.pageSizes.cbegin(); it != m_prefs.pageSizes.cend(); ++it)
        pageSizes[it.key()] = QJsonArray{it.value().width(), it.value().height()};
    root["pageSizes"] = pageSizes;
    root["lang"] = Lang::toString(m_prefs.lang);
    root["birthdaysByMonth"] = m_prefs.birthdaysByMonth;
    root["textScale"] = m_prefs.textScale;
    root["plannerView"] = m_prefs.plannerView;
    root["plannerHidden"] = QJsonArray::fromStringList(m_prefs.plannerHidden);
    QJsonArray timers;
    for (Timer *t : m_timers) timers.append(t->toJson());
    root["timers"] = timers;
    root["orderUpdated"] = double(m_orderUpdatedMs);
    QJsonObject deleted;
    for (auto it = m_deleted.cbegin(); it != m_deleted.cend(); ++it)
        deleted[it.key()] = double(it.value());
    root["deleted"] = deleted;
    root["updateCheck"] = m_prefs.updateCheck;
    root["lastUpdate"] = m_prefs.lastUpdateMs;
    root["latestSeen"] = m_prefs.latestSeen;
    root["backupEvery"] = m_prefs.backupEveryDays;
    root["backupAt"] = m_prefs.backupAt.toString("HH:mm");
    root["lastBackup"] = m_prefs.lastBackupMs;

    writeAtomic(path(), QJsonDocument(root).toJson(QJsonDocument::Indented));
    saveBirthdays();
    saveEvents();
    emit saved();
}

void Store::saveEvents() {
    if (!m_available || !m_eventsReadable) return;

    QJsonArray arr;
    for (Event *e : m_events) arr.append(e->toJson());
    QJsonArray cats;
    for (Event::Category *c : m_categories) cats.append(c->toJson());

    QJsonObject root;
    root["events"] = arr;
    root["categories"] = cats;
    writeAtomic(eventsPath(), QJsonDocument(root).toJson(QJsonDocument::Indented));
}

/// Same treatment as birthdays.json, without the migration: events never
/// lived in notes.json, so a missing file just means none yet.
void Store::loadEvents() {
    m_eventsReadable = true;
    if (!m_available) return;

    QFile f(eventsPath());
    if (!f.exists()) return;
    if (!f.open(QIODevice::ReadOnly)) {
        m_eventsReadable = false;
        return;
    }
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    if (!root.contains("events")) {
        m_eventsReadable = false;   // never overwrite what could not be read
        return;
    }
    qDeleteAll(m_events);
    m_events.clear();
    for (const QJsonValue v : root["events"].toArray())
        m_events.append(Event::fromJson(v.toObject()));
    qDeleteAll(m_categories);
    m_categories = readCategories(root);
}

/// Separate from save() so it can be skipped: an unreadable birthdays.json is
/// never overwritten.
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
        // No file: either no birthdays yet, or they come from when they lived in
        // notes.json, in which case readObject() already loaded them and a save
        // writes them where they belong now.
        if (!m_birthdays.isEmpty()) scheduleSave();
        return;
    }

    if (!f.open(QIODevice::ReadOnly)) {
        m_birthdaysReadable = false;
        return;
    }
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    if (!root.contains("birthdays")) {
        // The file exists but is not what it claims (truncated, something else):
        // leave it alone, which is the opposite of emptying it.
        m_birthdaysReadable = false;
        return;
    }

    // The file wins over whatever notes.json brought: that is a leftover of the
    // previous version.
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
    return f.commit();   // the file is replaced here, in one step
}

bool Store::copyToBackup(const QString &name) {
    const QString dir = backupDir();
    if (dir.isEmpty()) return false;     // folder missing: write nothing
    const QString src = path();
    if (!QFile::exists(src)) return false;   // nothing to set aside yet

    const QString target = dir + "/" + name;
    if (QFile::exists(target)) return false;
    if (!QFile::copy(src, target)) return false;

    // The sibling halves, same name with another prefix. They may not exist yet,
    // which does not invalidate the backup: the notes are what counts.
    const QString stamp = name.mid(int(qstrlen(kBackupPrefix)));
    if (QFile::exists(birthdaysPath()))
        QFile::copy(birthdaysPath(), dir + "/" + kBirthdayBackupPrefix + stamp);
    if (QFile::exists(eventsPath()))
        QFile::copy(eventsPath(), dir + "/" + kEventBackupPrefix + stamp);

    pruneBackups();
    return true;
}

namespace {

/// Whether both files have the same content. They are a few KB, so reading
/// them whole beats anything cleverer.
bool sameFile(const QString &a, const QString &b) {
    QFile fa(a), fb(b);
    if (!fa.open(QIODevice::ReadOnly) || !fb.open(QIODevice::ReadOnly)) return false;
    return fa.readAll() == fb.readAll();
}

}  // namespace

QDateTime Store::nextBackupDue() const {
    if (m_prefs.backupEveryDays <= 0) return {};   // manual only

    // With no backup yet the first is due today at that time, i.e. right away if
    // it has already passed.
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

    // Millisecond timestamps: with seconds, two backups in a row landed on one
    // file, the second was skipped and lastBackupMs was not recorded, which
    // knocked the schedule out.
    //
    // A collision is checked, not assumed identical: restoring rewrites
    // notes.json and requests a backup right after, inside the same millisecond.
    // Same content means done; different means try the next millisecond.
    QDateTime stamp = QDateTime::currentDateTime();
    for (int i = 0; i < 100; ++i, stamp = stamp.addMSecs(1)) {
        const QString name = kBackupPrefix + stamp.toString("yyyy-MM-dd-HHmmsszzz") + ".json";
        if (copyToBackup(name)) break;

        const QString target = dir + "/" + name;
        if (!QFile::exists(target)) return false;      // the copy itself failed
        if (!sameFile(target, path())) continue;       // that slot holds something else
        break;                                         // already taken, and it is this one
    }

    m_prefs.lastBackupMs = QDateTime::currentMSecsSinceEpoch();
    return true;
}

void Store::pruneBackups() {
    const QString dir = backupDir();
    if (dir.isEmpty()) return;

    // Names carry an ISO date, so sorting by name sorts by age. Each prefix is
    // pruned separately with the same limit; they share timestamps and so go
    // together.
    for (const char *prefix : {kBackupPrefix, kBirthdayBackupPrefix, kEventBackupPrefix}) {
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
    for (auto it = files.crbegin(); it != files.crend(); ++it) {   // newest first
        Backup b;
        b.path = dir + "/" + *it;
        // Three shapes, newest to oldest: milliseconds, seconds and date only. What
        // matches none is listed by file name rather than hidden (it may be a copy
        // placed there by hand).
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
    if (!root.contains("notes")) return false;   // not a notes.json: touch nothing
    f.close();

    // The current state becomes one more backup, so restoring can be undone.
    makeBackup();
    const qint64 justNow = m_prefs.lastBackupMs;

    qDeleteAll(m_notes);
    m_notes.clear();
    qDeleteAll(m_timers);
    m_timers.clear();
    // Areas go with their notes: a backup from before areas has none, and the
    // default one is recreated.
    qDeleteAll(m_areas);
    m_areas.clear();
    const QString activeArea = m_prefs.activeArea;
    m_prefs = Prefs{};
    readObject(root);
    ensureAreas();
    // The open area belongs to this machine, not to the backup.
    m_prefs.activeArea = activeArea;

    // The birthdays half of the same backup, if present. If absent (a backup from
    // before the split) the current ones are left alone: "that backup had none"
    // and "that backup did not know about this" are not the same, and emptying
    // the agenda for the second would lose it. The planner follows the same rule.
    const QString fileName = QFileInfo(file).fileName();
    if (fileName.startsWith(kBackupPrefix)) {
        const QString stamp = fileName.mid(int(qstrlen(kBackupPrefix)));
        // false if that half is missing or lacks its key: the current data stays.
        auto readMate = [&](const char *prefix, const char *key, QJsonObject *out) {
            QFile mf(QFileInfo(file).path() + "/" + prefix + stamp);
            if (!mf.exists() || !mf.open(QIODevice::ReadOnly)) return false;
            const QJsonObject mroot = QJsonDocument::fromJson(mf.readAll()).object();
            if (!mroot.contains(key)) return false;
            *out = mroot;
            return true;
        };
        QJsonObject mate;
        if (readMate(kBirthdayBackupPrefix, "birthdays", &mate)) {
            qDeleteAll(m_birthdays);
            m_birthdays.clear();
            for (const QJsonValue v : mate["birthdays"].toArray())
                m_birthdays.append(Birthday::fromJson(v.toObject()));
        }
        // Custom categories go with their events.
        if (readMate(kEventBackupPrefix, "events", &mate)) {
            qDeleteAll(m_events);
            m_events.clear();
            for (const QJsonValue v : mate["events"].toArray())
                m_events.append(Event::fromJson(v.toObject()));
            qDeleteAll(m_categories);
            m_categories = readCategories(mate);
        }
    }
    // readObject() brought the backup's old lastBackupMs; keeping it would ask for
    // another backup immediately.
    m_prefs.lastBackupMs = qMax(justNow, m_prefs.lastBackupMs);
    // Restoring means "put this over whatever is there", so the lock of an
    // unreadable file is lifted.
    m_birthdaysReadable = true;
    m_eventsReadable = true;
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

// Three outcomes that must be told apart: no file (fresh install), a file that
// reads, and a file that should be there and cannot be reached. Mistaking the
// third for the first is how an unmounted drive destroyed its owner's notes.
void Store::load() {
    m_available = dataDirAvailable();

    if (m_available && !QFile::exists(path())) {
        // Fresh install: the system language decides, set before seeding the demo
        // notes, which are translated too.
        m_prefs.lang = Lang::systemDefault();
        Lang::setCurrent(m_prefs.lang);
        seedDemoNotes();
        ensureAreas();
        // There may be birthdays without notes.
        loadBirthdays();
        loadEvents();
        resetSnapshots();
        return;
    }

    if (m_available && readFile()) {
        // A file from before areas has none: the default one is created before the
        // snapshot below, since it is not a user change and every machine creates it
        // with the same id.
        ensureAreas();
        // After readFile(): if birthdays still live in notes.json (older version) they
        // were just read there, and this decides whether their own file replaces them.
        loadBirthdays();
        loadEvents();
        resetSnapshots();   // what was read is not a change
        // Here and not only after a copy: copyToBackup() returns early when the
        // target exists, so hung only off it the folder could grow without limit.
        pruneBackups();
        // The app may have been closed at the scheduled time: catch up here, on the
        // file just read and before anything touches it.
        if (backupIfDue()) scheduleSave();   // to record the date
        return;
    }

    // The folder is missing, or its notes.json cannot be read. Seed nothing and
    // write nothing: the panel opens empty and read-only, and retryLoad() checks
    // again later.
    m_available = false;
    // The preferred language could not be read either, so follow the system's as
    // on a fresh install; otherwise the "not saving" warning (the only thing on
    // screen) could be in a language its owner does not read. retryLoad() brings
    // the real preference later.
    m_prefs.lang = Lang::systemDefault();
    Lang::setCurrent(m_prefs.lang);
    ensureAreas();   // the panel needs a tab even when empty
}

bool Store::readFile() {
    QFile f(path());
    if (!f.open(QIODevice::ReadOnly)) return false;
    return readObject(QJsonDocument::fromJson(f.readAll()).object());
}

/// Reads everything but the files themselves, since restoring a backup reads
/// the same from elsewhere. Tolerates older files: every key added over time
/// is checked before use.
bool Store::readObject(const QJsonObject &root) {
    if (root.contains("accent")) m_prefs.accent = QColor(root["accent"].toString());
    if (root.contains("opacity")) m_prefs.opacity = root["opacity"].toInt(96);
    if (root.contains("input")) m_prefs.input = root["input"].toString().toLatin1();
    m_prefs.onTop = root["onTop"].toBool();
    m_prefs.appMode = root["appMode"].toBool();   // older files: widget mode
    // Older files: on, the default.
    if (root.contains("sizePerPage")) m_prefs.sizePerPage = root["sizePerPage"].toBool();
    const QJsonObject pageSizes = root["pageSizes"].toObject();
    for (auto it = pageSizes.begin(); it != pageSizes.end(); ++it) {
        const QJsonArray wh = it.value().toArray();
        const QSize size(wh.at(0).toInt(), wh.at(1).toInt());
        if (size.isValid() && !size.isEmpty()) m_prefs.pageSizes.insert(it.key(), size);
    }
    // A file from before languages stays Spanish, which is what its owner was
    // seeing; the system language only decides on a blank start.
    m_prefs.lang = Lang::fromString(root["lang"].toString(), Lang::Es);
    Lang::setCurrent(m_prefs.lang);
    m_prefs.birthdaysByMonth = root["birthdaysByMonth"].toBool();
    if (root.contains("textScale")) m_prefs.textScale = qBound(80, root["textScale"].toInt(100), 150);
    if (root.contains("plannerView")) m_prefs.plannerView = qBound(0, root["plannerView"].toInt(2), 2);
    for (const QJsonValue v : root["plannerHidden"].toArray())
        m_prefs.plannerHidden << v.toString();
    // Guarded: toBool() on a missing key would give false instead of the default.
    if (root.contains("updateCheck")) m_prefs.updateCheck = root["updateCheck"].toBool();
    m_prefs.lastUpdateMs = qint64(root["lastUpdate"].toDouble());
    m_prefs.latestSeen = root["latestSeen"].toString();
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
    for (const QJsonValue v : root["areas"].toArray())
        m_areas.append(Area::fromJson(v.toObject()));
    m_prefs.activeArea = root["activeArea"].toString();
    // No guard needed: a missing key gives an empty array.
    for (const QJsonValue v : root["birthdays"].toArray())
        m_birthdays.append(Birthday::fromJson(v.toObject()));
    for (const QJsonValue v : root["timers"].toArray())
        m_timers.append(Timer::fromJson(v.toObject()));
    m_orderUpdatedMs = qint64(root["orderUpdated"].toDouble());
    // Merged, not replaced: restoring an old backup must not forget what has been
    // deleted since.
    const QJsonObject deleted = root["deleted"].toObject();
    for (auto it = deleted.begin(); it != deleted.end(); ++it)
        m_deleted[it.key()] = qMax(m_deleted.value(it.key()), qint64(it.value().toDouble()));
    return true;
}

bool Store::retryLoad() {
    if (m_available || !dataDirAvailable()) return false;

    if (QFile::exists(path())) {
        // What was written while the folder was missing is kept, on top like any new
        // note: recovering the file must not cost anyone a note.
        const QList<Note *> pending = m_notes;
        const QList<Birthday *> pendingBdays = m_birthdays;
        const QList<Timer *> pendingTimers = m_timers;
        const QList<Event *> pendingEvents = m_events;
        const QList<Event::Category *> pendingCats = m_categories;
        const QList<Area *> pendingAreas = m_areas;
        m_areas.clear();
        m_notes.clear();
        m_birthdays.clear();
        m_timers.clear();
        m_events.clear();
        m_categories.clear();
        if (!readFile()) {
            m_notes = pending;
            m_birthdays = pendingBdays;
            m_timers = pendingTimers;
            m_events = pendingEvents;
            m_categories = pendingCats;
            m_areas = pendingAreas;
            return false;
        }
        // Areas created meanwhile are kept, except those the file already has (the
        // default one, created when opening empty).
        QList<Area *> newAreas;
        for (Area *a : pendingAreas) {
            if (area(a->id)) {
                delete a;
                continue;
            }
            a->pos = 1 << 20;   // after the existing ones
            m_areas.append(a);
            newAreas.append(a);
        }
        ensureAreas();
        for (int i = int(pending.size()) - 1; i >= 0; --i) m_notes.prepend(pending[i]);
        for (int i = int(pendingTimers.size()) - 1; i >= 0; --i) m_timers.prepend(pendingTimers[i]);

        // loadBirthdays() keeps what its own file says, which is why what was written
        // meanwhile is added back afterwards.
        m_available = true;      // what loadBirthdays() checks
        loadBirthdays();
        m_birthdays += pendingBdays;
        loadEvents();
        m_events += pendingEvents;
        m_categories += pendingCats;
        // What was read is the baseline; what was written meanwhile is a change, so
        // it gets no snapshot and will be stamped.
        resetSnapshots();
        for (Note *n : pending) m_snap.remove(keyOf(n));
        for (Birthday *b : pendingBdays) m_snap.remove(keyOf(b));
        for (Timer *t : pendingTimers) m_snap.remove(keyOf(t));
        for (Event *e : pendingEvents) m_snap.remove(keyOf(e));
        for (Event::Category *c : pendingCats) m_snap.remove(keyOf(c));
        for (Area *a : newAreas) m_snap.remove(keyOf(a));
    }
    // The folder appeared without a file: keep what is in memory. Seeding the
    // demo notes now would put them over what the user just wrote.

    m_available = true;
    save();
    emit reloaded();
    return true;
}

void Store::changeDataDir(const QString &raw) {
    const QString to = trimmedDir(raw);
    const QString from = appDataDir();
    if (to.isEmpty() || to == from) return;

    // Attachments move with the notes, or they would point at the old folder.
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

    // The backup history moves too.
    QDir().mkpath(to + "/backups");
    for (const char *prefix : {kBackupPrefix, kBirthdayBackupPrefix, kEventBackupPrefix})
        for (const QString &name : QDir(from + "/backups")
                                       .entryList({QString(prefix) + "*.json"}, QDir::Files))
            QFile::copy(from + "/backups/" + name, to + "/backups/" + name);

    dataDirOverride() = to;
    QSettings().setValue("dataDir", to);
    // The new folder does exist (it comes from the picker), so writing is allowed
    // again, and the birthdays lock belonged to the folder left behind. The save()
    // below writes notes.json and its siblings at the destination.
    m_available = true;
    m_birthdaysReadable = true;
    m_eventsReadable = true;
    save();
}

/// The other way of changing folder: keep the notes already there instead of
/// bringing these, without writing there until it has been read.
void Store::adoptDataDir(const QString &raw) {
    const QString to = trimmedDir(raw);
    if (to.isEmpty() || to == appDataDir()) return;

    save();   // what was in memory stays saved where it was
    qDeleteAll(m_notes);
    m_notes.clear();
    qDeleteAll(m_birthdays);
    m_birthdays.clear();
    qDeleteAll(m_timers);
    m_timers.clear();
    qDeleteAll(m_events);
    m_events.clear();
    qDeleteAll(m_categories);
    m_categories.clear();
    qDeleteAll(m_areas);
    m_areas.clear();
    m_prefs = Prefs{};
    // Other notes, other history: these tombstones do not belong there.
    m_deleted.clear();
    m_orderUpdatedMs = 0;

    dataDirOverride() = to;
    QSettings().setValue("dataDir", to);
    load();
    emit reloaded();
}

void Store::resetSnapshots() {
    m_snap.clear();
    for (Note *n : m_notes) m_snap[keyOf(n)] = snapshotOf(n);
    for (Birthday *b : m_birthdays) m_snap[keyOf(b)] = snapshotOf(b);
    for (Event *e : m_events) m_snap[keyOf(e)] = snapshotOf(e);
    for (Event::Category *c : m_categories) m_snap[keyOf(c)] = snapshotOf(c);
    for (Timer *t : m_timers) m_snap[keyOf(t)] = snapshotOf(t);
    for (Area *a : m_areas) m_snap[keyOf(a)] = snapshotOf(a);
    m_orderSnap.clear();
    for (Note *n : m_notes) m_orderSnap << n->id;
}

void Store::stampChanges() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QSet<QString> alive;
    auto stamp = [&](auto *item) {
        const QString key = keyOf(item);
        alive.insert(key);
        const QByteArray snap = snapshotOf(item);
        auto it = m_snap.find(key);
        if (it != m_snap.end() && *it == snap) return;
        item->updatedMs = now;
        m_snap[key] = snap;
        m_deleted.remove(key);   // it exists again: drop the tombstone
    };
    for (Note *n : m_notes) stamp(n);
    // What is missing from an unreadable file was never loaded, not deleted.
    if (m_birthdaysReadable)
        for (Birthday *b : m_birthdays) stamp(b);
    if (m_eventsReadable) {
        for (Event *e : m_events) stamp(e);
        for (Event::Category *c : m_categories) stamp(c);
    }
    for (Timer *t : m_timers) stamp(t);
    for (Area *a : m_areas) stamp(a);

    for (auto it = m_snap.begin(); it != m_snap.end();) {
        if (alive.contains(it.key())) {
            ++it;
            continue;
        }
        m_deleted[it.key()] = now;
        it = m_snap.erase(it);
    }

    QStringList order;
    for (Note *n : m_notes) order << n->id;
    if (order != m_orderSnap) {
        m_orderSnap = order;
        m_orderUpdatedMs = now;
    }

    for (auto it = m_deleted.begin(); it != m_deleted.end();) {
        if (now - it.value() > kTombstoneMs) it = m_deleted.erase(it);
        else ++it;
    }
}

namespace {

/// Merges a list with its remote twin.
/// @return Whether anything changed here; reports every element whose winning
///         version came from outside.
template <typename T>
bool mergeList(QList<T *> &local, const QJsonArray &remote, QHash<QString, qint64> &deleted,
               bool prependNew, const std::function<void(T *)> &remoteWon) {
    bool changed = false;
    QHash<QString, T *> byId;
    for (T *item : local) byId.insert(item->id, item);

    QList<T *> added;
    for (const QJsonValue v : remote) {
        T *r = T::fromJson(v.toObject());
        // Deleted somewhere after its last edit: it does not come back.
        if (deleted.contains(keyOf(r)) && deleted.value(keyOf(r)) >= r->updatedMs) {
            delete r;
            continue;
        }
        T *l = byId.value(r->id);
        if (!l) {
            added.append(r);
            byId.insert(r->id, r);
            remoteWon(r);
            changed = true;
        } else if (r->updatedMs > l->updatedMs) {
            adopt(l, r);
            delete r;
            remoteWon(l);
            changed = true;
        } else {
            delete r;
        }
    }
    // New remote elements go on top, like any new note.
    if (prependNew)
        for (int i = int(added.size()) - 1; i >= 0; --i) local.prepend(added.at(i));
    else
        local += added;
    return changed;
}

/// Deletes here what another machine deleted after the last local edit.
template <typename T>
bool applyTombstones(QList<T *> &local, QHash<QString, qint64> &deleted,
                     const std::function<void(T *)> &beforeDelete = nullptr) {
    bool changed = false;
    for (int i = int(local.size()) - 1; i >= 0; --i) {
        T *item = local.at(i);
        const QString key = keyOf(item);
        if (!deleted.contains(key)) continue;
        if (deleted.value(key) < item->updatedMs) {
            // Edited here after the remote deletion: the edit wins and the tombstone is
            // dropped so it stops spreading.
            deleted.remove(key);
            continue;
        }
        if (beforeDelete) beforeDelete(item);
        local.removeAt(i);
        delete item;
        changed = true;
    }
    return changed;
}

}  // namespace

Store::MergeResult Store::mergeRemote(const QJsonObject &notesRoot,
                                      const QJsonObject &birthdaysRoot,
                                      const QJsonObject &eventsRoot) {
    MergeResult result;
    if (!m_available) return result;   // never merge over what was not read

    // Local changes stamped before comparing anything.
    stampChanges();

    if (notesRoot.contains("deleted")) {
        const QJsonObject deleted = notesRoot["deleted"].toObject();
        for (auto it = deleted.begin(); it != deleted.end(); ++it)
            m_deleted[it.key()] = qMax(m_deleted.value(it.key()), qint64(it.value().toDouble()));
    }

    auto pullNote = [&result](Note *n) {
        if (!n->audio.isEmpty()) result.pull.insert("audio/" + n->audio);
        for (const QString &image : n->images) result.pull.insert("images/" + image);
    };
    auto nothing = [](auto *) {};

    if (notesRoot.contains("notes")) {
        const QJsonArray remoteNotes = notesRoot["notes"].toArray();
        result.changed |= mergeList<Note>(m_notes, remoteNotes, m_deleted, true, pullNote);
        result.changed |= mergeList<Timer>(m_timers, notesRoot["timers"].toArray(), m_deleted,
                                           true, nothing);
        result.changed |= mergeList<Area>(m_areas, notesRoot["areas"].toArray(), m_deleted,
                                          false, nothing);

        // The order of whoever changed it last. Ids it does not know (just created
        // here) go on top, in local order.
        const qint64 remoteOrder = qint64(notesRoot["orderUpdated"].toDouble());
        if (remoteOrder > m_orderUpdatedMs) {
            QStringList ids;
            for (const QJsonValue v : remoteNotes) ids << v.toObject()["id"].toString();
            QList<Note *> ordered, fresh;
            QHash<QString, Note *> byId;
            for (Note *n : m_notes) {
                byId.insert(n->id, n);
                if (!ids.contains(n->id)) fresh.append(n);
            }
            ordered = fresh;
            for (const QString &id : ids)
                if (Note *n = byId.value(id)) ordered.append(n);
            if (ordered != m_notes) result.changed = true;
            m_notes = ordered;
            m_orderUpdatedMs = remoteOrder;
        }
    }
    if (m_birthdaysReadable && birthdaysRoot.contains("birthdays"))
        result.changed |= mergeList<Birthday>(m_birthdays, birthdaysRoot["birthdays"].toArray(),
                                              m_deleted, false, nothing);
    if (m_eventsReadable && eventsRoot.contains("events")) {
        result.changed |= mergeList<Event>(m_events, eventsRoot["events"].toArray(), m_deleted,
                                           false, nothing);
        result.changed |= mergeList<Event::Category>(
            m_categories, eventsRoot["categories"].toArray(), m_deleted, false, nothing);
    }

    // Notes deleted elsewhere take their attachments with them here too.
    result.changed |= applyTombstones<Note>(m_notes, m_deleted, [](Note *n) {
        if (!n->audio.isEmpty()) QFile::remove(n->audioPath());
        for (const QString &image : n->images) QFile::remove(Note::imagePath(image));
    });
    result.changed |= applyTombstones<Timer>(m_timers, m_deleted);
    result.changed |= applyTombstones<Area>(m_areas, m_deleted);
    sortAreas();
    if (m_birthdaysReadable) result.changed |= applyTombstones<Birthday>(m_birthdays, m_deleted);
    if (m_eventsReadable) {
        result.changed |= applyTombstones<Event>(m_events, m_deleted);
        result.changed |= applyTombstones<Event::Category>(m_categories, m_deleted);
    }

    // What just arrived is not a local change: stamping it would give it "now"
    // and it would beat the real version next time.
    resetSnapshots();
    // After the snapshot: if no area is left, the default one created now is a
    // local change, and save() stamps and uploads it.
    ensureAreas();
    save();
    if (result.changed) emit merged();
    return result;
}

QStringList Store::attachments() const {
    QStringList out;
    for (const Note *n : m_notes) {
        if (!n->audio.isEmpty()) out << "audio/" + n->audio;
        for (const QString &image : n->images) out << "images/" + image;
    }
    return out;
}

namespace {

/// Sorted by id: order means nothing in these lists, and two machines with
/// the same data must produce exactly the same bytes.
template <typename T>
QJsonArray sortedById(const QList<T *> &items) {
    QList<const T *> sorted(items.begin(), items.end());
    std::sort(sorted.begin(), sorted.end(),
              [](const T *a, const T *b) { return a->id < b->id; });
    QJsonArray out;
    for (const T *item : sorted) out.append(item->toJson());
    return out;
}

}  // namespace

QByteArray Store::syncPayload(const QString &name) const {
    QJsonObject root;
    if (name == "notes.json") {
        QJsonArray notes;   // this one keeps its order: the note order is shared
        for (const Note *n : m_notes) notes.append(n->toJson());
        root["notes"] = notes;
        root["timers"] = sortedById(m_timers);
        root["areas"] = sortedById(m_areas);
        root["orderUpdated"] = double(m_orderUpdatedMs);
        QJsonObject deleted;
        for (auto it = m_deleted.cbegin(); it != m_deleted.cend(); ++it)
            deleted[it.key()] = double(it.value());
        root["deleted"] = deleted;
    } else if (name == "birthdays.json") {
        root["birthdays"] = sortedById(m_birthdays);
    } else if (name == "events.json") {
        root["events"] = sortedById(m_events);
        root["categories"] = sortedById(m_categories);
    } else {
        return {};
    }
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

QStringList Store::syncableFiles() const {
    QStringList out{"notes.json"};
    if (m_birthdaysReadable) out << "birthdays.json";
    if (m_eventsReadable) out << "events.json";
    return out;
}
