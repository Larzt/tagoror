#include "core/calendarsync.hpp"

#include "core/drivesync.hpp"
#include "core/lang.hpp"
#include "core/store.hpp"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QSettings>
#include <QTimeZone>
#include <QUrl>

namespace {

// Lo que se deja atrás: un evento que terminó hace más de esto no se trae de
// Google ni se sube desde aquí. Sin límite, la primera pasada metería en
// events.json diez años de reuniones que nadie va a volver a mirar.
constexpr int kPastDays = 60;
// Hasta dónde se traen las vueltas de una serie que no cabe en Event::Repeat.
constexpr int kExpandBackDays = 30;
constexpr int kExpandAheadDays = 366;

QString enc(const QString &s) { return QString::fromLatin1(QUrl::toPercentEncoding(s)); }

QDate cutoff() { return QDate::currentDate().addDays(-kPastDays); }

qint64 updatedOf(const QJsonObject &item) {
    return QDateTime::fromString(item["updated"].toString(), Qt::ISODateWithMs).toMSecsSinceEpoch();
}

// Un instante de Google ("2026-09-27T10:00:00+02:00") en la hora de aquí.
QDateTime localTime(const QString &s) {
    QDateTime dt = QDateTime::fromString(s, Qt::ISODateWithMs);
    if (!dt.isValid()) dt = QDateTime::fromString(s, Qt::ISODate);
    return dt.toLocalTime();
}

// "20261231" o "20261231T225959Z" (UTC) o "20261231T235959" (de aquí).
QDate icalDate(const QString &v) {
    const QDate d = QDate::fromString(v.left(8), "yyyyMMdd");
    if (v.size() < 15 || v.at(8) != 'T') return d;
    const QTime t = QTime::fromString(v.mid(9, 6), "HHmmss");
    if (v.endsWith('Z')) return QDateTime(d, t, QTimeZone::UTC).toLocalTime().date();
    return d;
}

// Con qué día empezaba la vuelta que una excepción sustituye.
QDate originalDate(const QJsonObject &item) {
    const QJsonObject o = item["originalStartTime"].toObject();
    if (o.contains("date")) return QDate::fromString(o["date"].toString(), Qt::ISODate);
    return localTime(o["dateTime"].toString()).date();
}

QString rfc3339(const QDateTime &dt) {
    return dt.toOffsetFromUtc(dt.offsetFromUtc()).toString(Qt::ISODate);
}

const char *weekdayCode(int dow) {
    static const char *codes[] = {"MO", "TU", "WE", "TH", "FR", "SA", "SU"};
    return codes[qBound(1, dow, 7) - 1];
}

// Lee la repetición de Google. Devuelve si cabe en Event::Repeat; hasta
// cuándo se repite lo apunta aunque no quepa, porque decide si es antigua.
bool parseRecurrence(const QJsonArray &lines, Event &e) {
    e.repeat = Event::Once;
    e.until = QDate();
    if (lines.isEmpty()) return true;
    int rules = 0;
    int count = 0;
    bool fits = true;
    for (const QJsonValue v : lines) {
        const QString line = v.toString();
        if (line.startsWith("RRULE:")) {
            ++rules;
            for (const QString &part : line.mid(6).split(';', Qt::SkipEmptyParts)) {
                const QString key = part.section('=', 0, 0).toUpper();
                const QString val = part.section('=', 1).toUpper();
                if (key == "FREQ") {
                    if (val == "DAILY") e.repeat = Event::Daily;
                    else if (val == "WEEKLY") e.repeat = Event::Weekly;
                    else if (val == "MONTHLY") e.repeat = Event::Monthly;
                    else if (val == "YEARLY") e.repeat = Event::Yearly;
                    else fits = false;
                } else if (key == "INTERVAL") {
                    fits &= val.toInt() == 1;
                } else if (key == "UNTIL") {
                    e.until = icalDate(val);
                } else if (key == "COUNT") {
                    count = val.toInt();
                } else if (key == "BYDAY") {
                    // Semanal el mismo día que empieza es lo que ya hace Weekly.
                    fits &= val == weekdayCode(e.date.dayOfWeek());
                } else if (key == "BYMONTHDAY") {
                    fits &= val.toInt() == e.date.day();
                } else if (key == "BYMONTH") {
                    fits &= val.toInt() == e.date.month();
                } else if (key != "WKST") {
                    fits = false;
                }
            }
        } else if (line.startsWith("EXDATE")) {
            for (const QString &d : line.section(':', 1).split(',', Qt::SkipEmptyParts))
                if (const QDate day = icalDate(d); day.isValid() && !e.skip.contains(day))
                    e.skip.append(day);
        } else {
            fits = false;   // RDATE, EXRULE: días sueltos que no son un patrón
        }
    }
    fits &= rules == 1;
    const QString byDay = [&] {
        for (const QJsonValue v : lines)
            if (v.toString().contains("BYDAY=")) return v.toString();
        return QString();
    }();
    // Diaria solo entre semana, o mensual "el segundo martes": no caben.
    if (!byDay.isEmpty() && e.repeat != Event::Weekly) fits = false;

    // COUNT se convierte en la fecha de la última vuelta. Tiene tope, por un
    // 29 de febrero anual que tardaría siglos en sumar las que pide.
    if (fits && count > 0 && e.repeat != Event::Once) {
        Event probe = e;
        probe.until = QDate();
        probe.skip.clear();
        int seen = 0;
        for (QDate d = e.date; seen < count && e.date.daysTo(d) < 40000; d = d.addDays(1))
            if (probe.occursOn(d) && ++seen == count) e.until = d;
    }
    return fits;
}

}  // namespace

