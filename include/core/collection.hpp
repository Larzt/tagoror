#pragma once

#include <QColor>
#include <QDate>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUuid>

/// @file
/// Colecciones (1.3.0): lists of things that move through states — books
/// that are pending, being read and read. Kept in collections.json, next to
/// notes.json, with its own backups and the same sync as tracking.json.
///
/// The model is generic: a collection is made from a template (Libros today;
/// Películas, Series, Juegos, Discos and En blanco share the shape) that
/// names its states and switches its fields on. What is specific to books —
/// the author sorted by surname, pages read — is just fields a template turns
/// on and the words it uses for them.

/// One tab of a collection. At most one is "in progress" (shown as a card with
/// progress); the last one is the finished state, which records the date.
struct CollectionState {
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString name;
    bool inProgress = false;

    QJsonObject toJson() const {
        QJsonObject o;
        o["id"] = id;
        o["name"] = name;
        if (inProgress) o["inProgress"] = true;
        return o;
    }
    static CollectionState fromJson(const QJsonObject &o) {
        CollectionState s;
        s.id = o["id"].toString(s.id);
        s.name = o["name"].toString();
        s.inProgress = o["inProgress"].toBool();
        return s;
    }
};

/// A field of the user's own (the design's "+ Campo propio"): free text.
struct CollectionField {
    QString key = "f-" + QUuid::createUuid().toString(QUuid::Id128).left(8);
    QString name;

    QJsonObject toJson() const { return {{"key", key}, {"name", name}}; }
    static CollectionField fromJson(const QJsonObject &o) {
        CollectionField f;
        f.key = o["key"].toString(f.key);
        f.name = o["name"].toString();
        return f;
    }
};

struct Collection {
    /// Templates. Only Books has its own icon and words in 1.3.0; the rest
    /// work with the generic ones.
    enum Template { Books, Movies, Series, Games, Records, Blank };
    static constexpr int kTemplates = 6;

    /// Built-in fields a template can turn on. Stored by key.
    static constexpr const char *kAuthor = "author";
    static constexpr const char *kYear = "year";
    static constexpr const char *kProgress = "progress";
    static constexpr const char *kPriority = "priority";
    static constexpr const char *kRating = "rating";
    static constexpr const char *kIsbn = "isbn";
    static constexpr const char *kSynopsis = "synopsis";   ///< 1.3.3
    static QStringList builtinFields() {
        return {kAuthor, kYear, kProgress, kPriority, kRating, kIsbn, kSynopsis};
    }

    /// Sort keys. In the finished tab "priority" means the rating and "added"
    /// the day it was finished.
    static constexpr const char *kByPriority = "priority";
    static constexpr const char *kByYear = "year";
    static constexpr const char *kByAuthor = "author";
    static constexpr const char *kByTitle = "title";
    static constexpr const char *kByAdded = "added";
    static QStringList sortKeys() { return {kByPriority, kByYear, kByAuthor, kByTitle, kByAdded}; }
    /// The direction each key starts in: high priority first, the rest oldest
    /// or A first, and the newest additions first.
    static bool defaultAscending(const QString &key) {
        return key != kByPriority && key != kByAdded;
    }

    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString name;
    Template kind = Books;
    QList<CollectionState> states;
    QStringList fields;                ///< Built-in keys turned on.
    QList<CollectionField> custom;     ///< The user's own, always on.
    /// Built-in fields this collection has been offered: one added later
    /// (Sinopsis in 1.3.3) turns on in collections whose template uses it,
    /// once, and stays off if turned off afterwards.
    QStringList known;
    QString sortKey = kByPriority;
    bool sortAscending = false;
    /// Yearly goal of finished items (the wide mode's "RETO"); 0: none.
    int goal = 0;
    int pos = 0;                       ///< Order among collections.
    qint64 updatedMs = 0;

    /// A new collection as @p kind makes it, with its states and fields. The
    /// state names are Spanish and go through L() when shown, like the
    /// built-in planner categories, so a collection made in one language reads
    /// in the other until renamed.
    static Collection *fromTemplate(Template kind, const QString &name);
    static QString templateKey(Template t);
    static Template templateFromKey(const QString &key);
    /// "Libros", "Películas"…: Spanish, translated when shown.
    static const char *templateName(Template t);
    /// paintIcon() kind for a template.
    static QString templateIcon(Template t);

    bool has(const QString &field) const { return fields.contains(field); }
    int stateIndex(const QString &stateId) const {
        for (int i = 0; i < states.size(); ++i)
            if (states.at(i).id == stateId) return i;
        return -1;
    }
    const CollectionState *state(const QString &stateId) const {
        const int i = stateIndex(stateId);
        return i >= 0 ? &states.at(i) : nullptr;
    }
    QString firstState() const { return states.isEmpty() ? QString() : states.first().id; }
    /// The finished one: the last.
    QString finalState() const { return states.isEmpty() ? QString() : states.last().id; }
    QString inProgressState() const {
        for (const CollectionState &s : states)
            if (s.inProgress) return s.id;
        return QString();
    }
    bool isFinal(const QString &stateId) const {
        return !stateId.isEmpty() && stateId == finalState();
    }
    bool isInProgress(const QString &stateId) const {
        const CollectionState *s = state(stateId);
        return s && s->inProgress;
    }

