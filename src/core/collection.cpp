#include "core/collection.hpp"

#include "core/lang.hpp"
#include "core/paths.hpp"

#include <QCollator>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <algorithm>

namespace {

struct TemplateInfo {
    const char *key;
    const char *name;
    const char *icon;
    const char *author;
    const char *unit;
    const char *unitShort;
    const char *item;
};

const TemplateInfo &info(Collection::Template t) {
    static const TemplateInfo table[Collection::kTemplates] = {
        {"books", "Libros", "books", "Autor", "páginas", "p.", "libro"},
        // No icon of their own yet (1.3.0): the generic one.
        {"movies", "Películas", "checkdots", "Dirección", "minutos", "min", "película"},
        {"series", "Series", "checkdots", "Creación", "episodios", "ep.", "serie"},
        {"games", "Juegos", "checkdots", "Estudio", "horas", "h", "juego"},
        {"records", "Discos", "checkdots", "Artista", "pistas", "pistas", "disco"},
        {"blank", "En blanco", "checkdots", "Autor", "unidades", "u.", "elemento"},
    };
    return table[qBound(0, int(t), Collection::kTemplates - 1)];
}

CollectionState makeState(const char *name, bool inProgress = false) {
    CollectionState s;
    s.name = QString::fromUtf8(name);
    s.inProgress = inProgress;
    return s;
}

}  // namespace

// ---------------------------------------------------------------------------
// Collection

QString Collection::templateKey(Template t) { return QString::fromLatin1(info(t).key); }

Collection::Template Collection::templateFromKey(const QString &key) {
    for (int i = 0; i < kTemplates; ++i)
        if (key == QLatin1String(info(Template(i)).key)) return Template(i);
    return Books;
}

const char *Collection::templateName(Template t) { return info(t).name; }
QString Collection::templateIcon(Template t) { return QString::fromLatin1(info(t).icon); }
const char *Collection::authorLabel() const { return info(kind).author; }
const char *Collection::unitName() const { return info(kind).unit; }
const char *Collection::unitShort() const { return info(kind).unitShort; }
const char *Collection::itemName() const { return info(kind).item; }

Collection *Collection::fromTemplate(Template kind, const QString &name) {
    auto *c = new Collection;
    c->kind = kind;
    c->name = name;
    switch (kind) {
        case Books:
            c->states = {makeState("Pendiente"), makeState("Leyendo", true), makeState("Leído")};
            c->fields = {kAuthor, kYear, kProgress, kPriority, kRating, kSynopsis};
            break;
        case Movies:
            c->states = {makeState("Por ver"), makeState("Vista")};
            c->fields = {kAuthor, kYear, kPriority, kRating};
            break;
        case Series:
            c->states = {makeState("Por ver"), makeState("Viendo", true), makeState("Vista")};
            c->fields = {kYear, kProgress, kPriority, kRating};
            break;
        case Games:
            c->states = {makeState("Pendiente"), makeState("Jugando", true), makeState("Terminado")};
            c->fields = {kAuthor, kYear, kPriority, kRating};
            break;
        case Records:
            c->states = {makeState("Por escuchar"), makeState("Escuchado")};
            c->fields = {kAuthor, kYear, kRating};
            break;
        case Blank:
            c->states = {makeState("Pendiente"), makeState("En curso", true), makeState("Hecho")};
            c->fields = {kPriority};
            break;
    }
    c->known = builtinFields();
    c->sortKey = kByPriority;
    c->sortAscending = defaultAscending(kByPriority);
    return c;
}

QJsonObject Collection::toJson() const {
    QJsonObject o;
    o["id"] = id;
    o["name"] = name;
    o["template"] = templateKey(kind);
    QJsonArray st;
    for (const CollectionState &s : states) st.append(s.toJson());
    o["states"] = st;
    o["fields"] = QJsonArray::fromStringList(fields);
    o["known"] = QJsonArray::fromStringList(known);
    QJsonArray cu;
    for (const CollectionField &f : custom) cu.append(f.toJson());
    o["custom"] = cu;
    o["sort"] = sortKey;
    o["ascending"] = sortAscending;
    o["goal"] = goal;
    o["pos"] = pos;
    o["updated"] = double(updatedMs);
    return o;
}

