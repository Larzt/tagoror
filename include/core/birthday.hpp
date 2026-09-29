#pragma once

#include <QDate>
#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QTime>
#include <QUuid>

#include "core/lang.hpp"

/// A birthday: a person's date that comes back every year, not a Note.
///
/// The date is stored split (day, month, year) because the year is often
/// unknown and a QDate cannot be half-filled.
struct Birthday {
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString name;
    int day = 1;
    int month = 1;
    int year = 0;            ///< 0 means unknown, and then there is no age.
    QString relation;        ///< Free text: "sister", "work"…
    QTime remindAt;          ///< Invalid: no alarm. Valid: rings on the day at that time.
    int greetedYear = 0;     ///< Last year it was marked as greeted.
    int firedYear = 0;       ///< Last year it rang (and was silenced).
    bool ringing = false;    ///< Runtime only: ringing right now.
    /// Last change, for the Drive sync (newest version of each element wins).
    /// Set by Store::stampChanges() on save, never by the editor.
    qint64 updatedMs = 0;

    /// 2004 is a leap year, so a 29 February is accepted.
    bool isValid() const { return QDate(2004, month, day).isValid(); }

    /// The date in year @p y. A 29 February is invalid in common years instead of
    /// sliding to the 28th, the same rule as Note::occursOn().
    QDate dateIn(int y) const { return QDate(y, month, day); }

    /// The next one, today included. A loop rather than arithmetic because of the
    /// 29 February, which has to wait for the next leap year.
    QDate nextDate(const QDate &from = QDate::currentDate()) const {
        for (int y = from.year(); y <= from.year() + 8; ++y) {
            const QDate d = dateIn(y);
            if (d.isValid() && d >= from) return d;
        }
        return {};
    }

    /// Days left; -1 if the date is invalid.
    int daysUntil(const QDate &from = QDate::currentDate()) const {
        const QDate next = nextDate(from);
        return next.isValid() ? int(from.daysTo(next)) : -1;
    }

    bool isToday(const QDate &today = QDate::currentDate()) const {
        return dateIn(today.year()) == today;
    }

    /// Age at the next birthday, or 0 if the year is unknown.
    int turns(const QDate &from = QDate::currentDate()) const {
        const QDate next = nextDate(from);
        return (year > 0 && next.isValid()) ? next.year() - year : 0;
    }

    /// Already greeted this year. A year rather than a flag so the mark expires
    /// on its own.
    bool greeted(const QDate &today = QDate::currentDate()) const {
        return greetedYear > 0 && greetedYear == today.year();
    }

    /// Rings this year if it has a time, it is today and it has not rung yet.
    bool alarmDue(const QDateTime &now = QDateTime::currentDateTime()) const {
        if (!remindAt.isValid() || !isToday(now.date())) return false;
        if (firedYear == now.date().year()) return false;
        return now >= QDateTime(now.date(), remindAt);
    }

    /// One or two letters for the avatar; "?" without a name, since an empty
    /// circle reads as a drawing bug.
    QString initials() const {
        QString out;
        for (const QString &word : name.split(' ', Qt::SkipEmptyParts)) {
            out += word.at(0).toUpper();
            if (out.size() == 2) break;
        }
        return out.isEmpty() ? QStringLiteral("?") : out;
    }

    /// The day in the interface language, without the year.
    QString dateLabel(const QDate &from = QDate::currentDate()) const {
        const QDate next = nextDate(from);
        return next.isValid() ? Lang::locale().toString(next, "d MMM") : QString();
    }

    /// Time left in words: exact days for the first fortnight, then rounded
    /// weeks and months.
    QString whenLabel(const QDate &from = QDate::currentDate()) const {
        const int d = daysUntil(from);
        if (d < 0) return QString();
        if (d == 0) return L("hoy");
        if (d == 1) return L("mañana");
        if (d < 14) return L("en %1 días").arg(d);
        if (d < 60) return L("en %1 sem").arg(d / 7);
        return L("en %1 meses").arg(d / 30);   // d >= 60, so never "1 month"
    }

    /// Second line of the row ("turns 36 · work"); either half may be missing.
    QString subtitle(const QDate &from = QDate::currentDate()) const {
        QStringList parts;
        if (const int age = turns(from); age > 0) parts << L("cumple %1").arg(age);
        if (!relation.trimmed().isEmpty()) parts << relation.trimmed();
        return parts.join(" · ");
    }

    /// Text for the editor's date field, with the year if known.
    QString dateText() const {
        const QString dm = QString("%1/%2").arg(day, 2, 10, QChar('0'))
                                           .arg(month, 2, 10, QChar('0'));
        return year > 0 ? dm + "/" + QString::number(year) : dm;
    }

    /// Parses "24/12", "24/12/1990", "24-12-90" or "24.12.1990" (year optional).
    /// @return false without writing anything if the text is not a date, so a
    ///         typo has no effect instead of storing an invented 1 January.
    static bool parseDate(const QString &text, int *day, int *month, int *year) {
        QString norm = text.trimmed();
        norm.replace('-', '/').replace('.', '/');
        const QStringList parts = norm.split('/', Qt::SkipEmptyParts);
        if (parts.size() < 2 || parts.size() > 3) return false;

        bool okD = false, okM = false;
        const int d = parts.at(0).toInt(&okD);
        const int m = parts.at(1).toInt(&okM);
        if (!okD || !okM || !QDate(2004, m, d).isValid()) return false;   // 2004 is a leap year

        int y = 0;
        if (parts.size() == 3) {
            bool okY = false;
            y = parts.at(2).toInt(&okY);
            if (!okY) return false;
            // Two digits: above the current year means last century.
            if (parts.at(2).size() <= 2) y += (y > QDate::currentDate().year() % 100) ? 1900 : 2000;
            if (y < 1900 || y > QDate::currentDate().year()) return false;
        }

        *day = d;
        *month = m;
        *year = y;
        return true;
    }

    bool matches(const QString &query) const {
        if (query.isEmpty()) return true;
        return name.contains(query, Qt::CaseInsensitive) ||
               relation.contains(query, Qt::CaseInsensitive);
    }

    QJsonObject toJson() const {
        QJsonObject o;
        o["id"] = id;
        o["name"] = name;
        o["day"] = day;
        o["month"] = month;
        o["year"] = year;
        o["relation"] = relation;
        o["remindAt"] = remindAt.isValid() ? remindAt.toString("HH:mm") : QString();
        o["greetedYear"] = greetedYear;
        o["firedYear"] = firedYear;
        o["updated"] = double(updatedMs);
        return o;
    }

    static Birthday *fromJson(const QJsonObject &o) {
        auto *b = new Birthday;
        b->id = o["id"].toString(QUuid::createUuid().toString(QUuid::WithoutBraces));
        b->name = o["name"].toString();
        b->day = o["day"].toInt(1);
        b->month = o["month"].toInt(1);
        b->year = o["year"].toInt(0);
        b->relation = o["relation"].toString();
        b->remindAt = QTime::fromString(o["remindAt"].toString(), "HH:mm");
        b->greetedYear = o["greetedYear"].toInt(0);
        b->firedYear = o["firedYear"].toInt(0);
        b->updatedMs = qint64(o["updated"].toDouble());
        return b;
    }
};
