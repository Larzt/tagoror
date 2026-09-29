#pragma once

#include <algorithm>

#include <QColor>
#include <QDate>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QTime>
#include <QUuid>

#include "core/lang.hpp"

/// A planner entry: an event or a timed task. Unlike a reminder it has a start
/// and an end, which is why it is not a Note. Stored in events.json.
struct Event {
    enum Kind { Meeting, Task };
    enum Repeat { Once, Daily, Weekly, Monthly, Yearly };

    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString title;
    Kind kind = Meeting;
    QDate date;                       ///< First day.
    QTime start{10, 0};
    QTime end{11, 0};
    Repeat repeat = Once;
    /// All-day: no times, drawn in the band above the grid. The times stay at
    /// 00:00–23:59 so the alert has a start to count from.
    bool allDay = false;
    /// Last day of a multi-day event; invalid means the same day. Only all-day
    /// events cover every day; a timed one crossing midnight is drawn on its
    /// first day until the end.
    QDate endDate;
    /// Last day of the repetition (inclusive); invalid means no end.
    QDate until;
    /// Occurrences that do not happen (cancelled or moved in Google).
    QList<QDate> skip;
    QString category = "work";
    bool remind = false;              ///< Rings remindBeforeMin before it starts.
    /// Alert lead time in minutes; 0 means at the start.
    int remindBeforeMin = kDefaultRemindMin;
    QString description;
    /// Days on which a repeating task is done (a single flag would tick every
    /// occurrence at once).
    QList<QDate> doneOn;
    /// Start (ms) of the last occurrence that already rang. A boolean would make
    /// a weekly class ring once in a lifetime.
    qint64 firedMs = 0;
    qint64 ringingMs = 0;             ///< Runtime only: the occurrence ringing now.
    /// Last change, for the Drive sync (newest version of each element wins).
    /// Set by Store::stampChanges() on save, never by the editor.
    qint64 updatedMs = 0;

    /// Google Calendar link.
    /// Calendar and Google event this one is linked to (empty: Tagoror only), and
    /// the fingerprint of the shared fields the last time both sides agreed (see
    /// CalendarSync::fingerprint). They travel through Drive, so another machine
    /// on the same account knows it is already in Google.
    QString gcalCal;
    QString gcalId;
    QString gcalHash;
    /// A Google series whose rule does not fit #Repeat: it is not drawn, its
    /// occurrences are imported as separate events. It is hidden rather than
    /// deleted because its tombstone would travel through Drive and another
    /// machine would take it for a user deletion and delete it in Google.
    bool gcalExpanded = false;

    bool linked() const { return !gcalId.isEmpty(); }
    /// An occurrence of a Google series: ids are "<series>_<instant>", and
    /// Google ids never contain an underscore otherwise.
    bool gcalInstance() const { return gcalId.contains('_'); }

    static constexpr int kDefaultRemindMin = 10;
    /// At most a day: alarmDue() only looks as far ahead as the lead reaches.
    static constexpr int kMaxRemindMin = 24 * 60;

    /// A category: four built-in ones (never stored) plus the user's, which live
    /// in events.json and sync like any other element.
    struct Category {
        QString id;
        QString label;    ///< Built-in: Spanish, translated with L(). Custom: as typed.
        QColor color;     ///< Invalid means the accent.
        qint64 updatedMs = 0;   ///< Custom only: see Event::updatedMs.