Collection *Collection::fromJson(const QJsonObject &o) {
    auto *c = new Collection;
    c->id = o["id"].toString(c->id);
    c->name = o["name"].toString();
    c->kind = templateFromKey(o["template"].toString());
    for (const QJsonValue v : o["states"].toArray()) c->states.append(CollectionState::fromJson(v.toObject()));
    // A collection always has somewhere to put things.
    if (c->states.isEmpty()) {
        Collection *t = fromTemplate(c->kind, QString());
        c->states = t->states;
        delete t;
    }
    for (const QJsonValue v : o["fields"].toArray()) c->fields << v.toString();
    // Built-in fields added since this collection was saved: on if its
    // template has them. Without "known" it is from 1.3.0, which knew these.
    if (o.contains("known")) {
        for (const QJsonValue v : o["known"].toArray()) c->known << v.toString();
    } else {
        c->known = {kAuthor, kYear, kProgress, kPriority, kRating, kIsbn};
    }
    Collection *t = fromTemplate(c->kind, QString());
    for (const QString &f : builtinFields())
        if (!c->known.contains(f) && t->fields.contains(f) && !c->fields.contains(f)) c->fields << f;
    delete t;
    c->known = builtinFields();
    for (const QJsonValue v : o["custom"].toArray()) c->custom.append(CollectionField::fromJson(v.toObject()));
    c->sortKey = o["sort"].toString(kByPriority);
    if (!sortKeys().contains(c->sortKey)) c->sortKey = kByPriority;
    c->sortAscending = o.contains("ascending") ? o["ascending"].toBool() : defaultAscending(c->sortKey);
    c->goal = qMax(0, o["goal"].toInt());
    c->pos = o["pos"].toInt();
    c->updatedMs = qint64(o["updated"].toDouble());
    return c;
}

// ---------------------------------------------------------------------------
// CollectionItem

QString CollectionItem::coverPath() const {
    return cover.isEmpty() ? QString() : imageDir() + "/" + cover;
}

QString CollectionItem::sortAuthor() const {
    if (!authorSortKey.trimmed().isEmpty()) return authorSortKey.trimmed();
    const QStringList words = author.simplified().split(' ', Qt::SkipEmptyParts);
    return words.isEmpty() ? QString() : words.last();
}

QJsonObject CollectionItem::toJson() const {
    QJsonObject o;
    o["id"] = id;
    o["collection"] = collection;
    o["title"] = title;
    o["author"] = author;
    o["authorKey"] = authorSortKey;
    o["year"] = year;
    o["total"] = total;
    o["progress"] = progress;
    o["state"] = state;
    o["priority"] = priority;
    o["queuePos"] = queuePos;
    o["rating"] = rating;
    o["tags"] = QJsonArray::fromStringList(tags);
    o["color"] = color;
    o["cover"] = cover;
    o["added"] = double(addedMs);
    o["started"] = double(startedMs);
    o["finished"] = double(finishedMs);
    o["isbn"] = isbn;
    o["synopsis"] = synopsis;
    o["note"] = noteId;
    o["extra"] = extra;
    o["updated"] = double(updatedMs);
    return o;
}

CollectionItem *CollectionItem::fromJson(const QJsonObject &o) {
    auto *i = new CollectionItem;
    i->id = o["id"].toString(i->id);
    i->collection = o["collection"].toString();
    i->title = o["title"].toString();
    i->author = o["author"].toString();
    i->authorSortKey = o["authorKey"].toString();
    i->year = o["year"].toInt();
    i->total = qMax(0, o["total"].toInt());
    i->progress = qMax(0, o["progress"].toInt());
    i->state = o["state"].toString();
    i->priority = qBound(1, o["priority"].toInt(2), 3);
    i->queuePos = o["queuePos"].toDouble();
    i->rating = qBound(0, o["rating"].toInt(), 5);
    for (const QJsonValue v : o["tags"].toArray()) i->tags << v.toString();
    i->color = qBound(0, o["color"].toInt(), kCovers - 1);
    // A bare file name: nothing from a synced file reaches outside images/.
    i->cover = o["cover"].toString();
    if (i->cover.contains('/') || i->cover.contains('\\') || i->cover.startsWith('.')) i->cover.clear();
    i->addedMs = qint64(o["added"].toDouble());
    i->startedMs = qint64(o["started"].toDouble());
    i->finishedMs = qint64(o["finished"].toDouble());
    i->isbn = o["isbn"].toString();
    i->synopsis = o["synopsis"].toString();
    i->noteId = o["note"].toString();
    i->extra = o["extra"].toObject();
    i->updatedMs = qint64(o["updated"].toDouble());
    return i;
}