// ---------------------------------------------------------------------------

CalendarSync::CalendarSync(DriveSync *drive, Store *store)
    : QObject(drive), m_drive(drive), m_store(store) {
    load();
}

void CalendarSync::load() {
    QSettings s;
    s.beginGroup("calendar");
    m_enabled = s.value("enabled").toBool();
    m_seeded = s.value("seeded").toBool();
    m_target = s.value("target").toString();
    m_calendars.clear();
    for (const QJsonValue v :
         QJsonDocument::fromJson(s.value("calendars").toByteArray()).array()) {
        const QJsonObject o = v.toObject();
        m_calendars.append({o["id"].toString(), o["name"].toString(), QColor(o["color"].toString()),
                            o["writable"].toBool(), o["primary"].toBool(),
                            o["remind"].toInt(-1)});
    }
    auto readMap = [&s](const char *key) {
        QHash<QString, QString> out;
        const QJsonObject o = QJsonDocument::fromJson(s.value(key).toByteArray()).object();
        for (auto it = o.begin(); it != o.end(); ++it) out.insert(it.key(), it.value().toString());
        return out;
    };
    m_tokens = readMap("tokens");
    m_links = readMap("links");
    m_expandedDay = readMap("expanded");
    s.endGroup();
}

void CalendarSync::saveState() {
    QSettings s;
    s.beginGroup("calendar");
    s.setValue("enabled", m_enabled);
    s.setValue("seeded", m_seeded);
    s.setValue("target", m_target);
    QJsonArray cals;
    for (const Calendar &c : m_calendars)
        cals.append(QJsonObject{{"id", c.id},
                                {"name", c.name},
                                {"color", c.color.name()},
                                {"writable", c.writable},
                                {"primary", c.primary},
                                {"remind", c.defaultRemind}});
    s.setValue("calendars", QJsonDocument(cals).toJson(QJsonDocument::Compact));
    auto writeMap = [&s](const char *key, const QHash<QString, QString> &map) {
        QJsonObject o;
        for (auto it = map.cbegin(); it != map.cend(); ++it) o[it.key()] = it.value();
        s.setValue(key, QJsonDocument(o).toJson(QJsonDocument::Compact));
    };
    writeMap("tokens", m_tokens);
    writeMap("links", m_links);
    writeMap("expanded", m_expandedDay);
    s.endGroup();
}

void CalendarSync::forget() {
    QSettings s;
    s.beginGroup("calendar");
    s.remove("");
    s.endGroup();
    ++m_pass;   // lo que estuviera en vuelo ya no es de nadie
    m_running = false;
    m_enabled = false;
    m_seeded = false;
    m_target.clear();
    m_calendars.clear();
    m_tokens.clear();
    m_links.clear();
    m_expandedDay.clear();
    m_error.clear();
    emit changed();
}

void CalendarSync::setEnabled(bool on) {
    if (m_enabled == on) return;
    m_enabled = on;
    m_error.clear();
    saveState();
    emit changed();
}

const CalendarSync::Calendar *CalendarSync::calendar(const QString &id) const {
    for (const Calendar &c : m_calendars)
        if (c.id == id) return &c;
    return nullptr;
}

Event::Category *CalendarSync::categoryFor(const QString &calId) const {
    const QString id = categoryId(calId);
    for (Event::Category *c : m_store->categories())
        if (c->id == id) return c;
    return nullptr;
}

bool CalendarSync::isFollowed(const QString &calId) const { return categoryFor(calId) != nullptr; }

void CalendarSync::follow(const QString &calId) {
    if (isFollowed(calId)) return;
    const Calendar *cal = calendar(calId);
    auto *c = new Event::Category;
    c->id = categoryId(calId);
    c->label = cal ? cal->name : calId;
    c->color = cal ? cal->color : QColor();
    m_store->addCategory(c);
    // Uno que se vuelve a seguir se lee entero: lo que se quitó al dejarlo
    // no va a llegar como cambio.
    m_tokens.remove(calId);
    saveState();
    emit changed();
}

QString CalendarSync::target() const { return m_target; }

void CalendarSync::setTarget(const QString &calId) {
    m_target = calId;
    saveState();
    emit changed();
}

bool CalendarSync::active(const QString &calId) const {
    return !calId.isEmpty() && calendar(calId) && isFollowed(calId);
}

QString CalendarSync::activeTarget() const {
    const Calendar *c = calendar(m_target);
    return c && c->writable && isFollowed(m_target) ? m_target : QString();
}

// El calendario que le toca: el de su categoría si es de Google, el de
// destino si es de Tagoror. Uno de un calendario que ya no se sigue, o de solo
// lectura, no va a ninguno: subirlo a otro sería cambiarlo de sitio sin pedirlo.
QString CalendarSync::desiredCalendar(const Event *e) const {
    if (e->category.startsWith("gcal:")) {
        const QString cal = e->category.mid(5);
        const Calendar *c = calendar(cal);
        return active(cal) && c->writable ? cal : QString();
    }
    return activeTarget();
}

Event *CalendarSync::byGoogle(const QString &cal, const QString &gid) const {
    for (Event *e : m_store->events())
        if (e->gcalId == gid && e->gcalCal == cal) return e;
    return nullptr;
}

Event *CalendarSync::byId(const QString &id) const {
    for (Event *e : m_store->events())
        if (e->id == id) return e;
    return nullptr;
}

