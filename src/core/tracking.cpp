#include "core/tracking.hpp"

#include <algorithm>

#include <QHash>
#include <QRegularExpression>
#include <QSaveFile>

#include "core/lang.hpp"

namespace Track {

QString clock(qint64 ms) {
    const qint64 total = qMax<qint64>(0, ms) / 1000;
    return QString("%1:%2:%3")
        .arg(total / 3600)
        .arg((total / 60) % 60, 2, 10, QChar('0'))
        .arg(total % 60, 2, 10, QChar('0'));
}

QString hm(qint64 ms) {
    const qint64 minutes = qMax<qint64>(0, ms) / 60000;
    return QString("%1:%2").arg(minutes / 60).arg(minutes % 60, 2, 10, QChar('0'));
}

QString words(qint64 ms) {
    const qint64 minutes = qMax<qint64>(0, ms) / 60000;
    const qint64 h = minutes / 60, m = minutes % 60;
    if (h == 0) return L("%1 min").arg(m);
    if (m == 0) return L("%1 h").arg(h);
    return L("%1 h %2 min").arg(h).arg(m);
}

QString money(double amount) {
    return Lang::locale().toString(amount, 'f', 2) + QStringLiteral(" €");
}

QString rateLabel(double rate) {
    const bool whole = qAbs(rate - qRound64(rate)) < 0.005;
    return Lang::locale().toString(rate, 'f', whole ? 0 : 2) + QStringLiteral(" €/h");
}

double parseRate(const QString &text, bool *ok) {
    QString t = text.trimmed();
    t.remove(QStringLiteral("€")).remove(QStringLiteral("/h")).remove(' ');
    *ok = true;
    if (t.isEmpty()) return -1;
    // Either separator is the decimal one: nobody types a thousands separator
    // in an hourly rate.
    t.replace(',', '.');
    const double v = t.toDouble(ok);
    if (!*ok || v < 0) {
        *ok = false;
        return -1;
    }
    return v;
}

qint64 parseDuration(const QString &text) {
    const QString t = text.trimmed().toLower();
    if (t.isEmpty()) return -1;
    static const QRegularExpression hmRe(R"(^(\d{1,3})\s*[:h]\s*(\d{1,2})?\s*(m|min)?$)");
    static const QRegularExpression minRe(R"(^(\d{1,4})\s*(m|min)$)");
    static const QRegularExpression hoursRe(R"(^(\d{1,3}(?:[.,]\d+)?)\s*h?$)");
    if (const auto m = minRe.match(t); m.hasMatch()) return m.captured(1).toLongLong() * 60000;
    if (const auto m = hmRe.match(t); m.hasMatch()) {
        const qint64 minutes = m.captured(2).isEmpty() ? 0 : m.captured(2).toLongLong();
        if (minutes > 59) return -1;
        return (m.captured(1).toLongLong() * 60 + minutes) * 60000;
    }
    if (const auto m = hoursRe.match(t); m.hasMatch()) {
        QString n = m.captured(1);
        return qint64(n.replace(',', '.').toDouble() * 3600000.0 + 0.5);
    }
    return -1;
}

QDate weekStart(const QDate &d) { return d.addDays(1 - d.dayOfWeek()); }

QList<const Session *> inRange(const QList<Session *> &sessions, const QDate &from,
                               const QDate &to, const QString &project) {
    QList<const Session *> out;
    for (const Session *s : sessions) {
        const QDate day = s->day();
        if (day < from || day > to) continue;
        if (project == QLatin1String("-") ? !s->project.isEmpty()
                                          : !project.isEmpty() && s->project != project)
            continue;
        out << s;
    }
    std::sort(out.begin(), out.end(),
              [](const Session *a, const Session *b) { return a->startMs > b->startMs; });
    return out;
}

namespace {

/// A field quoted when it carries the separator, a quote or a line break.
QString field(const QString &text, QChar sep) {
    if (!text.contains(sep) && !text.contains('"') && !text.contains('\n')) return text;
    QString q = text;
    q.replace('"', QStringLiteral("\"\""));
    return '"' + q + '"';
}

QString number(double v, bool comma) {
    QString s = QString::number(v, 'f', 2);
    if (comma) s.replace('.', ',');
    return s;
}

}  // namespace

QStringList csvLines(const QList<Session *> &sessions, const QList<Project *> &projects,
                     const CsvOptions &options) {
    // Spanish: ';' and a decimal comma, as Excel opens it in Spanish without
    // asking. English: the usual ',' and point.
    const bool es = Lang::current() == Lang::Es;
    const QChar sep = es ? ';' : ',';
    QHash<QString, const Project *> byId;
    for (const Project *p : projects) byId.insert(p->id, p);

    QStringList out;
    out << (es ? QStringList{"fecha", "inicio", "fin", "horas", "proyecto", "cliente", "tarea",
                             "tarifa", "importe"}
               : QStringList{"date", "start", "end", "hours", "project", "client", "task", "rate",
                             "amount"})
               .join(sep);

    // Oldest first, as an invoice reads. Running sessions are left out: they
    // have no end to bill yet.
    QList<const Session *> rows;
    for (const Session *s : inRange(sessions, options.from, options.to, options.project))
        if (!s->running()) rows.prepend(s);

    struct Row {
        QDate day;
        qint64 start = 0, end = 0, ms = 0;
        QString project, task;
        double rate = -1, amount = 0;
    };
    QList<Row> lines;
    for (const Session *s : rows) {
        if (!options.perSession) {
            // One row per day, project and task: the span from the first start to
            // the last end, and the sum of what was worked in between.
            bool merged = false;
            for (Row &r : lines) {
                if (r.day != s->day() || r.project != s->project || r.task != s->task) continue;
                r.start = qMin(r.start, s->startMs);
                r.end = qMax(r.end, s->endMs);
                r.ms += s->durationMs();
                r.amount += s->amount();
                if (s->hasRate()) r.rate = s->rate;
                merged = true;
                break;
            }
            if (merged) continue;
        }
        lines << Row{s->day(), s->startMs, s->endMs, s->durationMs(), s->project, s->task,
                     s->rate, s->amount()};
    }

    struct Total {
        qint64 ms = 0;
        double amount = 0;
        bool rated = false;
    };
    QHash<QString, Total> totals;
    QStringList order;
    for (const Row &r : lines) {
        const Project *p = byId.value(r.project);
        const auto time = [](qint64 ms) { return QDateTime::fromMSecsSinceEpoch(ms).toString("HH:mm"); };
        out << QStringList{r.day.toString(Qt::ISODate), time(r.start), time(r.end),
                           number(r.ms / 3600000.0, es), field(p ? p->name : QString(), sep),
                           field(p ? p->client : QString(), sep), field(r.task, sep),
                           r.rate >= 0 ? number(r.rate, es) : QString(),
                           r.rate >= 0 ? number(r.amount, es) : QString()}
                   .join(sep);
        if (!totals.contains(r.project)) order << r.project;
        Total &t = totals[r.project];
        t.ms += r.ms;
        t.amount += r.amount;
        t.rated |= r.rate >= 0;
    }

    if (options.totals) {
        for (const QString &id : order) {
            const Project *p = byId.value(id);
            const Total t = totals.value(id);
            const QString name = QStringLiteral("Total ") + (p ? p->name : L("Sin proyecto"));
            out << QStringList{QString(), QString(), QString(), number(t.ms / 3600000.0, es),
                               field(name, sep), field(p ? p->client : QString(), sep), QString(),
                               QString(), t.rated ? number(t.amount, es) : QString()}
                       .join(sep);
        }
    }
    return out;
}

bool writeCsv(const QString &file, const QStringList &lines) {
    QSaveFile out(file);
    if (!out.open(QIODevice::WriteOnly)) return false;
    out.write("\xEF\xBB\xBF");
    out.write((lines.join("\r\n") + "\r\n").toUtf8());
    return out.commit();
}

}  // namespace Track
