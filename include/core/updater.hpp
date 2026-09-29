#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

/// Checks whether a newer release than the running one has been published.
///
/// It downloads and installs nothing: it asks GitHub for the latest release,
/// compares and reports. Together with DriveSync it is the only code that
/// goes to the network, and only when enabled.
class Updater : public QObject {
    Q_OBJECT

public:
    explicit Updater(QObject *parent = nullptr);

    /// Starts the query. Does nothing if one is already in flight.
    void check();
    bool busy() const { return m_reply != nullptr; }

    /// The compiled version, without the tag's leading 'v'.
    static QString current();

    /// Compares two versions such as "2.0.0" or "v2.0.0".
    /// @return <0, 0 or >0. Non-numeric parts are ignored, so an odd tag ties
    ///         rather than inventing an update.
    static int compare(const QString &a, const QString &b);

signals:
    /// @param version Latest published version (empty on error).
    /// @param url     Its release page (empty on error).
    /// @param error   Translated error text, empty on success.
    void finished(const QString &version, const QString &url, const QString &error);

private:
    void onReply();

    QNetworkAccessManager *m_net = nullptr;
    QNetworkReply *m_reply = nullptr;
};