// El id de Tagoror de un evento que vino de Google. Tiene que salir igual en
// todos los equipos, o dos que lo traen a la vez lo duplicarían en Drive.
QString CalendarSync::derivedId(const QString &cal, const QString &gid) {
    return "gcal-" + QString::fromLatin1(
                         QCryptographicHash::hash((cal + "/" + gid).toUtf8(), QCryptographicHash::Sha1)
                             .toHex()
                             .left(32));
}

QSet<QString> CalendarSync::knownCategories() const {
    QSet<QString> out;
    for (const Event::Category &c : Event::categories()) out.insert(c.id);
    for (const Event::Category *c : m_store->categories()) out.insert(c->id);
    return out;
}

// --- traducción ------------------------------------------------------------------

QString CalendarSync::fingerprint(const Event &e) {
    QJsonObject o;
    o["t"] = e.title;
    o["d"] = e.description;
    o["day"] = e.date.toString(Qt::ISODate);
    if (e.endDate.isValid()) o["last"] = e.endDate.toString(Qt::ISODate);
    if (e.allDay) {
        o["all"] = true;
    } else {
        o["s"] = e.start.toString("HH:mm");
        o["e"] = e.end.toString("HH:mm");
    }
    o["r"] = int(e.repeat);
    if (e.repeat != Event::Once && e.until.isValid()) o["u"] = e.until.toString(Qt::ISODate);
    if (e.remind) o["rm"] = e.remindBeforeMin;
    o["c"] = e.category;
    if (e.kind == Event::Task) {
        QList<QDate> done = e.doneOn;
        std::sort(done.begin(), done.end());
        QStringList days;
        for (const QDate &d : done) days << d.toString(Qt::ISODate);
        o["done"] = days.join(',');
    }
    return QString::fromLatin1(
        QCryptographicHash::hash(QJsonDocument(o).toJson(QJsonDocument::Compact),
                                 QCryptographicHash::Sha1)
            .toHex()
            .left(20));
}

CalendarSync::Mapped CalendarSync::fromGoogle(const QJsonObject &item, const Event *base,
                                               const QString &defaultCategory, int defaultRemind,
                                               const QSet<QString> &knownCategories) {
    Mapped m;
    Event &e = m.event;
    if (base) e = *base;
    m.updatedMs = updatedOf(item);
    e.title = item["summary"].toString();
    e.description = item["description"].toString();

    const QJsonObject start = item["start"].toObject();
    const QJsonObject end = item["end"].toObject();
    if (start.contains("date")) {
        e.allDay = true;
        e.date = QDate::fromString(start["date"].toString(), Qt::ISODate);
        // En Google el fin de uno de todo el día es el día siguiente al último.
        const QDate last = QDate::fromString(end["date"].toString(), Qt::ISODate).addDays(-1);
        e.endDate = last > e.date ? last : QDate();
        e.start = QTime(0, 0);
        e.end = QTime(23, 59);
    } else {
        e.allDay = false;
        const QDateTime s = localTime(start["dateTime"].toString());
        const QDateTime f = localTime(end["dateTime"].toString());
        e.date = s.date();
        e.start = QTime(s.time().hour(), s.time().minute());
        e.end = QTime(f.time().hour(), f.time().minute());
        e.endDate = f.date() > s.date() ? f.date() : QDate();
    }

    // Los días saltados que ya tenía aquí vienen de excepciones de Google (que
    // llegan aparte); los de EXDATE se suman.
    m.representable = parseRecurrence(item["recurrence"].toArray(), e);

    const QJsonObject reminders = item["reminders"].toObject();
    int minutes = -1;
    if (reminders["useDefault"].toBool()) {
        minutes = defaultRemind;
    } else {
        for (const QJsonValue v : reminders["overrides"].toArray()) {
            const int mins = v.toObject()["minutes"].toInt(-1);
            if (mins >= 0 && (minutes < 0 || mins < minutes)) minutes = mins;
        }
    }
    e.remind = minutes >= 0;
    if (e.remind) e.remindBeforeMin = qMin(minutes, int(Event::kMaxRemindMin));

    const QJsonObject priv = item["extendedProperties"].toObject()["private"].toObject();
    m.tagororId = item["recurringEventId"].toString().isEmpty() ? priv["tagororId"].toString()
                                                                 : QString();
    e.kind = priv["tagororKind"].toString() == "task" ? Event::Task : Event::Meeting;
    e.doneOn.clear();
    if (e.kind == Event::Task)
        for (const QString &d : priv["tagororDone"].toString().split(',', Qt::SkipEmptyParts))
            if (const QDate day = QDate::fromString(d, Qt::ISODate); day.isValid())
                e.doneOn.append(day);
    const QString cat = priv["tagororCategory"].toString();
    if (!cat.isEmpty() && knownCategories.contains(cat)) e.category = cat;
    else if (!base) e.category = defaultCategory;
    return m;
}