// ---------------------------------------------------------------------------
// Coll

namespace Coll {

namespace {

int sign(qint64 v) { return v < 0 ? -1 : v > 0 ? 1 : 0; }

/// Ties keep the hand-made order, then the oldest addition first, whatever
/// the direction: flipping "Alta primero" should not shuffle a priority's
/// books among themselves.
int tieBreak(const CollectionItem *a, const CollectionItem *b) {
    if (a->queuePos != b->queuePos) return a->queuePos < b->queuePos ? -1 : 1;
    if (a->addedMs != b->addedMs) return a->addedMs < b->addedMs ? -1 : 1;
    return a->id < b->id ? -1 : a->id > b->id ? 1 : 0;
}

}  // namespace

QList<CollectionItem *> sorted(const QList<CollectionItem *> &items, const Collection &c,
                               const QString &stateId) {
    QList<CollectionItem *> out;
    for (CollectionItem *i : items)
        if (i->collection == c.id && (stateId.isEmpty() || i->state == stateId)) out << i;

    const bool finished = c.isFinal(stateId);
    const QString key = c.sortKey;
    const bool asc = c.sortAscending;
    QCollator collator(Lang::locale());
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    collator.setNumericMode(true);

    const auto primary = [&](const CollectionItem *a, const CollectionItem *b) -> int {
        if (key == Collection::kByPriority)
            return finished ? a->rating - b->rating : a->priority - b->priority;
        if (key == Collection::kByYear) {
            // Unknown years go last either way.
            if ((a->year == 0) != (b->year == 0)) return 0;
            return a->year - b->year;
        }
        if (key == Collection::kByAuthor) return collator.compare(a->sortAuthor(), b->sortAuthor());
        if (key == Collection::kByTitle) return collator.compare(a->title, b->title);
        return sign(finished ? a->finishedMs - b->finishedMs : a->addedMs - b->addedMs);
    };
    std::stable_sort(out.begin(), out.end(), [&](const CollectionItem *a, const CollectionItem *b) {
        if (key == Collection::kByYear && (a->year == 0) != (b->year == 0)) return b->year == 0;
        int r = primary(a, b);
        if (r != 0) return asc ? r < 0 : r > 0;
        if (key == Collection::kByAuthor) {
            r = collator.compare(a->title, b->title);
            if (r != 0) return r < 0;
        }
        return tieBreak(a, b) < 0;
    });
    return out;
}

QList<CollectionItem *> queue(const QList<CollectionItem *> &items, const Collection &c) {
    QList<CollectionItem *> out;
    const QString first = c.firstState();
    for (CollectionItem *i : items)
        if (i->collection == c.id && i->state == first) out << i;
    std::stable_sort(out.begin(), out.end(), [](const CollectionItem *a, const CollectionItem *b) {
        if (a->priority != b->priority) return a->priority > b->priority;
        return tieBreak(a, b) < 0;
    });
    return out;
}

void moveInQueue(QList<CollectionItem *> order, CollectionItem *item, int index) {
    order.removeOne(item);
    index = qBound(0, index, int(order.size()));
    order.insert(index, item);
    // Dropped among another priority, it takes that one: the queue is ordered by
    // priority first, and a book dragged above the high ones is a high one now.
    const CollectionItem *prev = index > 0 ? order.at(index - 1) : nullptr;
    const CollectionItem *next = index + 1 < order.size() ? order.at(index + 1) : nullptr;
    if (prev && next && prev->priority == next->priority) item->priority = prev->priority;
    else if (!prev && next) item->priority = qMax(item->priority, next->priority);
    else if (prev && !next) item->priority = qMin(item->priority, prev->priority);
    else if (prev && next) item->priority = qBound(next->priority, item->priority, prev->priority);
    // The whole queue renumbered: a handful of books, and no fractions to drift.
    for (int i = 0; i < order.size(); ++i) order.at(i)->queuePos = i;
}

void setState(CollectionItem *item, const Collection &c, const QString &stateId) {
    if (item->state == stateId || c.stateIndex(stateId) < 0) return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const bool wasFinal = c.isFinal(item->state);
    item->state = stateId;
    if (c.isInProgress(stateId) && item->startedMs == 0) item->startedMs = now;
    if (c.isFinal(stateId)) {
        item->finishedMs = now;
        if (item->total > 0 && c.has(Collection::kProgress)) item->progress = item->total;
    } else if (wasFinal) {
        item->finishedMs = 0;
    }
}

QString importCover(const QString &source, const QString &itemId) {
    QImageReader reader(source);
    reader.setAutoTransform(true);   // a phone photo stands the right way up
    QImage image = reader.read();
    if (image.isNull()) return QString();
    if (image.height() > kCoverHeight)
        image = image.scaledToHeight(kCoverHeight, Qt::SmoothTransformation);
    // JPEG has no transparency: a PNG with it goes over white, not black.
    if (image.hasAlphaChannel()) {
        QImage flat(image.size(), QImage::Format_RGB32);
        flat.fill(Qt::white);
        QPainter p(&flat);
        p.drawImage(0, 0, image);
        p.end();
        image = flat;
    }
    // The item and a timestamp: a new cover never overwrites the one a synced
    // machine may still be showing.
    const QString name = QString("cover-%1-%2.jpg").arg(itemId).arg(QDateTime::currentMSecsSinceEpoch());
    if (!image.save(imageDir() + "/" + name, "JPG", 88)) return QString();
    return name;
}

void removeCover(const QString &name) {
    if (!name.isEmpty() && !name.contains('/') && !name.contains('\\')) QFile::remove(imageDir() + "/" + name);
}

Length lengthOf(const CollectionItem *item) {
    if (item->total <= 0) return AnyLength;
    if (item->total <= kShortUpTo) return Short;
    if (item->total <= kMediumUpTo) return Medium;
    return Long;
}

bool filtersByLength(const Collection &c) {
    return c.kind == Collection::Books && c.has(Collection::kProgress);
}

bool adoptSynopsisField(Collection &c, const QList<CollectionItem *> &items) {
    if (!c.has(Collection::kSynopsis)) return false;
    static const QStringList names{"sinopsis", "sipnosis", "synopsis"};
    for (int f = 0; f < c.custom.size(); ++f) {
        if (!names.contains(c.custom.at(f).name.trimmed().toLower())) continue;
        const QString key = c.custom.at(f).key;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        for (CollectionItem *i : items) {
            if (i->collection != c.id || !i->extra.contains(key)) continue;
            if (i->synopsis.isEmpty()) i->synopsis = i->extra.value(key).toString().trimmed();
            i->extra.remove(key);
            i->updatedMs = now;
        }
        c.custom.removeAt(f);
        c.updatedMs = now;
        return true;
    }
    return false;
}

int countIn(const QList<CollectionItem *> &items, const QString &collectionId,
            const QString &stateId) {
    int n = 0;
    for (const CollectionItem *i : items)
        if (i->collection == collectionId && (stateId.isEmpty() || i->state == stateId)) ++n;
    return n;
}

QString shortDate(qint64 ms) {
    if (ms <= 0) return QString();
    const QDate d = QDateTime::fromMSecsSinceEpoch(ms).date();
    QString s = Lang::locale().toString(d, d.year() == QDate::currentDate().year() ? "d MMM" : "d MMM yyyy");
    s.remove('.');
    return s;
}

QString number(int n) { return Lang::locale().toString(n); }

}  // namespace Coll
