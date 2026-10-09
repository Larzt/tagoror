#include "core/synccontroller.hpp"

#include <QSettings>
#include <QTimer>

#include "core/calendarsync.hpp"
#include "core/drivesync.hpp"
#include "core/folderbackend.hpp"
#include "core/store.hpp"
#include "core/syncengine.hpp"
#include "core/webdavbackend.hpp"

SyncController::SyncController(Store *store, QObject *parent)
    : QObject(parent), m_store(store) {
    m_drive = new DriveSync(store, this);
    m_webdav = new WebDavBackend(this);
    m_folder = new FolderBackend(this);
    m_engine = new SyncEngine(store, this);

    QSettings s;
    // Before backends there was only Drive: whoever had it connected keeps it,
    // without touching anything.
    m_provider = s.contains("sync/provider") ? s.value("sync/provider").toString()
                                              : (m_drive->connected() ? QStringLiteral("drive")
                                                                      : QString());
    const QString when = s.value("sync/when").toString();
    m_when = when == "5min" ? Every5 : when == "manual" ? Manual : OnSave;
    m_engine->setBackend(backendFor(m_provider));

    // Calendar is the middle step of every pass, whatever the backend.
    m_engine->middleStep = [this](std::function<void()> next) {
        if (calendarOn()) m_drive->calendar()->run(std::move(next));
        else next();
    };

    m_soon = new QTimer(this);
    m_soon->setSingleShot(true);
    m_soon->setInterval(20 * 1000);
    connect(m_soon, &QTimer::timeout, this, &SyncController::syncNow);
    connect(m_store, &Store::saved, this, [this] {
        if (m_when == OnSave && active() && m_engine->state() != SyncEngine::Syncing)
            m_soon->start();
    });
    // No merging while typing: retry shortly.
    connect(m_engine, &SyncEngine::deferred, this, [this] { m_soon->start(); });

    m_poll = new QTimer(this);
    connect(m_poll, &QTimer::timeout, this, &SyncController::syncNow);
    applyWhen();

    // The first pass right after connecting Google, if Drive is where notes go;
    // otherwise Calendar on its own, if it is on.
    connect(m_drive, &DriveSync::connectedNow, this, &SyncController::syncNow);

    for (QObject *o : {static_cast<QObject *>(m_drive), static_cast<QObject *>(m_webdav),
                       static_cast<QObject *>(m_folder)})
        connect(static_cast<SyncBackend *>(o), &SyncBackend::changed, this, &SyncController::changed);
    connect(m_engine, &SyncEngine::changed, this, &SyncController::changed);
    connect(m_drive->calendar(), &CalendarSync::changed, this, &SyncController::changed);
}

SyncBackend *SyncController::backendFor(const QString &id) const {
    if (id == "drive") return m_drive;
    if (id == "webdav") return m_webdav;
    if (id == "folder") return m_folder;
    return nullptr;
}

void SyncController::setProvider(const QString &id) {
    if (id == m_provider) return;
    m_provider = id;
    QSettings().setValue("sync/provider", id);
    m_engine->setBackend(backendFor(id));
    emit changed();
}

void SyncController::setWhen(When when) {
    if (when == m_when) return;
    m_when = when;
    QSettings().setValue("sync/when", when == Every5 ? "5min" : when == Manual ? "manual" : "save");
    applyWhen();
    emit changed();
}

void SyncController::applyWhen() {
    m_soon->stop();
    if (m_when == Manual) {
        m_poll->stop();
        return;
    }
    m_poll->setInterval(m_when == Every5 ? 5 * 60 * 1000 : 90 * 1000);
    m_poll->start();
}

void SyncController::setCanApply(std::function<bool()> canApply) {
    m_engine->canApply = canApply;
    m_drive->canApply = std::move(canApply);
}

bool SyncController::calendarOn() const {
    return m_drive->connected() && m_drive->calendar()->enabled() && m_drive->hasCalendarScope();
}

bool SyncController::active() const {
    SyncBackend *b = m_engine->backend();
    return (b && b->ready()) || calendarOn();
}

void SyncController::syncSoon() {
    if (active()) m_soon->start();
}

void SyncController::syncNow() {
    if (!m_store->available()) return;
    m_soon->stop();
    SyncBackend *b = m_engine->backend();
    if (b && b->ready()) m_engine->syncNow();
    else runCalendarAlone();
}

void SyncController::runCalendarAlone() {
    if (!calendarOn() || m_calendarRunning) return;
    m_calendarRunning = true;
    m_drive->calendar()->run([this] { m_calendarRunning = false; });
}