QJsonObject CalendarSync::toGoogle(const Event &e) {
    QJsonObject o;
    o["summary"] = e.title;
    o["description"] = e.description;
    const QString tz = QString::fromLatin1(QTimeZone::systemTimeZoneId());
    if (e.allDay) {
        const QDate last = e.endDate.isValid() && e.endDate > e.date ? e.endDate : e.date;
        o["start"] = QJsonObject{{"date", e.date.toString(Qt::ISODate)}};
        o["end"] = QJsonObject{{"date", last.addDays(1).toString(Qt::ISODate)}};
    } else {
        const QDate lastDay = e.endDate.isValid() && e.endDate > e.date ? e.endDate : e.date;
        QTime endTime = e.end;
        if (!endTime.isValid() || (lastDay == e.date && endTime <= e.start))
            endTime = e.start.addSecs(3600) > e.start ? e.start.addSecs(3600) : QTime(23, 59);
        o["start"] = QJsonObject{{"dateTime", rfc3339(QDateTime(e.date, e.start))}, {"timeZone", tz}};
        o["end"] = QJsonObject{{"dateTime", rfc3339(QDateTime(lastDay, endTime))}, {"timeZone", tz}};
    }

    if (!e.gcalInstance()) {
        QJsonArray rec;
        if (e.repeat != Event::Once) {
            static const char *freq[] = {"", "DAILY", "WEEKLY", "MONTHLY", "YEARLY"};
            QString rule = QString("RRULE:FREQ=%1").arg(freq[int(e.repeat)]);
            if (e.until.isValid())
                rule += ";UNTIL=" + (e.allDay ? e.until.toString("yyyyMMdd")
                                              : QDateTime(e.until, QTime(23, 59, 59))
                                                    .toUTC()
                                                    .toString("yyyyMMdd'T'HHmmss'Z'"));
            rec.append(rule);
            // Los días saltados siguen saltados: sin esto, cambiar la serie
            // desde aquí devolvería en Google las vueltas que se cancelaron.
            QList<QDate> skip = e.skip;
            std::sort(skip.begin(), skip.end());
            for (const QDate &d : skip)
                rec.append(e.allDay ? "EXDATE;VALUE=DATE:" + d.toString("yyyyMMdd")
                                    : QString("EXDATE;TZID=%1:%2")
                                          .arg(tz, QDateTime(d, e.start).toString("yyyyMMdd'T'HHmmss")));
        }
        o["recurrence"] = rec;   // vacía también: una serie que deja de repetirse
    }

    QJsonArray overrides;
    if (e.remind) overrides.append(QJsonObject{{"method", "popup"}, {"minutes", e.remindBeforeMin}});
    o["reminders"] = QJsonObject{{"useDefault", false}, {"overrides", overrides}};

    QJsonObject priv;
    if (!e.gcalInstance()) priv["tagororId"] = e.id;
    priv["tagororKind"] = e.kind == Event::Task ? "task" : "event";
    priv["tagororCategory"] = e.category;
    if (e.kind == Event::Task) {
        QList<QDate> done = e.doneOn;
        std::sort(done.begin(), done.end());
        QStringList days;
        for (const QDate &d : done) days << d.toString(Qt::ISODate);
        priv["tagororDone"] = days.join(',');
    }
    o["extendedProperties"] = QJsonObject{{"private", priv}};
    return o;
}

// --- peticiones -------------------------------------------------------------------

int CalendarSync::status(QNetworkReply *r) {
    return r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
}

void CalendarSync::request(const QByteArray &verb, const QString &path, const QString &query,
                           const QJsonObject &body, Done done) {
    const int pass = m_pass;
    QString url = DriveSync::endpoints().calendar + path;
    if (!query.isEmpty()) url += "?" + query;
    const bool hasBody = verb == "POST" || verb == "PATCH" || verb == "PUT";
    m_drive->api(verb, QUrl::fromEncoded(url.toLatin1(), QUrl::StrictMode),
                 hasBody ? QJsonDocument(body).toJson(QJsonDocument::Compact) : QByteArray(),
                 hasBody ? QByteArray("application/json; charset=UTF-8") : QByteArray(),
                 [this, pass, done](QNetworkReply *r) {
                     // Una pasada vieja (la cuenta se desconectó, o Drive falló y
                     // empezó otra) no sigue: su done() ya no espera nadie.
                     if (pass != m_pass || !m_running ||
                         m_drive->state() != DriveSync::Syncing)
                         return;
                     done(r);
                 });
}

bool CalendarSync::ok(QNetworkReply *r, const QString &what) {
    const int code = status(r);
    if (r->error() == QNetworkReply::NoError && code >= 200 && code < 300) return true;
    const QJsonObject err = QJsonDocument::fromJson(r->readAll()).object()["error"].toObject();
    const QString why = code == 0 ? L("sin conexión")
                                  : err["message"].toString(QString::number(code));
    m_error = L("Google Calendar: %1 (%2)").arg(what, why);
    return false;
}

// --- la pasada ------------------------------------------------------------------------

void CalendarSync::run(std::function<void()> done) {
    ++m_pass;
    m_done = std::move(done);
    m_running = true;
    m_applied = false;
    m_changed = false;
    m_active.clear();
    m_fetched.clear();
    m_newTokens.clear();
    m_full.clear();
    m_failed.clear();
    m_expandQueue.clear();
    m_touched.clear();
    m_pushes.clear();
    m_deletes.clear();
    m_listing.clear();

    if (!m_enabled || !m_drive->hasCalendarScope() || !m_store->available() ||
        !m_store->eventsReadable()) {
        finish();
        return;
    }
    m_error.clear();
    fetchCalendars(QString());
}

