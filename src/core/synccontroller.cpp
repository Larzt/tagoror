#include "core/synccontroller.hpp"

#include <QTimer>

#include "core/drivesync.hpp"
#include "core/store.hpp"

SyncController::SyncController(Store *store, QObject *parent)
    : QObject(parent), m_store(store), m_drive(new DriveSync(store, this)) {
    m_soon = new QTimer(this);
    m_soon->setSingleShot(true);
    m_soon->setInterval(20 * 1000);
    connect(m_soon, &QTimer::timeout, this, [this] {
        // Never with the folder missing: what is in memory is not the notes.
        if (m_store->available()) m_drive->syncNow();
    });
    connect(m_store, &Store::saved, this, [this] {
        if (m_drive->connected() && m_drive->state() != DriveSync::Syncing) m_soon->start();
    });
    // No merging while typing: retry shortly.
    connect(m_drive, &DriveSync::deferred, this, [this] { m_soon->start(); });

    m_poll = new QTimer(this);
    m_poll->setInterval(90 * 1000);
    connect(m_poll, &QTimer::timeout, this, [this] {
        if (m_drive->connected() && m_store->available()) m_drive->syncNow();
    });
    m_poll->start();
}

void SyncController::setCanApply(std::function<bool()> canApply) {
    m_drive->canApply = std::move(canApply);
}

void SyncController::syncSoon() {
    if (m_drive->connected()) m_soon->start();
}

void SyncController::syncNow() {
    if (!m_store->available()) return;
    m_soon->stop();
    m_drive->syncNow();
}