        QJsonObject toJson() const {
            QJsonObject o;
            o["id"] = id;
            o["name"] = label;
            o["color"] = color.name();
            o["updated"] = double(updatedMs);
            return o;
        }
        static Category *fromJson(const QJsonObject &o) {
            auto *c = new Category;
            c->id = o["id"].toString(QUuid::createUuid().toString(QUuid::WithoutBraces));
            c->label = o["name"].toString();
            c->color = QColor(o["color"].toString());
            c->updatedMs = qint64(o["updated"].toDouble());
            return c;
        }
    };
    static const QList<Category> &categories() {
        static const QList<Category> list{
            {"work", "Trabajo", QColor(), 0},
            {"personal", "Personal", QColor("#6fcf97"), 0},
            {"study", "Estudios", QColor("#4ecdc4"), 0},
            {"other", "Otros", QColor("#b98cff"), 0},
        };
        return list;
    }
    static bool isBuiltinCategory(const QString &id) {
        for (const Category &c : categories())
            if (c.id == id) return true;
        return false;
    }
    /// Where the events of a deleted custom category go.
    static constexpr auto kFallbackCategory = "other";
    /// A category nobody knows (deleted on another machine meanwhile) is painted
    /// in the accent instead of vanishing.
    static QColor categoryColor(const QString &id, const QColor &accent,
                                const QList<Category *> *custom = nullptr) {
        for (const Category &c : categories())
            if (c.id == id) return c.color.isValid() ? c.color : accent;
        if (custom)
            for (const Category *c : *custom)
                if (c->id == id) return c->color.isValid() ? c->color : accent;
        return accent;
    }

    /// Whether it falls on @p d. A monthly one on the 31st skips short months
    /// and a yearly 29 February only falls on leap years, like Note::occursOn().
    bool occursOn(const QDate &d) const {
        if (gcalExpanded || !date.isValid() || !d.isValid() || d < date) return false;
        if (repeat != Once && until.isValid() && d > until) return false;
        if (skip.contains(d)) return false;
        switch (repeat) {
            case Daily:   return true;
            case Weekly:  return d.dayOfWeek() == date.dayOfWeek();
            case Monthly: return d.day() == date.day();
            case Yearly:  return d.day() == date.day() && d.month() == date.month();
            default:
                if (allDay && endDate.isValid()) return d <= endDate;
                return d == date;
        }
    }

    /// Days covered (1 = a single one).
    int spanDays() const {
        return endDate.isValid() && endDate > date ? int(date.daysTo(endDate)) + 1 : 1;
    }

    /// An end before the start (or invalid) is taken as one hour: a negative
    /// height block could be neither painted nor clicked.
    QTime effectiveEnd() const {
        if (allDay) return QTime(23, 59);
        // The grid does not split blocks across columns.
        if (endDate.isValid() && endDate > date) return QTime(23, 59);
        if (end.isValid() && end > start) return end;
        const QTime plus = start.addSecs(3600);
        return plus > start ? plus : QTime(23, 59);
    }

    QDateTime startOn(const QDate &d) const { return QDateTime(d, start); }
    QDateTime endOn(const QDate &d) const { return QDateTime(d, effectiveEnd()); }

    /// In decimal hours, to place the block on the grid.
    qreal startHours() const { return start.msecsSinceStartOfDay() / 3600000.0; }
    qreal endHours() const { return effectiveEnd().msecsSinceStartOfDay() / 3600000.0; }

    bool isDoneOn(const QDate &d) const { return kind == Task && doneOn.contains(d); }
    void setDoneOn(const QDate &d, bool on) {
        doneOn.removeAll(d);
        if (on) doneOn.append(d);
    }

    /// Whether it has to ring now.
    ///
    /// Looks at today's occurrence and the following ones as far as the lead
    /// reaches, and never rings for an occurrence that has already ended.
    /// @return Start of the occurrence in ms, or 0.
    qint64 alarmDue(const QDateTime &now = QDateTime::currentDateTime()) const {
        if (!remind) return 0;
        const int lead = qBound(0, remindBeforeMin, kMaxRemindMin);
        const int ahead = lead / (24 * 60) + 1;
        for (int i = 0; i <= ahead; ++i) {
            const QDate d = now.date().addDays(i);
            if (!occursOn(d) || isDoneOn(d)) continue;
            // A multi-day event rings when it starts, not on each of its days.
            if (repeat == Once && d != date) continue;
            const QDateTime begins = startOn(d);
            const qint64 key = begins.toMSecsSinceEpoch();
            if (firedMs >= key) continue;
            if (now >= begins.addSecs(-60 * lead) && now < endOn(d)) return key;
        }
        return 0;
    }