void CalendarSync::fetchCalendars(const QString &pageToken) {
    QString q = "maxResults=250";
    if (!pageToken.isEmpty()) q += "&pageToken=" + enc(pageToken);
    request("GET", "/users/me/calendarList", q, {}, [this](QNetworkReply *r) {
        if (!ok(r, L("no se pudo leer la lista de calendarios"))) {
            finish();
            return;
        }
        const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
        for (const QJsonValue v : o["items"].toArray()) {
            const QJsonObject c = v.toObject();
            const QString role = c["accessRole"].toString();
            if (role == "freeBusyReader") continue;   // sin títulos no hay nada que enseñar
            Calendar cal;
            cal.id = c["id"].toString();
            cal.name = c["summaryOverride"].toString(c["summary"].toString());
            cal.color = QColor(c["backgroundColor"].toString());
            cal.writable = role == "owner" || role == "writer";
            cal.primary = c["primary"].toBool();
            for (const QJsonValue d : c["defaultReminders"].toArray()) {
                const int mins = d.toObject()["minutes"].toInt(-1);
                if (mins >= 0 && (cal.defaultRemind < 0 || mins < cal.defaultRemind))
                    cal.defaultRemind = mins;
            }
            m_listing.append(cal);
        }
        const QString next = o["nextPageToken"].toString();
        if (!next.isEmpty()) {
            fetchCalendars(next);
            return;
        }
        // El principal, primero; los demás por nombre.
        std::sort(m_listing.begin(), m_listing.end(), [](const Calendar &a, const Calendar &b) {
            if (a.primary != b.primary) return a.primary;
            return a.name.localeAwareCompare(b.name) < 0;
        });
        m_calendars = m_listing;
        m_listing.clear();

        // La primera vez, el principal: es donde vive casi todo, y así hay algo
        // que ver sin tener que pasar antes por ajustes. Si otro equipo ya
        // sigue alguno (han llegado sus categorías por Drive), se respeta.
        if (!m_seeded) {
            bool any = false;
            for (const Event::Category *c : m_store->categories())
                any |= c->id.startsWith("gcal:");
            for (const Calendar &c : m_calendars)
                if (c.primary) {
                    if (!any) follow(c.id);
                    if (m_target.isEmpty() && c.writable) m_target = c.id;
                }
            m_seeded = true;
        }

        for (const Calendar &c : m_calendars)
            if (isFollowed(c.id)) m_active << c.id;
        listEvents(0, QString());
    });
}

// Lo que ha cambiado en cada calendario desde la última vez (syncToken), o
// todo si no hay token. Con showDeleted, lo borrado llega como "cancelled".
void CalendarSync::listEvents(int index, const QString &pageToken) {
    if (index >= m_active.size()) {
        apply();
        return;
    }
    const QString cal = m_active.at(index);
    const QString token = m_tokens.value(cal);
    if (pageToken.isEmpty() && token.isEmpty()) m_full.insert(cal);

    QString q = "maxResults=2500&showDeleted=true";
    if (!m_full.contains(cal)) q += "&syncToken=" + enc(token);
    if (!pageToken.isEmpty()) q += "&pageToken=" + enc(pageToken);

    request("GET", "/calendars/" + enc(cal) + "/events", q, {},
            [this, index, cal](QNetworkReply *r) {
        if (status(r) == 410) {
            // El token caducó: se lee el calendario entero otra vez.
            m_tokens.remove(cal);
            m_fetched.remove(cal);
            m_full.insert(cal);
            listEvents(index, QString());
            return;
        }
        if (!ok(r, L("no se pudo leer «%1»").arg(calendar(cal) ? calendar(cal)->name : cal))) {
            m_failed.insert(cal);
            m_fetched.remove(cal);
            listEvents(index + 1, QString());
            return;
        }
        const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
        QList<QJsonObject> &items = m_fetched[cal];
        for (const QJsonValue v : o["items"].toArray()) items.append(v.toObject());
        const QString next = o["nextPageToken"].toString();
        if (!next.isEmpty()) {
            listEvents(index, next);
            return;
        }
        m_newTokens.insert(cal, o["nextSyncToken"].toString());
        listEvents(index + 1, QString());
    });
}

void CalendarSync::apply() {
    // Como la mezcla de Drive: con el usuario escribiendo o un menú abierto, lo
    // de Google se deja para la próxima. Los tokens nuevos no se guardan, así
    // que la próxima pasada vuelve a pedir estos mismos cambios.
    if (m_drive->canApply && !m_drive->canApply()) {
        finish();
        return;
    }
    if (!m_store->available()) {
        finish();
        return;
    }
    m_applied = true;

    for (const QString &cal : m_active) {
        if (m_failed.contains(cal)) continue;
        QList<QJsonObject> items = m_fetched.value(cal);
        // Las series antes que sus excepciones: una excepción apunta a su serie.
        std::stable_partition(items.begin(), items.end(), [](const QJsonObject &i) {
            return i["recurringEventId"].toString().isEmpty();
        });
        for (const QJsonObject &item : items) handleItem(cal, item);

        if (m_full.contains(cal)) {
            // Leído entero: lo enlazado que ya no está allí se borró en Google
            // mientras aquí no se miraba (o el token caducó y no llegó el aviso).
            QSet<QString> seen;
            for (const QJsonObject &item : items) seen.insert(item["id"].toString());
            const QList<Event *> events = m_store->events();
            for (Event *e : events) {
                if (e->gcalCal != cal || !e->linked() || seen.contains(e->gcalId)) continue;
                if (e->gcalInstance()) {
                    // Las vueltas de una serie expandida las lleva la expansión.
                    const Event *master = byGoogle(cal, e->gcalId.section('_', 0, 0));
                    if (master && master->gcalExpanded) continue;
                }
                if (fingerprint(*e) != e->gcalHash) {
                    // Editado aquí sin subir: se vuelve a crear allí.
                    e->gcalCal.clear();
                    e->gcalId.clear();
                    e->gcalHash.clear();
                    m_changed = true;
                    continue;
                }
                removeLocal(e);
            }
        }
        if (const QString t = m_newTokens.value(cal); !t.isEmpty()) m_tokens.insert(cal, t);
    }

    // Las series expandidas: las tocadas en esta pasada y, una vez al día, el
    // resto, porque la ventana de un año avanza con el calendario.
    const QString today = QDate::currentDate().toString(Qt::ISODate);
    for (Event *e : m_store->events()) {
        if (!e->gcalExpanded || !active(e->gcalCal)) continue;
        const QString key = e->gcalCal + "\n" + e->gcalId;
        if (m_touched.contains(key) || m_expandedDay.value(key) != today)
            m_expandQueue.append({e->gcalCal, e->gcalId});
    }
    expandNext();
}

