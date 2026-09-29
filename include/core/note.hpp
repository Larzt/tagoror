#pragma once

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QUuid>

#include "core/lang.hpp"
#include "core/paths.hpp"

struct CheckItem {
    QString text;
    bool done = false;
};

/// A link attached to a note. @ref label is optional; without it the address
/// itself is shown. Any note type can carry links.
struct Link {
    QString url;
    QString label;
};

struct Note {
    enum Type { Text, Check, Reminder, Voice };
    /// How often a reminder comes back. A Weekly or Yearly reminder appears on
    /// every matching date in the planner, not only on the next one.
    enum Repeat { Once, Weekly, Yearly };

    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    Type type = Text;
    QString title;
    QString body;
    QString due;              ///< Reminder only.
    QList<CheckItem> items;   ///< Check only.
    QList<Link> links;        ///< Any note type.
    QList<QString> images;    ///< File names inside imageDir().
    /// Whether the image strip is folded. Per note, not per panel.
    bool imagesHidden = false;
    /// Area the note belongs to (see area.hpp). Empty means Area::kDefaultId, so
    /// notes from before areas keep their bytes and sync does not see them as
    /// edited. Always assign through Store::setNoteArea().
    QString area;
    QString audio;            ///< Voice only: file name inside audioDir().
    qint64 durationMs = 0;    ///< Voice only.
    QList<int> peaks;         ///< Voice only: waveform, 0..100, normalised to the take's loudest point.
    /// Absolute peak of the take (0..100, -1 unknown). Kept apart from @ref peaks,
    /// which are normalised and so cannot tell whether the recording was silent.
    int level = -1;

    /// Reminder: @ref due is the label shown and dueAtMs the real instant. With
    /// dueAtMs == 0 the date is free text and never rings.
    qint64 dueAtMs = 0;
    Repeat repeat = Once;     ///< Reminder only, and only with dueAtMs.
    bool fired = false;       ///< Already rang and was dismissed: it does not ring again.
    bool ringing = false;     ///< Runtime only: ringing right now.
    /// Last change, for the Drive sync (newest version of each element wins).
    /// Set by Store::stampChanges() on save, never by the editor.
    qint64 updatedMs = 0;

    /// A peak below 2% of full scale is inaudible: almost always a muted
    /// microphone or the wrong input.
    bool isSilentTake() const { return type == Voice && level >= 0 && level < 2; }

    bool isDue(qint64 nowMs) const {
        return type == Reminder && dueAtMs > 0 && !fired && nowMs >= dueAtMs;
    }

    /// Only a reminder with a real instant is placed on the planner; free-text
    /// dates are just labels.
    bool isScheduled() const { return type == Reminder && dueAtMs > 0; }

    bool repeats() const { return isScheduled() && repeat != Once; }

    QDateTime dueAt() const { return QDateTime::fromMSecsSinceEpoch(dueAtMs); }

    /// Whether the reminder falls on @p day. A repeating one matches its pattern
    /// in both directions, dates before dueAtMs included, so it stays on past
    /// cells when browsing back. A 29 February only matches leap years.
    bool occursOn(const QDate &day) const {
        if (!isScheduled() || !day.isValid()) return false;
        const QDate base = dueAt().date();
        switch (repeat) {
            case Weekly: return day.dayOfWeek() == base.dayOfWeek();
            case Yearly: return day.day() == base.day() && day.month() == base.month();
            default:     return day == base;
        }
    }

    /// The instant for a given day: same time, other date.
    QDateTime occurrenceOn(const QDate &day) const {
        return QDateTime(day, dueAt().time());
    }

    /// The next turn after @p fromMs.
    ///
    /// Every turn is counted from the original date, never from the previous one:
    /// chaining addYears() over a 29 February slides it to the 28th for good. It
    /// jumps straight to the due turn (the app may have been closed for years)
    /// and then requires occursOn(), which is why a 29 February waits four years.
    qint64 nextOccurrenceAfter(qint64 fromMs) const {
        if (!repeats()) return dueAtMs;

        const QDateTime base = dueAt();
        const qint64 baseMs = base.toMSecsSinceEpoch();
        int step = 0;
        if (fromMs > baseMs) {
            constexpr qint64 week = 7LL * 24 * 3600 * 1000;
            // Deliberately underestimated: overshooting would skip a turn, falling
            // short only costs a couple of iterations.
            step = repeat == Weekly
                       ? int((fromMs - baseMs) / week)
                       : QDateTime::fromMSecsSinceEpoch(fromMs).date().year() - base.date().year();
        }

        // Enough to refine any estimate, including a 29 February's four years.
        for (int i = 0; i < 16; ++i, ++step) {
            const QDateTime when = repeat == Weekly ? base.addDays(7LL * step)
                                                    : base.addYears(step);
            if (when.toMSecsSinceEpoch() > fromMs && occursOn(when.date()))
                return when.toMSecsSinceEpoch();
        }
        return dueAtMs;
    }