    /// The template's words: "Autor" / "Director"…, "páginas" / "episodios"…
    /// Spanish, translated when shown.
    const char *authorLabel() const;
    const char *unitName() const;    ///< "páginas"
    const char *unitShort() const;   ///< "p."
    const char *itemName() const;    ///< "libro"
    QString icon() const { return templateIcon(kind); }

    QJsonObject toJson() const;
    static Collection *fromJson(const QJsonObject &o);
};

/// One thing in a collection: a book. Fields a collection does not use are
/// simply left empty.
struct CollectionItem {
    /// Muted cover colours, readable under white text. Stored by index, like
    /// Project::paletteColor.
    static constexpr int kCovers = 12;
    static QColor coverColor(int index) {
        static const char *const palette[kCovers] = {
            "#3f6b73", "#7a6a3a", "#7a3f4a", "#3d5a80", "#6b4f7a", "#4f6d5a",
            "#3f5f7a", "#8a5a44", "#55606e", "#5a4a6e", "#3d6a5a", "#6e4a3a"};
        return QColor(palette[qBound(0, index, kCovers - 1)]);
    }

    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString collection;       ///< Collection id.
    QString title;
    QString author;
    /// What the author is sorted by. Empty: the author's last word, which fails
    /// with "Liu Cixin" or "García Márquez", hence editable.
    QString authorSortKey;
    int year = 0;             ///< Of publication; 0: unknown.
    int total = 0;            ///< Pages (episodes…); 0: unknown.
    int progress = 0;         ///< Current page.
    QString state;            ///< CollectionState id.
    int priority = 2;         ///< 1 low, 2 medium, 3 high.
    /// Hand-made order inside the queue, among equal priorities. Lower first.
    double queuePos = 0;
    int rating = 0;           ///< 0–5; 0: not rated.
    QStringList tags;
    int color = 0;            ///< coverColor() index: the cover without an image.
    /// 1.3.4: a picture for the cover, a file in images/ (as the notes' are),
    /// synced like them. Empty: the colour.
    QString cover;
    qint64 addedMs = 0;
    qint64 startedMs = 0;     ///< Entered the in-progress state.
    qint64 finishedMs = 0;    ///< Entered the final state.
    QString isbn;
    QString synopsis;         ///< A few lines on what it is about.
    QString noteId;           ///< Linked Markdown note; empty: none.
    QJsonObject extra;        ///< Custom fields, by CollectionField::key.
    qint64 updatedMs = 0;

    /// The cover image's file, or empty without one.
    QString coverPath() const;
    /// authorSortKey, or the author's last word.
    QString sortAuthor() const;
    /// 0–100, or -1 without a total.
    int percent() const {
        return total > 0 ? qBound(0, qRound(progress * 100.0 / total), 100) : -1;
    }

    QJsonObject toJson() const;
    static CollectionItem *fromJson(const QJsonObject &o);
};

/// Ordering, counting and dates, shared by the page and its wide mode. No
/// widgets: `core` does not know about `ui`.
namespace Coll {

/// Items of @p c in @p stateId (empty: all), in @p c's chosen order. In the
/// finished state "priority" sorts by rating and "added" by the day finished.
QList<CollectionItem *> sorted(const QList<CollectionItem *> &items, const Collection &c,
                               const QString &stateId);
/// The queue as Empezar reads it: highest priority first, then by hand.
QList<CollectionItem *> queue(const QList<CollectionItem *> &items, const Collection &c);
/// Copies the image at @p source into images/ as @p item's cover: scaled down
/// to at most kCoverHeight tall and saved as JPEG, so a 4000 px photo does not
/// travel with every sync. Returns the new file's name, or empty if
/// @p source is not an image that can be read.
constexpr int kCoverHeight = 720;
QString importCover(const QString &source, const QString &itemId);
/// Deletes @p name from images/ (a cover replaced or whose item is gone).
void removeCover(const QString &name);

/// Length bands of the page filter (1.3.3): up to 250, 251–500, over 500.
/// Only for things counted in pages, that is, books.
enum Length { AnyLength, Short, Medium, Long };
constexpr int kShortUpTo = 250;
constexpr int kMediumUpTo = 500;
/// The band of @p item; AnyLength when its length is unknown.
Length lengthOf(const CollectionItem *item);
/// Whether @p c counts in pages, so its items can be filtered by length.
bool filtersByLength(const Collection &c);
/// 1.3.3: a field of the user's own called "Sinopsis" (what there was before
/// the built-in one) becomes the built-in field: its text moves to
/// CollectionItem::synopsis where that is empty, and the own field goes.
/// Returns whether anything changed.
bool adoptSynopsisField(Collection &c, const QList<CollectionItem *> &items);
/// Moves @p item to @p index inside @p order (the queue as shown), giving it a
/// queuePos between its new neighbours and, if it lands among another
/// priority, that priority.
void moveInQueue(QList<CollectionItem *> order, CollectionItem *item, int index);
/// Puts @p item in @p stateId, recording when it started or finished.
void setState(CollectionItem *item, const Collection &c, const QString &stateId);
int countIn(const QList<CollectionItem *> &items, const QString &collectionId,
            const QString &stateId = QString());

/// "28 oct", or "28 oct 2025" from another year.
QString shortDate(qint64 ms);
/// "1.234" with the interface language's grouping.
QString number(int n);

}  // namespace Coll