void CalendarSync::handleItem(const QString &cal, const QJsonObject &item) {
    const QString gid = item["id"].toString();
    if (gid.isEmpty()) return;
    const bool cancelled = item["status"].toString() == "cancelled";
    const QString masterGid = item["recurringEventId"].toString();
    // Las vueltas heredan las propiedades de su serie, tagororId incluido: ese
    // id es el de la serie, no el suyo.
    const QString tagororId =
        masterGid.isEmpty()
            ? item["extendedProperties"].toObject()["private"].toObject()["tagororId"].toString()
            : QString();

    Event *local = byGoogle(cal, gid);
    if (!local && !tagororId.isEmpty()) {
        Event *t = byId(tagororId);
        if (t && t->linked() && !(t->gcalCal == cal && t->gcalId == gid)) {
            // Dos equipos lo subieron a la vez: el otro enlace es el bueno y
            // esta copia sobra (una vez borrada, ya no vuelve).
            if (!cancelled && t->gcalCal == cal) m_deletes.append({QString(), cal, gid});
            return;
        }
        local = t;
    }
    if (!local) local = byId(derivedId(cal, gid));
    Event *master = masterGid.isEmpty() ? nullptr : byGoogle(cal, masterGid);

    if (cancelled) {
        if (master && !master->gcalExpanded) {
            const QDate d = originalDate(item);
            if (d.isValid() && !master->skip.contains(d)) {
                master->skip.append(d);
                m_changed = true;
            }
        }
        if (!local) return;
        if (masterGid.isEmpty()) {
            // Editado aquí después de que allí se borrara: gana la edición, y
            // se vuelve a crear en Google en esta misma pasada.
            if (local->linked() && fingerprint(*local) != local->gcalHash &&
                local->updatedMs > updatedOf(item)) {
                local->gcalCal.clear();
                local->gcalId.clear();
                local->gcalHash.clear();
                m_changed = true;
                return;
            }
            const QList<Event *> events = m_store->events();
            for (Event *e : events)
                if (e != local && e->gcalCal == cal && e->gcalId.startsWith(gid + "_")) removeLocal(e);
        }
        removeLocal(local);
        return;
    }

    const Calendar *c = calendar(cal);
    const QString defaultCategory = master ? master->category : categoryId(cal);
    Mapped m = fromGoogle(item, local, defaultCategory, c ? c->defaultRemind : -1,
                          knownCategories());
    if (!local) {
        // Borrado aquí después de su último cambio allí: no se resucita, y la
        // fase de borrados de esta misma pasada lo quita de Google.
        const QString id = !m.tagororId.isEmpty() ? m.tagororId : derivedId(cal, gid);
        if (m_store->deletedAt("e:" + id) >= m.updatedMs) return;
        // Uno viejo que aquí no está no se trae.
        const Event &e = m.event;
        const bool old = !m.representable || e.repeat != Event::Once
                             ? e.until.isValid() && e.until < cutoff()
                             : (e.endDate.isValid() ? e.endDate : e.date) < cutoff();
        if (old) return;
    }

    if (!masterGid.isEmpty()) {
        // Una vuelta suelta: la movida de una serie, o una de una expandida.
        m.event.repeat = Event::Once;
        m.event.until = QDate();
        m.event.skip.clear();
        m.event.gcalExpanded = false;
        if (master && !master->gcalExpanded) {
            const QDate d = originalDate(item);
            if (d.isValid() && !master->skip.contains(d)) {
                master->skip.append(d);
                m_changed = true;
            }
        }
        upsert(local, m, cal, gid);
        return;
    }

    // Una vez expandida, una serie se queda así aunque después se simplifique:
    // volver a serie quitaría aquí vueltas que en Google siguen existiendo, y
    // en otro equipo ese quitar parecería un borrado del usuario.
    m.event.gcalExpanded = !m.representable || (local && local->gcalExpanded);
    Event *e = upsert(local, m, cal, gid);
    if (e && e->gcalExpanded) m_touched.insert(cal + "\n" + gid);
}

Event *CalendarSync::upsert(Event *local, const Mapped &m, const QString &cal, const QString &gid) {
    const QString remoteFp = fingerprint(m.event);
    if (!local) {
        auto *e = new Event(m.event);
        const QString wanted = m.tagororId.isEmpty() ? derivedId(cal, gid) : m.tagororId;
        if (!byId(wanted)) e->id = wanted;   // si no, el id nuevo del constructor
        e->gcalCal = cal;
        e->gcalId = gid;
        e->gcalHash = remoteFp;
        e->firedMs = 0;
        e->ringingMs = 0;
        e->updatedMs = 0;
        m_store->addEvent(e, false);
        m_changed = true;
        return e;
    }

    if (local->gcalCal != cal || local->gcalId != gid) {
        local->gcalCal = cal;
        local->gcalId = gid;
        m_changed = true;
    }
    const QString localFp = fingerprint(*local);
    if (localFp == remoteFp) {
        if (local->gcalHash != remoteFp) {
            local->gcalHash = remoteFp;
            m_changed = true;
        }
        if (m.event.gcalExpanded && !local->gcalExpanded) {
            local->gcalExpanded = true;
            m_changed = true;
        }
        return local;
    }
    const bool remoteChanged = remoteFp != local->gcalHash;
    const bool localChanged = localFp != local->gcalHash;
    if (!remoteChanged) return local;   // solo aquí: se sube en esta pasada
    if (localChanged && local->updatedMs > m.updatedMs) return local;   // aquí es más reciente

    const QString id = local->id;
    const qint64 fired = local->firedMs;
    const qint64 ringing = local->ringingMs;
    const qint64 updated = local->updatedMs;
    const bool moved = local->date != m.event.date || local->start != m.event.start;
    *local = m.event;
    local->id = id;
    local->gcalCal = cal;
    local->gcalId = gid;
    local->gcalHash = remoteFp;
    local->firedMs = moved ? 0 : fired;   // otra hora es otro aviso
    local->ringingMs = ringing;
    local->updatedMs = updated;
    m_changed = true;
    return local;
}