    bool matches(const QString &query) const {
        if (query.isEmpty()) return true;
        return title.contains(query, Qt::CaseInsensitive) ||
               description.contains(query, Qt::CaseInsensitive);
    }

    QJsonObject toJson() const {
        QJsonObject o;
        o["id"] = id;
        o["title"] = title;
        o["kind"] = kind == Task ? "task" : "event";
        o["date"] = date.toString(Qt::ISODate);
        o["start"] = start.toString("HH:mm");
        o["end"] = end.toString("HH:mm");
        o["repeat"] = int(repeat);
        // Written only when set, so older events keep their exact bytes and the
        // sync does not see them as changed.
        if (allDay) o["allDay"] = true;
        if (endDate.isValid()) o["endDate"] = endDate.toString(Qt::ISODate);
        if (until.isValid()) o["until"] = until.toString(Qt::ISODate);
        if (!skip.isEmpty()) {
            QList<QDate> sorted = skip;
            std::sort(sorted.begin(), sorted.end());
            QJsonArray a;
            for (const QDate &d : sorted) a.append(d.toString(Qt::ISODate));
            o["skip"] = a;
        }
        if (!gcalId.isEmpty()) {
            o["gcalCal"] = gcalCal;
            o["gcalId"] = gcalId;
            o["gcalHash"] = gcalHash;
            if (gcalExpanded) o["gcalExpanded"] = true;
        }
        o["category"] = category;
        o["remind"] = remind;
        // Only when not the default, for the same reason (see Store::stampChanges).
        if (remindBeforeMin != kDefaultRemindMin) o["remindBefore"] = remindBeforeMin;
        o["description"] = description;
        QJsonArray done;
        for (const QDate &d : doneOn) done.append(d.toString(Qt::ISODate));
        o["doneOn"] = done;
        o["fired"] = double(firedMs);
        o["updated"] = double(updatedMs);
        return o;
    }

    static Event *fromJson(const QJsonObject &o) {
        auto *e = new Event;
        e->id = o["id"].toString(e->id);
        e->title = o["title"].toString();
        e->kind = o["kind"].toString() == "task" ? Task : Meeting;
        e->date = QDate::fromString(o["date"].toString(), Qt::ISODate);
        if (const QTime t = QTime::fromString(o["start"].toString(), "HH:mm"); t.isValid())
            e->start = t;
        if (const QTime t = QTime::fromString(o["end"].toString(), "HH:mm"); t.isValid())
            e->end = t;
        e->repeat = Repeat(qBound(0, o["repeat"].toInt(), int(Yearly)));
        e->allDay = o["allDay"].toBool();
        e->endDate = QDate::fromString(o["endDate"].toString(), Qt::ISODate);
        e->until = QDate::fromString(o["until"].toString(), Qt::ISODate);
        for (const QJsonValue v : o["skip"].toArray())
            if (const QDate d = QDate::fromString(v.toString(), Qt::ISODate); d.isValid())
                e->skip.append(d);
        e->gcalCal = o["gcalCal"].toString();
        e->gcalId = o["gcalId"].toString();
        e->gcalHash = o["gcalHash"].toString();
        e->gcalExpanded = o["gcalExpanded"].toBool();
        e->category = o["category"].toString("work");
        e->remind = o["remind"].toBool();
        e->remindBeforeMin =
            qBound(0, o["remindBefore"].toInt(kDefaultRemindMin), int(kMaxRemindMin));
        e->description = o["description"].toString();
        for (const QJsonValue v : o["doneOn"].toArray())
            if (const QDate d = QDate::fromString(v.toString(), Qt::ISODate); d.isValid())
                e->doneOn.append(d);
        e->firedMs = qint64(o["fired"].toDouble());
        e->updatedMs = qint64(o["updated"].toDouble());
        return e;
    }
};
