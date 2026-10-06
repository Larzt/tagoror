#pragma once

#include <QColor>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <functional>
#include <memory>

#include "core/event.hpp"

class DriveSync;
class Store;
class QNetworkReply;

/// Two-way sync of the planner with Google Calendar.
///
/// It uses DriveSync's account and runs as one step of every Drive pass,
/// between the Drive merge and the upload, so what Google brings reaches the
/// other machines in the same pass.
///
/// Each followed calendar is a planner category "gcal:<calendarId>"; since
/// categories travel through Drive, every machine follows the same ones.
/// Events in Tagoror's own categories are uploaded to the per-machine target
/// calendar.
///
/// Changes are detected by content, never by timestamps: Event::gcalHash is
/// the fingerprint of what both sides had when they last agreed. Timestamps
/// made two machines importing the same thing bounce it back and forth.
///
/// Rules that fit Event::Repeat become a series; others are imported
/// occurrence by occurrence and the series is hidden (Event::gcalExpanded).
/// Local deletions reach Google only when someone deleted the event:
/// unfollowing a calendar never deletes anything there.
class CalendarSync : public QObject {
    Q_OBJECT

public:
    struct Calendar {
        QString id;
        QString name;
        QColor color;
        bool writable = false;
        bool primary = false;
        int defaultRemind = -1;   ///< Default alert in minutes, -1 for none.
    };

    CalendarSync(DriveSync *drive, Store *store);

    /// This machine's switch. Without the calendar scope nothing syncs until the
    /// account is authorised again.
    bool enabled() const { return m_enabled; }
    void setEnabled(bool on);

    /// The account's calendars as listed on the last pass.
    const QList<Calendar> &calendars() const { return m_calendars; }
    const Calendar *calendar(const QString &id) const;
    bool isFollowed(const QString &calId) const;
    /// Creates the calendar's category (Google's name and colour). Unfollowing is
    /// done by Panel, which knows what to repaint.
    void follow(const QString &calId);
    Event::Category *categoryFor(const QString &calId) const;

    /// Calendar that events in Tagoror's own categories are uploaded to. Empty
    /// means none: they stay local.
    QString target() const;
    void setTarget(const QString &calId);

    QString lastError() const { return m_error; }

    /// One step of the Drive pass. Always calls @p done, whatever fails, so
    /// Calendar can never leave the notes' sync half done.
    void run(std::function<void()> done);
    /// On disconnect: forgets everything stored for this machine.
    void forget();

    static QString categoryId(const QString &calId) { return "gcal:" + calId; }
    /// Digest of an event's shared fields. See Event::gcalHash.
    static QString fingerprint(const Event &e);
    /// A Google event as a Tagoror event. The base supplies what Google does not
    /// know (id, alerts already given, skipped days).
    struct Mapped {
        Event event;
        bool representable = true;   ///< Its repetition fits Event::Repeat.
        QString tagororId;           ///< Set by Tagoror when it uploaded the event.
        qint64 updatedMs = 0;
    };
    static Mapped fromGoogle(const QJsonObject &item, const Event *base,
                             const QString &defaultCategory, int defaultRemind,
                             const QSet<QString> &knownCategories);
    /// The reverse. An occurrence of a series carries no repetition or own id.
    static QJsonObject toGoogle(const Event &e);

signals:
    /// Events changed: the planner and the alarms must look again.
    void eventsChanged();
    /// What the settings show changed (list, target, error).
    void changed();

private:
    using Done = std::function<void(QNetworkReply *)>;
    void request(const QByteArray &verb, const QString &path, const QString &query,
                 const QJsonObject &body, Done done);
    /// True on 2xx; otherwise records the error (Google's, if given).
    bool ok(QNetworkReply *r, const QString &what);
    static int status(QNetworkReply *r);

    void fetchCalendars(const QString &pageToken);
    void listEvents(int index, const QString &pageToken);
    void apply();
    void handleItem(const QString &cal, const QJsonObject &item);
    /// Makes the local event match the remote one, unless the local one is a
    /// newer edit (uploaded later).
    /// @return The local event.
    Event *upsert(Event *local, const Mapped &m, const QString &cal, const QString &gid);
    void removeLocal(Event *e);
    void expandNext();
    // By id, not by pointer: between request and reply the user may have
    // deleted events, and an earlier pointer may no longer be valid.
    void fetchInstances(const QString &cal, const QString &masterGid, const QString &pageToken,
                        std::shared_ptr<QSet<QString>> seen);
    void planPushes();
    void pushNext();
    void planDeletes();
    void deleteNext();
    void finish();

    QString activeTarget() const;
    QString desiredCalendar(const Event *e) const;
    bool active(const QString &calId) const;
    Event *byGoogle(const QString &cal, const QString &gid) const;
    Event *byId(const QString &id) const;
    static QString derivedId(const QString &cal, const QString &gid);
    QSet<QString> knownCategories() const;
    bool deletedHere(const QString &cal, const QString &id, qint64 remoteUpdatedMs) const;
    void load();
    void saveState();

    DriveSync *m_drive = nullptr;
    Store *m_store = nullptr;
    bool m_enabled = false;
    bool m_seeded = false;   ///< The first time the primary calendar is followed.
    QString m_target;
    QList<Calendar> m_calendars;
    QHash<QString, QString> m_tokens;     ///< calendar -> syncToken
    /// Linked event id -> "calendar\ngoogleId" at the end of the last pass: tells
    /// what to delete in Google when an event disappears here.
    QHash<QString, QString> m_links;
    QHash<QString, QString> m_expandedDay;   ///< series -> day of the last expansion
    QHash<QString, QString> m_followedAt;    ///< calendar -> ms when it was followed here
    QString m_error;

    /// Pass in progress. m_pass drops replies from an older pass nobody waits
    /// for any more.
    std::function<void()> m_done;
    int m_pass = 0;
    bool m_running = false;
    bool m_applied = false;                ///< The merge was reached.
    QList<Calendar> m_listing;             ///< Calendar list, partially read.
    QStringList m_active;
    QHash<QString, QList<QJsonObject>> m_fetched;
    QHash<QString, QString> m_newTokens;
    QSet<QString> m_full;                  ///< Calendars read in full.
    QSet<QString> m_failed;                ///< Calendars that could not be read.
    QList<QPair<QString, QString>> m_expandQueue;   ///< calendar, series
    QSet<QString> m_touched;               ///< Expanded series that changed now.
    struct Push {
        enum Kind { Insert, Patch, Move } kind;
        QString eventId;
        QString cal;       ///< Insert: where; Move: where to.
    };
    QList<Push> m_pushes;
    struct Delete {
        QString id;    ///< Tagoror's; empty for a duplicate copy.
        QString cal;
        QString gid;
    };
    QList<Delete> m_deletes;
    bool m_changed = false;
};