void CalendarSync::removeLocal(Event *e) {
    // Lo quita Google, no el usuario: no hay nada que borrar allí.
    m_links.remove(e->id);
    m_store->removeEvent(e, false);
    m_changed = true;
}

// --- series que no caben --------------------------------------------------------

void CalendarSync::expandNext() {
    if (m_expandQueue.isEmpty()) {
        planPushes();
        return;
    }
    const auto [cal, masterGid] = m_expandQueue.takeFirst();
    fetchInstances(cal, masterGid, QString(), std::make_shared<QSet<QString>>());
}

void CalendarSync::fetchInstances(const QString &cal, const QString &masterGid,
                                  const QString &pageToken,
                                  std::shared_ptr<QSet<QString>> seen) {
    const QDate from = QDate::currentDate().addDays(-kExpandBackDays);
    const QDate to = QDate::currentDate().addDays(kExpandAheadDays);
    QString q = QString("maxResults=2500&timeMin=%1&timeMax=%2")
                    .arg(enc(rfc3339(QDateTime(from, QTime(0, 0)))),
                         enc(rfc3339(QDateTime(to, QTime(0, 0)))));
    if (!pageToken.isEmpty()) q += "&pageToken=" + enc(pageToken);

    request("GET", "/calendars/" + enc(cal) + "/events/" + enc(masterGid) + "/instances", q, {},
            [=, this](QNetworkReply *r) {
        const int code = status(r);
        if (code == 404 || code == 410) {   // la serie ya no está: lo dirá el listado
            expandNext();
            return;
        }
        if (!ok(r, L("no se pudieron leer las repeticiones de un evento"))) {
            expandNext();
            return;
        }
        // Se vuelve a buscar: mientras llegaba la respuesta el usuario ha podido
        // borrar cosas, y un puntero de antes podría no valer ya.
        Event *master = byGoogle(cal, masterGid);
        if (!master || !master->gcalExpanded) {
            expandNext();
            return;
        }
        const Calendar *c = calendar(cal);
        const QJsonObject o = QJsonDocument::fromJson(r->readAll()).object();
        for (const QJsonValue v : o["items"].toArray()) {
            const QJsonObject item = v.toObject();
            const QString iid = item["id"].toString();
            if (iid.isEmpty() || item["status"].toString() == "cancelled") continue;
            seen->insert(iid);
            Event *local = byGoogle(cal, iid);
            if (!local) local = byId(derivedId(cal, iid));
            Mapped m = fromGoogle(item, local, master->category, c ? c->defaultRemind : -1,
                                  knownCategories());
            if (!local && m_store->deletedAt("e:" + derivedId(cal, iid)) >= m.updatedMs) continue;
            m.event.repeat = Event::Once;
            m.event.until = QDate();
            m.event.skip.clear();
            m.event.gcalExpanded = false;
            upsert(local, m, cal, iid);
            master = byGoogle(cal, masterGid);   // añadir puede mover la lista
        }
        const QString next = o["nextPageToken"].toString();
        if (!next.isEmpty()) {
            fetchInstances(cal, masterGid, next, seen);
            return;
        }
        // Lo que había aquí de esa serie dentro de la ventana y Google ya no
        // tiene (se cambió la regla, se canceló una vuelta) sobra. Fuera de la
        // ventana no se toca: lo pasado se queda como historia.
        const QList<Event *> events = m_store->events();
        for (Event *e : events)
            if (e->gcalCal == cal && e->gcalId.startsWith(masterGid + "_") &&
                !seen->contains(e->gcalId) && e->date >= from && e->date <= to)
                removeLocal(e);
        m_expandedDay.insert(cal + "\n" + masterGid, QDate::currentDate().toString(Qt::ISODate));
        expandNext();
    });
}

// --- de aquí a Google -----------------------------------------------------------

void CalendarSync::planPushes() {
    for (Event *e : m_store->events()) {
        if (e->gcalExpanded) continue;
        if (e->linked()) {
            const Calendar *c = calendar(e->gcalCal);
            if (!active(e->gcalCal) || !c->writable) continue;
            // Pasarlo a la categoría de otro calendario es llevarlo allí.
            if (e->category.startsWith("gcal:") && !e->gcalInstance()) {
                const QString dest = e->category.mid(5);
                const Calendar *d = calendar(dest);
                if (dest != e->gcalCal && active(dest) && d->writable)
                    m_pushes.append({Push::Move, e->id, dest});
            }
            if (fingerprint(*e) != e->gcalHash) m_pushes.append({Push::Patch, e->id, e->gcalCal});
            continue;
        }
        const QString dest = desiredCalendar(e);
        if (dest.isEmpty()) continue;
        const bool old = e->repeat != Event::Once
                             ? e->until.isValid() && e->until < cutoff()
                             : (e->endDate.isValid() ? e->endDate : e->date) < cutoff();
        if (old || !e->date.isValid()) continue;
        m_pushes.append({Push::Insert, e->id, dest});
    }
    pushNext();
}