    QString repeatLabel() const {
        switch (repeat) {
            case Weekly: return L("Cada semana");
            case Yearly: return L("Cada año");
            default:     return QString();
        }
    }

    static QString repeatToString(Repeat r) {
        switch (r) {
            case Weekly: return "weekly";
            case Yearly: return "yearly";
            default:     return "once";
        }
    }

    static Repeat repeatFromString(const QString &s) {
        if (s == "weekly") return Weekly;
        if (s == "yearly") return Yearly;
        return Once;
    }

    /// Absolute path of an attached image.
    static QString imagePath(const QString &name) { return imageDir() + "/" + name; }

    /// The date as shown. With a real instant it is re-derived in the current
    /// language (@ref due keeps the label as generated); free text is kept as is.
    QString dueLabel() const {
        if (!isScheduled()) return due;
        // A yearly one is shown without the year, which would always be the next.
        return Lang::locale().toString(dueAt(), repeat == Yearly ? "d MMM HH:mm"
                                                                : "ddd d MMM HH:mm");
    }
    QDate dueDate() const { return isScheduled() ? dueAt().date() : QDate(); }

    /// Absolute path of the voice take; empty if none was recorded.
    QString audioPath() const {
        return audio.isEmpty() ? QString() : audioDir() + "/" + audio;
    }

    QJsonObject toJson() const {
        QJsonObject o;
        o["id"] = id;
        o["type"] = typeToString(type);
        o["title"] = title;
        o["body"] = body;
        o["due"] = due;
        o["audio"] = audio;
        o["durationMs"] = durationMs;
        o["dueAtMs"] = dueAtMs;
        o["repeat"] = repeatToString(repeat);
        o["fired"] = fired;
        o["imagesHidden"] = imagesHidden;
        o["updated"] = double(updatedMs);
        if (!area.isEmpty()) o["area"] = area;

        QJsonArray imgArr;
        for (const QString &name : images) imgArr.append(name);
        o["images"] = imgArr;

        QJsonArray peakArr;
        for (int v : peaks) peakArr.append(v);
        o["peaks"] = peakArr;
        o["level"] = level;

        QJsonArray arr;
        for (const CheckItem &it : items)
            arr.append(QJsonObject{{"text", it.text}, {"done", it.done}});
        o["items"] = arr;

        QJsonArray linkArr;
        for (const Link &l : links)
            linkArr.append(QJsonObject{{"url", l.url}, {"label", l.label}});
        o["links"] = linkArr;
        return o;
    }

    static Note *fromJson(const QJsonObject &o) {
        auto *n = new Note;
        n->id = o["id"].toString(QUuid::createUuid().toString(QUuid::WithoutBraces));
        n->type = typeFromString(o["type"].toString());
        n->title = o["title"].toString();
        n->body = o["body"].toString();
        n->due = o["due"].toString();
        n->audio = o["audio"].toString();
        n->durationMs = qint64(o["durationMs"].toDouble());
        n->dueAtMs = qint64(o["dueAtMs"].toDouble());
        n->repeat = repeatFromString(o["repeat"].toString());
        n->fired = o["fired"].toBool();
        n->imagesHidden = o["imagesHidden"].toBool();
        n->updatedMs = qint64(o["updated"].toDouble());
        n->area = o["area"].toString();
        for (const QJsonValue v : o["images"].toArray())
            n->images.append(v.toString());
        for (const QJsonValue v : o["peaks"].toArray())
            n->peaks.append(v.toInt());
        n->level = o.contains("level") ? o["level"].toInt(-1) : -1;

        for (const QJsonValue v : o["items"].toArray()) {
            const QJsonObject io = v.toObject();
            n->items.append(CheckItem{io["text"].toString(), io["done"].toBool()});
        }
        for (const QJsonValue v : o["links"].toArray()) {
            const QJsonObject lo = v.toObject();
            n->links.append(Link{lo["url"].toString(), lo["label"].toString()});
        }
        return n;
    }

    int doneCount() const {
        int c = 0;
        for (const CheckItem &it : items)
            if (it.done) ++c;
        return c;
    }

    bool matches(const QString &query) const {
        if (query.isEmpty()) return true;
        if (title.contains(query, Qt::CaseInsensitive)) return true;
        if (body.contains(query, Qt::CaseInsensitive)) return true;
        for (const CheckItem &it : items)
            if (it.text.contains(query, Qt::CaseInsensitive)) return true;
        for (const Link &l : links)
            if (l.label.contains(query, Qt::CaseInsensitive) ||
                l.url.contains(query, Qt::CaseInsensitive))
                return true;
        return false;
    }

    static QString typeToString(Type t) {
        switch (t) {
            case Check:    return "check";
            case Reminder: return "reminder";
            case Voice:    return "voice";
            default:       return "text";
        }
    }

    static Type typeFromString(const QString &s) {
        if (s == "check")    return Check;
        if (s == "reminder") return Reminder;
        if (s == "voice")    return Voice;
        return Text;
    }
};
