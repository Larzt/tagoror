#pragma once

#include "core/syncbackend.hpp"

/// A folder that something else keeps in step between machines — Syncthing,
/// the Dropbox, OneDrive or Nextcloud desktop clients — as a sync backend
/// (5.0). Almost free, and unlike pointing the data folder itself there, two
/// machines editing at once still merge element by element instead of the
/// last copy of notes.json overwriting the other. Tagoror only writes inside
/// the folder chosen.
class FolderBackend : public SyncBackend {
    Q_OBJECT

public:
    explicit FolderBackend(QObject *parent = nullptr);

    QString id() const override { return QStringLiteral("folder"); }
    /// The folder is there (a removable drive may not be mounted).
    bool ready() const override;

    QString path() const { return m_path; }
    /// @return false, with error() set, for the data folder itself or one
    /// inside it (or around it): syncing a folder with itself destroys data.
    bool setPath(const QString &path);
    void forget();

    void list(const QString &dir, std::function<void(bool, Listing)> done) override;
    void download(const QString &dir, const QString &name, const Remote &remote,
                  std::function<void(bool, QByteArray)> done) override;
    void upload(const QString &dir, const QString &name, const Remote &existing,
                const QByteArray &data, std::function<void(bool, Remote)> done) override;

private:
    QString dirPath(const QString &dir) const;

    QString m_path;
};