void CalendarSync::pushNext() {
    if (m_pushes.isEmpty()) {
        planDeletes();
        return;
    }
    const Push p = m_pushes.takeFirst();
    Event *e = byId(p.eventId);
    if (!e) {
        pushNext();
        return;
    }
    const QString id = e->id;
    // La huella de lo que se manda, no de lo que haya al volver: si entre
    // medias el usuario lo cambia, eso otro sigue pendiente de subir.
    const QString fp = fingerprint(*e);

    switch (p.kind) {
        case Push::Insert:
            request("POST", "/calendars/" + enc(p.cal) + "/events", QString(), toGoogle(*e),
                    [this, id, fp, cal = p.cal](QNetworkReply *r) {
                if (ok(r, L("no se pudo crear un evento"))) {
                    const QString gid = QJsonDocument::fromJson(r->readAll()).object()["id"].toString();
                    if (Event *e = byId(id); e && !e->linked() && !gid.isEmpty()) {
                        e->gcalCal = cal;
                        e->gcalId = gid;
                        e->gcalHash = fp;
                        m_changed = true;
                    }
                }
                pushNext();
            });
            return;
        case Push::Patch: {
            const QString cal = e->gcalCal;
            const QString gid = e->gcalId;
            request("PATCH", "/calendars/" + enc(cal) + "/events/" + enc(gid), QString(),
                    toGoogle(*e), [this, id, fp, gid](QNetworkReply *r) {
                const int code = status(r);
                Event *e = byId(id);
                if ((code == 404 || code == 410) && e && e->gcalId == gid) {
                    // Borrado allí mientras aquí se editaba: la edición gana y
                    // se crea de nuevo.
                    e->gcalCal.clear();
                    e->gcalId.clear();
                    e->gcalHash.clear();
                    m_changed = true;
                    if (const QString dest = desiredCalendar(e); !dest.isEmpty())
                        m_pushes.prepend({Push::Insert, id, dest});
                } else if (ok(r, L("no se pudo guardar un evento")) && e && e->gcalId == gid) {
                    e->gcalHash = fp;
                    m_changed = true;
                }
                pushNext();
            });
            return;
        }
        case Push::Move: {
            const QString gid = e->gcalId;
            request("POST",
                    "/calendars/" + enc(e->gcalCal) + "/events/" + enc(gid) + "/move",
                    "destination=" + enc(p.cal), {}, [this, id, gid, dest = p.cal](QNetworkReply *r) {
                if (ok(r, L("no se pudo mover un evento de calendario")))
                    if (Event *e = byId(id); e && e->gcalId == gid) {
                        e->gcalCal = dest;
                        m_changed = true;
                    }
                pushNext();
            });
            return;
        }
    }
}

// Lo que estaba enlazado en la pasada anterior y ya no está aquí. Solo se
// borra en Google si lo borró alguien: tiene que tener lápida (cambiar de
// carpeta de datos no deja ninguna) y su calendario tiene que seguirse todavía
// (dejar de seguirlo se lleva sus eventos de Tagoror, no de Google).
void CalendarSync::planDeletes() {
    for (auto it = m_links.begin(); it != m_links.end();) {
        const QString id = it.key();
        const QString cal = it.value().section('\n', 0, 0);
        const QString gid = it.value().section('\n', 1);
        if (byId(id)) {
            ++it;
            continue;
        }
        const Calendar *c = calendar(cal);
        if (m_store->deletedAt("e:" + id) > 0 && active(cal) && c->writable)
            m_deletes.append({id, cal, gid});
        it = m_links.erase(it);
    }
    deleteNext();
}

void CalendarSync::deleteNext() {
    if (m_deletes.isEmpty()) {
        finish();
        return;
    }
    const Delete d = m_deletes.takeFirst();
    request("DELETE", "/calendars/" + enc(d.cal) + "/events/" + enc(d.gid), QString(), {},
            [this, d](QNetworkReply *r) {
        const int code = status(r);
        // Ya no estaba: lo que se quería.
        if (code != 404 && code != 410 && !ok(r, L("no se pudo borrar un evento")) &&
            !d.id.isEmpty())
            m_links.insert(d.id, d.cal + "\n" + d.gid);   // se reintenta la próxima vez
        deleteNext();
    });
}

void CalendarSync::finish() {
    if (m_applied) {
        // Lo enlazado ahora, para saber la próxima vez qué ha desaparecido.
        // Se añade a lo que quedó (los borrados que fallaron) sin pisarlo.
        for (const Event *e : m_store->events())
            if (e->linked() && active(e->gcalCal)) m_links.insert(e->id, e->gcalCal + "\n" + e->gcalId);
        for (auto it = m_expandedDay.begin(); it != m_expandedDay.end();) {
            const Event *master = byGoogle(it.key().section('\n', 0, 0), it.key().section('\n', 1));
            if (master && master->gcalExpanded) ++it;
            else it = m_expandedDay.erase(it);
        }
    }
    if (m_changed) {
        m_store->save();
        emit eventsChanged();
    }
    saveState();
    emit changed();
    m_running = false;
    if (auto done = std::move(m_done)) {
        m_done = nullptr;
        done();
    }
}
