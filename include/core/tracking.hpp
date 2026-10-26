#pragma once

#include <QColor>
#include <QDate>
#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUuid>

/// @file
/// Seguimiento (1.1): a clock that counts up against a task and, optionally,
/// a project. Timers count down and ring; this one adds up, and everything it
/// measures stays in a log. Kept in tracking.json, next to notes.json, with
/// its own backups and the same sync as the other files.

/// Who the hours are for. Only the name is required: without a rate it counts
/// hours and no money. Archiving hides it from the pickers without losing a
/// minute of what was logged against it.
struct Project {
    /// Eight fixed colours, told apart on the dark panel and on a light one. A
    /// project stores the index, so the palette can be retouched for everyone.
    static constexpr int kColors = 8;
    static QColor paletteColor(int index) {
        static const char *const palette[kColors] = {"#6f9bff", "#4fc99a", "#f2b757", "#ff7a6b",
                                                     "#b98cff", "#45bdd8", "#ef86b8", "#a9b85f"};
        return QColor(palette[qBound(0, index, kColors - 1)]);
    }

    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString name;
    int color = 0;
    QString client;          ///< Optional.
    double rate = -1;        ///< Per hour; negative: none.
    bool archived = false;
    /// Last change, for the Drive sync. Set by Store::stampChanges() on save.
    qint64 updatedMs = 0;

    bool hasRate() const { return rate >= 0; }
    QColor tint() const { return paletteColor(color); }

    QJsonObject toJson() const {
        QJsonObject o;
        o["id"] = id;
        o["name"] = name;
        o["color"] = color;
        o["client"] = client;
        o["rate"] = rate;
        o["archived"] = archived;
        o["updated"] = double(updatedMs);
        return o;
    }

    static Project *fromJson(const QJsonObject &o) {
        auto *p = new Project;
        p->id = o["id"].toString(p->id);
        p->name = o["name"].toString();
        p->color = qBound(0, o["color"].toInt(), kColors - 1);
        p->client = o["client"].toString();
        p->rate = o["rate"].toDouble(-1);
        p->archived = o["archived"].toBool();
        p->updatedMs = qint64(o["updated"].toDouble());
        return p;
    }
};

/// One stretch of work. Running while it has no end; pausing does not close
/// it (the pause is subtracted), stopping does. Like a timer it stores
/// instants, not a count, so it keeps adding up while the app is closed.
///
/// The rate is copied from the project when the session starts: changing a
/// project's rate later asks whether the past changes with it.
struct Session {
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString task;
    QString project;         ///< Project id; empty: none.
    qint64 startMs = 0;
    qint64 endMs = 0;        ///< 0 while running.
    qint64 pausedMs = 0;     ///< Pauses already over, in total.
    qint64 pauseStartMs = 0; ///< Paused right now since then; 0 otherwise.
    double rate = -1;        ///< Per hour, copied from the project; negative: none.
    qint64 updatedMs = 0;

    bool running() const { return endMs == 0; }
    bool paused() const { return running() && pauseStartMs > 0; }
    bool hasRate() const { return rate >= 0; }

    /// The end, or @p now while it runs.
    qint64 endOr(qint64 now = QDateTime::currentMSecsSinceEpoch()) const {
        return running() ? now : endMs;
    }

    /// Worked time: from start to end without the pauses.
    qint64 durationMs(qint64 now = QDateTime::currentMSecsSinceEpoch()) const {
        qint64 pause = pausedMs;
        if (paused()) pause += qMax<qint64>(0, now - pauseStartMs);
        return qMax<qint64>(0, endOr(now) - startMs - pause);
    }

    /// What it is worth, or 0 without a rate.
    double amount(qint64 now = QDateTime::currentMSecsSinceEpoch()) const {
        return hasRate() ? durationMs(now) / 3600000.0 * rate : 0.0;
    }

    QDate day() const { return QDateTime::fromMSecsSinceEpoch(startMs).date(); }

    void pause(qint64 now = QDateTime::currentMSecsSinceEpoch()) {
        if (running() && !paused()) pauseStartMs = now;
    }
    void resume(qint64 now = QDateTime::currentMSecsSinceEpoch()) {
        if (!paused()) return;
        pausedMs += qMax<qint64>(0, now - pauseStartMs);
        pauseStartMs = 0;
    }
    /// Closes it. Stopped while paused, it ends where the pause began: the
    /// minutes after that were not work.
    void stop(qint64 now = QDateTime::currentMSecsSinceEpoch()) {
        if (!running()) return;
        if (paused()) {
            now = pauseStartMs;
            pauseStartMs = 0;
        }
        endMs = qMax(now, startMs);
    }

    QJsonObject toJson() const {
        QJsonObject o;
        o["id"] = id;
        o["task"] = task;
        o["project"] = project;
        o["start"] = double(startMs);
        o["end"] = double(endMs);
        o["paused"] = double(pausedMs);
        o["pauseStart"] = double(pauseStartMs);
        o["rate"] = rate;
        o["updated"] = double(updatedMs);
        return o;
    }

    static Session *fromJson(const QJsonObject &o) {
        auto *s = new Session;
        s->id = o["id"].toString(s->id);
        s->task = o["task"].toString();
        s->project = o["project"].toString();
        s->startMs = qint64(o["start"].toDouble());
        s->endMs = qint64(o["end"].toDouble());
        s->pausedMs = qint64(o["paused"].toDouble());
        s->pauseStartMs = qint64(o["pauseStart"].toDouble());
        s->rate = o["rate"].toDouble(-1);
        s->updatedMs = qint64(o["updated"].toDouble());
        return s;
    }
};

/// Formatting and the sums the page and the export share. No widgets here:
/// `core` does not know about `ui`.
namespace Track {

/// "1:06:15": the running clock, with seconds.
QString clock(qint64 ms);
/// "1:25": a session's length, hours and minutes.
QString hm(qint64 ms);
/// "2 h 31 min", "45 min": a total in words.
QString words(qint64 ms);
/// "687,92 €" in the interface language.
QString money(double amount);
/// "40 €/h", without decimals when it has none.
QString rateLabel(double rate);
/// Reads "35", "35,5" or "35.50"; empty is no rate (-1). @p ok false on
/// anything else.
double parseRate(const QString &text, bool *ok);
/// "2:45", "2h45", "2,5" (hours) or "90m" as milliseconds; -1 if unreadable.
qint64 parseDuration(const QString &text);

/// Monday of @p d's week: the week starts on Monday in both languages.
QDate weekStart(const QDate &d);

/// Sessions whose start falls between @p from and @p to (both included),
/// optionally of one project ("-" for those without one).
QList<const Session *> inRange(const QList<Session *> &sessions, const QDate &from,
                               const QDate &to, const QString &project = QString());

/// The CSV for an invoice: ';' and decimal comma in Spanish, as Excel expects
/// there; ',' and a point in English.
struct CsvOptions {
    QDate from;
    QDate to;
    QString project;           ///< Empty: all; "-": without a project.
    bool perSession = true;    ///< Otherwise one row per day, project and task.
    bool totals = false;       ///< A closing row per project.
};
QStringList csvLines(const QList<Session *> &sessions, const QList<Project *> &projects,
                     const CsvOptions &options);
/// Writes them whole or not at all, with the BOM that makes Excel read the
/// accents as UTF-8 instead of guessing.
bool writeCsv(const QString &file, const QStringList &lines);

}  // namespace Track
